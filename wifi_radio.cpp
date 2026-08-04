#include "wifi_radio.h"
#include "configuracao.h"
#include "display_radio.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

namespace {

void atualizarLedConexao() {
    static unsigned long ultimaPiscada = 0;
    static bool ledAceso = false;

    unsigned long agora = millis();

    if (agora - ultimaPiscada < 100) {
        return;
    }

    ultimaPiscada = agora;
    ledAceso = !ledAceso;

    rgbLedWrite(
        PIN_LED_RGB,
        0,
        0,
        ledAceso ? BRILHO_LED_RGB : 0
    );
}

void apagarLedConexao() {
    rgbLedWrite(
        PIN_LED_RGB,
        0,
        0,
        0
    );
}

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
        atualizarLedConexao();

        conectado =
            WiFi.status() == WL_CONNECTED;

        delay(10);
    }

    apagarLedConexao();

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
