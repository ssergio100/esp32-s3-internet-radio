#ifndef RADIO_FM_H
#define RADIO_FM_H

#include <Arduino.h>

enum class EstadoRadioFm : uint8_t {
    INDISPONIVEL,
    REPOUSO,
    ATIVO,
    BUSCANDO,
    ERRO
};

struct StatusRadioFm {
    EstadoRadioFm estado = EstadoRadioFm::INDISPONIVEL;
    uint16_t frequencia10kHz = 0;
    int16_t nivelDecimosDbuv = 0;
    bool qualidadeValida = false;
    char nomeEstacao[25] = {};
    char ultimoErro[64] = {};
    uint32_t revisao = 0;
};

// Inicializa somente o receptor TEF6686. Display, encoders, Wi-Fi e o restante
// do equipamento permanecem sob responsabilidade do programa principal.
bool iniciarRadioFm(int volumeInterface);

bool ativarRadioFm(int volumeInterface);
void desativarRadioFm();
void ajustarVolumeRadioFm(int volumeInterface);

// Cada detente procura a estação seguinte (positivo) ou anterior (negativo).
// As etapas ficam enfileiradas para movimentos rápidos não perderem passos.
void navegarRadioFm(long deslocamento);

// Executa no máximo uma etapa da busca e uma consulta periódica por chamada.
// Retorna true quando alguma informação visível mudou.
bool processarRadioFm();

StatusRadioFm obterStatusRadioFm();
const char* obterTextoEstadoRadioFm(EstadoRadioFm estado);

#endif
