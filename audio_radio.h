#ifndef AUDIO_RADIO_H
#define AUDIO_RADIO_H

#include <Arduino.h>

void iniciarAudio(int volume);

void processarAudio();

bool tocarRadio(
    const String& nome,
    const String& url
);

void alterarVolumeAudio(int volume);

uint8_t obterPercentualBufferAudio();

#endif