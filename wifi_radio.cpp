#include "wifi_radio.h"
#include "display_radio.h"
#include "indicador_led.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

namespace {

constexpr unsigned long INTERVALO_RECONEXAO_WIFI_MS =
    10000;

}

void supervisionarWifi() {
    static wl_status_t estadoAnterior =
        WL_CONNECTED;
    static unsigned long ultimaTentativa = 0;

    wl_status_t estadoAtual = WiFi.status();

    if (estadoAtual == WL_CONNECTED) {
        if (estadoAnterior != WL_CONNECTED) {
            Serial.println();
            Serial.println("Wi-Fi reconectado.");
            Serial.print("BSSID: ");
            Serial.println(WiFi.BSSIDstr());
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
        }

        estadoAnterior = estadoAtual;
        return;
    }

    unsigned long agora = millis();

    if (estadoAnterior == WL_CONNECTED) {
        Serial.println();
        Serial.println("Wi-Fi desconectado.");

        // Permite uma tentativa imediata ao detectar a queda.
        ultimaTentativa =
            agora - INTERVALO_RECONEXAO_WIFI_MS;
    }

    estadoAnterior = estadoAtual;

    if (
        agora - ultimaTentativa <
        INTERVALO_RECONEXAO_WIFI_MS
    ) {
        return;
    }

    ultimaTentativa = agora;

    Serial.println(
        "Solicitando reconexao do Wi-Fi..."
    );

    // Usa as credenciais persistidas e deixa a pilha
    // escolher normalmente o ponto de acesso disponível.
    WiFi.reconnect();
}

bool conectarWifi() {
    WiFi.mode(WIFI_STA);

    mostrarMensagem("Conectando Wi-Fi");

    Serial.println();
    Serial.println("Conectando ao Wi-Fi...");

    WiFiManager wifiManager;

    wifiManager.setConnectTimeout(20);

    wifiManager.setAPCallback([](WiFiManager *gerenciador) {
        Serial.println();
        Serial.println("Portal de configuração iniciado.");

        Serial.print("Rede: ");
        Serial.println(
            gerenciador->getConfigPortalSSID()
        );

        Serial.println(
            "Endereço: http://192.168.4.1"
        );

       mostrarConfiguracaoWifi();
    });

    wifiManager.setCustomHeadElement(
        "<script>"
        "document.addEventListener('DOMContentLoaded',function(){"
            "document.querySelectorAll('.msg').forEach(function(el){"
                "if(el.textContent.trim()==='No AP set'){"
                    "el.remove();"
                "}"
            "});"
        "});"
        "</script>"
    );

    // O portal permanece disponível até receber
    // credenciais válidas. Isso permite levar o rádio
    // para um local com outra rede sem regravar o firmware.
    wifiManager.setConfigPortalBlocking(false);

    bool conectado = wifiManager.autoConnect(
        "RADIO-WEB"
    );

    while (!conectado) {
        wifiManager.process();
        atualizarIndicadorConexaoWifi();

        conectado =
            WiFi.status() == WL_CONNECTED;

        delay(10);
    }

    apagarIndicadorLed();

    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);

    Serial.println();
    Serial.println("Wi-Fi conectado.");

    Serial.print("Rede: ");
    Serial.println(WiFi.SSID());

    Serial.print("Sinal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("BSSID: ");
    Serial.println(WiFi.BSSIDstr());

    Serial.print("Canal: ");
    Serial.println(WiFi.channel());

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    mostrarMensagem("Wi-Fi conectado");

    delay(500);

    return true;
}
