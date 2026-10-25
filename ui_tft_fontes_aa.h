#ifndef UI_TFT_FONTES_AA_H
#define UI_TFT_FONTES_AA_H

#include <Arduino.h>

struct GlifoFonteAa {
    uint16_t codigo;
    uint32_t offset;
    uint8_t largura;
    uint8_t altura;
    uint8_t avanco;
    int8_t deslocamentoX;
    int8_t deslocamentoY;
};

struct FonteAa {
    const uint8_t* bitmap;
    const GlifoFonteAa* glifos;
    uint16_t quantidadeGlifos;
    uint8_t ascendente;
    uint8_t descendente;
};

extern const FonteAa FONTE_AA_PEQUENA;
extern const FonteAa FONTE_AA_NORMAL;
extern const FonteAa FONTE_AA_DESTAQUE;
extern const FonteAa FONTE_AA_FREQUENCIA;

#endif
