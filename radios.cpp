#include "radios.h"
#include "persistencia_radios.h"

#include <ArduinoJson.h>
#include <FFat.h>

namespace {

Radio radios[QUANTIDADE_MAXIMA_RADIOS];

String nomes[QUANTIDADE_MAXIMA_RADIOS];
String urls[QUANTIDADE_MAXIMA_RADIOS];

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

    for (
        size_t indice = 0;
        indice < QUANTIDADE_MAXIMA_RADIOS;
        indice++
    ) {
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

bool carregarDocumentoRadios(
    const char* caminho,
    DynamicJsonDocument& documento
) {
    File arquivo =
        FFat.open(
            caminho,
            FILE_READ
        );

    if (!arquivo) {
        return false;
    }

    documento.clear();

    DeserializationError erro =
        deserializeJson(
            documento,
            arquivo
        );

    arquivo.close();

    return
        !erro &&
        documento.is<JsonArray>();
}

}

bool dadosRadioValidos(
    const char* nome,
    const char* url
) {
    if (
        nome == nullptr ||
        nome[0] == '\0' ||
        strlen(nome) >
            TAMANHO_MAXIMO_NOME_RADIO ||
        url == nullptr ||
        url[0] == '\0' ||
        strlen(url) >
            TAMANHO_MAXIMO_URL_RADIO
    ) {
        return false;
    }

    return
        strncmp(
            url,
            "http://",
            7
        ) == 0 ||
        strncmp(
            url,
            "https://",
            8
        ) == 0;
}

bool carregarRadios() {
    limparRadios();

    DynamicJsonDocument documento(16384);

    bool carregado =
        carregarDocumentoRadios(
            CAMINHO_RADIOS_ATIVO,
            documento
        );

    if (!carregado) {
        carregado =
            carregarDocumentoRadios(
                CAMINHO_RADIOS_BACKUP,
                documento
            );

        if (carregado) {
            Serial.println(
                "Usando backup de radios.json."
            );
        }
    }

    if (!carregado) {
        Serial.println(
            "radios.json e radios.bak indisponiveis ou invalidos."
        );

        carregarRadiosReserva();

        return false;
    }

    JsonArray lista =
        documento.as<JsonArray>();

    for (JsonObject item : lista) {
        if (
            quantidadeRadios >=
            QUANTIDADE_MAXIMA_RADIOS
        ) {
            break;
        }

        const char* nome =
            item["nome"];

        const char* url =
            item["url"];

        if (!dadosRadioValidos(nome, url)) {
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
