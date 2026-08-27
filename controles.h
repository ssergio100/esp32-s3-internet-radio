#ifndef CONTROLES_H
#define CONTROLES_H

struct LeituraControles {
    bool cliqueNavegacaoDetectado = false;
    bool cliqueVolumeDetectado = false;
    long deslocamentoEncoderNavegacao = 0;
    long deslocamentoEncoderVolume = 0;
};

void iniciarControles();

LeituraControles lerControles();

#endif
