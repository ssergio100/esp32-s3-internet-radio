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
#include "controles.h"
#include "indicador_led.h"
#include "relogio.h"
#include "servidor_web.h"
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

bool telaVolumeVisivel = false;

// =====================================================
// Protótipos
// =====================================================

void mostrarRadioNoDisplay(int indiceRadio);
void solicitarReproducaoRadio(int indiceRadio);

void entrarModoSelecaoRadio();
void confirmarSelecaoRadio();

void processarEventoEncoder(
    EventoEncoder evento
);

void processarAjusteVolume(
    EventoEncoder evento
);

void processarNavegacaoRadios(
    EventoEncoder evento
);

void restaurarDisplayAposTempoVolume();
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

    iniciarDisplay();
    iniciarControles();

    if (!conectarWifi()) {
        return;
    }

    if (!iniciarServidorWeb()) {
        mostrarMensagem(
            "Erro no armazenamento"
        );

        return;
    }

    carregarRadios();

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
    supervisionarWifi();
    processarDisplay();
    processarServidorWeb();

    EventoEncoder evento = lerControles();

    processarEventoEncoder(evento);

    cancelarSelecaoRadioPorInatividade();
    restaurarDisplayAposTempoVolume();

    atualizarIndicadorEstadoAudio();
    atualizarDisplayEstadoAudio();

    registrarTelemetriaPeriodica();
}

// =====================================================
// Eventos do encoder
// =====================================================

void processarEventoEncoder(
    EventoEncoder evento
) {
    if (evento == EventoEncoder::NENHUM) {
        return;
    }

    if (evento == EventoEncoder::CLIQUE) {
        if (modoInterface == ModoInterface::VOLUME) {
            entrarModoSelecaoRadio();
        } else {
            confirmarSelecaoRadio();
        }

        return;
    }

    if (modoInterface == ModoInterface::VOLUME) {
        processarAjusteVolume(evento);
    } else {
        processarNavegacaoRadios(evento);
    }
}

void entrarModoSelecaoRadio() {
    modoInterface = ModoInterface::SELECAO_RADIO;
    telaVolumeVisivel = false;

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
    EventoEncoder evento
) {
    int novoVolume = volumeAtual;

    if (evento == EventoEncoder::DIREITA) {
        novoVolume++;
    }

    if (evento == EventoEncoder::ESQUERDA) {
        novoVolume--;
    }

    novoVolume = constrain(
        novoVolume,
        VOLUME_MINIMO,
        VOLUME_MAXIMO
    );

    if (novoVolume == volumeAtual) {
        return;
    }

    volumeAtual = novoVolume;

    alterarVolumeAudio(volumeAtual);
    mostrarVolume(volumeAtual);

    telaVolumeVisivel = true;

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
    EventoEncoder evento
) {
    int quantidadeRadios =
        obterQuantidadeRadios();

    if (quantidadeRadios <= 0) {
        return;
    }

    if (evento == EventoEncoder::DIREITA) {
        indiceRadioEmSelecao++;
    }

    if (evento == EventoEncoder::ESQUERDA) {
        indiceRadioEmSelecao--;
    }

    // Navegação circular
    if (indiceRadioEmSelecao >= quantidadeRadios) {
        indiceRadioEmSelecao = 0;
    }

    if (indiceRadioEmSelecao < 0) {
        indiceRadioEmSelecao =
            quantidadeRadios - 1;
    }

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
    telaVolumeVisivel = false;

    Serial.println(
        "Seleção cancelada por inatividade; modo: volume"
    );

    atualizarDisplayEstadoAudio(true);
}

// =====================================================
// Retorno automático da tela de volume
// =====================================================

void restaurarDisplayAposTempoVolume() {
    if (modoInterface != ModoInterface::VOLUME) {
        telaVolumeVisivel = false;
        return;
    }

    if (!telaVolumeVisivel) {
        return;
    }

    if (
        millis() - momentoUltimaAlteracaoVolumeMs <
        TEMPO_TELA_VOLUME_MS
    ) {
        return;
    }

    telaVolumeVisivel = false;

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

    mostrarMensagem(
        "Conectando..."
    );

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
        telaVolumeVisivel
    ) {
        return;
    }

    switch (statusAudio.estado) {
        case EstadoAudio::CONECTANDO:
            mostrarMensagem(
                "Conectando..."
            );
            break;

        case EstadoAudio::BUFFERIZANDO:
            mostrarMensagem(
                "Bufferizando..."
            );
            break;

        case EstadoAudio::TOCANDO:
        case EstadoAudio::DEGRADADO:
            mostrarRadioNoDisplay(indiceRadioAtual);
            break;

        case EstadoAudio::RECONECTANDO:
            mostrarMensagem(
                "Reconectando..."
            );
            break;

        case EstadoAudio::ERRO:
            mostrarMensagem(
                "Erro no audio"
            );
            break;

        default:
            break;
    }
}
