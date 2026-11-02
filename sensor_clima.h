#ifndef SENSOR_CLIMA_H
#define SENSOR_CLIMA_H

#include <Arduino.h>

struct LeituraSensorClima {
    bool disponivel = false;
    bool valida = false;
    int16_t temperaturaDecimosC = 0;
    uint16_t umidadeDecimosRh = 0;
    uint32_t revisao = 0;
};

// O Wire deve estar inicializado antes desta chamada.
void iniciarSensorClima();

// Serviço cooperativo: dispara a conversão e coleta o resultado em passagens
// distintas pelo loop, sem delay() e sem executar I2C na tarefa do display.
void processarSensorClima();

// Cópia atômica do último estado para consumidores em outras tarefas.
LeituraSensorClima obterLeituraSensorClima();

#endif
