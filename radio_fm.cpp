#include "radio_fm.h"

#include <limits.h>
#include <Wire.h>

#include "chaveamento_audio.h"
#include "configuracao.h"
#include "src/tef6686/RadioTef6686.h"

namespace {

    tef6686::RadioTef6686 radio;
    StatusRadioFm status;
    bool inicializado = false;
    bool ativo = false;
    long buscasPendentes = 0;
    int8_t direcaoBuscaAtual = 0;
    unsigned long ultimaLeituraQualidadeMs = 0;
    unsigned long ultimaLeituraRdsMs = 0;
    unsigned long ultimaLeituraPilotoEstereoMs = 0;
    constexpr unsigned long INTERVALO_MEDIA_SINAL_MS = 2000;
    constexpr unsigned long INTERVALO_PILOTO_ESTEREO_MS = 1000;
    constexpr unsigned long TEMPO_ESTABILIZACAO_ALIMENTACAO_FM_MS = 20;
    int32_t somaAmostrasSinal = 0;
    uint16_t quantidadeAmostrasSinal = 0;
    unsigned long inicioJanelaMediaSinalMs = 0;

    void reiniciarMediaSinal() {
        somaAmostrasSinal = 0;
        quantidadeAmostrasSinal = 0;
        inicioJanelaMediaSinalMs = 0;
    }

    void incrementarRevisao() {
        ++status.revisao;
        if (status.revisao == 0) {
            ++status.revisao;
        }
    }

    void copiarTexto(char* destino, size_t tamanho, const char* origem) {
        if (tamanho == 0) {
            return;
        }

        snprintf(
            destino,
            tamanho,
            "%s",
            origem != nullptr ? origem : ""
        );
    }

    void limparDadosEstacao() {
        bool mudou =
            status.qualidadeValida ||
            status.nivelDecimosDbuv != 0 ||
            status.larguraBandaValida ||
            status.larguraBandaDecimosKHz != 0 ||
            status.pilotoEstereoValido ||
            status.pilotoEstereo ||
            status.nomeEstacaoValido ||
            status.radioTextoValido ||
            status.nomeEstacao[0] != '\0' ||
            status.radioTexto[0] != '\0';

        status.qualidadeValida = false;
        status.nivelDecimosDbuv = 0;
        status.larguraBandaValida = false;
        status.larguraBandaDecimosKHz = 0;
        status.pilotoEstereo = false;
        status.pilotoEstereoValido = false;
        status.nomeEstacao[0] = '\0';
        status.radioTexto[0] = '\0';
        status.nomeEstacaoValido = false;
        status.radioTextoValido = false;
        reiniciarMediaSinal();

        if (mudou) {
            incrementarRevisao();
        }
    }

    void publicarEstado(EstadoRadioFm novoEstado) {
        if (status.estado == novoEstado) {
            return;
        }

        status.estado = novoEstado;
        incrementarRevisao();
    }

    void publicarErro(
        const char* operacao,
        tef6686::Erro erro,
        EstadoRadioFm estadoErro = EstadoRadioFm::ERRO
    ) {
        char texto[sizeof(status.ultimoErro)];
        snprintf(
            texto,
            sizeof(texto),
            "%s: %s",
            operacao,
            tef6686::RadioTef6686::textoErro(erro)
        );

        bool mudou =
            status.estado != estadoErro ||
            strcmp(status.ultimoErro, texto) != 0;

        status.estado = estadoErro;
        copiarTexto(status.ultimoErro, sizeof(status.ultimoErro), texto);

        if (mudou) {
            incrementarRevisao();
        }

        Serial.printf("Radio FM - %s\n", texto);
    }

    void limparErro() {
        if (status.ultimoErro[0] == '\0') {
            return;
        }

        status.ultimoErro[0] = '\0';
        incrementarRevisao();
    }

    int16_t converterVolumeParaDb(int volumeInterface) {
        int volume = constrain(
            volumeInterface,
            VOLUME_MINIMO,
            VOLUME_MAXIMO
        );

        constexpr int FAIXA_VOLUME_DB = 60;
        return static_cast<int16_t>(
            -FAIXA_VOLUME_DB +
            (
                volume * FAIXA_VOLUME_DB +
                VOLUME_MAXIMO / 2
            ) / VOLUME_MAXIMO
        );
    }

    bool aplicarVolume(int volumeInterface) {
        tef6686::Erro erro = radio.definirVolumeDb(
            converterVolumeParaDb(volumeInterface)
        );

        if (erro != tef6686::Erro::Nenhum) {
            publicarErro("volume", erro);
            return false;
        }

        erro = radio.definirMute(volumeInterface <= VOLUME_MINIMO);

        if (erro != tef6686::Erro::Nenhum) {
            publicarErro("mute", erro);
            return false;
        }

        return true;
    }

    void somarBuscasPendentes(long deslocamento) {
        if (
            deslocamento > 0 &&
            buscasPendentes > LONG_MAX - deslocamento
        ) {
            buscasPendentes = LONG_MAX;
        } else if (
            deslocamento < 0 &&
            buscasPendentes < LONG_MIN - deslocamento
        ) {
            buscasPendentes = LONG_MIN;
        } else {
            buscasPendentes += deslocamento;
        }
    }

    bool iniciarProximaBusca() {
        if (!ativo || buscasPendentes == 0) {
            return false;
        }

        direcaoBuscaAtual = buscasPendentes > 0 ? 1 : -1;
        buscasPendentes -= direcaoBuscaAtual;

        tef6686::Erro erro = radio.iniciarBusca(
            direcaoBuscaAtual > 0
                ? tef6686::Direcao::Acima
                : tef6686::Direcao::Abaixo
        );

        if (erro != tef6686::Erro::Nenhum) {
            buscasPendentes = 0;
            direcaoBuscaAtual = 0;
            publicarErro("busca", erro);
            return false;
        }

        limparErro();
        limparDadosEstacao();
        publicarEstado(EstadoRadioFm::BUSCANDO);
        return true;
    }

    bool atualizarFrequenciaPublicada() {
        uint16_t frequencia = radio.frequencia();

        if (status.frequencia10kHz == frequencia) {
            return false;
        }

        status.frequencia10kHz = frequencia;
        status.nomeEstacao[0] = '\0';
        status.radioTexto[0] = '\0';
        status.nomeEstacaoValido = false;
        status.radioTextoValido = false;
        status.qualidadeValida = false;
        status.nivelDecimosDbuv = 0;
        status.larguraBandaValida = false;
        status.larguraBandaDecimosKHz = 0;
        status.pilotoEstereo = false;
        status.pilotoEstereoValido = false;
        reiniciarMediaSinal();
        incrementarRevisao();
        return true;
    }

    bool processarQualidade() {
        unsigned long agoraMs = millis();

        if (
            agoraMs - ultimaLeituraQualidadeMs <
            INTERVALO_LEITURA_QUALIDADE_RADIO_FM_MS
        ) {
            return false;
        }

        ultimaLeituraQualidadeMs = agoraMs;

        bool disponivel = false;
        tef6686::Erro erro = radio.qualidadeDisponivel(disponivel);

        if (erro != tef6686::Erro::Nenhum || !disponivel) {
            return false;
        }

        tef6686::Qualidade qualidade;
        erro = radio.lerQualidade(qualidade);

        if (
            erro != tef6686::Erro::Nenhum ||
            !radio.qualidadeCoerente(qualidade)
        ) {
            return false;
        }

        if (quantidadeAmostrasSinal == 0) {
            inicioJanelaMediaSinalMs = agoraMs;
        }

        somaAmostrasSinal += qualidade.nivelDecimosDbuv;
        ++quantidadeAmostrasSinal;

        if (
            agoraMs - inicioJanelaMediaSinalMs <
            INTERVALO_MEDIA_SINAL_MS
        ) {
            return false;
        }

        int32_t media = somaAmostrasSinal /
            static_cast<int32_t>(quantidadeAmostrasSinal);
        reiniciarMediaSinal();

        bool mudou =
            !status.qualidadeValida ||
            status.nivelDecimosDbuv != media ||
            !status.larguraBandaValida ||
            status.larguraBandaDecimosKHz != qualidade.larguraBanda;

        status.qualidadeValida = true;
        status.nivelDecimosDbuv = static_cast<int16_t>(media);
        status.larguraBandaValida = true;
        status.larguraBandaDecimosKHz = qualidade.larguraBanda;

        if (!mudou) {
            return false;
        }

        incrementarRevisao();
        return true;
    }

    bool processarRds() {
        unsigned long agoraMs = millis();

        if (
            agoraMs - ultimaLeituraRdsMs <
            INTERVALO_LEITURA_RDS_RADIO_FM_MS
        ) {
            return false;
        }

        ultimaLeituraRdsMs = agoraMs;

        tef6686::Rds rds;
        tef6686::Erro erro = radio.lerRds(rds);

        if (erro != tef6686::Erro::Nenhum) {
            return false;
        }

        const char* nome = rds.psValido ? rds.ps : "";
        const char* texto = rds.textoValido ? rds.texto : "";
        bool mudou =
            status.nomeEstacaoValido != rds.psValido ||
            status.radioTextoValido != rds.textoValido ||
            strcmp(status.nomeEstacao, nome) != 0 ||
            strcmp(status.radioTexto, texto) != 0;

        if (!mudou) {
            return false;
        }

        copiarTexto(
            status.nomeEstacao,
            sizeof(status.nomeEstacao),
            nome
        );
        copiarTexto(
            status.radioTexto,
            sizeof(status.radioTexto),
            texto
        );
        status.nomeEstacaoValido = rds.psValido;
        status.radioTextoValido = rds.textoValido;
        incrementarRevisao();
        return true;
    }

    bool processarPilotoEstereo() {
        unsigned long agoraMs = millis();

        if (
            agoraMs - ultimaLeituraPilotoEstereoMs <
            INTERVALO_PILOTO_ESTEREO_MS
        ) {
            return false;
        }

        ultimaLeituraPilotoEstereoMs = agoraMs;

        bool presente = false;
        tef6686::Erro erro = radio.lerPilotoEstereo(presente);

        if (erro != tef6686::Erro::Nenhum) {
            bool mudou = status.pilotoEstereoValido || status.pilotoEstereo;
            status.pilotoEstereo = false;
            status.pilotoEstereoValido = false;
            if (mudou) {
                incrementarRevisao();
            }
            return mudou;
        }

        bool mudou =
            !status.pilotoEstereoValido ||
            status.pilotoEstereo != presente;
        status.pilotoEstereo = presente;
        status.pilotoEstereoValido = true;

        if (mudou) {
            incrementarRevisao();
        }
        return mudou;
    }

}

bool iniciarRadioFm(int volumeInterface) {
    const uint16_t frequenciaInicial = status.frequencia10kHz != 0
        ? status.frequencia10kHz
        : FREQUENCIA_INICIAL_RADIO_FM_10KHZ;

    status = StatusRadioFm{};
    status.frequencia10kHz = frequenciaInicial;
    buscasPendentes = 0;
    direcaoBuscaAtual = 0;
    reiniciarMediaSinal();
    inicializado = false;
    ativo = false;

    // GPA4 é a alimentação/habilitação ativa em HIGH do TEF6686. O módulo
    // precisa estar energizado antes da primeira tentativa de comunicação.
    if (!configurarHabilitacaoRadioFm(true)) {
        copiarTexto(
            status.ultimoErro,
            sizeof(status.ultimoErro),
            "alimentacao: falha no MCP23017"
        );
        incrementarRevisao();
        Serial.println("Radio FM: GPA4=HIGH nao foi confirmado.");
        return false;
    }
    delay(TEMPO_ESTABILIZACAO_ALIMENTACAO_FM_MS);

    tef6686::Configuracao configuracao;
    configuracao.enderecoI2C = ENDERECO_I2C_TEF6686;
    configuracao.clockHz = CLOCK_TEF6686_HZ;
    configuracao.clockExterno = CLOCK_TEF6686_EXTERNO;

    tef6686::Erro erro = radio.begin(Wire, configuracao);

    if (erro != tef6686::Erro::Nenhum) {
        configurarHabilitacaoRadioFm(false);
        publicarErro(
            "inicializacao",
            erro,
            EstadoRadioFm::INDISPONIVEL
        );
        return false;
    }

    inicializado = true;

    erro = radio.sintonizar(
        tef6686::Banda::FM,
        frequenciaInicial
    );

    if (erro != tef6686::Erro::Nenhum) {
        inicializado = false;
        configurarHabilitacaoRadioFm(false);
        publicarErro("sintonia inicial", erro);
        return false;
    }

    status.frequencia10kHz = radio.frequencia();

    erro = radio.definirVolumeDb(converterVolumeParaDb(volumeInterface));

    if (erro == tef6686::Erro::Nenhum) {
        erro = radio.definirRepouso(true);
    }

    if (erro != tef6686::Erro::Nenhum) {
        inicializado = false;
        configurarHabilitacaoRadioFm(false);
        publicarErro("repouso inicial", erro);
        return false;
    }

    limparErro();
    publicarEstado(EstadoRadioFm::REPOUSO);
    Serial.printf(
        "Radio FM pronto em %u.%02u MHz e mantido em repouso.\n",
        status.frequencia10kHz / 100,
        status.frequencia10kHz % 100
    );
    return true;
}

bool ativarRadioFm(int volumeInterface) {
    if (!inicializado) {
        // Permite recuperar uma ausência transitória no boot sem reiniciar o
        // equipamento. A tela "Ativando" já foi publicada pelo coordenador.
        if (!iniciarRadioFm(volumeInterface)) {
            publicarEstado(EstadoRadioFm::INDISPONIVEL);
            return false;
        }
    }

    buscasPendentes = 0;
    direcaoBuscaAtual = 0;

    tef6686::Erro erro = radio.definirRepouso(false);

    if (erro != tef6686::Erro::Nenhum) {
        inicializado = false;
        configurarHabilitacaoRadioFm(false);
        publicarErro("ativacao", erro);
        return false;
    }

    ativo = true;
    limparErro();
    atualizarFrequenciaPublicada();

    if (!aplicarVolume(volumeInterface)) {
        desativarRadioFm();
        return false;
    }

    ultimaLeituraQualidadeMs = 0;
    ultimaLeituraRdsMs = 0;
    ultimaLeituraPilotoEstereoMs = 0;
    reiniciarMediaSinal();
    publicarEstado(EstadoRadioFm::ATIVO);
    return true;
}

void desativarRadioFm() {
    buscasPendentes = 0;
    direcaoBuscaAtual = 0;

    if (!inicializado) {
        ativo = false;
        configurarHabilitacaoRadioFm(false);
        return;
    }

    if (radio.buscaAtiva()) {
        radio.cancelarBusca(true);
    }

    tef6686::Erro erro = radio.definirRepouso(true);
    ativo = false;
    inicializado = false;

    const bool alimentacaoDesligada =
        configurarHabilitacaoRadioFm(false);

    if (erro != tef6686::Erro::Nenhum) {
        publicarErro("repouso", erro);
        return;
    }

    if (!alimentacaoDesligada) {
        copiarTexto(
            status.ultimoErro,
            sizeof(status.ultimoErro),
            "alimentacao: falha ao colocar GPA4 em LOW"
        );
        incrementarRevisao();
        Serial.println("Radio FM: GPA4=LOW nao foi confirmado.");
        return;
    }

    limparDadosEstacao();
    status.sequenciaEventoEncoder = 0;
    status.momentoEventoEncoderUs = 0;
    limparErro();
    publicarEstado(EstadoRadioFm::REPOUSO);
}

void ajustarVolumeRadioFm(int volumeInterface) {
    if (!inicializado) {
        return;
    }

    tef6686::Erro erro = radio.definirVolumeDb(
        converterVolumeParaDb(volumeInterface)
    );

    if (erro != tef6686::Erro::Nenhum) {
        publicarErro("volume", erro);
        return;
    }

    if (ativo && !radio.buscaAtiva()) {
        erro = radio.definirMute(volumeInterface <= VOLUME_MINIMO);

        if (erro != tef6686::Erro::Nenhum) {
            publicarErro("mute", erro);
        }
    }
}

void navegarRadioFm(
    long deslocamento,
    uint32_t momentoEventoEncoderUs,
    uint32_t sequenciaEventoEncoder
) {
    if (!inicializado || !ativo || deslocamento == 0) {
        return;
    }

    if (sequenciaEventoEncoder != 0) {
        status.sequenciaEventoEncoder = sequenciaEventoEncoder;
        status.momentoEventoEncoderUs = momentoEventoEncoderUs;
    }

    somarBuscasPendentes(deslocamento);

    if (radio.buscaAtiva()) {
        int8_t direcaoPendente = buscasPendentes > 0
            ? 1
            : buscasPendentes < 0 ? -1 : 0;

        if (
            direcaoPendente != 0 &&
            direcaoPendente != direcaoBuscaAtual
        ) {
            radio.cancelarBusca(false);
            direcaoBuscaAtual = 0;
            iniciarProximaBusca();
        }

        return;
    }

    iniciarProximaBusca();
}

bool processarRadioFm() {
    if (!inicializado || !ativo) {
        return false;
    }

    uint32_t revisaoAnterior = status.revisao;

    if (radio.buscaAtiva()) {
        tef6686::EstadoBusca resultado = radio.processarBusca();
        atualizarFrequenciaPublicada();

        if (resultado == tef6686::EstadoBusca::Falha) {
            buscasPendentes = 0;
            direcaoBuscaAtual = 0;
            publicarErro("busca", radio.ultimoErro());
        } else if (
            resultado == tef6686::EstadoBusca::EstacaoEncontrada ||
            resultado == tef6686::EstadoBusca::NaoEncontrada
        ) {
            direcaoBuscaAtual = 0;

            if (buscasPendentes != 0) {
                iniciarProximaBusca();
            } else {
                publicarEstado(EstadoRadioFm::ATIVO);
            }
        }

        return status.revisao != revisaoAnterior;
    }

    if (buscasPendentes != 0) {
        iniciarProximaBusca();
        return status.revisao != revisaoAnterior;
    }

    processarQualidade();
    processarRds();
    processarPilotoEstereo();
    return status.revisao != revisaoAnterior;
}

StatusRadioFm obterStatusRadioFm() {
    return status;
}

const char* obterTextoEstadoRadioFm(EstadoRadioFm estado) {
    switch (estado) {
        case EstadoRadioFm::INDISPONIVEL:
            return "Indisponivel";

        case EstadoRadioFm::REPOUSO:
            return "Repouso";

        case EstadoRadioFm::ATIVO:
            return "Ativo";

        case EstadoRadioFm::BUSCANDO:
            return "Buscando...";

        case EstadoRadioFm::ERRO:
            return "Erro";
    }

    return "Desconhecido";
}
