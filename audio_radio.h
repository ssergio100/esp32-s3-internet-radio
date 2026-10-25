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
    bool radioFmAlarme = false;

    char radio[64] = "";
    char titulo[128] = "";
    char codec[16] = "";
    char ultimoErro[192] = "";
};

// Cria o serviço proprietário. Quando iniciarDesligado é true, decoder e I2S
// permanecem ausentes até a primeira chamada de retomarAudio().
bool iniciarAudio(int volume, bool iniciarDesligado = false);

bool tocarRadio(
    const String& nome,
    const String& url
);

bool tocarRadioAlarme(
    const String& nome,
    const String& url,
    int volume
);

// Interrompe a fonte atual e reproduz o WAV padrão armazenado na FFat.
bool tocarArquivoAlarme(
    const String& caminho,
    int volume
);

bool pararAudio();

// Bloqueia até destruir o decoder, remover o canal I2S e manter BCK/LRCK/DIN
// em nível baixo. Retorna false se a confirmação não chegar no prazo.
bool suspenderAudio();

// Recria decoder e I2S, reativa a tarefa dedicada e restaura o volume.
bool retomarAudio(int volume, bool paraPlayer = false);

void alterarVolumeAudio(int volume);

bool decoderDacEstaAtivo();

// Publica na telemetria um alarme reproduzido diretamente pelo TEF6686.
void definirAlarmeFmAtivo(bool ativo);

StatusAudio obterStatusAudio();

const char* obterTextoEstadoAudio(
    EstadoAudio estado
);

#endif
