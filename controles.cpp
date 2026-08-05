#include "controles.h"
#include "configuracao.h"

#include <Arduino.h>
#include "AiEsp32RotaryEncoder.h"

namespace {

AiEsp32RotaryEncoder encoder(
    PIN_ENCODER_DT,
    PIN_ENCODER_CLK,
    PIN_ENCODER_SW,
    PIN_ENCODER_VCC,
    TRANSICOES_ENCODER_POR_DETENTE,
    false
);

volatile bool bordaBotaoPendente = false;

portMUX_TYPE muxBotao =
    portMUX_INITIALIZER_UNLOCKED;

unsigned long ultimoClique = 0;
unsigned long inicioValidacaoBotao = 0;

bool validandoBotao = false;
bool aguardandoSolturaBotao = false;

void IRAM_ATTR encoderISR() {
    encoder.readEncoder_ISR();
}

void IRAM_ATTR botaoISR() {
    portENTER_CRITICAL_ISR(&muxBotao);
    bordaBotaoPendente = true;
    portEXIT_CRITICAL_ISR(&muxBotao);
}

bool consumirBordaBotao() {
    bool pendente;

    portENTER_CRITICAL(&muxBotao);
    pendente = bordaBotaoPendente;
    bordaBotaoPendente = false;
    portEXIT_CRITICAL(&muxBotao);

    return pendente;
}

bool detectarCliqueConfirmado() {
    unsigned long agora = millis();
    bool bordaDetectada =
        consumirBordaBotao();

    if (
        aguardandoSolturaBotao
    ) {
        if (
            digitalRead(PIN_ENCODER_SW) ==
            HIGH
        ) {
            aguardandoSolturaBotao = false;
        }

        return false;
    }

    if (
        bordaDetectada &&
        !validandoBotao
    ) {
        validandoBotao = true;
        inicioValidacaoBotao = agora;
    }

    if (!validandoBotao) {
        return false;
    }

    if (
        digitalRead(PIN_ENCODER_SW) ==
        HIGH
    ) {
        validandoBotao = false;

        return false;
    }

    if (
        agora - inicioValidacaoBotao <
        TEMPO_VALIDACAO_CLIQUE_ENCODER_MS
    ) {
        return false;
    }

    validandoBotao = false;
    aguardandoSolturaBotao = true;

    if (
        agora - ultimoClique <
        INTERVALO_MINIMO_CLIQUES_ENCODER_MS
    ) {
        return false;
    }

    ultimoClique = agora;

    return true;
}

}

void iniciarControles() {
    encoder.begin();

    encoder.setup(
        encoderISR
    );

    attachInterrupt(
        digitalPinToInterrupt(PIN_ENCODER_SW),
        botaoISR,
        FALLING
    );

    // Limite amplo. Depois controlamos volume
    // e rádio no programa principal.
    encoder.setBoundaries(
        -10000,
        10000,
        false
    );

    encoder.setEncoderValue(0);

    encoder.disableAcceleration();

    ultimoClique =
        millis() - INTERVALO_MINIMO_CLIQUES_ENCODER_MS;

    Serial.println("Encoder inicializado.");
}

LeituraControles lerControles() {
    LeituraControles leitura;

    if (detectarCliqueConfirmado()) {
        leitura.cliqueDetectado = true;

        return leitura;
    }

    // A própria biblioteca informa o deslocamento acumulado e sua direção.
    leitura.deslocamentoEncoder =
        encoder.encoderChanged();

    return leitura;
}
