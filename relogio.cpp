#include "relogio.h"

#include <Arduino.h>

#include "configuracao.h"

namespace {

    constexpr long SEGUNDOS_POR_HORA = 3600;
    constexpr uint32_t TEMPO_LIMITE_CONSULTA_RELOGIO_MS = 10;

}

void iniciarRelogio() {
    long deslocamentoUtcSegundos =
        FUSO_HORARIO_UTC_HORAS * SEGUNDOS_POR_HORA;

    long ajusteHorarioVeraoSegundos =
        AJUSTE_HORARIO_VERAO_HORAS * SEGUNDOS_POR_HORA;

    configTime(
        deslocamentoUtcSegundos,
        ajusteHorarioVeraoSegundos,
        SERVIDOR_NTP_PRIMARIO,
        SERVIDOR_NTP_SECUNDARIO
    );
}

bool obterDataHoraLocal(struct tm& dataHora) {
    return getLocalTime(
        &dataHora,
        TEMPO_LIMITE_CONSULTA_RELOGIO_MS
    );
}
