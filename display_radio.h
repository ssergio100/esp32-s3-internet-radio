#ifndef DISPLAY_RADIO_H
#define DISPLAY_RADIO_H

#include <Arduino.h>

#include "jogo_breakout.h"

void iniciarDisplay();

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

void mostrarEstadoRadio(
    const String& estado,
    int indiceAtual,
    int quantidadeRadios
);

void mostrarVolume(int volume);

void mostrarJogoBreakout(
    const QuadroJogoBreakout& quadro
);

// Apaga o painel do OLED antes da entrada em deep sleep.
void desligarDisplay();

/*
 * Deve ser chamada continuamente no loop().
 * Atualiza e movimenta o diagnóstico e a faixa central.
 */
void processarDisplay();

#endif
