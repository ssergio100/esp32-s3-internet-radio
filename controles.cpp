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
unsigned long inicioPressionamentoBotao = 0;
unsigned long inicioValidacaoSolturaBotao = 0;

enum class EstadoBotao {
    SOLTO,
    VALIDANDO_PRESSAO,
    PRESSIONADO,
    VALIDANDO_SOLTURA
};

enum class EventoBotao {
    NENHUM,
    CLIQUE_CURTO,
    CLIQUE_LONGO
};

EstadoBotao estadoBotao = EstadoBotao::SOLTO;

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

EventoBotao detectarEventoBotaoConfirmado() {
    unsigned long agora = millis();

    switch (estadoBotao) {
        case EstadoBotao::SOLTO:
            if (consumirBordaBotao()) {
                inicioPressionamentoBotao = agora;
                estadoBotao =
                    EstadoBotao::VALIDANDO_PRESSAO;
            }
            break;

        case EstadoBotao::VALIDANDO_PRESSAO:
            if (digitalRead(PIN_ENCODER_SW) == HIGH) {
                estadoBotao = EstadoBotao::SOLTO;
            } else if (
                agora - inicioPressionamentoBotao >=
                TEMPO_VALIDACAO_CLIQUE_ENCODER_MS
            ) {
                estadoBotao = EstadoBotao::PRESSIONADO;
            }
            break;

        case EstadoBotao::PRESSIONADO:
            if (digitalRead(PIN_ENCODER_SW) == HIGH) {
                inicioValidacaoSolturaBotao = agora;
                estadoBotao =
                    EstadoBotao::VALIDANDO_SOLTURA;
            }
            break;

        case EstadoBotao::VALIDANDO_SOLTURA:
            if (digitalRead(PIN_ENCODER_SW) == LOW) {
                estadoBotao = EstadoBotao::PRESSIONADO;
                break;
            }

            if (
                agora - inicioValidacaoSolturaBotao <
                TEMPO_VALIDACAO_CLIQUE_ENCODER_MS
            ) {
                break;
            }

            estadoBotao = EstadoBotao::SOLTO;

            // Descarta uma eventual borda de bounce acumulada durante
            // a confirmação da soltura.
            consumirBordaBotao();

            if (
                inicioValidacaoSolturaBotao -
                    inicioPressionamentoBotao >=
                TEMPO_CLIQUE_LONGO_ENCODER_MS
            ) {
                return EventoBotao::CLIQUE_LONGO;
            }

            if (
                agora - ultimoClique <
                INTERVALO_MINIMO_CLIQUES_ENCODER_MS
            ) {
                break;
            }

            ultimoClique = agora;
            return EventoBotao::CLIQUE_CURTO;
    }

    return EventoBotao::NENHUM;
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
    EventoBotao eventoBotao =
        detectarEventoBotaoConfirmado();

    if (eventoBotao == EventoBotao::CLIQUE_LONGO) {
        leitura.cliqueLongoDetectado = true;

        return leitura;
    }

    if (eventoBotao == EventoBotao::CLIQUE_CURTO) {
        leitura.cliqueDetectado = true;

        return leitura;
    }

    // A própria biblioteca informa o deslocamento acumulado e sua direção.
    leitura.deslocamentoEncoder =
        encoder.encoderChanged();

    return leitura;
}
