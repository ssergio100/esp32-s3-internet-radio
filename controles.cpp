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
    PASSOS_ENCODER,
    false
);

long valorAnterior = 0;

bool botaoAnterior = false;

unsigned long ultimoClique = 0;

void IRAM_ATTR encoderISR() {
    encoder.readEncoder_ISR();
}

}

void iniciarControles() {
    encoder.begin();

    encoder.setup(
        encoderISR
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

    valorAnterior = encoder.readEncoder();

    botaoAnterior =
        encoder.isEncoderButtonDown();

    Serial.println("Encoder inicializado.");
}

EventoEncoder lerControles() {
    bool botaoAtual =
        encoder.isEncoderButtonDown();

    if (
        botaoAtual &&
        !botaoAnterior &&
        millis() - ultimoClique >= DEBOUNCE_ENCODER_MS
    ) {
        ultimoClique = millis();
        botaoAnterior = botaoAtual;

        return EventoEncoder::CLIQUE;
    }

    botaoAnterior = botaoAtual;

    if (!encoder.encoderChanged()) {
        return EventoEncoder::NENHUM;
    }

    long valorAtual =
        encoder.readEncoder();

    EventoEncoder evento =
        EventoEncoder::NENHUM;

    if (valorAtual > valorAnterior) {
        evento = EventoEncoder::DIREITA;
    } else if (valorAtual < valorAnterior) {
        evento = EventoEncoder::ESQUERDA;
    }

    valorAnterior = valorAtual;

    return evento;
}