#include <Arduino.h>

#include "configuracao.h"
#include "secrets.h"
#include "radios.h"
#include "display_radio.h"
#include "wifi_radio.h"
#include "audio_radio.h"
#include "controles.h"

enum class ModoControle {
    VOLUME,
    SELECAO_RADIO
};

ModoControle modoControle = ModoControle::VOLUME;

int volume = VOLUME_PADRAO;

int radioAtual = 0;
int radioSelecionada = 0;

unsigned long ultimaAlteracaoVolume = 0;
unsigned long ultimaAlteracaoRadio = 0;

bool telaVolumeAtiva = false;
bool radioAguardandoConfirmacao = false;

// =====================================================
// Protótipos
// =====================================================

void exibirRadio(int indice);
void iniciarRadio(int indice);

void processarEventoEncoder(
    EventoEncoder evento
);

void processarModoVolume(
    EventoEncoder evento
);

void processarModoSelecaoRadio(
    EventoEncoder evento
);

void verificarTelaVolume();
void verificarConfirmacaoRadio();

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

    iniciarAudio(volume);

    radioAtual = 0;
    radioSelecionada = radioAtual;

    iniciarRadio(radioAtual);
}

// =====================================================
// Loop
// =====================================================

void loop() {
    processarAudio();

    EventoEncoder evento =
        lerControles();

    processarEventoEncoder(evento);

    verificarTelaVolume();
    verificarConfirmacaoRadio();
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
        if (modoControle == ModoControle::VOLUME) {
            modoControle =
                ModoControle::SELECAO_RADIO;

            radioSelecionada =
                radioAtual;

            radioAguardandoConfirmacao =
                false;

            exibirRadio(
                radioSelecionada
            );

            Serial.println(
                "Modo: seleção de rádio"
            );

        } else {
            modoControle =
                ModoControle::VOLUME;

            radioSelecionada =
                radioAtual;

            radioAguardandoConfirmacao =
                false;

            exibirRadio(
                radioAtual
            );

            Serial.println(
                "Modo: volume"
            );
        }

        return;
    }

    if (modoControle == ModoControle::VOLUME) {
        processarModoVolume(evento);
    } else {
        processarModoSelecaoRadio(evento);
    }
}

// =====================================================
// Modo volume
// =====================================================

void processarModoVolume(
    EventoEncoder evento
) {
    int novoVolume = volume;

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

    if (novoVolume == volume) {
        return;
    }

    volume = novoVolume;

    alterarVolumeAudio(volume);
    mostrarVolume(volume);

    telaVolumeAtiva = true;

    ultimaAlteracaoVolume =
        millis();

    Serial.printf(
        "Volume: %d%%\n",
        volume
    );
}

// =====================================================
// Modo seleção de rádio
// =====================================================

void processarModoSelecaoRadio(
    EventoEncoder evento
) {
    int quantidade =
        obterQuantidadeRadios();

    if (quantidade <= 0) {
        return;
    }

    if (evento == EventoEncoder::DIREITA) {
        radioSelecionada++;
    }

    if (evento == EventoEncoder::ESQUERDA) {
        radioSelecionada--;
    }

    // Navegação circular
    if (radioSelecionada >= quantidade) {
        radioSelecionada = 0;
    }

    if (radioSelecionada < 0) {
        radioSelecionada =
            quantidade - 1;
    }

    exibirRadio(
        radioSelecionada
    );

    ultimaAlteracaoRadio =
        millis();

    radioAguardandoConfirmacao =
        true;

    Serial.print(
        "Selecionada: "
    );

    const Radio* radio =
        obterRadio(
            radioSelecionada
        );

    if (radio != nullptr) {
        Serial.println(
            radio->nome
        );
    }
}

// =====================================================
// Confirmação automática após 1 segundo
// =====================================================

void verificarConfirmacaoRadio() {
    if (
        modoControle !=
            ModoControle::SELECAO_RADIO
    ) {
        return;
    }

    if (!radioAguardandoConfirmacao) {
        return;
    }

    if (
        millis() - ultimaAlteracaoRadio <
            TEMPO_CONFIRMAR_RADIO_MS
    ) {
        return;
    }

    radioAguardandoConfirmacao =
        false;

    if (radioSelecionada == radioAtual) {
        return;
    }

    iniciarRadio(
        radioSelecionada
    );
}

// =====================================================
// Retorno automático da tela de volume
// =====================================================

void verificarTelaVolume() {
    if (!telaVolumeAtiva) {
        return;
    }

    if (
        millis() - ultimaAlteracaoVolume <
            TEMPO_TELA_VOLUME_MS
    ) {
        return;
    }

    telaVolumeAtiva = false;

    exibirRadio(
        radioAtual
    );
}

// =====================================================
// Rádio
// =====================================================

void iniciarRadio(int indice) {
    const Radio* radio =
        obterRadio(indice);

    if (radio == nullptr) {
        mostrarMensagem(
            "Radio invalida"
        );

        return;
    }

    mostrarMensagem(
        "Conectando..."
    );

    bool conectado =
        tocarRadio(
            radio->nome,
            radio->url
        );

    if (!conectado) {
        mostrarMensagem(
            "Falha na radio"
        );

        delay(800);

        exibirRadio(
            radioAtual
        );

        return;
    }

    radioAtual = indice;
    radioSelecionada = indice;

    exibirRadio(
        radioAtual
    );
}

void exibirRadio(int indice) {
    const Radio* radio =
        obterRadio(indice);

    if (radio == nullptr) {
        return;
    }

    mostrarNomeRadio(
        radio->nome,
        indice,
        obterQuantidadeRadios()
    );
}