
#ifndef RADIOS_H
#define RADIOS_H

#include <Arduino.h>

struct Radio {
    const char* nome;
    const char* url;
};

bool carregarRadios();

int obterQuantidadeRadios();

const Radio* obterRadio(int indice);

#endif