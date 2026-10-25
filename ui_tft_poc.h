#ifndef UI_TFT_POC_H
#define UI_TFT_POC_H

#include <Arduino.h>

#include "configuracao.h"

#if RADIO_UI_TFT_POC

enum class TelaUiTftPoc : uint8_t {
    MENSAGEM,
    CONFIGURACAO_WIFI,
    RADIO_WEB,
    RADIO_FM,
    RELOGIO,
    ALARME
};

struct EstadoUiTftPoc {
    TelaUiTftPoc tela = TelaUiTftPoc::MENSAGEM;
    uint32_t revisao = 0;
    uint32_t momentoAlteracaoMs = 0;
    char titulo[24] = {};
    char mensagem[96] = {};
    char estacao[64] = {};
    char faixa[128] = {};
    char codec[16] = {};
    char data[64] = {};
    char psFm[25] = {};
    char radioTextoFm[193] = {};
    char estadoFm[64] = {};
    uint16_t frequenciaFm10kHz = 0;
    int16_t sinalFmDecimosDbuv = 0;
    bool sinalFmValido = false;
    bool psFmValido = false;
    bool radioTextoFmValido = false;
    bool fmBuscando = false;
    bool fmBuscaConcluida = false;
    uint32_t sequenciaEventoEncoderFm = 0;
    uint32_t momentoEventoEncoderFmUs = 0;
    uint32_t bitrate = 0;
    uint32_t bufferMs = 0;
    bool wifiConectado = false;
    int16_t wifiRssi = 0;
    bool horarioDisponivel = false;
    uint8_t hora = 0;
    uint8_t minuto = 0;
    int16_t volume = 0;
};

struct MetricasUiTftPoc {
    bool disponivel = false;
    uint32_t atualizacoes = 0;
    uint32_t camposRedesenhados = 0;
    uint32_t duracaoUltimaUs = 0;
    uint32_t duracaoMaximaUs = 0;
    uint32_t duracaoMediaUs = 0;
    uint32_t latenciaUltimaMs = 0;
    uint32_t latenciaBuscandoUltimaUs = 0;
    uint32_t latenciaBuscandoMaximaUs = 0;
    uint32_t latenciaBuscandoP95Us = 0;
    uint32_t amostrasLatenciaBuscando = 0;
    uint32_t latenciaResultadoUltimaUs = 0;
    uint32_t latenciaResultadoMaximaUs = 0;
    uint32_t latenciaResultadoP95Us = 0;
    uint32_t amostrasLatenciaResultado = 0;
    int32_t variacaoHeapBytes = 0;
    int32_t variacaoPsramBytes = 0;
};

bool iniciarUiTftPoc();
void processarUiTftPoc(const EstadoUiTftPoc& estado);
MetricasUiTftPoc obterMetricasUiTftPoc();

#endif

#endif
