
#include "audio_radio.h"
#include "alarmes.h"
#include "configuracao.h"
#include "radios.h"

#include <Arduino.h>
#include <FFat.h>
#include <WiFi.h>
#include <atomic>
#include <cstring>
#include <new>
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

constexpr uint32_t TEMPO_LIMITE_DESLIGAMENTO_AUDIO_MS =
    2000;

constexpr uint32_t TEMPO_LIMITE_INICIALIZACAO_AUDIO_MS =
    5000;

enum class TipoComandoAudio : uint8_t {
    TOCAR_RADIO,
    TOCAR_ALARME_RADIO,
    TOCAR_ALARME,
    PARAR,
    SUSPENDER,
    RETOMAR
};

enum class FonteAudioAtiva : uint8_t {
    NENHUMA,
    RADIO,
    ALARME_RADIO,
    ALARME
};

struct ComandoAudio {
    TipoComandoAudio tipo =
        TipoComandoAudio::PARAR;

    uint8_t volume = 0;
    bool perfilPlayer = false;

    char nome[TAMANHO_NOME_RADIO] = "";
    char url[TAMANHO_URL_RADIO] = "";
    char caminhoArquivo[256] = "";
};

Audio* audio = nullptr;

QueueHandle_t filaComandos = nullptr;
SemaphoreHandle_t mutexStatus = nullptr;
SemaphoreHandle_t semaforoAudioDesligado = nullptr;
SemaphoreHandle_t semaforoAudioIniciado = nullptr;
TaskHandle_t tarefaAudioHandle = nullptr;
std::atomic<bool> servicoAudioAtivo{true};
std::atomic<bool> decoderDacAtivo{false};
std::atomic<bool> ultimaInicializacaoBemSucedida{false};
std::atomic<uint8_t> volumeDesejado{VOLUME_PADRAO};
std::atomic<bool> ajusteVolumePendente{false};

StatusAudio statusAudio;

char nomeDesejado[TAMANHO_NOME_RADIO] = "";
char urlDesejada[TAMANHO_URL_RADIO] = "";

uint32_t inicioBufferizacao = 0;
uint32_t inicioDegradacao = 0;
uint32_t proximaTentativa = 0;
uint32_t ultimaAmostraStatus = 0;
uint32_t geracaoConexaoAudio = 0;

uint8_t falhasConsecutivas = 0;
FonteAudioAtiva fonteAudioAtiva =
    FonteAudioAtiva::NENHUMA;
FonteAudioAtiva fonteRadioDesejada =
    FonteAudioAtiva::RADIO;
bool perfilPlayerAtivo = false;

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

void atualizarStatusAlarme(
    bool ativo,
    bool arquivoSolicitado,
    bool arquivoDisponivel,
    bool somPadrao,
    bool radio = false
) {
    if (!bloquearStatus()) {
        return;
    }

    statusAudio.alarmeAtivo = ativo;
    statusAudio.arquivoAlarmeSolicitado = arquivoSolicitado;
    statusAudio.arquivoAlarmeDisponivel = arquivoDisponivel;
    statusAudio.somPadraoAlarme = somPadrao;
    statusAudio.radioAlarme = radio;
    statusAudio.radioFmAlarme = false;

    liberarStatus();
}

void limparStatusAlarme() {
    atualizarStatusAlarme(false, false, false, false);
}

void marcarAlarmeInativo() {
    if (!bloquearStatus()) {
        return;
    }

    statusAudio.alarmeAtivo = false;

    liberarStatus();
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
        if (fonteAudioAtiva == FonteAudioAtiva::ALARME) {
            marcarAlarmeInativo();
        }

        fonteAudioAtiva = FonteAudioAtiva::NENHUMA;
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
    ++geracaoConexaoAudio;

    definirEstado(
        reconexao
            ? EstadoAudio::RECONECTANDO
            : EstadoAudio::CONECTANDO
    );

    audio->stopSong();

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

    fonteAudioAtiva = fonteRadioDesejada;

    bool conectado =
        audio->connecttohost(
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

void abrirArquivoAlarmeAgora(
    const char* caminho,
    uint8_t volume
) {
    bool arquivoSolicitado =
        caminho != nullptr && caminho[0] != '\0';
    bool arquivoDisponivel = false;

    nomeDesejado[0] = '\0';
    urlDesejada[0] = '\0';
    proximaTentativa = 0;
    inicioDegradacao = 0;
    fonteAudioAtiva = FonteAudioAtiva::NENHUMA;

    atualizarRadioStatus(
        arquivoSolicitado
            ? caminho
            : CAMINHO_SOM_PADRAO_ALARME
    );
    atualizarStatusAlarme(
        true,
        arquivoSolicitado,
        false,
        !arquivoSolicitado
    );
    limparErro();
    definirEstado(EstadoAudio::BUFFERIZANDO);

    audio->stopSong();
    audio->setVolume(volume);
    vTaskDelay(pdMS_TO_TICKS(20));

    if (arquivoSolicitado) {
        Serial.print("Alarme: arquivo indisponivel; usando padrao: ");
        Serial.println(caminho);
        audio->stopSong();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    bool somPadraoAberto =
        FFat.exists(CAMINHO_SOM_PADRAO_ALARME) &&
        audio->connecttoFS(
            FFat,
            CAMINHO_SOM_PADRAO_ALARME
        );

    if (!somPadraoAberto) {
        fonteAudioAtiva = FonteAudioAtiva::NENHUMA;
        atualizarStatusAlarme(
            false,
            arquivoSolicitado,
            arquivoDisponivel,
            false
        );
        definirErro("Som padrao do alarme indisponivel");
        definirEstado(EstadoAudio::ERRO);
        return;
    }

    fonteAudioAtiva = FonteAudioAtiva::ALARME;
    inicioBufferizacao = 0;
    atualizarRadioStatus(CAMINHO_SOM_PADRAO_ALARME);
    atualizarStatusAlarme(
        true,
        arquivoSolicitado,
        arquivoDisponivel,
        true
    );
    definirEstado(EstadoAudio::TOCANDO);
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

uint32_t calcularDuracaoBufferMs(
    uint32_t preenchido,
    uint32_t bitrate
) {
    if (bitrate == 0) {
        return 0;
    }

    return static_cast<uint32_t>(
        static_cast<uint64_t>(preenchido) *
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
                (
                    fonteAudioAtiva == FonteAudioAtiva::RADIO ||
                    fonteAudioAtiva == FonteAudioAtiva::ALARME_RADIO
                ) &&
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
                if (audio != nullptr && audio->isRunning()) {
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
            if (
                fonteAudioAtiva == FonteAudioAtiva::RADIO ||
                fonteAudioAtiva == FonteAudioAtiva::ALARME_RADIO
            ) {
                agendarReconexao(
                    "Fim inesperado do fluxo"
                );
            } else if (fonteAudioAtiva == FonteAudioAtiva::ALARME) {
                fonteAudioAtiva = FonteAudioAtiva::NENHUMA;
                marcarAlarmeInativo();
                definirEstado(EstadoAudio::PARADO);
                Serial.println("Alarme: ciclo de audio finalizado.");
            }

            break;

        default:
            break;
    }
}

void manterLinhasI2sEmNivelBaixo() {
    // Sem BCK e LRCK o PCM5102A entra no power-down automático. Fixar também
    // DIN em zero evita que as linhas deixadas pelo periférico flutuem.
    digitalWrite(PIN_PCM5102A_BCK, LOW);
    digitalWrite(PIN_PCM5102A_LRCK, LOW);
    digitalWrite(PIN_PCM5102A_DIN, LOW);

    pinMode(PIN_PCM5102A_BCK, OUTPUT);
    pinMode(PIN_PCM5102A_LRCK, OUTPUT);
    pinMode(PIN_PCM5102A_DIN, OUTPUT);
}

bool iniciarDecoderDacAgora(int volume) {
    if (audio != nullptr) {
        audio->setMute(false);
        audio->setVolume(volume);
        decoderDacAtivo.store(true, std::memory_order_release);
        return true;
    }

    manterLinhasI2sEmNivelBaixo();

    audio = new (std::nothrow) Audio();
    if (audio == nullptr) {
        definirErro("Sem memoria para iniciar decoder de audio");
        return false;
    }

    Audio::audio_info_callback = tratarEventoAudio;

    if (!audio->setPinout(
        PIN_PCM5102A_BCK,
        PIN_PCM5102A_LRCK,
        PIN_PCM5102A_DIN
    )) {
        delete audio;
        audio = nullptr;
        manterLinhasI2sEmNivelBaixo();
        definirErro("Falha ao inicializar I2S ou PSRAM");
        return false;
    }

    audio->setAudioTaskCore(NUCLEO_DECODIFICADOR_AUDIO);
    audio->setMute(false);
    audio->setVolume(volume);
    decoderDacAtivo.store(true, std::memory_order_release);

    return true;
}

void desligarDecoderDacAgora() {
    decoderDacAtivo.store(false, std::memory_order_release);

    if (audio != nullptr) {
        audio->setMute(true);
        audio->stopSong();
        delete audio;
        audio = nullptr;
    }

    manterLinhasI2sEmNivelBaixo();

    Serial.println(
        "Audio web desligado: decoder destruido, I2S removido e pinos em LOW."
    );
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
        audio->inBufferFilled();

    uint32_t total =
        audio->getInBufferSize();

    uint32_t bitrate =
        audio->getBitRate();

    uint32_t bufferMs =
        calcularDuracaoBufferMs(
            preenchido,
            bitrate
        );

    const char* codec =
        audio->getCodecname();

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
        audio->stopSong();

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
        !audio->isRunning()
    ) {
        agendarReconexao(
            "Conexao de audio encerrada"
        );

        return;
    }

    if (
        estado ==
            EstadoAudio::DEGRADADO &&
        audio->isRunning()
    ) {
        uint32_t bufferMs =
            calcularDuracaoBufferMs(
                audio->inBufferFilled(),
                audio->getBitRate()
            );

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
            audio->stopSong();
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
        case TipoComandoAudio::TOCAR_RADIO:
        case TipoComandoAudio::TOCAR_ALARME_RADIO:
        {
            bool radioDeAlarme =
                comando.tipo == TipoComandoAudio::TOCAR_ALARME_RADIO;

            fonteRadioDesejada =
                radioDeAlarme
                    ? FonteAudioAtiva::ALARME_RADIO
                    : FonteAudioAtiva::RADIO;
            perfilPlayerAtivo = false;
            vTaskPrioritySet(
                nullptr,
                PRIORIDADE_SERVICO_AUDIO_RADIO
            );

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

            if (radioDeAlarme) {
                audio->setVolume(comando.volume);
            } else {
                limparStatusAlarme();
            }

            atualizarRadioStatus(nomeDesejado);

            if (radioDeAlarme) {
                atualizarStatusAlarme(
                    true,
                    false,
                    false,
                    false,
                    true
                );
            }

            limparErro();
            conectarAgora(false);
            break;
        }



        case TipoComandoAudio::TOCAR_ALARME:
            perfilPlayerAtivo = true;
            vTaskPrioritySet(
                nullptr,
                PRIORIDADE_SERVICO_AUDIO_RADIO
            );

            falhasConsecutivas = 0;
            proximaTentativa = 0;
            inicioDegradacao = 0;

            abrirArquivoAlarmeAgora(
                comando.caminhoArquivo,
                comando.volume
            );
            break;

        case TipoComandoAudio::PARAR:
            limparStatusAlarme();
            nomeDesejado[0] = '\0';
            urlDesejada[0] = '\0';
            proximaTentativa = 0;
            falhasConsecutivas = 0;
            inicioDegradacao = 0;
            fonteAudioAtiva = FonteAudioAtiva::NENHUMA;

            audio->stopSong();

            definirEstado(
                EstadoAudio::PARADO
            );
            break;

        case TipoComandoAudio::SUSPENDER:
            limparStatusAlarme();
            nomeDesejado[0] = '\0';
            urlDesejada[0] = '\0';
            proximaTentativa = 0;
            falhasConsecutivas = 0;
            inicioDegradacao = 0;
            fonteAudioAtiva = FonteAudioAtiva::NENHUMA;

            desligarDecoderDacAgora();
            atualizarRadioStatus("");
            definirEstado(EstadoAudio::DESLIGADO);
            servicoAudioAtivo.store(
                false,
                std::memory_order_release
            );
            if (semaforoAudioDesligado != nullptr) {
                xSemaphoreGive(semaforoAudioDesligado);
            }
            break;

        case TipoComandoAudio::RETOMAR:
            // RETOMAR também precisa reativar o laço dentro da própria
            // tarefa. Em uma alternância rápida, SUSPENDER e RETOMAR podem
            // estar juntos na fila; o último comando deve determinar o estado.
            perfilPlayerAtivo = comando.perfilPlayer;
            vTaskPrioritySet(
                nullptr,
                perfilPlayerAtivo
                    ? PRIORIDADE_SERVICO_AUDIO_RADIO
                    : PRIORIDADE_SERVICO_AUDIO_RADIO
            );

            ultimaInicializacaoBemSucedida.store(
                iniciarDecoderDacAgora(comando.volume),
                std::memory_order_release
            );

            if (ultimaInicializacaoBemSucedida.load(
                std::memory_order_acquire
            )) {
                servicoAudioAtivo.store(
                    true,
                    std::memory_order_release
                );
                definirEstado(EstadoAudio::PARADO);
            } else {
                servicoAudioAtivo.store(
                    false,
                    std::memory_order_release
                );
                definirEstado(EstadoAudio::ERRO);
                Serial.println(
                    "Falha ao recriar decoder e I2S."
                );
            }

            if (semaforoAudioIniciado != nullptr) {
                xSemaphoreGive(semaforoAudioIniciado);
            }
            break;
    }
}

void tarefaAudio(
    void* parametro
) {
    (void)parametro;

    ComandoAudio comando;

    // Diagnostico restrito a tarefa proprietaria do objeto Audio.
    constexpr uint32_t JANELA_DIAGNOSTICO_MS = 5000;
    uint32_t janelaInicioMs = 0;
    uint32_t geracaoMedida = UINT32_MAX;
    uint32_t chamadas = 0;
    uint64_t somaDuracoesUs = 0;
    uint32_t maiorDuracaoUs = 0;
    uint32_t proximaAmostraBufferMs = 0;
    uint32_t bufferInicial = 0;
    uint32_t bufferFinal = 0;
    uint32_t bufferMinimo = UINT32_MAX;
    uint32_t bufferMaximo = 0;
    uint32_t amostrasBuffer = 0;

    while (true) {
        if (!servicoAudioAtivo.load(
            std::memory_order_acquire
        )) {
            ulTaskNotifyTake(
                pdTRUE,
                portMAX_DELAY
            );

            continue;
        }

        while (
            xQueueReceive(
                filaComandos,
                &comando,
                0
            ) == pdTRUE
        ) {
            processarComando(comando);

            if (
                !servicoAudioAtivo.load(std::memory_order_acquire) ||
                audio == nullptr
            ) {
                break;
            }
        }

        // SUSPENDER destrói o objeto Audio nesta própria tarefa. Não pode
        // existir nenhuma chamada ao decoder depois da confirmação enviada ao
        // coordenador.
        if (
            !servicoAudioAtivo.load(std::memory_order_acquire) ||
            audio == nullptr
        ) {
            continue;
        }

        // O ajuste interativo sempre vence comandos de transição que também
        // carregam volume (alarme e retomada). Vários detentes são
        // consolidados no valor mais recente.
        if (ajusteVolumePendente.exchange(
            false,
            std::memory_order_acq_rel
        )) {
            audio->setVolume(
                volumeDesejado.load(std::memory_order_acquire)
            );
        }

        // Reinicia a janela ao trocar de stream, reconectar ou parar.
        const bool radioAtiva =
            fonteAudioAtiva == FonteAudioAtiva::RADIO ||
            fonteAudioAtiva == FonteAudioAtiva::ALARME_RADIO;

        if (!radioAtiva || geracaoMedida != geracaoConexaoAudio) {
            geracaoMedida = geracaoConexaoAudio;
            janelaInicioMs = 0;
            chamadas = 0;
            somaDuracoesUs = 0;
            maiorDuracaoUs = 0;
            proximaAmostraBufferMs = 0;
            bufferMinimo = UINT32_MAX;
            bufferMaximo = 0;
            amostrasBuffer = 0;
        }

        const uint32_t inicioLoopUs = micros();
        audio->loop();
        const uint32_t duracaoLoopUs = micros() - inicioLoopUs;

        supervisionarAudio();
        atualizarAmostraStatus();

        if (radioAtiva && audio->isRunning()) {
            const uint32_t agoraMs = millis();
            if (janelaInicioMs == 0) {
                janelaInicioMs = agoraMs;
                proximaAmostraBufferMs = agoraMs;
            }

            ++chamadas;
            somaDuracoesUs += duracaoLoopUs;
            if (duracaoLoopUs > maiorDuracaoUs) {
                maiorDuracaoUs = duracaoLoopUs;
            }

            // Amostra buffer a cada 250 ms, sem acesso por outra tarefa.
            if (static_cast<int32_t>(agoraMs - proximaAmostraBufferMs) >= 0) {
                const uint32_t bytes = audio->inBufferFilled();
                if (amostrasBuffer == 0) bufferInicial = bytes;
                bufferFinal = bytes;
                if (bytes < bufferMinimo) bufferMinimo = bytes;
                if (bytes > bufferMaximo) bufferMaximo = bytes;
                ++amostrasBuffer;
                proximaAmostraBufferMs = agoraMs + INTERVALO_AMOSTRA_STATUS_MS;
            }

            const uint32_t decorridoMs = agoraMs - janelaInicioMs;
            if (decorridoMs >= JANELA_DIAGNOSTICO_MS && chamadas > 0) {
                const float loopsPorSegundo =
                    static_cast<float>(chamadas) * 1000.0f / decorridoMs;
                const float tempoMedioMs =
                    static_cast<float>(somaDuracoesUs) / chamadas / 1000.0f;
                const int32_t variacaoBytes =
                    static_cast<int32_t>(bufferFinal) -
                    static_cast<int32_t>(bufferInicial);

                Serial.printf(
                    "Audio diag (%lu ms): %.1f loops/s | medio %.3f ms | "
                    "maximo %.3f ms\n",
                    (unsigned long)decorridoMs,
                    loopsPorSegundo,
                    tempoMedioMs,
                    maiorDuracaoUs / 1000.0f
                );
                if (amostrasBuffer > 0) {
                    Serial.printf(
                        "Audio buffer: inicial %lu | final %lu | "
                        "minimo %lu | maximo %lu | variacao %+ld bytes/janela\n",
                        (unsigned long)bufferInicial,
                        (unsigned long)bufferFinal,
                        (unsigned long)bufferMinimo,
                        (unsigned long)bufferMaximo,
                        (long)variacaoBytes
                    );
                }

                janelaInicioMs = 0;
                chamadas = 0;
                somaDuracoesUs = 0;
                maiorDuracaoUs = 0;
                amostrasBuffer = 0;
                bufferMinimo = UINT32_MAX;
                bufferMaximo = 0;
            }
        } else {
            janelaInicioMs = 0;
            chamadas = 0;
            somaDuracoesUs = 0;
            maiorDuracaoUs = 0;
            amostrasBuffer = 0;
            bufferMinimo = UINT32_MAX;
            bufferMaximo = 0;
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                perfilPlayerAtivo
                    ? INTERVALO_SERVICO_AUDIO_PLAYER_MS
                    : INTERVALO_SERVICO_AUDIO_RADIO_MS
            )
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

bool iniciarAudio(int volume, bool iniciarDesligado) {
    if (tarefaAudioHandle != nullptr) {
        return true;
    }

    mutexStatus =
        xSemaphoreCreateMutex();

    semaforoAudioDesligado =
        xSemaphoreCreateBinary();

    semaforoAudioIniciado =
        xSemaphoreCreateBinary();

    filaComandos =
        xQueueCreate(
            8,
            sizeof(ComandoAudio)
        );

    if (
        mutexStatus == nullptr ||
        semaforoAudioDesligado == nullptr ||
        semaforoAudioIniciado == nullptr ||
        filaComandos == nullptr
    ) {
        Serial.println(
            "Falha ao criar o servico de audio."
        );

        return false;
    }

    int volumeLimitado =
        constrain(
            volume,
            VOLUME_MINIMO,
            VOLUME_MAXIMO
        );

    if (
        !iniciarDesligado &&
        !iniciarDecoderDacAgora(volumeLimitado)
    ) {
        definirEstado(
            EstadoAudio::ERRO
        );

        Serial.println(
            "Falha ao inicializar I2S ou PSRAM."
        );

        return false;
    }

    // A interface e a ESP32-audioI2S usam a mesma escala de 0 a 21.
    volumeDesejado.store(
        static_cast<uint8_t>(volumeLimitado),
        std::memory_order_release
    );

    if (iniciarDesligado) {
        manterLinhasI2sEmNivelBaixo();
        servicoAudioAtivo.store(false, std::memory_order_release);
    } else {
        servicoAudioAtivo.store(true, std::memory_order_release);
    }

    BaseType_t criada =
        xTaskCreatePinnedToCore(
            tarefaAudio,
            "AudioService",
            PILHA_SERVICO_AUDIO_BYTES,
            nullptr,
            PRIORIDADE_SERVICO_AUDIO_RADIO,
            &tarefaAudioHandle,
            NUCLEO_SERVICO_AUDIO
        );

    if (criada != pdPASS) {
        tarefaAudioHandle = nullptr;
        desligarDecoderDacAgora();

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

    definirEstado(
        iniciarDesligado
            ? EstadoAudio::DESLIGADO
            : EstadoAudio::PARADO
    );

    if (audio != nullptr) {
        Serial.printf(
            "Servico de audio iniciado. Biblioteca %s, volume %d.\n",
            audio->getVersion(),
            volumeLimitado
        );
    } else {
        Serial.println(
            "Servico de audio pronto em standby; decoder e I2S desligados."
        );
    }

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
        TipoComandoAudio::TOCAR_RADIO;

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

bool tocarRadioAlarme(
    const String& nome,
    const String& url,
    int volume
) {
    if (!dadosRadioValidos(nome.c_str(), url.c_str())) {
        definirErro("Nome ou URL da radio do alarme invalido");
        return false;
    }

    ComandoAudio comando;
    comando.tipo = TipoComandoAudio::TOCAR_ALARME_RADIO;
    comando.volume = static_cast<uint8_t>(
        constrain(volume, VOLUME_MINIMO, VOLUME_MAXIMO)
    );

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



bool tocarArquivoAlarme(
    const String& caminho,
    int volume
) {
    if (caminho.length() > 0) {
        definirErro("Caminho do alarme invalido");
        return false;
    }

    ComandoAudio comando;
    comando.tipo = TipoComandoAudio::TOCAR_ALARME;
    comando.volume = static_cast<uint8_t>(
        constrain(volume, VOLUME_MINIMO, VOLUME_MAXIMO)
    );

    copiarTexto(
        comando.caminhoArquivo,
        sizeof(comando.caminhoArquivo),
        caminho.c_str()
    );

    return enviarComando(comando);
}

bool pararAudio() {
    ComandoAudio comando;

    comando.tipo =
        TipoComandoAudio::PARAR;

    return enviarComando(comando);
}

bool suspenderAudio() {
    if (
        tarefaAudioHandle == nullptr ||
        semaforoAudioDesligado == nullptr
    ) {
        return false;
    }

    if (!decoderDacAtivo.load(std::memory_order_acquire)) {
        manterLinhasI2sEmNivelBaixo();
        return true;
    }

    xSemaphoreTake(semaforoAudioDesligado, 0);

    ComandoAudio comando;

    comando.tipo =
        TipoComandoAudio::SUSPENDER;

    if (!enviarComando(comando)) {
        return false;
    }

    return xSemaphoreTake(
        semaforoAudioDesligado,
        pdMS_TO_TICKS(TEMPO_LIMITE_DESLIGAMENTO_AUDIO_MS)
    ) == pdTRUE;
}

bool retomarAudio(int volume, bool paraPlayer) {
    if (
        tarefaAudioHandle == nullptr ||
        semaforoAudioIniciado == nullptr
    ) {
        return false;
    }

    xSemaphoreTake(semaforoAudioIniciado, 0);
    ultimaInicializacaoBemSucedida.store(
        false,
        std::memory_order_release
    );

    ComandoAudio comando;

    comando.tipo =
        TipoComandoAudio::RETOMAR;
    comando.perfilPlayer = paraPlayer;
    comando.volume =
        static_cast<uint8_t>(
            constrain(
                volume,
                VOLUME_MINIMO,
                VOLUME_MAXIMO
            )
        );

    if (!enviarComando(comando)) {
        return false;
    }

    servicoAudioAtivo.store(
        true,
        std::memory_order_release
    );
    xTaskNotifyGive(tarefaAudioHandle);

    if (xSemaphoreTake(
        semaforoAudioIniciado,
        pdMS_TO_TICKS(TEMPO_LIMITE_INICIALIZACAO_AUDIO_MS)
    ) != pdTRUE) {
        return false;
    }

    return ultimaInicializacaoBemSucedida.load(
        std::memory_order_acquire
    );
}

void alterarVolumeAudio(int volume) {
    volumeDesejado.store(
        static_cast<uint8_t>(
            constrain(volume, VOLUME_MINIMO, VOLUME_MAXIMO)
        ),
        std::memory_order_release
    );
    ajusteVolumePendente.store(true, std::memory_order_release);
}

bool decoderDacEstaAtivo() {
    return decoderDacAtivo.load(
        std::memory_order_acquire
    );
}

void definirAlarmeFmAtivo(bool ativo) {
    if (!bloquearStatus()) {
        return;
    }

    statusAudio.alarmeAtivo = ativo;
    statusAudio.radioFmAlarme = ativo;
    statusAudio.radioAlarme = false;
    statusAudio.somPadraoAlarme = false;

    liberarStatus();
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
