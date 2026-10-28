#include "api_alarmes.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "alarmes.h"
#include "persistencia_alarmes.h"
#include "radios.h"

namespace {

void responderErro(
    WebServer& servidor,
    int codigo,
    const char* mensagem
) {
    JsonDocument documento;
    documento["erro"] = mensagem;

    String resposta;
    serializeJson(documento, resposta);

    servidor.send(
        codigo,
        "application/json",
        resposta
    );
}

bool lerCorpoJson(
    WebServer& servidor,
    JsonDocument& documento
) {
    if (!servidor.hasArg("plain")) {
        responderErro(servidor, 400, "Dados não enviados");
        return false;
    }

    DeserializationError erro =
        deserializeJson(documento, servidor.arg("plain"));

    if (erro || !documento.is<JsonObject>()) {
        responderErro(servidor, 400, "JSON inválido");
        return false;
    }

    return true;
}

bool copiarDadosAlarme(
    JsonObject destino,
    JsonObjectConst origem,
    uint32_t id
) {
    JsonVariantConst arquivo = origem["arquivo"];
    JsonVariantConst radioId = origem["radioId"];
    JsonVariantConst radioFm = origem["radioFm"];
    JsonVariantConst ultimaEstacaoWeb =
        origem["usarUltimaEstacaoWeb"];
    JsonVariantConst somDfPlayer = origem["somDfPlayer"];
    JsonVariantConst duracaoMinutos = origem["duracaoMinutos"];
    bool usaRadioFm =
        radioFm.is<bool>() && radioFm.as<bool>();
    bool usaUltimaEstacaoWeb =
        ultimaEstacaoWeb.is<bool>() &&
        ultimaEstacaoWeb.as<bool>();
    bool possuiDias = origem["dias"].is<JsonArrayConst>();
    bool possuiData = origem["data"].is<const char*>();

    if (
        possuiDias == possuiData ||
        (
            !arquivo.isNull()
        ) ||
        (
            !radioId.isNull() &&
            (
                !radioId.is<uint32_t>() ||
                radioId.as<uint32_t>() == 0
            )
        ) ||
        (
            !radioFm.isNull() &&
            (!radioFm.is<bool>() || !usaRadioFm)
        ) ||
        (
            !ultimaEstacaoWeb.isNull() &&
            (
                !ultimaEstacaoWeb.is<bool>() ||
                !usaUltimaEstacaoWeb
            )
        ) ||
        (
            !somDfPlayer.isNull() &&
            (
                !somDfPlayer.is<int>() ||
                somDfPlayer.as<int>() < 1 ||
                somDfPlayer.as<int>() > 255
            )
        ) ||
        (
            !duracaoMinutos.isNull() &&
            (
                !duracaoMinutos.is<int>() ||
                duracaoMinutos.as<int>() < 1 ||
                duracaoMinutos.as<int>() >
                    DURACAO_MAXIMA_CONFIGURAVEL_ALARME_MINUTOS
            )
        ) ||
        (
            static_cast<uint8_t>(usaRadioFm) +
            static_cast<uint8_t>(radioId.is<uint32_t>()) +
            static_cast<uint8_t>(usaUltimaEstacaoWeb) +
            static_cast<uint8_t>(somDfPlayer.is<int>()) > 1
        ) ||
        (
            !origem["ativo"].isNull() &&
            !origem["ativo"].is<bool>()
        )
    ) {
        return false;
    }

    destino["id"] = id;
    destino["nome"] = origem["nome"];
    destino["ativo"] =
        origem["ativo"].is<bool>()
            ? origem["ativo"].as<bool>()
            : true;
    destino["horario"] = origem["horario"];
    destino["volume"] = origem["volume"];

    if (possuiDias) {
        JsonArray diasDestino = destino["dias"].to<JsonArray>();

        for (JsonVariantConst dia : origem["dias"].as<JsonArrayConst>()) {
            diasDestino.add(dia);
        }
    } else {
        destino["data"] = origem["data"];
    }

    if (radioId.is<uint32_t>()) {
        destino["radioId"] = radioId;
    }

    if (usaRadioFm) {
        destino["radioFm"] = true;
    }

    if (usaUltimaEstacaoWeb) {
        destino["usarUltimaEstacaoWeb"] = true;
    }

    if (somDfPlayer.is<int>()) {
        destino["somDfPlayer"] = somDfPlayer;
    }

    if (duracaoMinutos.is<int>()) {
        destino["duracaoMinutos"] = duracaoMinutos;
    }

    return true;
}

bool obterIdRequisitado(
    WebServer& servidor,
    uint32_t& id
) {
    if (!servidor.hasArg("id")) {
        responderErro(servidor, 400, "ID não informado");
        return false;
    }

    unsigned long valor = servidor.arg("id").toInt();

    if (valor == 0) {
        responderErro(servidor, 400, "ID inválido");
        return false;
    }

    id = static_cast<uint32_t>(valor);
    return true;
}

bool salvarERecarregar(
    WebServer& servidor,
    JsonDocument& documento,
    int codigoSucesso
) {
    if (!documentoAlarmesValido(documento)) {
        responderErro(servidor, 400, "Dados do alarme inválidos");
        return false;
    }

    if (!salvarDocumentoAlarmes(documento)) {
        responderErro(servidor, 500, "Falha ao salvar os alarmes");
        return false;
    }

    carregarAlarmes();
    servidor.send(
        codigoSucesso,
        "application/json",
        "{\"sucesso\":true}"
    );

    return true;
}

}

void responderListaAlarmes(WebServer& servidor) {
    JsonDocument documento;

    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        responderErro(servidor, 500, "Falha ao ler os alarmes");
        return;
    }

    String resposta;
    serializeJson(documento, resposta);

    servidor.send(
        200,
        "application/json",
        resposta
    );
}

void adicionarAlarmePelaApi(WebServer& servidor) {
    JsonDocument requisicao;

    if (!lerCorpoJson(servidor, requisicao)) {
        return;
    }

    JsonDocument documento;

    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        responderErro(servidor, 500, "Falha ao ler os alarmes");
        return;
    }

    JsonArray alarmes = documento["alarmes"].as<JsonArray>();

    if (alarmes.size() >= QUANTIDADE_MAXIMA_ALARMES) {
        responderErro(servidor, 409, "Limite de alarmes atingido");
        return;
    }

    uint32_t maiorId = 0;

    for (JsonObject alarme : alarmes) {
        maiorId = max(maiorId, alarme["id"].as<uint32_t>());
    }

    if (maiorId == UINT32_MAX) {
        responderErro(servidor, 409, "Não há ID disponível");
        return;
    }

    JsonObject novo = alarmes.add<JsonObject>();

    if (
        !copiarDadosAlarme(
            novo,
            requisicao.as<JsonObjectConst>(),
            maiorId + 1
        )
    ) {
        responderErro(servidor, 400, "Dados do alarme inválidos");
        return;
    }

    salvarERecarregar(servidor, documento, 201);
}

void atualizarAlarmePelaApi(WebServer& servidor) {
    uint32_t id;

    if (!obterIdRequisitado(servidor, id)) {
        return;
    }

    JsonDocument requisicao;

    if (!lerCorpoJson(servidor, requisicao)) {
        return;
    }

    JsonDocument documento;

    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        responderErro(servidor, 500, "Falha ao ler os alarmes");
        return;
    }

    JsonArray alarmes = documento["alarmes"].as<JsonArray>();
    int indiceEncontrado = -1;

    for (size_t indice = 0; indice < alarmes.size(); indice++) {
        if ((alarmes[indice]["id"] | 0) == id) {
            indiceEncontrado = static_cast<int>(indice);
            break;
        }
    }

    if (indiceEncontrado < 0) {
        responderErro(servidor, 404, "Alarme não encontrado");
        return;
    }

    JsonVariant destino = alarmes[indiceEncontrado];
    destino.clear();
    JsonObject alarme = destino.to<JsonObject>();

    if (
        !copiarDadosAlarme(
            alarme,
            requisicao.as<JsonObjectConst>(),
            id
        )
    ) {
        responderErro(servidor, 400, "Dados do alarme inválidos");
        return;
    }

    salvarERecarregar(servidor, documento, 200);
}

void excluirAlarmePelaApi(WebServer& servidor) {
    uint32_t id;

    if (!obterIdRequisitado(servidor, id)) {
        return;
    }

    JsonDocument documento;

    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        responderErro(servidor, 500, "Falha ao ler os alarmes");
        return;
    }

    JsonArray alarmes = documento["alarmes"].as<JsonArray>();
    bool encontrou = false;

    for (size_t indice = 0; indice < alarmes.size(); indice++) {
        if ((alarmes[indice]["id"] | 0) == id) {
            alarmes.remove(indice);
            encontrou = true;
            break;
        }
    }

    if (!encontrou) {
        responderErro(servidor, 404, "Alarme não encontrado");
        return;
    }

    salvarERecarregar(servidor, documento, 200);
}


void responderRadiosParaAlarmes(WebServer& servidor) {
    JsonDocument documento;
    JsonArray radios = documento.to<JsonArray>();
    int quantidade = obterQuantidadeRadios();

    for (int indice = 0; indice < quantidade; indice++) {
        const Radio* radio = obterRadio(indice);

        if (radio == nullptr) {
            continue;
        }

        JsonObject item = radios.add<JsonObject>();
        item["id"] = radio->id;
        item["nome"] = radio->nome;
    }

    String resposta;
    serializeJson(documento, resposta);
    servidor.send(200, "application/json", resposta);
}
