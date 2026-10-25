#include "chime.h"

#include <ArduinoJson.h>

#include "configuracao.h"
#include "persistencia_alarmes.h"
#include "relogio.h"

namespace {

ConfiguracaoChime configuracaoAtual;
uint64_t ultimaJanelaDisparada = UINT64_MAX;
uint64_t ultimoMinutoDiagnosticado = UINT64_MAX;
unsigned long ultimoErroRelogioDiagnosticadoMs = 0;

ConfiguracaoChime configuracaoPadrao() {
    ConfiguracaoChime configuracao;
    configuracao.ativo = false;
    configuracao.som = 1;
    configuracao.volume = VOLUME_PADRAO;
    configuracao.intervaloMinutos = 60;
    return configuracao;
}

bool configuracaoValida(const ConfiguracaoChime& configuracao) {
    return
        configuracao.som >= 1 &&
        configuracao.volume >= 1 &&
        configuracao.volume <= VOLUME_MAXIMO &&
        configuracao.intervaloMinutos >= 1 &&
        configuracao.intervaloMinutos <= 60;
}

}

void carregarConfiguracaoChime() {
    configuracaoAtual = configuracaoPadrao();

    JsonDocument documento;
    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        Serial.println(
            "CHIME DEBUG config: falha ao carregar /alarmes.json."
        );
        return;
    }

    JsonVariantConst valor = documento["chime"];
    if (!valor.is<JsonObjectConst>()) {
        Serial.println(
            "CHIME DEBUG config: objeto ausente; usando padrao desativado."
        );
        return;
    }

    JsonObjectConst chime = valor.as<JsonObjectConst>();
    configuracaoAtual.ativo = chime["ativo"] | false;
    configuracaoAtual.som = static_cast<uint8_t>(chime["som"] | 1);
    configuracaoAtual.volume =
        static_cast<uint8_t>(chime["volume"] | VOLUME_PADRAO);
    configuracaoAtual.intervaloMinutos =
        static_cast<uint8_t>(chime["intervaloMinutos"] | 60);
    ultimaJanelaDisparada = UINT64_MAX;

    Serial.printf(
        "CHIME DEBUG config carregada: ativo=%u som=%03u volume=%u/%u "
        "intervalo=%u min.\n",
        configuracaoAtual.ativo ? 1U : 0U,
        static_cast<unsigned>(configuracaoAtual.som),
        static_cast<unsigned>(configuracaoAtual.volume),
        static_cast<unsigned>(VOLUME_MAXIMO),
        static_cast<unsigned>(configuracaoAtual.intervaloMinutos)
    );
}

ConfiguracaoChime obterConfiguracaoChime() {
    return configuracaoAtual;
}

bool salvarConfiguracaoChime(const ConfiguracaoChime& configuracao) {
    if (!configuracaoValida(configuracao)) {
        return false;
    }

    JsonDocument documento;
    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        return false;
    }

    JsonObject chime = documento["chime"].to<JsonObject>();
    chime["ativo"] = configuracao.ativo;
    chime["som"] = configuracao.som;
    chime["volume"] = configuracao.volume;
    chime["intervaloMinutos"] = configuracao.intervaloMinutos;

    if (!salvarDocumentoAlarmes(documento)) {
        return false;
    }

    configuracaoAtual = configuracao;
    ultimaJanelaDisparada = UINT64_MAX;
    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG config salva: ativo=%u som=%03u volume=%u/%u "
            "intervalo=%u min; janela reiniciada.\n",
            configuracaoAtual.ativo ? 1U : 0U,
            static_cast<unsigned>(configuracaoAtual.som),
            static_cast<unsigned>(configuracaoAtual.volume),
            static_cast<unsigned>(VOLUME_MAXIMO),
            static_cast<unsigned>(configuracaoAtual.intervaloMinutos)
        );
    }
    return true;
}

bool verificarDisparoChime(ConfiguracaoChime& configuracao) {
    if (!configuracaoAtual.ativo) {
        return false;
    }

    struct tm dataHora = {};
    if (!obterDataHoraLocal(dataHora)) {
        const unsigned long agoraMs = millis();
        if (
            DEBUG_CHIME &&
            agoraMs - ultimoErroRelogioDiagnosticadoMs >= 5000
        ) {
            ultimoErroRelogioDiagnosticadoMs = agoraMs;
            Serial.println(
                "CHIME DEBUG agendador: hora local indisponivel."
            );
        }
        return false;
    }

    if (dataHora.tm_sec == 0) {
        return false;
    }

    // O primeiro segundo fica reservado ao agendador de alarmes, que roda
    // antes deste módulo e tem prioridade quando os dois coincidem.

    const uint16_t minutoDoDia =
        static_cast<uint16_t>(dataHora.tm_hour * 60 + dataHora.tm_min);

    const uint64_t chaveMinuto =
        static_cast<uint64_t>(dataHora.tm_year + 1900) * 1000000ULL +
        static_cast<uint64_t>(dataHora.tm_yday) * 1440ULL +
        minutoDoDia;

    const bool janelaElegivel =
        minutoDoDia % configuracaoAtual.intervaloMinutos == 0;

    if (DEBUG_CHIME && chaveMinuto != ultimoMinutoDiagnosticado) {
        ultimoMinutoDiagnosticado = chaveMinuto;
        Serial.printf(
            "CHIME DEBUG agendador: %04d-%02d-%02d %02d:%02d:%02d "
            "ativo=1 intervalo=%u minutoDia=%u elegivel=%u.\n",
            dataHora.tm_year + 1900,
            dataHora.tm_mon + 1,
            dataHora.tm_mday,
            dataHora.tm_hour,
            dataHora.tm_min,
            dataHora.tm_sec,
            static_cast<unsigned>(configuracaoAtual.intervaloMinutos),
            static_cast<unsigned>(minutoDoDia),
            janelaElegivel ? 1U : 0U
        );
    }

    if (!janelaElegivel) {
        return false;
    }

    const uint64_t chaveJanela = chaveMinuto;

    if (chaveJanela == ultimaJanelaDisparada) {
        return false;
    }

    ultimaJanelaDisparada = chaveJanela;
    configuracao = configuracaoAtual;
    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG agendador: DISPARO liberado em %02d:%02d:%02d.\n",
            dataHora.tm_hour,
            dataHora.tm_min,
            dataHora.tm_sec
        );
    }
    return true;
}
