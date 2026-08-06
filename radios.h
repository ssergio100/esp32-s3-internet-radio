
#ifndef RADIOS_H
#define RADIOS_H

#include <Arduino.h>

struct Radio {
    const char* nome;
    const char* url;
};

constexpr size_t QUANTIDADE_MAXIMA_RADIOS = 50;
constexpr size_t TAMANHO_MAXIMO_NOME_RADIO = 63;
constexpr size_t TAMANHO_MAXIMO_URL_RADIO = 511;

bool dadosRadioValidos(
    const char* nome,
    const char* url
);

void carregarRadios();

int obterQuantidadeRadios();

const Radio* obterRadio(int indice);

#endif
