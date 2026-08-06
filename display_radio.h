#ifndef DISPLAY_RADIO_H
#define DISPLAY_RADIO_H

#include <Arduino.h>

bool iniciarDisplay();

void mostrarConfiguracaoWifi();

void mostrarMensagem(const String& mensagem);

void mostrarNomeRadio(
    const String& nome,
    int indiceAtual,
    int quantidadeRadios
);

void mostrarSelecaoRadio(
    const String& nome,
    int indiceSelecionado,
    int quantidadeRadios
);

void mostrarVolume(int volume);

// Apaga o painel do OLED antes da entrada em deep sleep.
void desligarDisplay();

/*
 * Deve ser chamada continuamente no loop().
 * Atualiza o relógio e movimenta o nome da estação.
 */
void processarDisplay();

#endif
