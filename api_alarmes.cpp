#include "api_alarmes.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "alarmes.h"
#include "persistencia_alarmes.h"
#include "player.h"

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
    bool possuiDias = origem["dias"].is<JsonArrayConst>();
    bool possuiData = origem["data"].is<const char*>();

    if (
        possuiDias == possuiData ||
        (
            !arquivo.isNull() &&
            !arquivo.is<const char*>()
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

    if (
        arquivo.is<const char*>() &&
        arquivo.as<const char*>()[0] != '\0'
    ) {
        destino["arquivo"] = arquivo;
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

void responderArquivosPlayer(WebServer& servidor) {
    JsonDocument documento;
    JsonArray arquivos = documento.to<JsonArray>();
    int quantidade = obterQuantidadeArquivosPlayer();

    for (int indice = 0; indice < quantidade; indice++) {
        const ArquivoPlayer* arquivo = obterArquivoPlayer(indice);

        if (arquivo == nullptr) {
            continue;
        }

        JsonObject item = arquivos.add<JsonObject>();
        item["nome"] = arquivo->nome;
        item["caminho"] = arquivo->caminho;
    }

    String resposta;
    serializeJson(documento, resposta);

    servidor.send(
        200,
        "application/json",
        resposta
    );
}
