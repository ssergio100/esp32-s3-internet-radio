#include "wifi_radio.h"
#include "configuracao.h"
#include "display_radio.h"

#include <Arduino.h>
#include <WiFi.h>

bool conectarWifi() {
    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_SENHA
    );

    mostrarMensagem("Conectando Wi-Fi");

    Serial.print("Conectando ao Wi-Fi");

    unsigned long inicio = millis();

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");

        if (millis() - inicio >= 20000) {
            Serial.println();
            Serial.println("Falha ao conectar ao Wi-Fi.");

            mostrarMensagem("Falha no Wi-Fi");

            return false;
        }
    }

    Serial.println();
    Serial.println("Wi-Fi conectado.");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    mostrarMensagem("Wi-Fi conectado");

    delay(500);

    return true;
}