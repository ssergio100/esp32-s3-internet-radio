#ifndef RELOGIO_H
#define RELOGIO_H

#include <time.h>

enum class FonteHorario {
    INDISPONIVEL,
    RTC,
    NTP
};

// Inicia o RTC, usa sua hora como ponto de partida quando confiável e inicia
// a sincronização SNTP não bloqueante. O DS3231 armazena sempre horário UTC.
void iniciarRelogio();

// Aplica ao RTC uma sincronização NTP já confirmada. Deve ser chamada
// continuamente pelo loop; o acesso ao I2C nunca ocorre na tarefa de rede.
void processarRelogio();

// Copia a data e hora locais para o destino sem bloquear o loop por longos
// períodos. Retorna false enquanto nenhuma fonte confiável estiver disponível.
bool obterDataHoraLocal(struct tm& dataHora);

FonteHorario obterFonteHorario();

#endif
