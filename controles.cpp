#include "controles.h"
#include "configuracao.h"

#include <Arduino.h>
#include "AiEsp32RotaryEncoder.h"

namespace {

AiEsp32RotaryEncoder encoderNavegacao(
    ENCODER_NAVEGACAO.pinoDt,
    ENCODER_NAVEGACAO.pinoClk,
    ENCODER_NAVEGACAO.pinoBotao,
    ENCODER_NAVEGACAO.pinoVcc,
    ENCODER_NAVEGACAO.transicoesPorDetente,
    false
);

AiEsp32RotaryEncoder encoderVolume(
    ENCODER_VOLUME.pinoDt,
    ENCODER_VOLUME.pinoClk,
    ENCODER_VOLUME.pinoBotao,
    ENCODER_VOLUME.pinoVcc,
    ENCODER_VOLUME.transicoesPorDetente,
    false
);

portMUX_TYPE muxBotao =
    portMUX_INITIALIZER_UNLOCKED;

enum class EstadoBotao {
    SOLTO,
    VALIDANDO_PRESSAO,
    PRESSIONADO,
    VALIDANDO_SOLTURA
};

enum class EventoBotao {
    NENHUM,
    CLIQUE_CURTO,
    PRESSAO_LONGA
};

struct ControleBotao {
    volatile bool bordaPendente = false;
    unsigned long ultimoClique = 0;
    unsigned long inicioPressionamento = 0;
    unsigned long inicioValidacaoSoltura = 0;
    EstadoBotao estado = EstadoBotao::SOLTO;
};

ControleBotao botaoNavegacao;
ControleBotao botaoVolume;

void IRAM_ATTR encoderNavegacaoISR() {
    encoderNavegacao.readEncoder_ISR();
}

void IRAM_ATTR encoderVolumeISR() {
    encoderVolume.readEncoder_ISR();
}

void IRAM_ATTR botaoNavegacaoISR() {
    portENTER_CRITICAL_ISR(&muxBotao);
    botaoNavegacao.bordaPendente = true;
    portEXIT_CRITICAL_ISR(&muxBotao);
}

void IRAM_ATTR botaoVolumeISR() {
    portENTER_CRITICAL_ISR(&muxBotao);
    botaoVolume.bordaPendente = true;
    portEXIT_CRITICAL_ISR(&muxBotao);
}

bool consumirBordaBotao(ControleBotao& controle) {
    bool pendente;

    portENTER_CRITICAL(&muxBotao);
    pendente = controle.bordaPendente;
    controle.bordaPendente = false;
    portEXIT_CRITICAL(&muxBotao);

    return pendente;
}

EventoBotao detectarEventoBotaoConfirmado(
    ControleBotao& controle,
    int pinoBotao
) {
    unsigned long agora = millis();

    switch (controle.estado) {
        case EstadoBotao::SOLTO:
            if (consumirBordaBotao(controle)) {
                controle.inicioPressionamento = agora;
                controle.estado =
                    EstadoBotao::VALIDANDO_PRESSAO;
            }
            break;

        case EstadoBotao::VALIDANDO_PRESSAO:
            if (digitalRead(pinoBotao) == HIGH) {
                controle.estado = EstadoBotao::SOLTO;
            } else if (
                agora - controle.inicioPressionamento >=
                TEMPO_VALIDACAO_CLIQUE_ENCODER_MS
            ) {
                controle.estado = EstadoBotao::PRESSIONADO;
            }
            break;

        case EstadoBotao::PRESSIONADO:
            if (digitalRead(pinoBotao) == HIGH) {
                controle.inicioValidacaoSoltura = agora;
                controle.estado =
                    EstadoBotao::VALIDANDO_SOLTURA;
            }
            break;

        case EstadoBotao::VALIDANDO_SOLTURA:
            if (digitalRead(pinoBotao) == LOW) {
                controle.estado = EstadoBotao::PRESSIONADO;
                break;
            }

            if (
                agora - controle.inicioValidacaoSoltura <
                TEMPO_VALIDACAO_CLIQUE_ENCODER_MS
            ) {
                break;
            }

            controle.estado = EstadoBotao::SOLTO;

            // Descarta uma eventual borda de bounce acumulada durante
            // a confirmação da soltura.
            consumirBordaBotao(controle);

            if (
                controle.inicioValidacaoSoltura -
                    controle.inicioPressionamento >=
                TEMPO_MAXIMO_CLIQUE_CURTO_ENCODER_MS
            ) {
                return EventoBotao::PRESSAO_LONGA;
            }

            if (
                agora - controle.ultimoClique <
                INTERVALO_MINIMO_CLIQUES_ENCODER_MS
            ) {
                break;
            }

            controle.ultimoClique = agora;
            return EventoBotao::CLIQUE_CURTO;
    }

    return EventoBotao::NENHUM;
}

void iniciarRotacaoEncoder(
    AiEsp32RotaryEncoder& encoder,
    void (*rotinaInterrupcao)()
) {
    encoder.begin();
    encoder.setup(rotinaInterrupcao);

    // O limite amplo evita perder deslocamentos acumulados. As regras de
    // navegação e volume ficam no programa principal.
    encoder.setBoundaries(
        -10000,
        10000,
        false
    );

    encoder.setEncoderValue(0);
    encoder.disableAcceleration();
}

}

void iniciarControles() {
    iniciarRotacaoEncoder(
        encoderNavegacao,
        encoderNavegacaoISR
    );

    iniciarRotacaoEncoder(
        encoderVolume,
        encoderVolumeISR
    );

    attachInterrupt(
        digitalPinToInterrupt(
            ENCODER_NAVEGACAO.pinoBotao
        ),
        botaoNavegacaoISR,
        FALLING
    );

    attachInterrupt(
        digitalPinToInterrupt(
            ENCODER_VOLUME.pinoBotao
        ),
        botaoVolumeISR,
        FALLING
    );

    botaoNavegacao.ultimoClique =
        millis() - INTERVALO_MINIMO_CLIQUES_ENCODER_MS;
    botaoVolume.ultimoClique =
        millis() - INTERVALO_MINIMO_CLIQUES_ENCODER_MS;

    Serial.println("Encoders de navegacao e volume inicializados.");
}

LeituraControles lerControles() {
    LeituraControles leitura;
    EventoBotao eventoBotaoNavegacao =
        detectarEventoBotaoConfirmado(
            botaoNavegacao,
            ENCODER_NAVEGACAO.pinoBotao
        );
    EventoBotao eventoBotaoVolume =
        detectarEventoBotaoConfirmado(
            botaoVolume,
            ENCODER_VOLUME.pinoBotao
        );

    // O volume é independente dos eventos do botão principal e nunca deve
    // perder passos porque houve um clique no mesmo ciclo.
    leitura.deslocamentoEncoderVolume =
        encoderVolume.encoderChanged();

    // Somente cliques curtos chegam à aplicação. Pressões longas são
    // reconhecidas para não virarem um clique ao soltar, mas não geram ação.
    if (eventoBotaoNavegacao == EventoBotao::CLIQUE_CURTO) {
        leitura.cliqueNavegacaoDetectado = true;
    }

    if (eventoBotaoVolume == EventoBotao::CLIQUE_CURTO) {
        leitura.cliqueVolumeDetectado = true;
    }

    if (
        leitura.cliqueNavegacaoDetectado ||
        leitura.cliqueVolumeDetectado
    ) {
        return leitura;
    }

    // A própria biblioteca informa o deslocamento acumulado e sua direção.
    leitura.deslocamentoEncoderNavegacao =
        encoderNavegacao.encoderChanged();

    return leitura;
}
