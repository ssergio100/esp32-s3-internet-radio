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

enum class TemaUiTft : uint8_t {
    LARANJA,
    VERDE,
    AZUL,
    BRANCO,
    VERMELHO,
    APENAS_LARANJA
};

// Gerada a partir de docs/ui_handoff/kit_referencia/config/ui_tokens.json.
// Outras paletas permanecem fora do firmware ate a etapa de configuracao.
constexpr PaletaUiTft PALETA_UI_TFT_LARANJA = {
    //0xFD49,
    0xFC80,  // Laranja principal (teste)
    0xA240,
    0x8180,
    0x30E0,  // superficie: RGB  49,  28, 0
    0x0000
};

constexpr PaletaUiTft PALETA_UI_TFT_VERDE = {
    0x8F8F, 0x654B, 0x32C6, 0x08C1, 0x0000
};

constexpr PaletaUiTft PALETA_UI_TFT_AZUL = {
    0x8E5F, 0x64D8, 0x328D, 0x0883, 0x0000
};

constexpr PaletaUiTft PALETA_UI_TFT_BRANCO = {
    0xE73C, 0xAD75, 0x630C, 0x18C3, 0x0000
};

constexpr PaletaUiTft PALETA_UI_TFT_VERMELHO = {
    0xFC0F, 0xBAEA, 0x6186, 0x1861, 0x0000
};

// Tema monocromatico completo. Cada papel visual conserva a mesma matiz
// laranja, reduzindo apenas a luminosidade: destaque, texto secundario,
// informacao atenuada, superficies e fundo. Assim permanecem visiveis as
// barras, os cartoes e suas bordas, como nos demais temas.
constexpr PaletaUiTft PALETA_UI_TFT_APENAS_LARANJA = {
    0xFC80,  // primaria:   RGB 255, 146, 0
    0xBB60,  // secundaria: RGB 189, 109, 0
    0x7A40,  // atenuada:   RGB 123,  73, 0
    0x30E0,  // superficie: RGB  49,  28, 0
    0x1060   // fundo:      RGB  16,  12, 0
};

inline const PaletaUiTft& obterPaletaUiTft(TemaUiTft tema) {
    switch (tema) {
        case TemaUiTft::VERDE:
            return PALETA_UI_TFT_VERDE;
        case TemaUiTft::AZUL:
            return PALETA_UI_TFT_AZUL;
        case TemaUiTft::BRANCO:
            return PALETA_UI_TFT_BRANCO;
        case TemaUiTft::VERMELHO:
            return PALETA_UI_TFT_VERMELHO;
        case TemaUiTft::APENAS_LARANJA:
            return PALETA_UI_TFT_APENAS_LARANJA;
        case TemaUiTft::LARANJA:
        default:
            return PALETA_UI_TFT_LARANJA;
    }
}

#endif
