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
    // Valor real da leitura de qualidade do TEF6686, em 0,1 kHz.
    uint16_t larguraBandaDecimosKHz = 0;
    bool larguraBandaValida = false;
    bool pilotoEstereo = false;
    bool pilotoEstereoValido = false;
    char nomeEstacao[25] = {};
    char radioTexto[193] = {};
    bool nomeEstacaoValido = false;
    bool radioTextoValido = false;
    char ultimoErro[64] = {};
    uint32_t sequenciaEventoEncoder = 0;
    uint32_t momentoEventoEncoderUs = 0;
    uint32_t revisao = 0;
};

// Eleva GPA4, inicializa somente o receptor TEF6686 e preserva a frequência
// anterior em RAM. Display, encoders, Wi-Fi e o restante permanecem sob
// responsabilidade do programa principal.
bool iniciarRadioFm(int volumeInterface);

bool ativarRadioFm(int volumeInterface);
void desativarRadioFm();
void ajustarVolumeRadioFm(int volumeInterface);

// Cada detente procura a estação seguinte (positivo) ou anterior (negativo).
// As etapas ficam enfileiradas para movimentos rápidos não perderem passos.
void navegarRadioFm(
    long deslocamento,
    uint32_t momentoEventoEncoderUs = 0,
    uint32_t sequenciaEventoEncoder = 0
);

// Executa no máximo uma etapa da busca e uma consulta periódica por chamada.
// Retorna true quando alguma informação visível mudou.
bool processarRadioFm();

StatusRadioFm obterStatusRadioFm();
const char* obterTextoEstadoRadioFm(EstadoRadioFm estado);

#endif
