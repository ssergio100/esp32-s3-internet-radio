#include "relogio_rtc.h"

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

namespace {

constexpr time_t PRIMEIRO_INSTANTE_SUPORTADO_UTC = 946684800; // 2000-01-01
constexpr time_t PRIMEIRO_INSTANTE_CONFIAVEL_UTC = 1704067200; // 2024-01-01
constexpr time_t ULTIMO_INSTANTE_SUPORTADO_UTC = 4102444799;   // 2099-12-31
constexpr uint8_t ENDERECO_I2C_DS3231 = 0x68;

RTC_DS3231 rtc;
bool disponivel = false;

bool obterDataHoraRtc(DateTime& dataHora) {
    if (!disponivel) {
        return false;
    }

    dataHora = rtc.now();
    return dataHora.isValid();
}

}

bool iniciarRelogioRtc() {
    disponivel = rtc.begin(&Wire);

    if (!disponivel) {
        Serial.println("RTC DS3231 não encontrado no I2C.");
        return false;
    }

    // A saída de 32,768 kHz não é usada pelo rádio. Desabilitá-la evita
    // manter uma saída ativa sem consumidor, inclusive durante a bateria.
    rtc.disable32K();

    if (rtc.lostPower()) {
        Serial.println(
            "RTC DS3231 perdeu a referência; aguardando NTP."
        );
    } else {
        Serial.println("RTC DS3231 disponível.");
    }

    return true;
}

bool relogioRtcDisponivel() {
    return disponivel;
}

bool relogioRtcRespondendo() {
    Wire.beginTransmission(ENDERECO_I2C_DS3231);
    return Wire.endTransmission() == 0;
}

bool horarioRelogioRtcConfiavel() {
    if (!disponivel || rtc.lostPower()) {
        return false;
    }

    time_t instanteUtc;

    return
        obterHorarioUtcRelogioRtc(instanteUtc) &&
        instanteUtc >= PRIMEIRO_INSTANTE_CONFIAVEL_UTC;
}

bool obterHorarioUtcRelogioRtc(time_t& instanteUtc) {
    DateTime dataHora;

    if (!obterDataHoraRtc(dataHora)) {
        return false;
    }

    instanteUtc = static_cast<time_t>(dataHora.unixtime());
    return true;
}

bool ajustarHorarioUtcRelogioRtc(time_t instanteUtc) {
    if (
        !disponivel ||
        instanteUtc < PRIMEIRO_INSTANTE_SUPORTADO_UTC ||
        instanteUtc > ULTIMO_INSTANTE_SUPORTADO_UTC
    ) {
        return false;
    }

    DateTime dataHora(static_cast<uint32_t>(instanteUtc));

    if (!dataHora.isValid()) {
        return false;
    }

    rtc.adjust(dataHora);

    // adjust() não informa erro de I2C. A releitura confirma a gravação e
    // tolera a virada normal de um segundo entre as duas operações.
    time_t instanteConfirmado;

    if (
        !obterHorarioUtcRelogioRtc(instanteConfirmado) ||
        rtc.lostPower()
    ) {
        return false;
    }

    double diferencaSegundos = difftime(
        instanteConfirmado,
        instanteUtc
    );

    return
        diferencaSegundos >= 0.0 &&
        diferencaSegundos <= 1.0;
}
