#ifndef CHAVEAMENTO_AUDIO_H
#define CHAVEAMENTO_AUDIO_H

#include <Arduino.h>

struct EstadoSaidaAudio {
    bool valido = false;
    uint8_t valorPortaA = 0;
};

// Inicializa GPA7/GPA6 em HIGH (mute), GPA5 em LOW (lado FM) e GPA4 em LOW
// (TEF6686 desligado). O Wire deve ter sido iniciado antes desta chamada.
bool iniciarChaveamentoAudio();

// Liga/desliga o TEF6686 exclusivamente por GPA4, ativo em HIGH, preservando
// o relé e os enables dos NS8002. Retorna true após confirmar o nível no MCP.
bool configurarHabilitacaoRadioFm(bool habilitar);

// Standby: amplificadores NS8002 desabilitados.
void configurarSaidaAudioStandby();

// Rádio Web ou alarme digital: seleciona o caminho DAC pelo relé;
// mutar=true eleva GPA7/GPA6; GPA5 permanece HIGH no lado DAC e GPA4 LOW.
// Retorna true somente após reler IODIRA, OLATA e GPIOA com o estado pedido.
bool configurarSaidaAudioDac(bool mutar = false);

// DFPlayer: altera somente GPA7/GPA6, enables ativos em LOW dos NS8002.
// GPA5/GPA4 são preservados porque DAC_L/DAC_R chegam direto aos amplificadores.
// Retorna true somente depois de confirmar GPA7/GPA6 no MCP23017.
bool configurarSaidaAudioDfPlayer(bool mutar = false);

// Fotografa e restaura exatamente GPA7/GPA6/GPA5/GPA4. O DFPlayer usa isso
// para ligar temporariamente os NS8002 sem alterar a fonte e o estado do FM.
EstadoSaidaAudio obterEstadoSaidaAudio();
void restaurarEstadoSaidaAudio(const EstadoSaidaAudio& estado);

// Rádio FM: preserva GPA4 em HIGH, coloca GPA5 em LOW e, salvo mute,
// confirma GPA7/GPA6 em LOW para habilitar os dois NS8002.
bool configurarSaidaAudioFm(bool mutar = false);

#endif
