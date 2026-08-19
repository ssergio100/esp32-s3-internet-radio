#ifndef AUDIO_RADIO_H
#define AUDIO_RADIO_H

#include <Arduino.h>

enum class EstadoAudio : uint8_t {
    DESLIGADO,
    INICIALIZANDO,
    PARADO,
    CONECTANDO,
    BUFFERIZANDO,
    TOCANDO,
    DEGRADADO,
    RECONECTANDO,
    ERRO
};

struct StatusAudio {
    EstadoAudio estado = EstadoAudio::DESLIGADO;

    uint32_t bitrate = 0;
    uint32_t bufferBytes = 0;
    uint32_t bufferTotalBytes = 0;
    uint32_t bufferMilissegundos = 0;

    uint32_t eventosFluxoLento = 0;
    uint32_t tentativasReconexao = 0;
    uint32_t stackMinimoBytes = 0;

    char radio[64] = "";
    char titulo[128] = "";
    char codec[16] = "";
    char ultimoErro[192] = "";
};

bool iniciarAudio(int volume);

bool tocarRadio(
    const String& nome,
    const String& url
);

// Interrompe a fonte atual e reproduz um arquivo do diretório /sons no cartão.
// Ao terminar, o serviço permanece parado; a retomada da rádio será uma regra
// explícita do futuro agendador de eventos.
bool tocarArquivoAudio(
    const String& caminho
);

// Dispara o chime horário do relógio. Se uma rádio web estiver tocando,
// o chime é mixado em tempo real com ducking suave sem interromper a transmissão.
// Se o áudio estiver parado, toca o chime como arquivo individual.
bool dispararChimeRelogio();

bool pararAudio();

void alterarVolumeAudio(int volume);

StatusAudio obterStatusAudio();

const char* obterTextoEstadoAudio(
    EstadoAudio estado
);

#endif
