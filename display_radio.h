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

struct DadosDisplayRadioFm {
    uint16_t frequencia10kHz = 0;
    const char* nomeEstacao = "";
    const char* radioTexto = "";
    const char* estado = "";
    bool nomeEstacaoValido = false;
    bool radioTextoValido = false;
    bool buscando = false;
    bool buscaConcluida = false;
    bool qualidadeValida = false;
    int16_t nivelDecimosDbuv = 0;
    uint32_t sequenciaEventoEncoder = 0;
    uint32_t momentoEventoEncoderUs = 0;
};

// Atualiza o modelo da tela FM. A implementação do display não acessa o
// driver TEF6686; recebe apenas dados já tratados pelo coordenador principal.
void mostrarRadioFm(const DadosDisplayRadioFm& dados);

void mostrarVolume(int volume);

// Troca o OLED para a tela permanente do estado Relógio.
void mostrarTelaRelogio();

// Sobrepõe temporariamente a interface enquanto um alarme está tocando.
void mostrarAlarme(const String& nome, int volume);

/*
 * Mantém compatibilidade como caminho de contingência no loop(). Quando o
 * serviço dedicado foi criado, somente a tarefa do display faz a atualização.
 */
void processarDisplay();

#endif
