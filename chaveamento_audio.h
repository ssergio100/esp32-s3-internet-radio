#ifndef CHAVEAMENTO_AUDIO_H
#define CHAVEAMENTO_AUDIO_H

#include <Arduino.h>

struct EstadoSaidaAudio {
    bool valido = false;
    uint8_t valorPortaA = 0;
};

// Inicializa GPA7 em LOW e GPA6/GPA5 em HIGH (mute). O barramento Wire deve
// ter sido iniciado antes desta chamada. Falhas são apenas diagnosticadas:
// estes sinais não comandam a máquina de estados do equipamento.
bool iniciarChaveamentoAudio();

// Standby: amplificadores NS8002 desabilitados.
void configurarSaidaAudioStandby();

// Rádio Web ou alarme digital: seleciona o caminho DAC pelo relé;
// mutar=true eleva GPA6/GPA5.
void configurarSaidaAudioDac(bool mutar = false);

// DFPlayer: altera somente os enables ativos em LOW dos NS8002. GPA7 é
// preservado porque DAC_L/DAC_R do DFPlayer chegam diretamente aos 8002.
// Retorna true somente depois de confirmar GPA6/GPA5 no MCP23017.
bool configurarSaidaAudioDfPlayer(bool mutar = false);

// Fotografa e restaura exatamente GPA7/GPA6/GPA5. O DFPlayer usa isso para
// ligar temporariamente os NS8002 sem alterar permanentemente a fonte anterior.
EstadoSaidaAudio obterEstadoSaidaAudio();
void restaurarEstadoSaidaAudio(const EstadoSaidaAudio& estado);

// Rádio FM: seleciona o TEF6686; mutar=true eleva GPA6/GPA5.
void configurarSaidaAudioFm(bool mutar = false);

#endif
