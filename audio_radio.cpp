#include "audio_radio.h"
#include "configuracao.h"

#include <Arduino.h>
#include "Audio.h"

namespace {

Audio audio;

}

void iniciarAudio(int volume) {
    audio.setPinout(
        PIN_MAX98357A_BCLK,
        PIN_MAX98357A_LRC,
        PIN_MAX98357A_DIN
    );

    audio.setVolumeSteps(100);
    audio.setVolume(volume);

    Serial.printf(
        "Áudio iniciado com volume %d%%\n",
        volume
    );
}

void processarAudio() {
    audio.loop();
}

bool tocarRadio(
    const String& nome,
    const String& url
) {
    Serial.println();
    Serial.print("Conectando à rádio: ");
    Serial.println(nome);

    audio.stopSong();

    delay(200);

    bool conectado =
        audio.connecttohost(
            url.c_str()
        );

    if (!conectado) {
        Serial.println(
            "Não foi possível iniciar a rádio."
        );
    }

    return conectado;
}

void alterarVolumeAudio(int volume) {
    audio.setVolume(volume);
}

// Callbacks usados pela biblioteca Audio

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

void audio_bitrate(const char* info) {
    Serial.print("Bitrate: ");
    Serial.println(info);
}