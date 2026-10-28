#include "dfplayer.h"

#include "chaveamento_audio.h"
#include "configuracao.h"

#include <DFRobotDFPlayerMini.h>
#include <cstdio>

namespace {

static_assert(
    VOLUME_MAXIMO > 0,
    "A escala geral de volume precisa ter limite positivo."
);

HardwareSerial serialDfPlayer(UART_DFPLAYER);
DFRobotDFPlayerMini driverDfPlayer;
EstadoDfPlayer estado;
CatalogoSonsDfPlayer catalogo;
bool saidaDfPlayerAtiva = false;
EstadoSaidaAudio estadoSaidaAntesDfPlayer;
bool nivelBusyConhecido = false;
bool ultimoBusyAtivo = false;

void desativarSaidaDfPlayer() {
    if (!saidaDfPlayerAtiva && !estadoSaidaAntesDfPlayer.valido) {
        return;
    }

    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG DFPlayer desligando: uso=%u fase=%u "
            "snapshotValido=%u snapshotPortaA=0x%02X.\n",
            static_cast<unsigned>(estado.uso),
            static_cast<unsigned>(estado.fase),
            estadoSaidaAntesDfPlayer.valido ? 1U : 0U,
            estadoSaidaAntesDfPlayer.valorPortaA
        );
    }

    configurarSaidaAudioDfPlayer(true);
    driverDfPlayer.disableDAC();
    saidaDfPlayerAtiva = false;
    restaurarEstadoSaidaAudio(estadoSaidaAntesDfPlayer);
    estadoSaidaAntesDfPlayer = EstadoSaidaAudio{};
    if (DEBUG_CHIME) {
        Serial.println(
            "CHIME DEBUG DFPlayer desligado; saida anterior restaurada."
        );
    }
}

uint8_t limitarVolumeEquipamento(uint8_t volume) {
    return volume > VOLUME_MAXIMO ? VOLUME_MAXIMO : volume;
}

uint8_t converterVolumeParaModulo(uint8_t volumeEquipamento) {
    const uint16_t volumeLimitado =
        limitarVolumeEquipamento(volumeEquipamento);
    return static_cast<uint8_t>(
        (volumeLimitado * VOLUME_MAXIMO_DFPLAYER + VOLUME_MAXIMO / 2) /
        VOLUME_MAXIMO
    );
}

void limparRecepcaoSerial() {
    while (serialDfPlayer.available() > 0) {
        serialDfPlayer.read();
    }
}

int prepararCatalogoSons() {
    // Alguns DFPlayers/clones retornam uma contagem incompleta para o comando
    // específico da pasta (0x4E). Como /01 concentra os sons deste projeto,
    // usamos a maior resposta entre a pasta e o total de arquivos do SD.
    limparRecepcaoSerial();
    const int quantidadePasta =
        driverDfPlayer.readFileCountsInFolder(PASTA_SONS_DFPLAYER);
    limparRecepcaoSerial();
    const int quantidadeCartao = driverDfPlayer.readFileCounts();

    catalogo.quantidadeInformadaPasta = quantidadePasta;
    catalogo.quantidadeInformadaCartao = quantidadeCartao;
    catalogo.contagensDivergentes =
        quantidadePasta >= 0 &&
        quantidadeCartao >= 0 &&
        quantidadePasta != quantidadeCartao;

    Serial.printf(
        "DFPlayer: contagem /%02u=%d; cartao=%d.\n",
        static_cast<unsigned>(PASTA_SONS_DFPLAYER),
        quantidadePasta,
        quantidadeCartao
    );
    int quantidade = quantidadeCartao > quantidadePasta
        ? quantidadeCartao
        : quantidadePasta;

    if (quantidade > 255) {
        quantidade = 255;
    }
    if (quantidade > 0) {
        catalogo.disponivel = true;
        catalogo.quantidade = static_cast<uint8_t>(quantidade);
    }
    return quantidade;
}

bool erroInterrompeReproducao(uint16_t detalhe) {
    return
        detalhe == Busy ||
        detalhe == Sleeping ||
        detalhe == FileIndexOut ||
        detalhe == FileMismatch;
}

}

bool iniciarDfPlayer() {
    estado = EstadoDfPlayer{};
    catalogo = CatalogoSonsDfPlayer{};
    saidaDfPlayerAtiva = false;
    estadoSaidaAntesDfPlayer = EstadoSaidaAudio{};
    nivelBusyConhecido = false;
    ultimoBusyAtivo = false;
    estado.volumeEquipamento = limitarVolumeEquipamento(VOLUME_PADRAO);
    estado.volumeModulo =
        converterVolumeParaModulo(estado.volumeEquipamento);

    pinMode(PIN_DFPLAYER_BUSY, INPUT_PULLUP);
    serialDfPlayer.begin(
        BAUD_DFPLAYER,
        SERIAL_8N1,
        PIN_DFPLAYER_TX,  // RX do ESP recebe o TX do DFPlayer.
        PIN_DFPLAYER_RX   // TX do ESP envia ao RX do DFPlayer.
    );

    // O ACK fica desativado: alguns módulos e clones devolvem ACK antes da
    // resposta das consultas, o que desloca a fila da biblioteca. Os eventos
    // espontâneos e as respostas das consultas continuam disponíveis.
    driverDfPlayer.setTimeOut(TIMEOUT_DFPLAYER_MS);
    driverDfPlayer.begin(serialDfPlayer, false, true);

    // Estes dois comandos também estabilizam módulos que acabaram de sair de
    // reset antes da primeira consulta ao cartão.
    driverDfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    driverDfPlayer.volume(estado.volumeModulo);
    delay(100);

    int quantidade = prepararCatalogoSons();
    driverDfPlayer.disableDAC();
    if (quantidade < 0) {
        estado.erro = ErroDfPlayer::MODULO_NAO_RESPONDE;
        estado.fase = FaseDfPlayer::ERRO;
        return false;
    }

    estado.moduloRespondendo = true;
    if (quantidade == 0) {
        estado.erro = ErroDfPlayer::PASTA_SEM_SONS;
        estado.fase = FaseDfPlayer::ERRO;
        return false;
    }

    estado.quantidadeSons = catalogo.quantidade;
    estado.pronto = true;
    estado.erro = ErroDfPlayer::NENHUM;
    estado.fase = FaseDfPlayer::PRONTO;

    Serial.printf(
        "DFPlayer: %u som(ns) encontrado(s) em /%02u.\n",
        static_cast<unsigned>(estado.quantidadeSons),
        static_cast<unsigned>(PASTA_SONS_DFPLAYER)
    );
    return true;
}

void processarDfPlayer() {
    if (!estado.moduloRespondendo) {
        return;
    }

    const bool estavaReproduzindo = estado.reproduzindo;
    estado.reproduzindo = digitalRead(PIN_DFPLAYER_BUSY) == LOW;
    if (
        DEBUG_CHIME &&
        (
            !nivelBusyConhecido ||
            estado.reproduzindo != ultimoBusyAtivo
        )
    ) {
        nivelBusyConhecido = true;
        ultimoBusyAtivo = estado.reproduzindo;
        Serial.printf(
            "CHIME DEBUG DFPlayer BUSY=%u uso=%u fase=%u.\n",
            estado.reproduzindo ? 1U : 0U,
            static_cast<unsigned>(estado.uso),
            static_cast<unsigned>(estado.fase)
        );
    }
    if (
        estado.reproduzindo &&
        estado.fase == FaseDfPlayer::INICIANDO
    ) {
        estado.fase = FaseDfPlayer::TOCANDO;
    } else if (
        !estado.reproduzindo &&
        estavaReproduzindo &&
        estado.fase == FaseDfPlayer::TOCANDO
    ) {
        // Alguns módulos não enviam o evento PlayFinished de forma confiável.
        // A borda do pino BUSY também conclui a reprodução sem bloquear o loop.
        estado.fase = FaseDfPlayer::CONCLUIDO;
    }

    while (driverDfPlayer.available()) {
        const uint8_t tipo = driverDfPlayer.readType();
        const uint16_t detalhe = driverDfPlayer.read();
        estado.ultimoEvento = tipo;
        estado.detalheUltimoEvento = detalhe;

        if (tipo == DFPlayerPlayFinished) {
            estado.reproduzindo = false;
            estado.fase = FaseDfPlayer::CONCLUIDO;
        } else if (tipo == DFPlayerCardRemoved) {
            estado.pronto = false;
            estado.quantidadeSons = 0;
            catalogo = CatalogoSonsDfPlayer{};
            estado.erro = ErroDfPlayer::PASTA_SEM_SONS;
            estado.fase = FaseDfPlayer::ERRO;
            desativarSaidaDfPlayer();
        } else if (tipo == DFPlayerError) {
            if (erroInterrompeReproducao(detalhe)) {
                estado.erro = ErroDfPlayer::ERRO_REPORTADO_PELO_MODULO;
                estado.fase = FaseDfPlayer::ERRO;
                estado.reproduzindo = false;
                desativarSaidaDfPlayer();
                Serial.printf(
                    "DFPlayer: falha de reproducao %u.\n",
                    static_cast<unsigned>(detalhe)
                );
            } else {
                estado.avisosComunicacao++;
                Serial.printf(
                    "DFPlayer: aviso serial %u ignorado; audio mantido.\n",
                    static_cast<unsigned>(detalhe)
                );
            }
        }
    }
}

CatalogoSonsDfPlayer obterCatalogoSonsDfPlayer() {
    return catalogo;
}

bool obterSomDfPlayer(uint8_t numeroSom, SomDfPlayer& som) {
    if (
        !catalogo.disponivel ||
        numeroSom == 0 ||
        numeroSom > catalogo.quantidade
    ) {
        return false;
    }

    som = SomDfPlayer{};
    som.numero = numeroSom;
    snprintf(
        som.arquivo,
        sizeof(som.arquivo),
        "%03u.mp3",
        static_cast<unsigned>(numeroSom)
    );
    snprintf(
        som.nome,
        sizeof(som.nome),
        "Som %03u",
        static_cast<unsigned>(numeroSom)
    );
    return true;
}

bool reproduzirSomDfPlayer(
    uint8_t numeroSom,
    uint8_t volumeEquipamento,
    UsoDfPlayer uso
) {
    if (!estado.pronto) {
        if (DEBUG_CHIME) {
            Serial.printf(
                "CHIME DEBUG DFPlayer rejeitou pedido: modulo nao pronto, "
                "fase=%u erroAnterior=%u.\n",
                static_cast<unsigned>(estado.fase),
                static_cast<unsigned>(estado.erro)
            );
        }
        estado.erro = ErroDfPlayer::NAO_INICIADO;
        estado.fase = FaseDfPlayer::ERRO;
        return false;
    }

    if (
        numeroSom == 0 ||
        numeroSom > estado.quantidadeSons
    ) {
        if (DEBUG_CHIME) {
            Serial.printf(
                "CHIME DEBUG DFPlayer rejeitou som %u; quantidade=%u.\n",
                static_cast<unsigned>(numeroSom),
                static_cast<unsigned>(estado.quantidadeSons)
            );
        }
        estado.erro = ErroDfPlayer::NUMERO_DE_SOM_INVALIDO;
        estado.fase = FaseDfPlayer::ERRO;
        return false;
    }

    if (uso == UsoDfPlayer::NENHUM) {
        if (DEBUG_CHIME) {
            Serial.println(
                "CHIME DEBUG DFPlayer rejeitou pedido sem finalidade."
            );
        }
        estado.erro = ErroDfPlayer::NAO_INICIADO;
        estado.fase = FaseDfPlayer::ERRO;
        return false;
    }

    definirVolumeDfPlayer(volumeEquipamento);

    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG DFPlayer pedido: uso=%u som=%03u volume=%u/%u "
            "volumeModulo=%u pronto=%u quantidade=%u BUSY=%u.\n",
            static_cast<unsigned>(uso),
            static_cast<unsigned>(numeroSom),
            static_cast<unsigned>(estado.volumeEquipamento),
            static_cast<unsigned>(VOLUME_MAXIMO),
            static_cast<unsigned>(estado.volumeModulo),
            estado.pronto ? 1U : 0U,
            static_cast<unsigned>(estado.quantidadeSons),
            digitalRead(PIN_DFPLAYER_BUSY) == LOW ? 1U : 0U
        );
    }

    // DAC_L/DAC_R chegam diretamente aos NS8002. O relé em GPA5 não participa;
    // somente GPA7/GPA6, ativos em LOW, habilitam os amplificadores.
    if (!saidaDfPlayerAtiva) {
        estadoSaidaAntesDfPlayer = obterEstadoSaidaAudio();
        if (DEBUG_CHIME) {
            Serial.printf(
                "CHIME DEBUG saida antes do DFPlayer: valido=%u "
                "portaA(GPA7..GPA5)=0x%02X.\n",
                estadoSaidaAntesDfPlayer.valido ? 1U : 0U,
                estadoSaidaAntesDfPlayer.valorPortaA
            );
        }
        driverDfPlayer.enableDAC();
        if (!configurarSaidaAudioDfPlayer(false)) {
            driverDfPlayer.disableDAC();
            restaurarEstadoSaidaAudio(estadoSaidaAntesDfPlayer);
            estadoSaidaAntesDfPlayer = EstadoSaidaAudio{};
            estado.reproduzindo = false;
            estado.fase = FaseDfPlayer::ERRO;
            estado.uso = uso;
            estado.erro = ErroDfPlayer::SAIDA_AUDIO_INDISPONIVEL;
            Serial.println(
                "CHIME DEBUG DFPlayer abortado: nao foi possivel confirmar "
                "GPA7=LOW e GPA6=LOW."
            );
            return false;
        }
        saidaDfPlayerAtiva = true;
    }

    driverDfPlayer.playFolder(PASTA_SONS_DFPLAYER, numeroSom);
    estado.somAtual = numeroSom;
    estado.reproduzindo = false;
    estado.fase = FaseDfPlayer::INICIANDO;
    estado.uso = uso;
    estado.erro = ErroDfPlayer::NENHUM;
    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG comando playFolder enviado: /%02u/%03u.mp3.\n",
            static_cast<unsigned>(PASTA_SONS_DFPLAYER),
            static_cast<unsigned>(numeroSom)
        );
    }
    return true;
}

bool deslocarSomDfPlayer(
    long deslocamento,
    uint8_t volumeEquipamento,
    UsoDfPlayer uso
) {
    if (!estado.pronto || deslocamento == 0) {
        return false;
    }

    const int32_t total = estado.quantidadeSons;
    int32_t origem;

    if (estado.somAtual == 0) {
        // O primeiro passo positivo deve selecionar 001; o primeiro negativo,
        // o último arquivo da pasta.
        origem = deslocamento > 0 ? -1 : 0;
    } else {
        origem = estado.somAtual - 1;
    }

    int32_t indice = (origem + deslocamento) % total;
    if (indice < 0) {
        indice += total;
    }

    return reproduzirSomDfPlayer(
        static_cast<uint8_t>(indice + 1),
        volumeEquipamento,
        uso
    );
}

void pararDfPlayer() {
    if (estado.moduloRespondendo) {
        driverDfPlayer.stop();
    }

    // A saída física precisa ser liberada mesmo se a UART do módulo deixou
    // de responder. Assim GPA7/GPA6 não permanecem ativos depois de uma
    // prévia interrompida ou de uma falha do DFPlayer.
    desativarSaidaDfPlayer();
    estado.reproduzindo = false;
    estado.fase = estado.pronto
        ? FaseDfPlayer::PRONTO
        : FaseDfPlayer::INDISPONIVEL;
    estado.uso = UsoDfPlayer::NENHUM;
}

void definirVolumeDfPlayer(uint8_t volumeEquipamento) {
    estado.volumeEquipamento =
        limitarVolumeEquipamento(volumeEquipamento);
    estado.volumeModulo =
        converterVolumeParaModulo(estado.volumeEquipamento);
    if (estado.moduloRespondendo) {
        driverDfPlayer.volume(estado.volumeModulo);
    }

    if (saidaDfPlayerAtiva) {
        configurarSaidaAudioDfPlayer(estado.volumeEquipamento == 0);
    }
}

EstadoDfPlayer obterEstadoDfPlayer() {
    return estado;
}

const char* obterTextoFaseDfPlayer(FaseDfPlayer fase) {
    switch (fase) {
        case FaseDfPlayer::PRONTO:
            return "pronto";
        case FaseDfPlayer::INICIANDO:
            return "iniciando";
        case FaseDfPlayer::TOCANDO:
            return "tocando";
        case FaseDfPlayer::CONCLUIDO:
            return "concluido";
        case FaseDfPlayer::ERRO:
            return "erro";
        case FaseDfPlayer::INDISPONIVEL:
        default:
            return "indisponivel";
    }
}
