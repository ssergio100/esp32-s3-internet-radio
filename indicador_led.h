#ifndef INDICADOR_LED_H
#define INDICADOR_LED_H

/*
 * Este módulo é o único responsável por escrever no LED RGB.
 * Os demais módulos informam apenas o contexto que deve ser indicado.
 */

// Pisca em azul durante a configuração e conexão inicial do Wi-Fi.
// Deve ser chamada continuamente enquanto a conexão estiver em andamento.
void atualizarIndicadorConexaoWifi();

void apagarIndicadorLed();

// Apresenta a cor correspondente ao estado publicado pelo serviço de áudio.
// Deve ser chamada continuamente no loop().
void atualizarIndicadorEstadoAudio();

#endif
