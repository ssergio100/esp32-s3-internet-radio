#include <Arduino.h>
#include <WiFi.h>
#include "Audio.h"

const char* WIFI_NOME = "SEU_WIFI";
const char* WIFI_SENHA = "SUA_SENHA";

const char* URL_ANTENA_1 =
    "https://antenaone.crossradio.com.br/stream/1";

const int PIN_BCLK = 5;
const int PIN_LRC = 6;
const int PIN_DIN = 7;

Audio audio;

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_NOME, WIFI_SENHA);

    Serial.print("Conectando ao Wi-Fi");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.print("Wi-Fi conectado. IP: ");
    Serial.println(WiFi.localIP());

    WiFi.setSleep(false);

    audio.setPinout(
        PIN_BCLK,
        PIN_LRC,
        PIN_DIN
    );

    audio.setVolume(12);

    Serial.println("Conectando à Antena 1...");

    if (!audio.connecttohost(URL_ANTENA_1)) {
        Serial.println("Falha ao iniciar a Antena 1.");
    }
}

void loop() {
    audio.loop();
}

void audio_info(const char* info) {
    Serial.print("Áudio: ");
    Serial.println(info);
}

void audio_showstation(const char* info) {
    Serial.print("Estação: ");
    Serial.println(info);
}

void audio_showstreamtitle(const char* info) {
    Serial.print("Música: ");
    Serial.println(info);
}
