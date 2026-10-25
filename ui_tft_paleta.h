#ifndef UI_TFT_PALETA_H
#define UI_TFT_PALETA_H

#include <Arduino.h>

struct PaletaUiTft {
    uint16_t primaria;
    uint16_t secundaria;
    uint16_t atenuada;
    uint16_t superficie;
    uint16_t fundo;
};

// Gerada a partir de docs/ui_handoff/kit_referencia/config/ui_tokens.json.
// Outras paletas permanecem fora do firmware ate a etapa de configuracao.
constexpr PaletaUiTft PALETA_UI_TFT_LARANJA = {
    0xFD49,
    0xBBA6,
    0x6A24,
    0x1881,
    0x0000
};

#endif
