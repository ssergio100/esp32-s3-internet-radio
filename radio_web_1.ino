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
unsigned long ultimaAtividadeSelecao = 0;

bool telaVolumeAtiva = false;

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
void verificarInatividadeSelecao();

void atualizarInterfaceAudio(
    bool forcar = false
);
void atualizarLedEstadoAudio();
void registrarStatusSistema();

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

    if (!iniciarAudio(volume)) {
        mostrarMensagem(
            "Erro no audio"
        );

        return;
    }

    radioAtual = 0;
    radioSelecionada = radioAtual;

    iniciarRadio(radioAtual);
}

// =====================================================
// Loop
// =====================================================

void loop() {
    processarDisplay();
    processarServidorWeb();

    EventoEncoder evento = lerControles();

    processarEventoEncoder(evento);

    verificarInatividadeSelecao();
    verificarTelaVolume();

    atualizarLedEstadoAudio();
    atualizarInterfaceAudio();

    registrarStatusSistema();
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
            ultimaAtividadeSelecao = millis();

            const Radio* radio =
                obterRadio(radioSelecionada);

            if (radio != nullptr) {
                mostrarSelecaoRadio(
                    radio->nome,
                    radioSelecionada,
                    obterQuantidadeRadios()
                );
            }

            Serial.println(
                "Modo: seleção de rádio"
            );
        } else {
            modoControle = ModoControle::VOLUME;

            if (
                radioSelecionada ==
                radioAtual
            ) {
                atualizarInterfaceAudio(true);
            } else {
                iniciarRadio(
                    radioSelecionada
                );
            }

            Serial.println(
                "Rádio confirmada; modo: volume"
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

    ultimaAtividadeSelecao = millis();

    Serial.print(
        "Selecionada: "
    );

    const Radio* radio =
        obterRadio(
            radioSelecionada
        );

    if (radio != nullptr) {
        mostrarSelecaoRadio(
            radio->nome,
            radioSelecionada,
            quantidade
        );

        Serial.println(
            radio->nome
        );
    }
}

// =====================================================
// Retorno automático do controle para volume
// =====================================================

void verificarInatividadeSelecao() {
    if (
        modoControle !=
            ModoControle::SELECAO_RADIO
    ) {
        return;
    }

    if (
        millis() - ultimaAtividadeSelecao <
            TEMPO_INATIVIDADE_SELECAO_MS
    ) {
        return;
    }

    modoControle = ModoControle::VOLUME;
    radioSelecionada = radioAtual;
    telaVolumeAtiva = false;

    Serial.println(
        "Seleção cancelada por inatividade; modo: volume"
    );

    atualizarInterfaceAudio(true);
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

    atualizarInterfaceAudio(true);
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

    bool comandoAceito =
        tocarRadio(
            radio->nome,
            radio->url
        );

    if (!comandoAceito) {
        mostrarMensagem(
            "Comando rejeitado"
        );

        exibirRadio(
            radioAtual
        );

        return;
    }

    radioAtual = indice;
    radioSelecionada = indice;
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

void registrarStatusSistema() {

    static unsigned long ultimaAtualizacaoStatus = 0;

    if (millis() - ultimaAtualizacaoStatus < 5000) {
        return;
    }
    ultimaAtualizacaoStatus = millis();

    StatusAudio audio =
        obterStatusAudio();

    Serial.printf("Temperatura: %.1f °C\n", temperatureRead());
    Serial.printf("RAM livre: %u KB\n", ESP.getFreeHeap() / 1024);
    Serial.printf("Menor RAM livre: %u KB\n", ESP.getMinFreeHeap() / 1024);
    Serial.printf("Maior bloco RAM: %u KB\n", ESP.getMaxAllocHeap() / 1024);
    Serial.printf("PSRAM livre: %u KB\n", ESP.getFreePsram() / 1024);
    Serial.printf("Maior bloco PSRAM: %u KB\n", ESP.getMaxAllocPsram() / 1024);
    Serial.printf(
        "Wi-Fi: %d dBm | BSSID: %s | Canal: %d\n",
        WiFi.RSSI(),
        WiFi.BSSIDstr().c_str(),
        WiFi.channel()
    );
    Serial.printf(
        "Audio: %s | Buffer: %lu ms | Bitrate: %lu | Fluxo lento: %lu | Reconexoes: %lu | Pilha livre min.: %lu bytes\n",
        obterTextoEstadoAudio(
            audio.estado
        ),
        audio.bufferMilissegundos,
        audio.bitrate,
        audio.eventosFluxoLento,
        audio.tentativasReconexao,
        audio.stackMinimoBytes
    );
}

void atualizarLedEstadoAudio() {
    static unsigned long ultimaAtualizacao = 0;
    static EstadoAudio ultimoEstado =
        EstadoAudio::DESLIGADO;

    if (millis() - ultimaAtualizacao < 250) {
        return;
    }

    ultimaAtualizacao = millis();

    StatusAudio audio =
        obterStatusAudio();

    if (audio.estado == ultimoEstado) {
        return;
    }

    ultimoEstado = audio.estado;

    switch (audio.estado) {
        case EstadoAudio::CONECTANDO:
        case EstadoAudio::BUFFERIZANDO:
        case EstadoAudio::INICIALIZANDO:
            rgbLedWrite(
                PIN_LED_RGB,
                0,
                0,
                BRILHO_LED_RGB
            );
            break;

        case EstadoAudio::TOCANDO:
            rgbLedWrite(
                PIN_LED_RGB,
                0,
                BRILHO_LED_RGB,
                0
            );
            break;

        case EstadoAudio::DEGRADADO:
            rgbLedWrite(
                PIN_LED_RGB,
                BRILHO_LED_RGB,
                BRILHO_LED_RGB,
                0
            );
            break;

        case EstadoAudio::RECONECTANDO:
        case EstadoAudio::ERRO:
            rgbLedWrite(
                PIN_LED_RGB,
                BRILHO_LED_RGB,
                0,
                0
            );
            break;

        case EstadoAudio::DESLIGADO:
        case EstadoAudio::PARADO:
            rgbLedWrite(
                PIN_LED_RGB,
                0,
                0,
                0
            );
            break;
    }
}

void atualizarInterfaceAudio(
    bool forcar
) {
    static EstadoAudio estadoAnterior =
        EstadoAudio::DESLIGADO;

    StatusAudio audio =
        obterStatusAudio();

    if (
        !forcar &&
        audio.estado == estadoAnterior
    ) {
        return;
    }

    estadoAnterior = audio.estado;

    Serial.print("Estado do audio: ");
    Serial.println(
        obterTextoEstadoAudio(
            audio.estado
        )
    );

    if (
        modoControle !=
            ModoControle::VOLUME ||
        telaVolumeAtiva
    ) {
        return;
    }

    switch (audio.estado) {
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
            exibirRadio(radioAtual);
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
