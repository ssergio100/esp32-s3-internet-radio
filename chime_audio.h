#ifndef CHIME_AUDIO_H
#define CHIME_AUDIO_H

#include <Arduino.h>

enum class EstadoChime : uint8_t {
    INATIVO,
    TOCANDO,
    FINALIZANDO
};

// Carrega o arquivo WAV do cartão microSD (ou caminho especificado) para a PSRAM.
// Retorna true se o arquivo for carregado com sucesso em formato PCM.
bool iniciarChimeAudio(const char* caminhoArquivo = nullptr);

// Informa se o chime está carregado na PSRAM e disponível para reprodução.
bool chimeDisponivel();

// Dispara a reprodução do chime pelo mixer em tempo real.
// Se a rádio web estiver tocando, a rádio sofre atenuação suave (ducking)
// e o chime é sobreposto sem interromper o streaming.
bool dispararChime();

// Informa se o chime está atualmente em reprodução ou em transição de ducking.
bool chimeEmReproducao();

// Interrompe imediatamente o chime e restaura o volume normal da rádio.
void pararChime();

// Libera o buffer da PSRAM.
void descarregarChimeAudio();

// Processa a mixagem de amostras PCM (chamada no hook audio_process_i2s).
// Realiza ducking da rádio, interpolação de taxa de amostragem e soma protegida.
void processarMixagemChime(
    int32_t* outBuff,
    int16_t validSamples,
    uint32_t taxaAmostragemStream
);

// Funções de diagnóstico e telemetria
EstadoChime obterEstadoChime();
const char* obterTextoEstadoChime();
float obterFatorDuckingAtual();
uint32_t obterTaxaAmostragemChime();
uint16_t obterCanaisChime();
float obterDuracaoChimeSegundos();
size_t obterMemoriaChimeBytes();

#endif
