#ifndef RELOGIO_RTC_H
#define RELOGIO_RTC_H

#include <time.h>

// Inicializa o DS3231 no barramento I2C já iniciado pelo display.
// A ausência do componente não impede o restante do rádio de funcionar.
bool iniciarRelogioRtc();

bool relogioRtcDisponivel();

// Retorna false quando o RTC informou perda de alimentação ou contém uma
// data inválida. O DS3231 é mantido em UTC; o fuso é aplicado pelo sistema.
bool horarioRelogioRtcConfiavel();

bool obterHorarioUtcRelogioRtc(time_t& instanteUtc);
bool ajustarHorarioUtcRelogioRtc(time_t instanteUtc);

bool obterTemperaturaRelogioRtc(float& temperaturaCelsius);

#endif
