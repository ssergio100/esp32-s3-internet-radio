#include "servidor_web.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FFat.h>
#include <WiFi.h>
#include <WebServer.h>

namespace {

WebServer servidor(80);

const char* ARQUIVO_RADIOS = "/radios.json";

bool salvarDocumento(
    DynamicJsonDocument& documento
) {
    const char* arquivoTemporario =
        "/radios.tmp";

    File arquivo =
        FFat.open(
            arquivoTemporario,
            FILE_WRITE
        );

    if (!arquivo) {
        return false;
    }

    bool salvo =
        serializeJson(
            documento,
            arquivo
        ) > 0;

    arquivo.close();

    if (!salvo) {
        FFat.remove(
            arquivoTemporario
        );

        return false;
    }

    FFat.remove(
        ARQUIVO_RADIOS
    );

    return FFat.rename(
        arquivoTemporario,
        ARQUIVO_RADIOS
    );
}

bool carregarDocumento(
    DynamicJsonDocument& documento
) {
    File arquivo =
        FFat.open(
            ARQUIVO_RADIOS,
            FILE_READ
        );

    if (!arquivo) {
        documento.to<JsonArray>();

        return true;
    }

    DeserializationError erro =
        deserializeJson(
            documento,
            arquivo
        );

    arquivo.close();

    return !erro &&
        documento.is<JsonArray>();
}

void listarRadios() {
    File arquivo =
        FFat.open(
            ARQUIVO_RADIOS,
            FILE_READ
        );

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

void adicionarRadio() {
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

    const char* nome =
        novaRadio["nome"];

    const char* url =
        novaRadio["url"];

    int estrelas =
        novaRadio["estrelas"] | 0;

    if (
        nome == nullptr ||
        url == nullptr ||
        strlen(nome) == 0 ||
        strlen(url) == 0 ||
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

    if (!carregarDocumento(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao ler as rádios\"}"
        );

        return;
    }

    JsonArray radios =
        documento.as<JsonArray>();

    unsigned long maiorId = 0;

    for (JsonObject radio : radios) {
        unsigned long id =
            radio["id"] | 0;

        if (id > maiorId) {
            maiorId = id;
        }
    }

    JsonObject radio =
        radios.createNestedObject();

    radio["id"] = maiorId + 1;
    radio["nome"] = nome;
    radio["url"] = url;
    radio["estrelas"] = estrelas;

    if (!salvarDocumento(documento)) {
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

void excluirRadio() {
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

    if (!carregarDocumento(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao ler as rádios\"}"
        );

        return;
    }

    JsonArray radios =
        documento.as<JsonArray>();

    bool encontrou = false;

    for (
        size_t indice = 0;
        indice < radios.size();
        indice++
    ) {
        unsigned long id =
            radios[indice]["id"] | 0;

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

    if (!salvarDocumento(documento)) {
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

void abrirPagina() {
    File arquivo =
        FFat.open(
            "/index.html",
            FILE_READ
        );

    if (!arquivo) {
        servidor.send(
            404,
            "text/plain",
            "Arquivo index.html não encontrado"
        );

        return;
    }

    servidor.streamFile(
        arquivo,
        "text/html"
    );

    arquivo.close();
}

}

bool iniciarServidorWeb() {
    if (!FFat.begin(true)) {
        Serial.println(
            "Falha ao iniciar o FFat."
        );

        return false;
    }

    servidor.on(
        "/",
        HTTP_GET,
        abrirPagina
    );

    servidor.on(
        "/api/radios",
        HTTP_GET,
        listarRadios
    );

    servidor.on(
        "/api/radios",
        HTTP_POST,
        adicionarRadio
    );

    servidor.on(
        "/api/radios",
        HTTP_DELETE,
        excluirRadio
    );

    servidor.onNotFound([]() {
        servidor.send(
            404,
            "application/json",
            "{\"erro\":\"Endereço não encontrado\"}"
        );
    });

    servidor.begin();

    Serial.println(
        "Servidor web iniciado."
    );

    Serial.print(
        "Acesse: http://"
    );

    Serial.println(
        WiFi.localIP()
    );

    return true;
}

void processarServidorWeb() {
    servidor.handleClient();
}