#ifndef CONFIGURACAO_H
#define CONFIGURACAO_H

#include <Arduino.h>

// LED RGB
#define PIN_LED_RGB 48
#define BRILHO_LED_RGB 50

// MAX98357A
constexpr int PIN_MAX98357A_BCLK = 5;
constexpr int PIN_MAX98357A_LRC  = 6;
constexpr int PIN_MAX98357A_DIN  = 7;

// Display OLED
constexpr int PIN_DISPLAY_SDA = 9;
constexpr int PIN_DISPLAY_SCL = 10;

constexpr int DISPLAY_LARGURA  = 128;
constexpr int DISPLAY_ALTURA   = 64;
constexpr int DISPLAY_ENDERECO = 0x3C;

// Encoder
constexpr int PIN_ENCODER_CLK = 15;
constexpr int PIN_ENCODER_DT  = 16;
constexpr int PIN_ENCODER_SW  = 17;
constexpr int PIN_ENCODER_VCC = -1;

constexpr int PASSOS_ENCODER = 2;

// Volume
constexpr int VOLUME_MINIMO = 0;
constexpr int VOLUME_MAXIMO = 21;
constexpr int VOLUME_PADRAO = 10;

// Tempos
constexpr unsigned long TEMPO_TELA_VOLUME_MS = 2000;
constexpr unsigned long TEMPO_CONFIRMAR_RADIO_MS = 1000;
constexpr unsigned long DEBOUNCE_ENCODER_MS = 250;


#endif