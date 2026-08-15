/*
 * Arquivo principal e mapa de execução do firmware.
 *
 * O setup() inicializa, nesta ordem: interface física, Wi-Fi,
 * armazenamento/servidor, lista de estações, relógio e serviço de áudio.
 *
 * O loop() apenas coordena os módulos. A reprodução de áudio acontece em
 * uma tarefa dedicada implementada em audio_radio.cpp.
 * As regras de volume e seleção de estação permanecem neste arquivo para
 * que o fluxo principal do produto possa ser lido em um só lugar.
 */

#include <Arduino.h>

#include "configuracao.h"
#include "radios.h"
#include "display_radio.h"
#include "wifi_radio.h"
#include "audio_radio.h"
#include "arquivos_audio.h"
#include "controles.h"
#include "indicador_led.h"
#include "relogio.h"
#include "servidor_web.h"
#include "sono_profundo.h"
#include "telemetria.h"

enum class ModoInterface {
    VOLUME,
    SELECAO_RADIO
};

// Estado da interação com o encoder e o display.
ModoInterface modoInterface = ModoInterface::VOLUME;

int volumeAtual = VOLUME_PADRAO;

int indiceRadioAtual = 0;
int indiceRadioEmSelecao = 0;

unsigned long momentoUltimaAlteracaoVolumeMs = 0;
unsigned long momentoUltimaAtividadeSelecaoMs = 0;

bool barraVolumeVisivel = false;

bool preparandoSonoProfundo = false;
unsigned long momentoSolicitacaoSonoProfundoMs = 0;

// =====================================================
// Protótipos
// =====================================================

void mostrarRadioNoDisplay(int indiceRadio);
void solicitarReproducaoRadio(int indiceRadio);

void entrarModoSelecaoRadio();
void confirmarSelecaoRadio();

void solicitarEntradaSonoProfundo();
void concluirEntradaSonoProfundoQuandoAudioParar();

void processarLeituraControles(
    const LeituraControles& leitura
);

void processarAjusteVolume(
    long deslocamentoEncoder
);

void processarNavegacaoRadios(
    long deslocamentoEncoder
);

void restaurarBarraAposTempoVolume();
void cancelarSelecaoRadioPorInatividade();

void atualizarDisplayEstadoAudio(
    bool forcarAtualizacao = false
);

// =====================================================
// Setup
// =====================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    informarMotivoDespertar();

    iniciarDisplay();
    iniciarControles();

    conectarWifi();

    if (!iniciarArmazenamentoEServidorWeb()) {
        mostrarMensagem(
            "Erro no armazenamento"
        );

        return;
    }

    carregarRadios();

    // A ausência do cartão não impede a função principal de rádio web.
    iniciarArquivosAudio();

    iniciarRelogio();

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
    if (preparandoSonoProfundo) {
        concluirEntradaSonoProfundoQuandoAudioParar();
        return;
    }

    supervisionarWifi();
    processarDisplay();
    processarServidorWeb();

    LeituraControles leituraControles =
        lerControles();

    processarLeituraControles(leituraControles);

    if (preparandoSonoProfundo) {
        return;
    }

    cancelarSelecaoRadioPorInatividade();
    restaurarBarraAposTempoVolume();

    atualizarIndicadorEstadoAudio();
    atualizarDisplayEstadoAudio();

    registrarTelemetriaPeriodica();
}

// =====================================================
// Eventos dos controles
// =====================================================

void processarLeituraControles(
    const LeituraControles& leitura
) {
    if (leitura.cliqueLongoDetectado) {
        solicitarEntradaSonoProfundo();
        return;
    }

    if (leitura.cliqueDetectado) {
        if (modoInterface == ModoInterface::VOLUME) {
            entrarModoSelecaoRadio();
        } else {
            confirmarSelecaoRadio();
        }

        return;
    }

    if (leitura.deslocamentoEncoder == 0) {
        return;
    }

    if (modoInterface == ModoInterface::VOLUME) {
        processarAjusteVolume(
            leitura.deslocamentoEncoder
        );
    } else {
        processarNavegacaoRadios(
            leitura.deslocamentoEncoder
        );
    }
}

// =====================================================
// Sono profundo
// =====================================================

void solicitarEntradaSonoProfundo() {
    if (!configurarDespertarPeloBotaoEncoder()) {
        mostrarMensagem(
            "Falha ao desligar"
        );

        return;
    }

    preparandoSonoProfundo = true;
    momentoSolicitacaoSonoProfundoMs = millis();

    modoInterface = ModoInterface::VOLUME;
    barraVolumeVisivel = false;

    mostrarMensagem(
        "Desligando..."
    );
    apagarIndicadorLed();

    if (!pararAudio()) {
        Serial.println(
            "Comando de parada do audio nao foi aceito."
        );
    }

    Serial.println(
        "Clique longo: preparando sono profundo."
    );
}

void concluirEntradaSonoProfundoQuandoAudioParar() {
    EstadoAudio estadoAudio =
        obterStatusAudio().estado;

    bool audioParado =
        estadoAudio == EstadoAudio::PARADO ||
        estadoAudio == EstadoAudio::DESLIGADO;

    bool tempoEsgotado =
        millis() - momentoSolicitacaoSonoProfundoMs >=
        TEMPO_MAXIMO_PARADA_AUDIO_ANTES_SONO_MS;

    if (!audioParado && !tempoEsgotado) {
        return;
    }

    if (!audioParado) {
        Serial.println(
            "Tempo limite ao parar audio; continuando o desligamento."
        );
    }

    desligarDisplay();
    apagarIndicadorLed();

    // Sem controle elétrico de SD_MODE, os MAX98357A entrarão no standby
    // automático quando o deep sleep interromper o BCLK.
    entrarSonoProfundo();
}

void entrarModoSelecaoRadio() {
    modoInterface = ModoInterface::SELECAO_RADIO;
    barraVolumeVisivel = false;

    indiceRadioEmSelecao = indiceRadioAtual;
    momentoUltimaAtividadeSelecaoMs = millis();

    const Radio* radio =
        obterRadio(indiceRadioEmSelecao);

    if (radio != nullptr) {
        mostrarSelecaoRadio(
            radio->nome,
            indiceRadioEmSelecao,
            obterQuantidadeRadios()
        );
    }

    Serial.println("Modo: seleção de rádio");
}

void confirmarSelecaoRadio() {
    modoInterface = ModoInterface::VOLUME;

    if (indiceRadioEmSelecao == indiceRadioAtual) {
        atualizarDisplayEstadoAudio(true);
    } else {
        solicitarReproducaoRadio(indiceRadioEmSelecao);
    }

    Serial.println("Rádio confirmada; modo: volume");
}

// =====================================================
// Modo volume
// =====================================================

void processarAjusteVolume(
    long deslocamentoEncoder
) {
    long novoVolume =
        volumeAtual + deslocamentoEncoder;

    novoVolume = constrain(
        novoVolume,
        static_cast<long>(VOLUME_MINIMO),
        static_cast<long>(VOLUME_MAXIMO)
    );

    if (novoVolume == volumeAtual) {
        return;
    }

    volumeAtual = static_cast<int>(novoVolume);

    alterarVolumeAudio(volumeAtual);
    mostrarVolume(volumeAtual);

    barraVolumeVisivel = true;

    momentoUltimaAlteracaoVolumeMs =
        millis();

    Serial.printf(
        "Volume: %d\n",
        volumeAtual
    );
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
// Retorno automático do controle para volume
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

    modoInterface = ModoInterface::VOLUME;
    indiceRadioEmSelecao = indiceRadioAtual;
    barraVolumeVisivel = false;

    Serial.println(
        "Seleção cancelada por inatividade; modo: volume"
    );

    atualizarDisplayEstadoAudio(true);
}

// =====================================================
// Retorno automático da barra inferior
// =====================================================

void restaurarBarraAposTempoVolume() {
    if (modoInterface != ModoInterface::VOLUME) {
        barraVolumeVisivel = false;
        return;
    }

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

    atualizarDisplayEstadoAudio(true);
}

// =====================================================
// Rádio
// =====================================================

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
        modoInterface !=
            ModoInterface::VOLUME ||
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
