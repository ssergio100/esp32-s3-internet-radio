#include <Arduino.h>
#include <time.h>
#include "configuracao.h"
#include "radios.h"
#include "display_radio.h"
#include "wifi_radio.h"
#include "audio_radio.h"
#include "controles.h"
#include <WiFi.h>
#include "servidor_web.h"

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

void atualizarLedBuffer();

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

    

    configTime(
        -3 * 3600,
        0,
        "pool.ntp.org",
        "time.nist.gov"
    );

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
    processarDisplay();
    processarServidorWeb();

    EventoEncoder evento = lerControles();

    processarEventoEncoder(evento);

    verificarTelaVolume();
    
    verificarConfirmacaoRadio();

    // verificarBufferAudio();

    atualizarLedBuffer();

    status();
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
            modoControle = ModoControle::SELECAO_RADIO;

            telaVolumeAtiva = false;

            radioSelecionada = radioAtual;
            radioAguardandoConfirmacao = false;

            exibirRadio(radioSelecionada);

            Serial.println("Modo: seleção de rádio");
        } else {
            modoControle = ModoControle::VOLUME;

            radioSelecionada = radioAtual;
            radioAguardandoConfirmacao = false;

            exibirRadio(radioAtual);

            Serial.println("Modo: volume");
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
        "Volume: %d\n",
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
    if (modoControle != ModoControle::VOLUME) {
        telaVolumeAtiva = false;
        return;
    }

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

    exibirRadio(radioAtual);
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

void verificarBufferAudio() {
    static unsigned long ultimaVerificacao = 0;
    static uint8_t menorBuffer = 100;

    if (millis() - ultimaVerificacao < 250) {
        return;
    }

    ultimaVerificacao = millis();

    uint8_t percentual =
        obterPercentualBufferAudio();

    if (percentual < menorBuffer) {
        menorBuffer = percentual;
    }

    Serial.printf(
        "Buffer: %u%% | mínimo: %u%%\n",
        percentual,
        menorBuffer
    );
}

void status(){

    static unsigned long ultimaAtualizacaoStatus = 0;

    if (millis() - ultimaAtualizacaoStatus < 1000) {
        return;
    }
    ultimaAtualizacaoStatus = millis();

    Serial.printf("Temperatura: %.1f °C\n", temperatureRead());
    Serial.printf("RAM livre: %u KB\n", ESP.getFreeHeap() / 1024);
    Serial.printf("Menor RAM livre: %u KB\n", ESP.getMinFreeHeap() / 1024);
    Serial.printf("PSRAM livre: %u KB\n", ESP.getFreePsram() / 1024);
    Serial.printf("Sinal Wi-Fi: %d dBm\n", WiFi.RSSI());
}

void atualizarLedBuffer() {
    static unsigned long ultimaAtualizacao = 0;

    if (millis() - ultimaAtualizacao < 250) {
        return;
    }

    ultimaAtualizacao = millis();

    uint8_t buffer =
        obterPercentualBufferAudio();

    if (buffer == 0) {
        // Rádio fora ou sem receber áudio
        rgbLedWrite(
            PIN_LED_RGB,
            0,
            0,
            0
        );

    } else if (buffer <= 5) {
        // Buffer crítico: vermelho
        rgbLedWrite(
            PIN_LED_RGB,
            BRILHO_LED_RGB,
            0,
            0
        );

    } else if (buffer <= 15) {
        // Buffer baixo: amarelo
        rgbLedWrite(
            PIN_LED_RGB,
            BRILHO_LED_RGB,
            BRILHO_LED_RGB,
            0
        );

    } else {
        // Buffer normal: verde
        rgbLedWrite(
            PIN_LED_RGB,
            0,
            BRILHO_LED_RGB,
            0
        );
    }
}