#ifndef AUDIO_RADIO_H
#define AUDIO_RADIO_H

#include <Arduino.h>

enum class EstadoAudio : uint8_t {
    DESLIGADO,
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

    bool alarmeAtivo = false;
    bool arquivoAlarmeSolicitado = false;
    bool arquivoAlarmeDisponivel = false;
    bool somPadraoAlarme = false;
    bool radioAlarme = false;

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

bool tocarRadioAlarme(
    const String& nome,
    const String& url,
    int volume
);

// Reproduz progressivamente um MP3 de /sons usando o mesmo decoder e I2S da
// rádio. Rádio web e arquivo local nunca são executados simultaneamente.
bool tocarArquivoPlayer(const String& caminho);

// Interrompe a fonte atual e abre o MP3 solicitado. Quando o caminho estiver
// vazio, ausente ou não puder ser aberto, usa o WAV padrão armazenado na FFat.
bool tocarArquivoAlarme(
    const String& caminho,
    int volume
);

bool pararAudio();

// Para o stream e suspende a tarefa dedicada até o modo Rádio Web voltar.
bool suspenderAudio();

// Reativa a tarefa dedicada e restaura o volume de operação.
bool retomarAudio(int volume, bool paraPlayer = false);

void alterarVolumeAudio(int volume);

StatusAudio obterStatusAudio();

const char* obterTextoEstadoAudio(
    EstadoAudio estado
);

#endif
