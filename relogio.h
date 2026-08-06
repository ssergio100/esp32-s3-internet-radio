#ifndef RELOGIO_H
#define RELOGIO_H

#include <time.h>

// Inicia a sincronização do relógio pela rede usando a configuração NTP.
void iniciarRelogio();

// Copia a data e hora locais para o destino sem bloquear o loop por longos
// períodos. Retorna false enquanto o relógio ainda não estiver sincronizado.
bool obterDataHoraLocal(struct tm& dataHora);

#endif
