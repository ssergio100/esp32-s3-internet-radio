#include "relogio.h"

#include <Arduino.h>
#include <atomic>
#include <esp_sntp.h>
#include <sys/time.h>

#include "configuracao.h"
#include "relogio_rtc.h"

namespace {

constexpr long SEGUNDOS_POR_HORA = 3600;
constexpr uint32_t TEMPO_LIMITE_CONSULTA_RELOGIO_MS = 10;

// Evita aceitar o valor inicial do relógio do sistema como uma hora real.
constexpr time_t PRIMEIRO_INSTANTE_ACEITO_UTC = 1704067200; // 2024-01-01

std::atomic<bool> sincronizacaoNtpPendente{false};
std::atomic<bool> sincronizacaoAutomaticaAtiva{true};
std::atomic<bool> sincronizacaoNtpValida{false};

void registrarSincronizacaoNtp(struct timeval*);

void iniciarClienteNtp() {
    sntp_set_time_sync_notification_cb(
        registrarSincronizacaoNtp
    );
    sntp_set_sync_interval(
        INTERVALO_SINCRONIZACAO_NTP_MS
    );

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

void registrarSincronizacaoNtp(struct timeval*) {
    // O callback pertence à tarefa de rede. O acesso ao I2C fica para o loop.
    if (sincronizacaoAutomaticaAtiva.load(std::memory_order_acquire)) {
        sincronizacaoNtpPendente.store(
            true,
            std::memory_order_release
        );
    }
}

bool horarioSistemaConfiavel(time_t& instanteUtc) {
    time(&instanteUtc);
    return instanteUtc >= PRIMEIRO_INSTANTE_ACEITO_UTC;
}

void carregarHorarioSistemaPeloRtc() {
    if (!horarioRelogioRtcConfiavel()) {
        return;
    }

    time_t instanteUtc;

    if (!obterHorarioUtcRelogioRtc(instanteUtc)) {
        Serial.println("Não foi possível ler o horário do RTC.");
        return;
    }

    struct timeval horarioSistema = {};
    horarioSistema.tv_sec = instanteUtc;

    if (settimeofday(&horarioSistema, nullptr) != 0) {
        Serial.println("Não foi possível aplicar o RTC ao sistema.");
        return;
    }

    Serial.println("Relógio do sistema iniciado pelo DS3231 (UTC).");
}

bool rtcPrecisaSerAjustado(time_t instanteNtpUtc) {
    if (!horarioRelogioRtcConfiavel()) {
        return true;
    }

    time_t instanteRtcUtc;

    if (!obterHorarioUtcRelogioRtc(instanteRtcUtc)) {
        return true;
    }

    double desvioSegundos = difftime(
        instanteNtpUtc,
        instanteRtcUtc
    );

    if (desvioSegundos < 0.0) {
        desvioSegundos = -desvioSegundos;
    }

    return
        desvioSegundos >=
        static_cast<double>(DESVIO_MINIMO_AJUSTE_RTC_SEGUNDOS);
}

void atualizarRtcDepoisDaSincronizacaoNtp(time_t instanteNtpUtc) {
    if (!relogioRtcDisponivel()) {
        Serial.println("NTP sincronizado; RTC indisponível.");
        return;
    }

    if (!rtcPrecisaSerAjustado(instanteNtpUtc)) {
        Serial.println("NTP sincronizado; RTC já está dentro do limite.");
        return;
    }

    if (ajustarHorarioUtcRelogioRtc(instanteNtpUtc)) {
        Serial.println("RTC DS3231 ajustado pela referência NTP (UTC).");
    } else {
        Serial.println("Falha ao ajustar o RTC pela referência NTP.");
    }
}

}

void iniciarRelogio() {
    iniciarRelogioRtc();
    carregarHorarioSistemaPeloRtc();
    iniciarClienteNtp();
}

void processarRelogio() {
    if (!sincronizacaoAutomaticaAtiva.load(std::memory_order_acquire)) {
        sincronizacaoNtpPendente.store(false, std::memory_order_release);
        return;
    }
    if (!sincronizacaoNtpPendente.exchange(
        false,
        std::memory_order_acq_rel
    )) {
        return;
    }

    time_t instanteNtpUtc;

    if (!horarioSistemaConfiavel(instanteNtpUtc)) {
        Serial.println("Sincronização NTP devolveu horário inválido.");
        return;
    }

    sincronizacaoNtpValida.store(true, std::memory_order_release);
    atualizarRtcDepoisDaSincronizacaoNtp(instanteNtpUtc);
}

bool obterDataHoraLocal(struct tm& dataHora) {
    return getLocalTime(
        &dataHora,
        TEMPO_LIMITE_CONSULTA_RELOGIO_MS
    );
}

void configurarSincronizacaoAutomaticaRelogio(bool ativa) {
    bool anterior = sincronizacaoAutomaticaAtiva.exchange(
        ativa,
        std::memory_order_acq_rel
    );
    if (anterior == ativa) {
        return;
    }

    sincronizacaoNtpPendente.store(false, std::memory_order_release);
    if (ativa) {
        iniciarClienteNtp();
        Serial.println("Sincronização automática do relógio ativada.");
    } else {
        esp_sntp_stop();
        Serial.println("Sincronização automática do relógio desativada.");
    }
}

bool sincronizacaoAutomaticaRelogioAtiva() {
    return sincronizacaoAutomaticaAtiva.load(std::memory_order_acquire);
}

bool ultimaSincronizacaoNtpValida() {
    return sincronizacaoNtpValida.load(std::memory_order_acquire);
}

bool ajustarHoraLocalRelogio(uint8_t hora, uint8_t minuto) {
    if (hora > 23 || minuto > 59) {
        return false;
    }

    struct tm dataHoraLocal = {};
    if (!obterDataHoraLocal(dataHoraLocal)) {
        return false;
    }

    dataHoraLocal.tm_hour = hora;
    dataHoraLocal.tm_min = minuto;
    dataHoraLocal.tm_sec = 0;
    dataHoraLocal.tm_isdst = -1;

    // configTime() já instalou o fuso no ambiente TZ; mktime converte a
    // data/hora local editada para o instante UTC correspondente.
    time_t instanteUtc = mktime(&dataHoraLocal);
    if (instanteUtc < PRIMEIRO_INSTANTE_ACEITO_UTC) {
        return false;
    }
    struct timeval horarioSistema = {};
    horarioSistema.tv_sec = instanteUtc;
    if (settimeofday(&horarioSistema, nullptr) != 0) {
        return false;
    }

    if (relogioRtcDisponivel() && !ajustarHorarioUtcRelogioRtc(instanteUtc)) {
        Serial.println("Hora manual aplicada ao sistema, mas não ao RTC.");
    }
    sincronizacaoNtpPendente.store(false, std::memory_order_release);
    return true;
}
