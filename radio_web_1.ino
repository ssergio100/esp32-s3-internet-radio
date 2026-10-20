/*
 * Arquivo principal e mapa de execução do firmware.
 *
 * O setup() inicializa, nesta ordem: interface física, relógio, Wi-Fi,
 * armazenamento/servidor, lista de estações e serviço de áudio.
 *
 * O loop() apenas coordena os módulos. A reprodução de áudio acontece em
 * uma tarefa dedicada implementada em audio_radio.cpp.
 * As regras de estados, volume, estações e arquivos permanecem neste arquivo para
 * que o fluxo principal do produto possa ser lido em um só lugar.
 */

#include <Arduino.h>

#include "configuracao.h"
#include "alarmes.h"
#include "radios.h"
#include "display_radio.h"
#include "wifi_radio.h"
#include "audio_radio.h"
#include "controles.h"
#include "indicador_led.h"
#include "relogio.h"
#include "servidor_web.h"
#include "telemetria.h"

enum class EstadoEquipamento {
    RADIO_WEB,
    RELOGIO
};

enum class ModoInterface {
    REPOUSO,
    SELECAO_RADIO
};

enum class FaseAlarme {
    INATIVO,
    AGUARDANDO_PARADA_RADIO,
    AGUARDANDO_WIFI_RADIO,
    TOCANDO
};

// Estado da navegação comandada pelo encoder de rádios.
ModoInterface modoInterface = ModoInterface::REPOUSO;
EstadoEquipamento estadoEquipamento =
    EstadoEquipamento::RADIO_WEB;

int volumeAtual = VOLUME_PADRAO;

int indiceRadioAtual = 0;
int indiceRadioEmSelecao = 0;
uint32_t idRadioAtual = 0;

unsigned long momentoUltimaAlteracaoVolumeMs = 0;
unsigned long momentoUltimaAtividadeSelecaoMs = 0;

bool barraVolumeVisivel = false;
bool ativacaoRadioPendente = false;

bool alarmeEmExecucao = false;
bool alarmeObservouAudioAtivo = false;
FaseAlarme faseAlarme = FaseAlarme::INATIVO;
EstadoEquipamento estadoAntesDoAlarme =
    EstadoEquipamento::RADIO_WEB;
DisparoAlarme alarmeAtual;
unsigned long inicioAlarmeAtualMs = 0;
unsigned long inicioPreparacaoFonteAlarmeMs = 0;
unsigned long proximaTentativaAudioAlarmeMs = 0;

// =====================================================
// Protótipos
// =====================================================

void mostrarRadioNoDisplay(int indiceRadio);
void solicitarReproducaoRadio(int indiceRadio);
bool sincronizarRadioAtualComCatalogo();

void entrarModoSelecaoRadio();
void confirmarSelecaoRadio();
void entrarEstadoRelogio();
void entrarEstadoRadioWeb();
void processarAtivacaoRadioWeb();

void processarLeituraControles(
    const LeituraControles& leitura
);

void processarAjusteVolume(
    long deslocamentoVolume
);

void processarNavegacaoRadios(
    long deslocamentoEncoder
);

void restaurarBarraAposTempoVolume();
void cancelarSelecaoRadioPorInatividade();

void atualizarDisplayEstadoAudio(
    bool forcarAtualizacao = false
);

void processarAgendamentoAlarmes();
void iniciarExecucaoAlarme(const DisparoAlarme& disparo);
void prepararFonteAlarme();
bool alarmeAtualUsaRadioWeb();
void iniciarCicloAudioAlarme();
void processarExecucaoAlarme();
void finalizarExecucaoAlarme(bool interrompidoPeloUsuario);

// =====================================================
// Setup
// =====================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    iniciarDisplay();
    iniciarControles();

    // O RTC fornece hora imediatamente; o NTP o corrige quando a rede entrar.
    iniciarRelogio();

    conectarWifi();

    if (!iniciarArmazenamentoEServidorWeb()) {
        mostrarMensagem(
            "Erro no armazenamento"
        );

        return;
    }

    // Os alarmes e as estações são carregados antes do serviço de áudio.
    mostrarMensagem("Carregando dados");
    iniciarAlarmes();

    carregarRadios();

    if (!iniciarAudio(volumeAtual)) {
        mostrarMensagem(
            "Erro no audio"
        );

        return;
    }

    indiceRadioAtual = 0;
    indiceRadioEmSelecao = indiceRadioAtual;

    solicitarReproducaoRadio(indiceRadioAtual);
}

// =====================================================
// Loop
// =====================================================

void loop() {
    LeituraControles leituraControles =
        lerControles();

    // A interface física tem precedência sobre todos os serviços
    // cooperativos. Áudio e display continuam em suas tarefas dedicadas.
    processarRelogio();
    processarAgendamentoAlarmes();

    // Contingência: retorna imediatamente quando DisplayService está ativo.
    processarDisplay();

    if (alarmeEmExecucao) {
        if (leituraControles.deslocamentoEncoderVolume != 0) {
            processarAjusteVolume(
                leituraControles.deslocamentoEncoderVolume
            );
        }

        if (leituraControles.cliqueNavegacaoDetectado) {
            finalizarExecucaoAlarme(true);
        } else {
            processarExecucaoAlarme();
        }

        if (alarmeAtualUsaRadioWeb()) {
            supervisionarWifi();
        }

        atualizarIndicadorEstadoAudio();
        registrarTelemetriaPeriodica();
        return;
    }

    processarLeituraControles(leituraControles);

    restaurarBarraAposTempoVolume();

    if (ativacaoRadioPendente) {
        supervisionarWifi();
        processarAtivacaoRadioWeb();
        atualizarIndicadorEstadoAudio();
        registrarTelemetriaPeriodica();
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RADIO_WEB) {
        supervisionarWifi();
        processarServidorWeb();
        cancelarSelecaoRadioPorInatividade();
    }

    atualizarIndicadorEstadoAudio();
    atualizarDisplayEstadoAudio();

    registrarTelemetriaPeriodica();
}

// =====================================================
// Alarmes
// =====================================================

void processarAgendamentoAlarmes() {
    DisparoAlarme disparo;

    if (!verificarDisparoAlarme(disparo)) {
        return;
    }

    iniciarExecucaoAlarme(disparo);
}

void iniciarExecucaoAlarme(const DisparoAlarme& disparo) {
    bool iniciandoCadeia = !alarmeEmExecucao;

    if (iniciandoCadeia) {
        estadoAntesDoAlarme = estadoEquipamento;

        if (estadoAntesDoAlarme == EstadoEquipamento::RADIO_WEB) {
            desativarServidorWeb();
        } else if (estadoAntesDoAlarme == EstadoEquipamento::RELOGIO) {
            bool usarPerfilPlayer =
                disparo.radioId == 0 ||
                obterRadioPorId(disparo.radioId) == nullptr;

            if (!retomarAudio(disparo.volume, usarPerfilPlayer)) {
                Serial.println(
                    "Alarme: falha ao retomar o servico de audio."
                );
            }
        }
    } else {
        Serial.printf(
            "Alarme %lu substituiu o alarme %lu.\n",
            static_cast<unsigned long>(disparo.id),
            static_cast<unsigned long>(alarmeAtual.id)
        );
    }

    alarmeAtual = disparo;
    alarmeEmExecucao = true;
    alarmeObservouAudioAtivo = false;
    inicioAlarmeAtualMs = millis();
    inicioPreparacaoFonteAlarmeMs = 0;
    proximaTentativaAudioAlarmeMs = 0;

    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    prepararFonteAlarme();
    mostrarAlarme(
        alarmeAtual.nome,
        alarmeAtual.volume
    );

    Serial.printf(
        "Alarme iniciado: %s (ID %lu).\n",
        alarmeAtual.nome.c_str(),
        static_cast<unsigned long>(alarmeAtual.id)
    );
}

bool alarmeAtualUsaRadioWeb() {
    return
        alarmeAtual.radioId != 0 &&
        obterRadioPorId(alarmeAtual.radioId) != nullptr;
}

void prepararFonteAlarme() {
    if (alarmeAtualUsaRadioWeb()) {
        if (wifiConectado()) {
            faseAlarme = FaseAlarme::TOCANDO;
            iniciarCicloAudioAlarme();
            return;
        }

        if (!iniciarReconexaoWifiSalvo()) {
            Serial.println(
                "Alarme: rede salva indisponivel; usando som padrao."
            );
            alarmeAtual.radioId = 0;
            prepararFonteAlarme();
            return;
        }

        pararAudio();
        faseAlarme = FaseAlarme::AGUARDANDO_WIFI_RADIO;
        inicioPreparacaoFonteAlarmeMs = millis();
        return;
    }

    if (alarmeAtual.radioId != 0) {
        Serial.printf(
            "Alarme: radio ID %lu inexistente; usando som padrao.\n",
            static_cast<unsigned long>(alarmeAtual.radioId)
        );
    }

    // Arquivos locais só começam depois que qualquer stream de rede terminou
    // e a pilha Wi-Fi foi completamente desligada.
    if (wifiLigado()) {
        faseAlarme = FaseAlarme::AGUARDANDO_PARADA_RADIO;

        if (!pararAudio()) {
            proximaTentativaAudioAlarmeMs = millis() + 200;
            Serial.println(
                "Alarme: comando para parar a fonte de rede rejeitado."
            );
        }

        return;
    }

    faseAlarme = FaseAlarme::TOCANDO;
    iniciarCicloAudioAlarme();
}

void iniciarCicloAudioAlarme() {
    alarmeObservouAudioAtivo = false;

    const Radio* radio =
        obterRadioPorId(alarmeAtual.radioId);

    if (radio != nullptr) {
        if (inicioPreparacaoFonteAlarmeMs == 0) {
            inicioPreparacaoFonteAlarmeMs = millis();
        }

        if (!tocarRadioAlarme(
            radio->nome,
            radio->url,
            alarmeAtual.volume
        )) {
            proximaTentativaAudioAlarmeMs = millis() + 2000;
            Serial.println("Alarme: comando da radio rejeitado.");
        }

        return;
    }

    if (!tocarArquivoAlarme(
        alarmeAtual.arquivo,
        alarmeAtual.volume
    )) {
        proximaTentativaAudioAlarmeMs = millis() + 2000;
        Serial.println("Alarme: comando de audio rejeitado.");
    }
}

void processarExecucaoAlarme() {
    unsigned long agoraMs = millis();
    constexpr uint32_t MILISSEGUNDOS_POR_MINUTO = 60000;
    uint32_t duracaoMaximaMs =
        DURACAO_MAXIMA_ALARME_MINUTOS *
        MILISSEGUNDOS_POR_MINUTO;

    if (agoraMs - inicioAlarmeAtualMs >= duracaoMaximaMs) {
        finalizarExecucaoAlarme(false);
        return;
    }

    StatusAudio statusAudio = obterStatusAudio();

    if (faseAlarme == FaseAlarme::AGUARDANDO_WIFI_RADIO) {
        if (wifiConectado()) {
            faseAlarme = FaseAlarme::TOCANDO;
            inicioPreparacaoFonteAlarmeMs = 0;
            iniciarCicloAudioAlarme();
            Serial.println(
                "Alarme: Wi-Fi conectado; iniciando radio web."
            );
        } else if (
            agoraMs - inicioPreparacaoFonteAlarmeMs >=
            TEMPO_LIMITE_CONEXAO_RADIO_ALARME_MS
        ) {
            Serial.println(
                "Alarme: limite da conexao atingido; usando som padrao."
            );
            alarmeAtual.radioId = 0;
            prepararFonteAlarme();
        }

        return;
    }

    if (faseAlarme == FaseAlarme::AGUARDANDO_PARADA_RADIO) {
        if (statusAudio.estado == EstadoAudio::PARADO) {
            if (wifiLigado()) {
                desligarWifi();
            }

            faseAlarme = FaseAlarme::TOCANDO;
            iniciarCicloAudioAlarme();
            Serial.println(
                "Alarme: radio parada e Wi-Fi desligado; iniciando audio."
            );
        } else if (
            proximaTentativaAudioAlarmeMs != 0 &&
            static_cast<int32_t>(
                agoraMs - proximaTentativaAudioAlarmeMs
            ) >= 0
        ) {
            if (pararAudio()) {
                proximaTentativaAudioAlarmeMs = 0;
            } else {
                proximaTentativaAudioAlarmeMs = agoraMs + 200;
            }
        }

        return;
    }

    if (alarmeAtualUsaRadioWeb()) {
        if (
            statusAudio.estado == EstadoAudio::TOCANDO ||
            statusAudio.estado == EstadoAudio::DEGRADADO
        ) {
            inicioPreparacaoFonteAlarmeMs = 0;
            return;
        }

        if (inicioPreparacaoFonteAlarmeMs == 0) {
            inicioPreparacaoFonteAlarmeMs = agoraMs;
        }

        if (
            proximaTentativaAudioAlarmeMs != 0 &&
            (
                statusAudio.estado == EstadoAudio::ERRO ||
                statusAudio.estado == EstadoAudio::PARADO
            ) &&
            static_cast<int32_t>(
                agoraMs - proximaTentativaAudioAlarmeMs
            ) >= 0
        ) {
            proximaTentativaAudioAlarmeMs = agoraMs + 2000;
            iniciarCicloAudioAlarme();
        }

        if (
            agoraMs - inicioPreparacaoFonteAlarmeMs >=
            TEMPO_LIMITE_CONEXAO_RADIO_ALARME_MS
        ) {
            Serial.println(
                "Alarme: radio web indisponivel; usando som padrao."
            );
            alarmeAtual.radioId = 0;
            prepararFonteAlarme();
        }

        return;
    }

    if (
        statusAudio.estado == EstadoAudio::BUFFERIZANDO ||
        statusAudio.estado == EstadoAudio::TOCANDO ||
        statusAudio.estado == EstadoAudio::DEGRADADO
    ) {
        alarmeObservouAudioAtivo = true;
        return;
    }

    if (
        statusAudio.estado == EstadoAudio::PARADO &&
        alarmeObservouAudioAtivo
    ) {
        iniciarCicloAudioAlarme();
        return;
    }

    if (
        statusAudio.estado == EstadoAudio::ERRO &&
        static_cast<int32_t>(
            agoraMs - proximaTentativaAudioAlarmeMs
        ) >= 0
    ) {
        proximaTentativaAudioAlarmeMs = agoraMs + 2000;
        iniciarCicloAudioAlarme();
    }
}

void finalizarExecucaoAlarme(bool interrompidoPeloUsuario) {
    if (!alarmeEmExecucao) {
        return;
    }

    Serial.printf(
        "Alarme finalizado: %s (%s).\n",
        alarmeAtual.nome.c_str(),
        interrompidoPeloUsuario
            ? "clique no encoder de radios"
            : "limite de tempo"
    );

    alarmeEmExecucao = false;
    alarmeObservouAudioAtivo = false;
    faseAlarme = FaseAlarme::INATIVO;
    alarmeAtual = DisparoAlarme{};

    pararAudio();
    alterarVolumeAudio(volumeAtual);

    estadoEquipamento = estadoAntesDoAlarme;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    switch (estadoAntesDoAlarme) {
        case EstadoEquipamento::RADIO_WEB:
            // Reutiliza a mesma transição assíncrona da interface. Assim a
            // restauração depois de um alarme nunca prende os encoders em uma
            // reconexão Wi-Fi bloqueante.
            entrarEstadoRadioWeb();
            break;



        case EstadoEquipamento::RELOGIO:
            if (wifiLigado()) {
                desligarWifi();
            }

            suspenderAudio();
            apagarIndicadorLed();
            mostrarTelaRelogio();
            break;
    }
}

// =====================================================
// Eventos dos controles
// =====================================================

void processarLeituraControles(
    const LeituraControles& leitura
) {
    if (leitura.deslocamentoEncoderVolume != 0) {
        processarAjusteVolume(
            leitura.deslocamentoEncoderVolume
        );
    }

    if (leitura.cliqueVolumeDetectado) {
        if (
            estadoEquipamento == EstadoEquipamento::RADIO_WEB ||
            ativacaoRadioPendente
        ) {
            entrarEstadoRelogio();
        } else {
            entrarEstadoRadioWeb();
        }

        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        return;
    }

    if (leitura.cliqueNavegacaoDetectado) {
        if (modoInterface == ModoInterface::REPOUSO) {
            entrarModoSelecaoRadio();
        } else {
            confirmarSelecaoRadio();
        }

        return;
    }

    if (leitura.deslocamentoEncoderNavegacao == 0) {
        return;
    }

    if (modoInterface == ModoInterface::SELECAO_RADIO) {
        processarNavegacaoRadios(
            leitura.deslocamentoEncoderNavegacao
        );
    } else {
        entrarModoSelecaoRadio();
        if (modoInterface == ModoInterface::SELECAO_RADIO) {
            processarNavegacaoRadios(
                leitura.deslocamentoEncoderNavegacao
            );
        }
    }
}

// =====================================================
// Estados do equipamento
// =====================================================

void entrarEstadoRelogio() {
    ativacaoRadioPendente = false;
    estadoEquipamento = EstadoEquipamento::RELOGIO;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    // A troca visual acontece antes do desligamento dos serviços para que o
    // clique tenha resposta imediata no OLED.
    mostrarTelaRelogio();
    apagarIndicadorLed();
    desativarServidorWeb();

    if (!suspenderAudio()) {
        Serial.println(
            "Comando para suspender o audio nao foi aceito."
        );
    }

    desligarWifi();

    Serial.println("Estado do equipamento: Relogio.");
}

void entrarEstadoRadioWeb() {
    if (!sincronizarRadioAtualComCatalogo()) {
        mostrarMensagem("Sem radios");
        return;
    }

    mostrarMensagem("Ativando Radio");
    ativacaoRadioPendente = true;
    estadoEquipamento = EstadoEquipamento::RELOGIO;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    if (!wifiConectado() && !iniciarReconexaoWifiSalvo()) {
        ativacaoRadioPendente = false;
        mostrarMensagem("Wi-Fi indisponivel");
        Serial.println(
            "Nao ha credenciais Wi-Fi em RAM para reativar a radio."
        );
        return;
    }

    processarAtivacaoRadioWeb();
}

void processarAtivacaoRadioWeb() {
    if (!ativacaoRadioPendente || !wifiConectado()) {
        return;
    }

    reativarServidorWeb();

    if (!retomarAudio(volumeAtual)) {
        Serial.println("Falha ao retomar o servico de audio.");
        desativarServidorWeb();
        desligarWifi();
        ativacaoRadioPendente = false;
        mostrarTelaRelogio();
        return;
    }

    estadoEquipamento = EstadoEquipamento::RADIO_WEB;
    ativacaoRadioPendente = false;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    solicitarReproducaoRadio(indiceRadioAtual);

    Serial.println("Estado do equipamento: Radio Web.");
}

void entrarModoSelecaoRadio() {
    if (!sincronizarRadioAtualComCatalogo()) {
        mostrarMensagem("Sem radios");
        return;
    }

    modoInterface = ModoInterface::SELECAO_RADIO;
    barraVolumeVisivel = false;
    momentoUltimaAtividadeSelecaoMs = millis();

    const Radio* radio = obterRadio(indiceRadioEmSelecao);

    if (radio != nullptr) {
        mostrarSelecaoRadio(
            radio->nome,
            indiceRadioEmSelecao,
            obterQuantidadeRadios()
        );
    }

    Serial.println("Modo: selecao de radio.");
}

void confirmarSelecaoRadio() {
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    if (indiceRadioEmSelecao == indiceRadioAtual) {
        atualizarDisplayEstadoAudio(true);
    } else {
        solicitarReproducaoRadio(indiceRadioEmSelecao);
    }

    Serial.println("Radio confirmada; modo: repouso.");
}


// =====================================================
// Volume dedicado
// =====================================================

void processarAjusteVolume(
    long deslocamentoVolume
) {
    int volumeAnterior = alarmeEmExecucao
        ? static_cast<int>(alarmeAtual.volume)
        : volumeAtual;

    long novoVolume =
        volumeAnterior + deslocamentoVolume;

    novoVolume = constrain(
        novoVolume,
        static_cast<long>(VOLUME_MINIMO),
        static_cast<long>(VOLUME_MAXIMO)
    );

    if (novoVolume == volumeAnterior) {
        return;
    }

    int volumeAjustado = static_cast<int>(novoVolume);

    if (alarmeEmExecucao) {
        // O cadastro persistido permanece intacto. A partir do primeiro
        // ajuste, o volume ouvido no alarme também se torna o volume geral
        // que será conservado depois da restauração.
        alarmeAtual.volume = static_cast<uint8_t>(volumeAjustado);
    }

    volumeAtual = volumeAjustado;

    if (
        alarmeEmExecucao ||
        estadoEquipamento != EstadoEquipamento::RELOGIO
    ) {
        alterarVolumeAudio(volumeAjustado);
    }

    if (alarmeEmExecucao) {
        mostrarAlarme(
            alarmeAtual.nome,
            volumeAjustado
        );

        Serial.printf(
            "Volume do alarme e geral: %d\n",
            volumeAjustado
        );
        return;
    }

    mostrarVolume(volumeAjustado);

    barraVolumeVisivel = true;

    momentoUltimaAlteracaoVolumeMs =
        millis();

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        Serial.printf(
            "Volume para a proxima fonte: %d\n",
            volumeAjustado
        );
    } else {
        Serial.printf("Volume: %d\n", volumeAjustado);
    }
}

// =====================================================
// Modo seleção de rádio
// =====================================================

void processarNavegacaoRadios(
    long deslocamentoEncoder
) {
    int quantidadeRadios =
        obterQuantidadeRadios();

    if (quantidadeRadios <= 0) {
        return;
    }

    long novoIndice =
        indiceRadioEmSelecao + deslocamentoEncoder;

    // O resto preserva todos os passos e mantém a navegação circular.
    novoIndice %= quantidadeRadios;

    if (novoIndice < 0) {
        novoIndice += quantidadeRadios;
    }

    indiceRadioEmSelecao =
        static_cast<int>(novoIndice);

    momentoUltimaAtividadeSelecaoMs = millis();
    barraVolumeVisivel = false;

    Serial.print(
        "Selecionada: "
    );

    const Radio* radio =
        obterRadio(
            indiceRadioEmSelecao
        );

    if (radio != nullptr) {
        mostrarSelecaoRadio(
            radio->nome,
            indiceRadioEmSelecao,
            quantidadeRadios
        );

        Serial.println(
            radio->nome
        );
    }
}

// =====================================================
// Retorno automático da navegação para o repouso
// =====================================================

void cancelarSelecaoRadioPorInatividade() {
    if (
        modoInterface !=
            ModoInterface::SELECAO_RADIO
    ) {
        return;
    }

    if (
        millis() - momentoUltimaAtividadeSelecaoMs <
            TEMPO_INATIVIDADE_SELECAO_MS
    ) {
        return;
    }

    modoInterface = ModoInterface::REPOUSO;
    indiceRadioEmSelecao = indiceRadioAtual;
    barraVolumeVisivel = false;

    Serial.println(
        "Seleção cancelada por inatividade; modo: repouso"
    );

    atualizarDisplayEstadoAudio(true);
}


void restaurarBarraAposTempoVolume() {
    if (!barraVolumeVisivel) {
        return;
    }

    if (
        millis() - momentoUltimaAlteracaoVolumeMs <
        TEMPO_BARRA_VOLUME_MS
    ) {
        return;
    }

    barraVolumeVisivel = false;

    if (estadoEquipamento == EstadoEquipamento::RADIO_WEB) {
        if (modoInterface == ModoInterface::SELECAO_RADIO) {
            const Radio* radio =
                obterRadio(indiceRadioEmSelecao);

            if (radio != nullptr) {
                mostrarSelecaoRadio(
                    radio->nome,
                    indiceRadioEmSelecao,
                    obterQuantidadeRadios()
                );
            }
        } else {
            atualizarDisplayEstadoAudio(true);
        }
    } else if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        mostrarTelaRelogio();
    }
}

// =====================================================
// Rádio
// =====================================================

bool sincronizarRadioAtualComCatalogo() {
    int quantidadeRadios = obterQuantidadeRadios();

    if (quantidadeRadios <= 0) {
        indiceRadioAtual = 0;
        indiceRadioEmSelecao = 0;
        idRadioAtual = 0;
        return false;
    }

    int indicePeloId = obterIndiceRadioPorId(idRadioAtual);

    if (indicePeloId >= 0) {
        indiceRadioAtual = indicePeloId;
    } else {
        indiceRadioAtual = 0;
        idRadioAtual = obterRadio(0)->id;

        Serial.println(
            "Radio atual removida; selecionando a primeira do catalogo."
        );
    }

    indiceRadioEmSelecao = indiceRadioAtual;
    return true;
}

void solicitarReproducaoRadio(int indiceRadio) {
    const Radio* radio =
        obterRadio(indiceRadio);

    if (radio == nullptr) {
        mostrarMensagem(
            "Radio invalida"
        );

        return;
    }

    bool comandoAceito =
        tocarRadio(
            radio->nome,
            radio->url
        );

    if (!comandoAceito) {
        mostrarMensagem(
            "Comando rejeitado"
        );

        mostrarRadioNoDisplay(
            indiceRadioAtual
        );

        return;
    }

    indiceRadioAtual = indiceRadio;
    indiceRadioEmSelecao = indiceRadio;
    idRadioAtual = radio->id;

    mostrarEstadoRadio(
        "Conectando...",
        indiceRadioAtual,
        obterQuantidadeRadios()
    );
}

void mostrarRadioNoDisplay(int indiceRadio) {
    const Radio* radio =
        obterRadio(indiceRadio);

    if (radio == nullptr) {
        return;
    }

    mostrarNomeRadio(
        radio->nome,
        indiceRadio,
        obterQuantidadeRadios()
    );
}

// =====================================================
// Apresentação do estado do áudio no display
// =====================================================

void atualizarDisplayEstadoAudio(
    bool forcarAtualizacao
) {
    static EstadoAudio ultimoEstadoApresentado =
        EstadoAudio::DESLIGADO;

    if (estadoEquipamento != EstadoEquipamento::RADIO_WEB) {
        return;
    }

    StatusAudio statusAudio =
        obterStatusAudio();

    if (
        !forcarAtualizacao &&
        statusAudio.estado == ultimoEstadoApresentado
    ) {
        return;
    }

    ultimoEstadoApresentado = statusAudio.estado;

    Serial.print("Estado do audio: ");
    Serial.println(
        obterTextoEstadoAudio(
            statusAudio.estado
        )
    );

    if (
        modoInterface == ModoInterface::SELECAO_RADIO ||
        barraVolumeVisivel
    ) {
        return;
    }

    switch (statusAudio.estado) {
        case EstadoAudio::CONECTANDO:
            mostrarEstadoRadio(
                "Conectando...",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
            break;

        case EstadoAudio::BUFFERIZANDO:
            mostrarEstadoRadio(
                "Bufferizando...",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
            break;

        case EstadoAudio::TOCANDO:
        case EstadoAudio::DEGRADADO:
            mostrarRadioNoDisplay(indiceRadioAtual);
            break;

        case EstadoAudio::RECONECTANDO:
            mostrarEstadoRadio(
                "Reconectando...",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
            break;

        case EstadoAudio::ERRO:
            mostrarEstadoRadio(
                "Erro no audio",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
            break;

        default:
            break;
    }
}
