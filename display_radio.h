#ifndef DISPLAY_RADIO_H
#define DISPLAY_RADIO_H

#include <Arduino.h>

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

// Troca o OLED para a tela permanente do estado Relógio.
void mostrarTelaRelogio();

// Mostra uma opção do seletor usando toda a área do OLED.
void mostrarOpcaoEstado(const String& opcao);

void mostrarArquivoPlayer(
    const String& nome,
    int indiceAtual,
    int quantidadeArquivos,
    bool emSelecao
);

/*
 * Deve ser chamada continuamente no loop().
 * Atualiza a tela correspondente ao estado atual do equipamento.
 */
void processarDisplay();

#endif
