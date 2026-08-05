#ifndef CONFIGURACAO_H
#define CONFIGURACAO_H

#include <Arduino.h>

// =====================================================
// Ligações do hardware
// =====================================================

// LED RGB integrado à placa
#define PIN_LED_RGB 48
#define BRILHO_LED_RGB 50

// Amplificador de áudio I2S MAX98357A
constexpr int PIN_MAX98357A_BCLK = 5;
constexpr int PIN_MAX98357A_LRC  = 6;
constexpr int PIN_MAX98357A_DIN  = 7;

// Display OLED SSD1306
constexpr int PIN_DISPLAY_SDA = 9;
constexpr int PIN_DISPLAY_SCL = 10;

constexpr int DISPLAY_LARGURA  = 128;
constexpr int DISPLAY_ALTURA   = 64;
constexpr int DISPLAY_ENDERECO = 0x3C;

// Encoder rotativo
constexpr int PIN_ENCODER_CLK = 15;
constexpr int PIN_ENCODER_DT  = 16;
constexpr int PIN_ENCODER_SW  = 17;
constexpr int PIN_ENCODER_VCC = -1;

constexpr int PASSOS_ENCODER = 2;

// =====================================================
// Comportamento ajustável pelo usuário
// =====================================================

// Áudio
constexpr int VOLUME_MINIMO = 0;
constexpr int VOLUME_MAXIMO = 21;
constexpr int VOLUME_PADRAO = 10;

// Interface
constexpr unsigned long TEMPO_TELA_VOLUME_MS = 2000;
constexpr unsigned long TEMPO_INATIVIDADE_SELECAO_MS = 10000;

// O nome avança um pixel a cada passo.
// Diminua o intervalo para acelerar a rolagem; aumente para desacelerar.
constexpr unsigned long INTERVALO_PASSO_ROLAGEM_NOME_MS = 50;

// =====================================================
// Ajustes internos
// =====================================================

// Tarefas de áudio
constexpr int NUCLEO_DECODIFICADOR_AUDIO = 0;
constexpr int NUCLEO_SERVICO_AUDIO = 1;
constexpr uint32_t PILHA_SERVICO_AUDIO_BYTES = 12288;
constexpr UBaseType_t PRIORIDADE_SERVICO_AUDIO = 3;

// Tratamento dos controles
constexpr unsigned long TEMPO_VALIDAR_BOTAO_MS = 30;
constexpr unsigned long DEBOUNCE_ENCODER_MS = 200;

#endif
