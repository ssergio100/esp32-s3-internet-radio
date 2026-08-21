#include "telemetria.h"

#include <Arduino.h>
#include <WiFi.h>

#include "alarmes.h"
#include "audio_radio.h"
#include "configuracao.h"

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

        Serial.printf(
            "Audio: %s | Buffer: %lu ms | Bitrate: %lu | "
            "Fluxo lento: %lu | Reconexoes: %lu | "
            "Pilha livre min.: %lu bytes\n",
            obterTextoEstadoAudio(statusAudio.estado),
            statusAudio.bufferMilissegundos,
            statusAudio.bitrate,
            statusAudio.eventosFluxoLento,
            statusAudio.tentativasReconexao,
            statusAudio.stackMinimoBytes
        );
    }

    void registrarAlarmes() {
        StatusAlarmes alarmes = obterStatusAlarmes();
        StatusAudio audio = obterStatusAudio();

        const char* origemSom = "inativo";

        if (audio.alarmeAtivo) {
            if (audio.somPadraoAlarme) {
                origemSom =
                    audio.arquivoAlarmeSolicitado
                        ? "padrao (arquivo indisponivel)"
                        : "padrao";
            } else {
                origemSom = "arquivo do microSD";
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
    registrarAlarmes();
}
