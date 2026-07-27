#include "radios.h"

#include <ArduinoJson.h>
#include <FFat.h>

namespace {

constexpr int MAXIMO_RADIOS = 50;

Radio radios[MAXIMO_RADIOS];

String nomes[MAXIMO_RADIOS];
String urls[MAXIMO_RADIOS];

int quantidadeRadios = 0;

const Radio radiosReserva[] = {
    {
        "Antena 1",
        "https://antenaone.crossradio.com.br/stream/1"
    },
    {
        "Alpha FM",
        "https://e-spo-104.fabricahost.com.br/alphafmsp"
    },
    {
        "Schilling",
        "https://radio.loficafe.net/listen/chilling/radio.mp3"
    }
};

constexpr int QUANTIDADE_RESERVA =
    sizeof(radiosReserva) /
    sizeof(radiosReserva[0]);

void limparRadios() {
    quantidadeRadios = 0;

    for (int indice = 0; indice < MAXIMO_RADIOS; indice++) {
        nomes[indice] = "";
        urls[indice] = "";

        radios[indice].nome = nullptr;
        radios[indice].url = nullptr;
    }
}

void carregarRadiosReserva() {
    limparRadios();

    for (
        int indice = 0;
        indice < QUANTIDADE_RESERVA;
        indice++
    ) {
        nomes[indice] =
            radiosReserva[indice].nome;

        urls[indice] =
            radiosReserva[indice].url;

        radios[indice].nome =
            nomes[indice].c_str();

        radios[indice].url =
            urls[indice].c_str();
    }

    quantidadeRadios =
        QUANTIDADE_RESERVA;

    Serial.println(
        "Usando lista de radios reserva."
    );
}

}

bool carregarRadios() {
    limparRadios();

    File arquivo =
        FFat.open(
            "/radios.json",
            FILE_READ
        );

    if (!arquivo) {
        Serial.println(
            "Arquivo /radios.json nao encontrado."
        );

        carregarRadiosReserva();

        return false;
    }

    DynamicJsonDocument documento(16384);

    DeserializationError erro =
        deserializeJson(
            documento,
            arquivo
        );

    arquivo.close();

    if (erro) {
        Serial.print(
            "Erro ao ler radios.json: "
        );

        Serial.println(
            erro.c_str()
        );

        carregarRadiosReserva();

        return false;
    }

    if (!documento.is<JsonArray>()) {
        Serial.println(
            "radios.json nao contem uma lista."
        );

        carregarRadiosReserva();

        return false;
    }

    JsonArray lista =
        documento.as<JsonArray>();

    for (JsonObject item : lista) {
        if (
            quantidadeRadios >=
            MAXIMO_RADIOS
        ) {
            break;
        }

        const char* nome =
            item["nome"];

        const char* url =
            item["url"];

        if (
            nome == nullptr ||
            url == nullptr ||
            strlen(nome) == 0 ||
            strlen(url) == 0
        ) {
            continue;
        }

        nomes[quantidadeRadios] =
            nome;

        urls[quantidadeRadios] =
            url;

        radios[quantidadeRadios].nome =
            nomes[quantidadeRadios].c_str();

        radios[quantidadeRadios].url =
            urls[quantidadeRadios].c_str();

        quantidadeRadios++;
    }

    if (quantidadeRadios == 0) {
        Serial.println(
            "Nenhuma radio valida no JSON."
        );

        carregarRadiosReserva();

        return false;
    }

    Serial.printf(
        "%d radios carregadas do FFat.\n",
        quantidadeRadios
    );

    return true;
}

int obterQuantidadeRadios() {
    return quantidadeRadios;
}

const Radio* obterRadio(int indice) {
    if (
        indice < 0 ||
        indice >= quantidadeRadios
    ) {
        return nullptr;
    }

    return &radios[indice];
}