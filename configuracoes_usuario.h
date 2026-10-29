#ifndef CONFIGURACOES_USUARIO_H
#define CONFIGURACOES_USUARIO_H

#include <Arduino.h>

#include "ui_tft_paleta.h"

struct ConfiguracoesUsuario {
    bool sincronizacaoAutomatica = true;
    uint16_t repousoTftMinutos = 0;
    TemaUiTft temaTft = TemaUiTft::LARANJA;
    uint8_t atenuacaoFmDb = 0;
    uint8_t atenuacaoRadioWebDb = 0;
    uint8_t atenuacaoDfPlayerDb = 0;
};

// Preferências pequenas e independentes do catálogo e dos alarmes. São
// gravadas em NVS somente quando o usuário confirma uma alteração.
ConfiguracoesUsuario carregarConfiguracoesUsuario();
bool salvarConfiguracoesUsuario(const ConfiguracoesUsuario& configuracoes);

#endif
