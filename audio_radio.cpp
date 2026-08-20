#include "audio_radio.h"
#include "configuracao.h"
#include "radios.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include "Audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr size_t TAMANHO_NOME_RADIO =
    TAMANHO_MAXIMO_NOME_RADIO + 1;

constexpr size_t TAMANHO_URL_RADIO =
    TAMANHO_MAXIMO_URL_RADIO + 1;

constexpr uint32_t TEMPO_LIMITE_BUFFERIZACAO_MS =
    15000;

constexpr uint32_t TEMPO_LIMITE_DEGRADADO_MS =
    30000;

constexpr uint32_t BUFFER_RECUPERADO_MS =
    5000;

constexpr uint32_t INTERVALO_AMOSTRA_STATUS_MS =
    250;

enum class TipoComandoAudio : uint8_t {
    TOCAR,
    PARAR,
    VOLUME
};

struct ComandoAudio {
    TipoComandoAudio tipo =
        TipoComandoAudio::PARAR;

    uint8_t volume = 0;

    char nome[TAMANHO_NOME_RADIO] = "";
    char url[TAMANHO_URL_RADIO] = "";
};

Audio audio;

QueueHandle_t filaComandos = nullptr;
SemaphoreHandle_t mutexStatus = nullptr;
TaskHandle_t tarefaAudioHandle = nullptr;

StatusAudio statusAudio;

char nomeDesejado[TAMANHO_NOME_RADIO] = "";
char urlDesejada[TAMANHO_URL_RADIO] = "";

uint32_t inicioBufferizacao = 0;
uint32_t inicioDegradacao = 0;
uint32_t proximaTentativa = 0;
uint32_t ultimaAmostraStatus = 0;

uint8_t falhasConsecutivas = 0;

bool bloquearStatus(
    TickType_t espera = pdMS_TO_TICKS(20)
) {
    return
        mutexStatus != nullptr &&
        xSemaphoreTake(
            mutexStatus,
            espera
        ) == pdTRUE;
}

void liberarStatus() {
    xSemaphoreGive(mutexStatus);
}

void copiarTexto(
    char* destino,
    size_t tamanho,
    const char* origem
) {
    if (
        destino == nullptr ||
        tamanho == 0
    ) {
        return;
    }

    strlcpy(
        destino,
        origem != nullptr ? origem : "",
        tamanho
    );
}

void definirEstado(
    EstadoAudio estado
) {
    if (!bloquearStatus()) {
        return;
    }

    statusAudio.estado = estado;

    liberarStatus();
}

EstadoAudio lerEstado() {
    EstadoAudio estado =
        EstadoAudio::DESLIGADO;

    if (!bloquearStatus()) {
        return estado;
    }

    estado = statusAudio.estado;

    liberarStatus();

    return estado;
}

void definirErro(
    const char* erro
) {
    if (!bloquearStatus()) {
        return;
    }

    copiarTexto(
        statusAudio.ultimoErro,
        sizeof(statusAudio.ultimoErro),
        erro
    );

    liberarStatus();
}

void limparErro() {
    definirErro("");
}

void atualizarRadioStatus(
    const char* nome
) {
    if (!bloquearStatus()) {
        return;
    }

    copiarTexto(
        statusAudio.radio,
        sizeof(statusAudio.radio),
        nome
    );

    statusAudio.titulo[0] = '\0';
    statusAudio.bitrate = 0;
    statusAudio.bufferBytes = 0;
    statusAudio.bufferMilissegundos = 0;
    statusAudio.codec[0] = '\0';

    liberarStatus();
}

uint32_t calcularEsperaReconexao() {
    static constexpr uint32_t ESPERAS_MS[] = {
        1000,
        2000,
        5000,
        10000,
        30000
    };

    size_t indice =
        falhasConsecutivas == 0
            ? 0
            : falhasConsecutivas - 1;

    if (
        indice >=
        sizeof(ESPERAS_MS) /
            sizeof(ESPERAS_MS[0])
    ) {
        indice =
            sizeof(ESPERAS_MS) /
                sizeof(ESPERAS_MS[0]) -
            1;
    }

    return ESPERAS_MS[indice];
}

void agendarReconexao(
    const char* motivo
) {
    if (urlDesejada[0] == '\0') {
        definirEstado(
            EstadoAudio::PARADO
        );

        return;
    }

    if (falhasConsecutivas < UINT8_MAX) {
        falhasConsecutivas++;
    }

    proximaTentativa =
        millis() +
        calcularEsperaReconexao();

    inicioDegradacao = 0;

    definirErro(motivo);
    definirEstado(
        EstadoAudio::RECONECTANDO
    );
}

void registrarTentativaReconexao() {
    if (!bloquearStatus()) {
        return;
    }

    statusAudio.tentativasReconexao++;

    liberarStatus();
}

void conectarAgora(
    bool reconexao
) {
    if (urlDesejada[0] == '\0') {
        return;
    }

    if (reconexao) {
        registrarTentativaReconexao();
    }

    proximaTentativa = 0;

    definirEstado(
        reconexao
            ? EstadoAudio::RECONECTANDO
            : EstadoAudio::CONECTANDO
    );

    audio.stopSong();

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

    bool conectado =
        audio.connecttohost(
            urlDesejada
        );

    if (!conectado) {
        agendarReconexao(
            "Falha ao abrir a conexao da radio"
        );

        return;
    }

    inicioBufferizacao = millis();

    definirEstado(
        EstadoAudio::BUFFERIZANDO
    );
}

void marcarStreamPronto() {
    falhasConsecutivas = 0;
    proximaTentativa = 0;
    inicioBufferizacao = 0;
    inicioDegradacao = 0;

    limparErro();
    definirEstado(
        EstadoAudio::TOCANDO
    );
}

void registrarFluxoLento() {
    if (!bloquearStatus()) {
        return;
    }

    statusAudio.eventosFluxoLento++;

    if (
        statusAudio.estado ==
            EstadoAudio::TOCANDO
    ) {
        statusAudio.estado =
            EstadoAudio::DEGRADADO;

        inicioDegradacao =
            millis();
    } else if (
        statusAudio.estado ==
            EstadoAudio::DEGRADADO &&
        inicioDegradacao == 0
    ) {
        inicioDegradacao =
            millis();
    }

    copiarTexto(
        statusAudio.ultimoErro,
        sizeof(statusAudio.ultimoErro),
        "Fluxo de audio lento"
    );

    liberarStatus();
}

uint32_t calcularDuracaoBufferMs() {
    uint32_t bitrate =
        audio.getBitRate();

    if (bitrate == 0) {
        return 0;
    }

    return static_cast<uint32_t>(
        static_cast<uint64_t>(
            audio.inBufferFilled()
        ) *
        8000ULL /
        bitrate
    );
}

void registrarBitrate(
    const char* texto
) {
    uint32_t bitrate =
        texto != nullptr
            ? strtoul(
                texto,
                nullptr,
                10
            )
            : 0;

    if (!bloquearStatus()) {
        return;
    }

    statusAudio.bitrate = bitrate;

    liberarStatus();
}

void registrarTitulo(
    const char* titulo
) {
    if (!bloquearStatus()) {
        return;
    }

    copiarTexto(
        statusAudio.titulo,
        sizeof(statusAudio.titulo),
        titulo
    );

    liberarStatus();
}

void registrarNomeEstacao(
    const char* nome
) {
    if (!bloquearStatus()) {
        return;
    }

    if (
        nome != nullptr &&
        nome[0] != '\0'
    ) {
        copiarTexto(
            statusAudio.radio,
            sizeof(statusAudio.radio),
            nome
        );
    }

    liberarStatus();
}

void tratarEventoAudio(
    Audio::msg_t evento
) {
    const char* mensagem =
        evento.msg != nullptr
            ? evento.msg
            : "";

    switch (evento.e) {
        case Audio::evt_info:
            if (
                strncmp(
                    mensagem,
                    "next URL:",
                    9
                ) != 0
            ) {
                Serial.print("Audio: ");
                Serial.println(mensagem);
            }

            if (
                strstr(
                    mensagem,
                    "stream ready"
                ) != nullptr
            ) {
                marcarStreamPronto();
            } else if (
                strstr(
                    mensagem,
                    "slow stream"
                ) != nullptr
            ) {
                registrarFluxoLento();
            } else if (
                strstr(
                    mensagem,
                    "Stream lost"
                ) != nullptr
            ) {
                if (audio.isRunning()) {
                    inicioBufferizacao =
                        millis();

                    definirEstado(
                        EstadoAudio::BUFFERIZANDO
                    );
                } else {
                    agendarReconexao(
                        "Fluxo da radio perdido"
                    );
                }
            }

            break;

        case Audio::evt_log:
            Serial.print("Audio log: ");
            Serial.println(mensagem);
            definirErro(mensagem);
            break;

        case Audio::evt_bitrate:
            registrarBitrate(mensagem);
            break;

        case Audio::evt_name:
            registrarNomeEstacao(mensagem);
            break;

        case Audio::evt_streamtitle:
            registrarTitulo(mensagem);
            break;

        case Audio::evt_eof:
            if (urlDesejada[0] != '\0') {
                agendarReconexao(
                    "Fim inesperado do fluxo"
                );
            }

            break;

        default:
            break;
    }
}

void atualizarAmostraStatus() {
    uint32_t agora = millis();

    if (
        agora - ultimaAmostraStatus <
        INTERVALO_AMOSTRA_STATUS_MS
    ) {
        return;
    }

    ultimaAmostraStatus = agora;

    uint32_t preenchido =
        audio.inBufferFilled();

    uint32_t total =
        audio.getInBufferSize();

    uint32_t bitrate =
        audio.getBitRate();

    uint32_t bufferMs =
        calcularDuracaoBufferMs();

    const char* codec =
        audio.getCodecname();

    if (!bloquearStatus()) {
        return;
    }

    statusAudio.bufferBytes =
        preenchido;

    statusAudio.bufferTotalBytes =
        total;

    statusAudio.bufferMilissegundos =
        bufferMs;

    if (bitrate > 0) {
        statusAudio.bitrate = bitrate;
    }

    copiarTexto(
        statusAudio.codec,
        sizeof(statusAudio.codec),
        codec
    );

    if (tarefaAudioHandle != nullptr) {
        statusAudio.stackMinimoBytes =
            uxTaskGetStackHighWaterMark(
                tarefaAudioHandle
            );
    }

    liberarStatus();
}

void supervisionarAudio() {
    uint32_t agora = millis();
    EstadoAudio estado = lerEstado();

    if (
        estado ==
            EstadoAudio::RECONECTANDO &&
        proximaTentativa != 0 &&
        static_cast<int32_t>(
            agora - proximaTentativa
        ) >= 0
    ) {
        if (WiFi.status() != WL_CONNECTED) {
            proximaTentativa =
                agora + 1000;

            definirErro(
                "Aguardando o Wi-Fi reconectar"
            );

            return;
        }

        conectarAgora(true);

        return;
    }

    if (
        (
            estado ==
                EstadoAudio::CONECTANDO ||
            estado ==
                EstadoAudio::BUFFERIZANDO
        ) &&
        inicioBufferizacao != 0 &&
        agora - inicioBufferizacao >=
            TEMPO_LIMITE_BUFFERIZACAO_MS
    ) {
        audio.stopSong();

        agendarReconexao(
            "Tempo limite ao preparar a radio"
        );

        return;
    }

    if (
        (
            estado ==
                EstadoAudio::TOCANDO ||
            estado ==
                EstadoAudio::DEGRADADO ||
            estado ==
                EstadoAudio::BUFFERIZANDO
        ) &&
        !audio.isRunning()
    ) {
        agendarReconexao(
            "Conexao de audio encerrada"
        );

        return;
    }

    if (
        estado ==
            EstadoAudio::DEGRADADO &&
        audio.isRunning()
    ) {
        uint32_t bufferMs =
            calcularDuracaoBufferMs();

        if (
            bufferMs >=
            BUFFER_RECUPERADO_MS
        ) {
            inicioDegradacao = 0;

            limparErro();
            definirEstado(
                EstadoAudio::TOCANDO
            );

            return;
        }

        if (
            inicioDegradacao != 0 &&
            agora - inicioDegradacao >=
                TEMPO_LIMITE_DEGRADADO_MS
        ) {
            audio.stopSong();
            inicioDegradacao = 0;

            agendarReconexao(
                "Buffer nao se recuperou"
            );
        }
    }
}

void processarComando(
    const ComandoAudio& comando
) {
    switch (comando.tipo) {
        case TipoComandoAudio::TOCAR:
            copiarTexto(
                nomeDesejado,
                sizeof(nomeDesejado),
                comando.nome
            );

            copiarTexto(
                urlDesejada,
                sizeof(urlDesejada),
                comando.url
            );

            falhasConsecutivas = 0;
            proximaTentativa = 0;
            inicioDegradacao = 0;

            atualizarRadioStatus(
                nomeDesejado
            );

            limparErro();
            conectarAgora(false);
            break;

        case TipoComandoAudio::PARAR:
            nomeDesejado[0] = '\0';
            urlDesejada[0] = '\0';
            proximaTentativa = 0;
            falhasConsecutivas = 0;
            inicioDegradacao = 0;

            audio.stopSong();

            definirEstado(
                EstadoAudio::PARADO
            );
            break;

        case TipoComandoAudio::VOLUME:
            audio.setVolume(
                comando.volume
            );
            break;
    }
}

void tarefaAudio(
    void* parametro
) {
    (void)parametro;

    ComandoAudio comando;

    while (true) {
        while (
            xQueueReceive(
                filaComandos,
                &comando,
                0
            ) == pdTRUE
        ) {
            processarComando(comando);
        }

        audio.loop();

        supervisionarAudio();
        atualizarAmostraStatus();

        vTaskDelay(
            pdMS_TO_TICKS(1)
        );
    }
}

bool enviarComando(
    const ComandoAudio& comando
) {
    if (filaComandos == nullptr) {
        return false;
    }

    return
        xQueueSend(
            filaComandos,
            &comando,
            pdMS_TO_TICKS(20)
        ) == pdTRUE;
}

}

bool iniciarAudio(int volume) {
    if (tarefaAudioHandle != nullptr) {
        return true;
    }

    mutexStatus =
        xSemaphoreCreateMutex();

    filaComandos =
        xQueueCreate(
            8,
            sizeof(ComandoAudio)
        );

    if (
        mutexStatus == nullptr ||
        filaComandos == nullptr
    ) {
        Serial.println(
            "Falha ao criar o servico de audio."
        );

        return false;
    }

    definirEstado(
        EstadoAudio::INICIALIZANDO
    );

    Audio::audio_info_callback =
        tratarEventoAudio;

    bool i2sIniciado =
        audio.setPinout(
            PIN_MAX98357A_BCLK,
            PIN_MAX98357A_LRC,
            PIN_MAX98357A_DIN
        );

    if (!i2sIniciado) {
        definirErro(
            "Falha ao inicializar I2S ou PSRAM"
        );

        definirEstado(
            EstadoAudio::ERRO
        );

        Serial.println(
            "Falha ao inicializar I2S ou PSRAM."
        );

        return false;
    }

    audio.setAudioTaskCore(
        NUCLEO_DECODIFICADOR_AUDIO
    );

    int volumeLimitado =
        constrain(
            volume,
            VOLUME_MINIMO,
            VOLUME_MAXIMO
        );

    // A interface e a ESP32-audioI2S usam a mesma escala de 0 a 21.
    audio.setVolume(volumeLimitado);

    BaseType_t criada =
        xTaskCreatePinnedToCore(
            tarefaAudio,
            "AudioService",
            PILHA_SERVICO_AUDIO_BYTES,
            nullptr,
            PRIORIDADE_SERVICO_AUDIO,
            &tarefaAudioHandle,
            NUCLEO_SERVICO_AUDIO
        );

    if (criada != pdPASS) {
        tarefaAudioHandle = nullptr;

        definirErro(
            "Falha ao criar a tarefa de audio"
        );

        definirEstado(
            EstadoAudio::ERRO
        );

        Serial.println(
            "Falha ao criar a tarefa de audio."
        );

        return false;
    }

    definirEstado(EstadoAudio::PARADO);

    Serial.printf(
        "Servico de audio iniciado. Biblioteca %s, volume %d.\n",
        audio.getVersion(),
        volumeLimitado
    );

    return true;
}

bool tocarRadio(
    const String& nome,
    const String& url
) {
    if (
        !dadosRadioValidos(
            nome.c_str(),
            url.c_str()
        )
    ) {
        definirErro(
            "Nome ou URL da radio invalido"
        );

        return false;
    }

    ComandoAudio comando;

    comando.tipo =
        TipoComandoAudio::TOCAR;

    copiarTexto(
        comando.nome,
        sizeof(comando.nome),
        nome.c_str()
    );

    copiarTexto(
        comando.url,
        sizeof(comando.url),
        url.c_str()
    );

    return enviarComando(comando);
}

bool pararAudio() {
    ComandoAudio comando;

    comando.tipo =
        TipoComandoAudio::PARAR;

    return enviarComando(comando);
}

void alterarVolumeAudio(int volume) {
    ComandoAudio comando;

    comando.tipo =
        TipoComandoAudio::VOLUME;

    comando.volume =
        static_cast<uint8_t>(
            constrain(
                volume,
                VOLUME_MINIMO,
                VOLUME_MAXIMO
            )
        );

    enviarComando(comando);
}

StatusAudio obterStatusAudio() {
    StatusAudio copia;

    if (!bloquearStatus()) {
        return copia;
    }

    copia = statusAudio;

    liberarStatus();

    return copia;
}

const char* obterTextoEstadoAudio(
    EstadoAudio estado
) {
    switch (estado) {
        case EstadoAudio::DESLIGADO:
            return "desligado";

        case EstadoAudio::INICIALIZANDO:
            return "inicializando";

        case EstadoAudio::PARADO:
            return "parado";

        case EstadoAudio::CONECTANDO:
            return "conectando";

        case EstadoAudio::BUFFERIZANDO:
            return "bufferizando";

        case EstadoAudio::TOCANDO:
            return "tocando";

        case EstadoAudio::DEGRADADO:
            return "degradado";

        case EstadoAudio::RECONECTANDO:
            return "reconectando";

        case EstadoAudio::ERRO:
            return "erro";
    }

    return "desconhecido";
}
