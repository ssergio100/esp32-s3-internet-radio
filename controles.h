#ifndef CONTROLES_H
#define CONTROLES_H

#include <stdint.h>

struct LeituraControles {
    bool cliqueNavegacaoDetectado = false;
    bool cliqueVolumeDetectado = false;
    long deslocamentoEncoderNavegacao = 0;
    long deslocamentoEncoderVolume = 0;
    uint32_t momentoEncoderNavegacaoUs = 0;
    uint32_t sequenciaEncoderNavegacao = 0;
};

void iniciarControles();

LeituraControles lerControles();

#endif
