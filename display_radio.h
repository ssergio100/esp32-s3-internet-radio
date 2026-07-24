#ifndef DISPLAY_RADIO_H
#define DISPLAY_RADIO_H

#include <Arduino.h>

bool iniciarDisplay();

void mostrarMensagem(
    const String& mensagem
);

void mostrarNomeRadio(
    const String& nome,
    int indiceAtual,
    int quantidadeRadios
);

void mostrarVolume(
    int volume
);

#endif