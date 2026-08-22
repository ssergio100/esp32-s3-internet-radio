#include "indicador_led.h"

#include <Arduino.h>

#include "audio_radio.h"
#include "configuracao.h"

namespace {

    enum class CorIndicadorLed {
        APAGADO,
        AZUL,
        VERDE,
        AMARELO,
        VERMELHO
    };

    void exibirCor(CorIndicadorLed cor) {
        int vermelho = 0;
        int verde = 0;
        int azul = 0;

        switch (cor) {
            case CorIndicadorLed::AZUL:
                azul = BRILHO_LED_RGB;
                break;

            case CorIndicadorLed::VERDE:
                verde = BRILHO_LED_RGB;
                break;

            case CorIndicadorLed::AMARELO:
                vermelho = BRILHO_LED_RGB;
                verde = BRILHO_LED_RGB;
                break;

            case CorIndicadorLed::VERMELHO:
                vermelho = BRILHO_LED_RGB;
                break;

            case CorIndicadorLed::APAGADO:
                break;
        }

        rgbLedWrite(
            PIN_LED_RGB,
            vermelho,
            verde,
            azul
        );
    }

    CorIndicadorLed obterCorEstadoAudio(
        EstadoAudio estado
    ) {
        switch (estado) {
            case EstadoAudio::CONECTANDO:
            case EstadoAudio::BUFFERIZANDO:
                return CorIndicadorLed::AZUL;

            case EstadoAudio::TOCANDO:
                return CorIndicadorLed::VERDE;

            case EstadoAudio::DEGRADADO:
                return CorIndicadorLed::AMARELO;

            case EstadoAudio::RECONECTANDO:
            case EstadoAudio::ERRO:
                return CorIndicadorLed::VERMELHO;

            case EstadoAudio::DESLIGADO:
            case EstadoAudio::PARADO:
                return CorIndicadorLed::APAGADO;
        }

        return CorIndicadorLed::APAGADO;
    }

}

void atualizarIndicadorConexaoWifi() {
    static unsigned long momentoUltimaAlternanciaMs = 0;
    static bool indicadorAceso = false;

    unsigned long agoraMs = millis();

    if (
        agoraMs - momentoUltimaAlternanciaMs <
        INTERVALO_PISCA_LED_CONEXAO_WIFI_MS
    ) {
        return;
    }

    momentoUltimaAlternanciaMs = agoraMs;
    indicadorAceso = !indicadorAceso;

    exibirCor(
        indicadorAceso
            ? CorIndicadorLed::AZUL
            : CorIndicadorLed::APAGADO
    );
}

void apagarIndicadorLed() {
    exibirCor(CorIndicadorLed::APAGADO);
}

void atualizarIndicadorEstadoAudio() {
    static unsigned long momentoUltimaVerificacaoMs = 0;
    static EstadoAudio ultimoEstadoExibido =
        EstadoAudio::DESLIGADO;

    unsigned long agoraMs = millis();

    if (
        agoraMs - momentoUltimaVerificacaoMs <
        INTERVALO_VERIFICACAO_LED_AUDIO_MS
    ) {
        return;
    }

    momentoUltimaVerificacaoMs = agoraMs;

    EstadoAudio estadoAtual = obterStatusAudio().estado;

    if (estadoAtual == ultimoEstadoExibido) {
        return;
    }

    ultimoEstadoExibido = estadoAtual;
    exibirCor(obterCorEstadoAudio(estadoAtual));
}
