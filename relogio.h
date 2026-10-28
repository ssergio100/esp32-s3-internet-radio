#ifndef RELOGIO_H
#define RELOGIO_H

#include <time.h>

// Inicia o RTC, usa sua hora como ponto de partida quando confiável e inicia
// a sincronização SNTP não bloqueante. O DS3231 armazena sempre horário UTC.
void iniciarRelogio();

// Aplica ao RTC uma sincronização NTP já confirmada. Deve ser chamada
// continuamente pelo loop; o acesso ao I2C nunca ocorre na tarefa de rede.
void processarRelogio();

// Copia a data e hora locais para o destino sem bloquear o loop por longos
// períodos. Retorna false enquanto nenhuma fonte confiável estiver disponível.
bool obterDataHoraLocal(struct tm& dataHora);

// Ativa ou suspende o cliente SNTP. O RTC e o relógio do sistema continuam
// funcionando quando a sincronização automática está desligada.
void configurarSincronizacaoAutomaticaRelogio(bool ativa);
bool sincronizacaoAutomaticaRelogioAtiva();

// Indica se o sistema já recebeu ao menos uma sincronização NTP válida desde
// a inicialização. O estado permanece válido mesmo com o Wi-Fi desligado.
bool ultimaSincronizacaoNtpValida();

// Ajusta horas e minutos preservando a data local atual. A nova referência é
// aplicada ao sistema e ao DS3231, que continua armazenando UTC.
bool ajustarHoraLocalRelogio(uint8_t hora, uint8_t minuto);

#endif
