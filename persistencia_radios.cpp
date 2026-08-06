#include "persistencia_radios.h"

#include <Arduino.h>
#include <FFat.h>

#include "radios.h"

const char* const CAMINHO_RADIOS_ATIVO =
    "/radios.json";

const char* const CAMINHO_RADIOS_BACKUP =
    "/radios.bak";

namespace {

    const char* const CAMINHO_RADIOS_TEMPORARIO =
        "/radios.tmp";

    bool documentoRadiosValido(
        JsonDocument& documento
    ) {
        if (!documento.is<JsonArray>()) {
            return false;
        }

        JsonArray radios =
            documento.as<JsonArray>();

        if (
            radios.size() == 0 ||
            radios.size() > QUANTIDADE_MAXIMA_RADIOS
        ) {
            return false;
        }

        for (JsonObject radio : radios) {
            const char* nome = radio["nome"];
            const char* url = radio["url"];

            if (!dadosRadioValidos(nome, url)) {
                return false;
            }
        }

        return true;
    }

    bool arquivoRadiosValido(const char* caminho) {
        File arquivo = FFat.open(caminho, FILE_READ);

        if (!arquivo) {
            return false;
        }

        JsonDocument documento;

        DeserializationError erro =
            deserializeJson(documento, arquivo);

        arquivo.close();

        return
            !erro &&
            documentoRadiosValido(documento);
    }

}

bool carregarDocumentoRadiosDoArquivo(
    JsonDocument& documento,
    const char* caminho
) {
    File arquivo = FFat.open(caminho, FILE_READ);

    if (!arquivo) {
        return false;
    }

    documento.clear();

    DeserializationError erro =
        deserializeJson(documento, arquivo);

    arquivo.close();

    return
        !erro &&
        documentoRadiosValido(documento);
}

OrigemArquivoRadios carregarDocumentoRadiosPersistido(
    JsonDocument& documento
) {
    if (
        carregarDocumentoRadiosDoArquivo(
            documento,
            CAMINHO_RADIOS_ATIVO
        )
    ) {
        return OrigemArquivoRadios::ATIVO;
    }

    if (
        carregarDocumentoRadiosDoArquivo(
            documento,
            CAMINHO_RADIOS_BACKUP
        )
    ) {
        return OrigemArquivoRadios::BACKUP;
    }

    return OrigemArquivoRadios::NENHUM_VALIDO;
}

bool carregarDocumentoRadiosParaEdicao(
    JsonDocument& documento
) {
    OrigemArquivoRadios origem =
        carregarDocumentoRadiosPersistido(documento);

    if (origem == OrigemArquivoRadios::ATIVO) {
        return true;
    }

    if (origem == OrigemArquivoRadios::BACKUP) {
        Serial.println(
            "radios.json invalido; usando radios.bak."
        );

        return true;
    }

    if (
        !FFat.exists(CAMINHO_RADIOS_ATIVO) &&
        !FFat.exists(CAMINHO_RADIOS_BACKUP)
    ) {
        documento.to<JsonArray>();

        return true;
    }

    return false;
}

bool salvarDocumentoRadios(
    JsonDocument& documento
) {
    FFat.remove(CAMINHO_RADIOS_TEMPORARIO);

    File arquivo =
        FFat.open(
            CAMINHO_RADIOS_TEMPORARIO,
            FILE_WRITE
        );

    if (!arquivo) {
        return false;
    }

    bool salvo = serializeJson(documento, arquivo) > 0;

    arquivo.flush();
    arquivo.close();

    if (
        !salvo ||
        !arquivoRadiosValido(CAMINHO_RADIOS_TEMPORARIO)
    ) {
        FFat.remove(CAMINHO_RADIOS_TEMPORARIO);

        return false;
    }

    bool arquivoAnteriorValido =
        arquivoRadiosValido(CAMINHO_RADIOS_ATIVO);

    if (arquivoAnteriorValido) {
        FFat.remove(CAMINHO_RADIOS_BACKUP);

        if (
            !FFat.rename(
                CAMINHO_RADIOS_ATIVO,
                CAMINHO_RADIOS_BACKUP
            )
        ) {
            FFat.remove(CAMINHO_RADIOS_TEMPORARIO);

            return false;
        }
    } else if (FFat.exists(CAMINHO_RADIOS_ATIVO)) {
        FFat.remove(CAMINHO_RADIOS_ATIVO);
    }

    if (
        FFat.rename(
            CAMINHO_RADIOS_TEMPORARIO,
            CAMINHO_RADIOS_ATIVO
        )
    ) {
        return true;
    }

    if (arquivoAnteriorValido) {
        FFat.rename(
            CAMINHO_RADIOS_BACKUP,
            CAMINHO_RADIOS_ATIVO
        );
    }

    FFat.remove(CAMINHO_RADIOS_TEMPORARIO);

    return false;
}
