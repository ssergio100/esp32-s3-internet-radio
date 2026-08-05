#include "api_radios.h"

#include <ArduinoJson.h>
#include <FFat.h>
#include <WebServer.h>

#include "persistencia_radios.h"
#include "radios.h"

void responderListaRadios(WebServer& servidor) {
    const char* caminho = nullptr;

    if (arquivoRadiosValido(CAMINHO_RADIOS_ATIVO)) {
        caminho = CAMINHO_RADIOS_ATIVO;
    } else if (arquivoRadiosValido(CAMINHO_RADIOS_BACKUP)) {
        caminho = CAMINHO_RADIOS_BACKUP;
    }

    if (caminho == nullptr) {
        servidor.send(
            200,
            "application/json",
            "[]"
        );

        return;
    }

    File arquivo = FFat.open(caminho, FILE_READ);

    if (!arquivo) {
        servidor.send(
            200,
            "application/json",
            "[]"
        );

        return;
    }

    servidor.streamFile(
        arquivo,
        "application/json"
    );

    arquivo.close();
}

void adicionarRadioPelaApi(WebServer& servidor) {
    if (!servidor.hasArg("plain")) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"Dados não enviados\"}"
        );

        return;
    }

    DynamicJsonDocument novaRadio(1024);

    DeserializationError erro =
        deserializeJson(
            novaRadio,
            servidor.arg("plain")
        );

    if (erro) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"JSON inválido\"}"
        );

        return;
    }

    const char* nome = novaRadio["nome"];
    const char* url = novaRadio["url"];
    int estrelas = novaRadio["estrelas"] | 0;

    if (
        !dadosRadioValidos(nome, url) ||
        estrelas < 1 ||
        estrelas > 5
    ) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"Dados inválidos\"}"
        );

        return;
    }

    DynamicJsonDocument documento(16384);

    if (!carregarDocumentoRadiosParaEdicao(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao ler as rádios\"}"
        );

        return;
    }

    JsonArray radios = documento.as<JsonArray>();

    if (radios.size() >= QUANTIDADE_MAXIMA_RADIOS) {
        servidor.send(
            409,
            "application/json",
            "{\"erro\":\"Limite de rádios atingido\"}"
        );

        return;
    }

    unsigned long maiorId = 0;

    for (JsonObject radio : radios) {
        unsigned long id = radio["id"] | 0;

        if (id > maiorId) {
            maiorId = id;
        }
    }

    JsonObject radio = radios.createNestedObject();

    radio["id"] = maiorId + 1;
    radio["nome"] = nome;
    radio["url"] = url;
    radio["estrelas"] = estrelas;

    if (!salvarDocumentoRadios(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao salvar a rádio\"}"
        );

        return;
    }

    servidor.send(
        201,
        "application/json",
        "{\"sucesso\":true}"
    );
}

void excluirRadioPelaApi(WebServer& servidor) {
    if (!servidor.hasArg("id")) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"ID não informado\"}"
        );

        return;
    }

    unsigned long idProcurado =
        servidor.arg("id").toInt();

    DynamicJsonDocument documento(16384);

    if (!carregarDocumentoRadiosParaEdicao(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao ler as rádios\"}"
        );

        return;
    }

    JsonArray radios = documento.as<JsonArray>();

    bool encontrou = false;

    for (
        size_t indice = 0;
        indice < radios.size();
        indice++
    ) {
        unsigned long id = radios[indice]["id"] | 0;

        if (id == idProcurado) {
            radios.remove(indice);
            encontrou = true;

            break;
        }
    }

    if (!encontrou) {
        servidor.send(
            404,
            "application/json",
            "{\"erro\":\"Rádio não encontrada\"}"
        );

        return;
    }

    if (radios.size() == 0) {
        servidor.send(
            409,
            "application/json",
            "{\"erro\":\"O rádio precisa manter ao menos uma estação\"}"
        );

        return;
    }

    if (!salvarDocumentoRadios(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao salvar as alterações\"}"
        );

        return;
    }

    servidor.send(
        200,
        "application/json",
        "{\"sucesso\":true}"
    );
}
