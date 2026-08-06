#ifndef CONTROLES_H
#define CONTROLES_H

struct LeituraControles {
    bool cliqueDetectado = false;
    bool cliqueLongoDetectado = false;
    long deslocamentoEncoder = 0;
};

void iniciarControles();

LeituraControles lerControles();

#endif
