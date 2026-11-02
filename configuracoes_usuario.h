#ifndef CONFIGURACOES_USUARIO_H
#define CONFIGURACOES_USUARIO_H

#include <Arduino.h>

#include "ui_tft_paleta.h"

struct AjustesRecepcaoRadioFm {
    bool sintoniaPorScanner = true;
    uint16_t larguraBandaKHz = 0;
    bool stereoAutomatico = true;
    bool multipath = true;
    bool noiseBlanker = true;
    bool softmute = true;
    bool highcut = true;
    uint8_t passoSintonia10kHz = 10;
    bool rds = true;
    bool channelEqualizer = true;
    uint16_t deemphasisUs = 50;
    uint16_t rfAgcDecimosDbuv = 920;
    bool stereoBlend = true;
    bool stereoHighBlend = true;
    int16_t levelOffsetDecimosDb = 0;
    uint8_t bandwidthOptions = 1;
};

struct ConfiguracoesUsuario {
    bool sincronizacaoAutomatica = true;
    uint16_t repousoTftMinutos = 0;
    TemaUiTft temaTft = TemaUiTft::LARANJA;
    uint8_t atenuacaoFmDb = 0;
    uint8_t atenuacaoRadioWebDb = 0;
    uint8_t atenuacaoDfPlayerDb = 0;
    AjustesRecepcaoRadioFm radioFm;
};

// Preferências pequenas e independentes do catálogo e dos alarmes. São
// gravadas em NVS somente quando o usuário confirma uma alteração.
ConfiguracoesUsuario carregarConfiguracoesUsuario();
bool salvarConfiguracoesUsuario(const ConfiguracoesUsuario& configuracoes);

#endif
