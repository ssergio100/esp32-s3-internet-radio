#include "api_chime.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "chime.h"
#include "configuracao.h"
#include "dfplayer.h"

namespace {

void responderErroChime(
    WebServer& servidor,
    int codigo,
    const char* mensagem
) {
    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG API erro HTTP %d: %s.\n",
            codigo,
            mensagem
        );
    }

    JsonDocument documento;
    documento["erro"] = mensagem;
    String resposta;
    serializeJson(documento, resposta);
    servidor.send(codigo, "application/json", resposta);
}

void enviarConfiguracao(WebServer& servidor) {
    ConfiguracaoChime configuracao = obterConfiguracaoChime();
    EstadoDfPlayer estado = obterEstadoDfPlayer();
    JsonDocument documento;
    documento["ativo"] = configuracao.ativo;
    documento["som"] = configuracao.som;
    documento["volume"] = configuracao.volume;
    documento["intervaloMinutos"] = configuracao.intervaloMinutos;
    documento["executando"] =
        estado.uso == UsoDfPlayer::CHIME &&
        (
            estado.fase == FaseDfPlayer::INICIANDO ||
            estado.fase == FaseDfPlayer::TOCANDO
        );

    String resposta;
    serializeJson(documento, resposta);
    servidor.send(200, "application/json", resposta);
}

}

void responderConfiguracaoChime(WebServer& servidor) {
    enviarConfiguracao(servidor);
}

void atualizarConfiguracaoChime(WebServer& servidor) {
    if (!servidor.hasArg("plain")) {
        responderErroChime(servidor, 400, "Dados não enviados");
        return;
    }

    JsonDocument documento;
    DeserializationError erro =
        deserializeJson(documento, servidor.arg("plain"));
    if (erro || !documento.is<JsonObject>()) {
        responderErroChime(servidor, 400, "JSON inválido");
        return;
    }

    JsonObjectConst origem = documento.as<JsonObjectConst>();
    JsonVariantConst ativo = origem["ativo"];
    JsonVariantConst som = origem["som"];
    JsonVariantConst volume = origem["volume"];
    JsonVariantConst intervalo = origem["intervaloMinutos"];
    int numeroSom = som | -1;
    int nivelVolume = volume | -1;
    int intervaloMinutos = intervalo.isNull()
        ? obterConfiguracaoChime().intervaloMinutos
        : intervalo.as<int>();

    if (
        !ativo.is<bool>() ||
        !som.is<int>() || numeroSom < 1 || numeroSom > 255 ||
        !volume.is<int>() ||
        nivelVolume < 1 || nivelVolume > VOLUME_MAXIMO ||
        (
            !intervalo.isNull() &&
            !intervalo.is<int>()
        ) ||
        intervaloMinutos < 1 || intervaloMinutos > 60
    ) {
        responderErroChime(servidor, 400, "Configuração de chime inválida");
        return;
    }

    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG API PUT: ativo=%u som=%03d volume=%d/%u "
            "intervalo=%d min.\n",
            ativo.as<bool>() ? 1U : 0U,
            numeroSom,
            nivelVolume,
            static_cast<unsigned>(VOLUME_MAXIMO),
            intervaloMinutos
        );
    }

    CatalogoSonsDfPlayer catalogo = obterCatalogoSonsDfPlayer();
    if (
        ativo.as<bool>() &&
        (!catalogo.disponivel || numeroSom > catalogo.quantidade)
    ) {
        responderErroChime(
            servidor,
            409,
            "O som selecionado não está disponível no DFPlayer"
        );
        return;
    }

    ConfiguracaoChime configuracao;
    configuracao.ativo = ativo.as<bool>();
    configuracao.som = static_cast<uint8_t>(numeroSom);
    configuracao.volume = static_cast<uint8_t>(nivelVolume);
    configuracao.intervaloMinutos =
        static_cast<uint8_t>(intervaloMinutos);

    if (!salvarConfiguracaoChime(configuracao)) {
        responderErroChime(servidor, 500, "Falha ao salvar o chime");
        return;
    }

    enviarConfiguracao(servidor);
}

void reproduzirPreviaChime(WebServer& servidor) {
    if (!servidor.hasArg("plain")) {
        responderErroChime(servidor, 400, "Dados não enviados");
        return;
    }

    JsonDocument documento;
    DeserializationError erro =
        deserializeJson(documento, servidor.arg("plain"));
    JsonVariantConst som = documento["som"];
    JsonVariantConst volume = documento["volume"];
    int numeroSom = som | -1;
    int nivelVolume = volume | -1;

    if (
        erro || !documento.is<JsonObject>() ||
        !som.is<int>() || numeroSom < 1 || numeroSom > 255 ||
        !volume.is<int>() ||
        nivelVolume < 1 || nivelVolume > VOLUME_MAXIMO
    ) {
        responderErroChime(servidor, 400, "Dados da prévia inválidos");
        return;
    }

    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG API PREVIEW: som=%03d volume=%d/%u.\n",
            numeroSom,
            nivelVolume,
            static_cast<unsigned>(VOLUME_MAXIMO)
        );
    }

    CatalogoSonsDfPlayer catalogo = obterCatalogoSonsDfPlayer();
    if (!catalogo.disponivel || numeroSom > catalogo.quantidade) {
        responderErroChime(
            servidor,
            409,
            "O som selecionado não está disponível no DFPlayer"
        );
        return;
    }

    EstadoDfPlayer estado = obterEstadoDfPlayer();
    const bool reproduzindo =
        estado.fase == FaseDfPlayer::INICIANDO ||
        estado.fase == FaseDfPlayer::TOCANDO;

    if (reproduzindo && estado.uso != UsoDfPlayer::PREVIA) {
        responderErroChime(
            servidor,
            409,
            "O DFPlayer está ocupado com outra reprodução"
        );
        return;
    }

    if (reproduzindo) {
        pararDfPlayer();
    }

    if (!reproduzirSomDfPlayer(
        static_cast<uint8_t>(numeroSom),
        static_cast<uint8_t>(nivelVolume),
        UsoDfPlayer::PREVIA
    )) {
        responderErroChime(servidor, 503, "Falha ao iniciar a prévia");
        return;
    }

    servidor.send(
        200,
        "application/json",
        "{\"sucesso\":true}"
    );
    if (DEBUG_CHIME) {
        Serial.println("CHIME DEBUG API PREVIEW: comando aceito.");
    }
}
