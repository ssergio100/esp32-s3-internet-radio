
#ifndef RADIOS_H
#define RADIOS_H

#include <Arduino.h>

struct Radio {
    const char* nome;
    const char* url;
    uint32_t id = 0;
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
const Radio* obterRadioPorId(uint32_t id);
int obterIndiceRadioPorId(uint32_t id);

#endif
