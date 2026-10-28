
#include "telemetria.h"

#include <Arduino.h>
#include <WiFi.h>

#include "alarmes.h"
#include "audio_radio.h"
#include "chime.h"
#include "configuracao.h"
#include "dfplayer.h"
#include "relogio_rtc.h"
#include "ui_tft_poc.h"

namespace {

    void registrarTemperaturaEMemoria() {
        Serial.printf(
            "Temperatura: %.1f °C\n",
            temperatureRead()
        );
        Serial.printf(
            "RAM livre: %u KB\n",
            ESP.getFreeHeap() / 1024
        );
        Serial.printf(
            "Menor RAM livre: %u KB\n",
            ESP.getMinFreeHeap() / 1024
        );
        Serial.printf(
            "Maior bloco RAM: %u KB\n",
            ESP.getMaxAllocHeap() / 1024
        );
        Serial.printf(
            "PSRAM livre: %u KB\n",
            ESP.getFreePsram() / 1024
        );
        Serial.printf(
            "Maior bloco PSRAM: %u KB\n",
            ESP.getMaxAllocPsram() / 1024
        );
    }

    void registrarConexaoWifi() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Wi-Fi: desconectado");
            return;
        }

        Serial.printf(
            "Wi-Fi: %d dBm | BSSID: %s | Canal: %d\n",
            WiFi.RSSI(),
            WiFi.BSSIDstr().c_str(),
            WiFi.channel()
        );
    }

    void registrarAudio() {
        StatusAudio statusAudio = obterStatusAudio();

        const uint32_t total =
            statusAudio.bufferTotalBytes;

        const uint32_t ocupado =
            statusAudio.bufferBytes;

        const uint32_t livre =
            total >= ocupado ? total - ocupado : 0;

        const float percentual =
            total > 0
                ? ocupado * 100.0f / total
                : 0.0f;

        Serial.printf(
            "Audio: %s | Buffer: %lu ms | Bitrate: %lu | "
            "Fluxo lento: %lu | Reconexoes: %lu | "
            "Pilha livre min.: %lu bytes\n",
            obterTextoEstadoAudio(statusAudio.estado),
            (unsigned long)statusAudio.bufferMilissegundos,
            (unsigned long)statusAudio.bitrate,
            (unsigned long)statusAudio.eventosFluxoLento,
            (unsigned long)statusAudio.tentativasReconexao,
            (unsigned long)statusAudio.stackMinimoBytes
        );

        Serial.printf(
            "Buffer fisico: %lu / %lu bytes (%.1f%%) | "
            "Livre: %lu bytes\n",
            (unsigned long)ocupado,
            (unsigned long)total,
            percentual,
            (unsigned long)livre
        );
    }

    void registrarPresencaRtc() {
        Serial.printf(
            "RTC: %s\n",
            relogioRtcRespondendo()
                ? "presente e respondendo"
                : "ausente ou sem resposta"
        );
    }

    void registrarAlarmes() {
        StatusAlarmes alarmes = obterStatusAlarmes();
        StatusAudio audio = obterStatusAudio();

        const char* origemSom = "inativo";

        if (audio.alarmeAtivo) {
            if (audio.radioFmAlarme) {
                origemSom = "radio FM";
            } else if (audio.radioAlarme) {
                origemSom = "radio web";
            } else {
                origemSom = "padrao";
            }
        }

        Serial.printf(
            "Alarmes: %u/%u ativos | Som padrao: %s | Audio: %s\n",
            static_cast<unsigned int>(alarmes.quantidadeAtivos),
            static_cast<unsigned int>(alarmes.quantidade),
            alarmes.somPadraoDisponivel
                ? "disponivel"
                : "indisponivel",
            origemSom
        );
    }

    void registrarChime() {
        ConfiguracaoChime chime = obterConfiguracaoChime();
        EstadoDfPlayer dfPlayer = obterEstadoDfPlayer();

        Serial.printf(
            "CHIME DEBUG telemetria: %s | Intervalo: %u min | Som: %03u | Volume: %u/%u | "
            "DFPlayer: %s | Uso: %u | Erro: %u\n",
            chime.ativo ? "ativo" : "inativo",
            static_cast<unsigned>(chime.intervaloMinutos),
            static_cast<unsigned>(chime.som),
            static_cast<unsigned>(chime.volume),
            static_cast<unsigned>(VOLUME_MAXIMO),
            obterTextoFaseDfPlayer(dfPlayer.fase),
            static_cast<unsigned>(dfPlayer.uso),
            static_cast<unsigned>(dfPlayer.erro)
        );
    }

#if RADIO_UI_TFT_POC
    void registrarTftPoc() {
        MetricasUiTftPoc tft = obterMetricasUiTftPoc();

        Serial.printf(
            "TFT PoC: %s | Atualizacoes: %lu | Campos: %lu | "
            "Desenho ultimo/medio/max: %lu/%lu/%lu us | "
            "Latencia modelo: %lu ms | Heap: %+ld | PSRAM: %+ld bytes\n",
            tft.disponivel ? "driver inicializado" : "inativo",
            static_cast<unsigned long>(tft.atualizacoes),
            static_cast<unsigned long>(tft.camposRedesenhados),
            static_cast<unsigned long>(tft.duracaoUltimaUs),
            static_cast<unsigned long>(tft.duracaoMediaUs),
            static_cast<unsigned long>(tft.duracaoMaximaUs),
            static_cast<unsigned long>(tft.latenciaUltimaMs),
            static_cast<long>(tft.variacaoHeapBytes),
            static_cast<long>(tft.variacaoPsramBytes)
        );

        Serial.printf(
            "TFT FM encoder->BUSCANDO ultimo/max/p95: %lu/%lu/%lu us (%lu amostras) | "
            "encoder->resultado ultimo/max/p95: %lu/%lu/%lu us (%lu amostras)\n",
            static_cast<unsigned long>(tft.latenciaBuscandoUltimaUs),
            static_cast<unsigned long>(tft.latenciaBuscandoMaximaUs),
            static_cast<unsigned long>(tft.latenciaBuscandoP95Us),
            static_cast<unsigned long>(tft.amostrasLatenciaBuscando),
            static_cast<unsigned long>(tft.latenciaResultadoUltimaUs),
            static_cast<unsigned long>(tft.latenciaResultadoMaximaUs),
            static_cast<unsigned long>(tft.latenciaResultadoP95Us),
            static_cast<unsigned long>(tft.amostrasLatenciaResultado)
        );

        Serial.printf(
            "TFT Alarmes encoder->display ultimo/max/p95: %lu/%lu/%lu us (%lu amostras)\n",
            static_cast<unsigned long>(tft.latenciaAlarmesUltimaUs),
            static_cast<unsigned long>(tft.latenciaAlarmesMaximaUs),
            static_cast<unsigned long>(tft.latenciaAlarmesP95Us),
            static_cast<unsigned long>(tft.amostrasLatenciaAlarmes)
        );
    }
#endif

}

void registrarTelemetriaPeriodica() {
    static unsigned long momentoUltimoRegistroMs = 0;

    unsigned long agoraMs = millis();

    if (
        agoraMs - momentoUltimoRegistroMs <
        INTERVALO_TELEMETRIA_SERIAL_MS
    ) {
        return;
    }

    momentoUltimoRegistroMs = agoraMs;

    registrarTemperaturaEMemoria();
    registrarConexaoWifi();
    registrarAudio();
    registrarPresencaRtc();
    registrarAlarmes();
    registrarChime();
#if RADIO_UI_TFT_POC
    registrarTftPoc();
#endif
}
