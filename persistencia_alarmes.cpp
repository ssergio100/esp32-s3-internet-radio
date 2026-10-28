#include "persistencia_alarmes.h"

#include <Arduino.h>
#include <FFat.h>
#include <cstring>

#include "alarmes.h"
#include "configuracao.h"

const char* const CAMINHO_ALARMES_ATIVO =
    "/alarmes.json";

const char* const CAMINHO_ALARMES_BACKUP =
    "/alarmes.bak";

namespace {

const char* const CAMINHO_ALARMES_TEMPORARIO =
    "/alarmes.tmp";

bool textoComTamanhoValido(
    const char* texto,
    size_t tamanhoMaximo
) {
    return
        texto != nullptr &&
        texto[0] != '\0' &&
        strlen(texto) <= tamanhoMaximo;
}

bool horarioValido(const char* horario) {
    if (
        horario == nullptr ||
        strlen(horario) != 5 ||
        horario[2] != ':'
    ) {
        return false;
    }

    if (
        horario[0] < '0' || horario[0] > '9' ||
        horario[1] < '0' || horario[1] > '9' ||
        horario[3] < '0' || horario[3] > '9' ||
        horario[4] < '0' || horario[4] > '9'
    ) {
        return false;
    }

    int hora =
        (horario[0] - '0') * 10 + horario[1] - '0';
    int minuto =
        (horario[3] - '0') * 10 + horario[4] - '0';

    return hora <= 23 && minuto <= 59;
}

bool anoBissexto(int ano) {
    return
        ano % 400 == 0 ||
        (ano % 4 == 0 && ano % 100 != 0);
}

bool dataValida(const char* data) {
    if (
        data == nullptr ||
        strlen(data) != 10 ||
        data[4] != '-' ||
        data[7] != '-'
    ) {
        return false;
    }

    for (int indice = 0; indice < 10; indice++) {
        if (indice == 4 || indice == 7) {
            continue;
        }

        if (data[indice] < '0' || data[indice] > '9') {
            return false;
        }
    }

    int ano = atoi(data);
    int mes = atoi(data + 5);
    int dia = atoi(data + 8);

    if (ano < 2024 || mes < 1 || mes > 12 || dia < 1) {
        return false;
    }

    static constexpr uint8_t DIAS_POR_MES[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    int quantidadeDias = DIAS_POR_MES[mes - 1];

    if (mes == 2 && anoBissexto(ano)) {
        quantidadeDias = 29;
    }

    return dia <= quantidadeDias;
}

int indiceDiaSemana(const char* dia) {
    static constexpr const char* DIAS[] = {
        "dom", "seg", "ter", "qua", "qui", "sex", "sab"
    };

    if (dia == nullptr) {
        return -1;
    }

    for (int indice = 0; indice < 7; indice++) {
        if (strcmp(dia, DIAS[indice]) == 0) {
            return indice;
        }
    }

    return -1;
}

bool diasSemanaValidos(JsonArrayConst dias) {
    if (dias.size() == 0 || dias.size() > 7) {
        return false;
    }

    uint8_t mascara = 0;

    for (JsonVariantConst item : dias) {
        int indice = indiceDiaSemana(item.as<const char*>());

        if (indice < 0) {
            return false;
        }

        uint8_t bit = static_cast<uint8_t>(1U << indice);

        if ((mascara & bit) != 0) {
            return false;
        }

        mascara |= bit;
    }

    return true;
}

bool alarmeValido(JsonObjectConst alarme) {
    JsonVariantConst campoId = alarme["id"];
    JsonVariantConst campoVolume = alarme["volume"];
    JsonVariantConst campoDias = alarme["dias"];
    JsonVariantConst campoData = alarme["data"];
    uint32_t id = campoId | 0;
    const char* nome = alarme["nome"];
    const char* horario = alarme["horario"];
    int volume = campoVolume | -1;

    if (
        !campoId.is<uint32_t>() ||
        id == 0 ||
        !alarme["ativo"].is<bool>() ||
        !campoVolume.is<int>() ||
        !textoComTamanhoValido(
            nome,
            TAMANHO_MAXIMO_NOME_ALARME
        ) ||
        !horarioValido(horario) ||
        volume < 1 || volume > VOLUME_MAXIMO
    ) {
        return false;
    }

    if (
        (!campoDias.isNull() && !campoDias.is<JsonArrayConst>()) ||
        (!campoData.isNull() && !campoData.is<const char*>())
    ) {
        return false;
    }

    bool possuiDias = campoDias.is<JsonArrayConst>();
    bool possuiData = campoData.is<const char*>();

    if (possuiDias == possuiData) {
        return false;
    }

    if (
        possuiDias &&
        !diasSemanaValidos(
            alarme["dias"].as<JsonArrayConst>()
        )
    ) {
        return false;
    }

    if (
        possuiData &&
        !dataValida(alarme["data"].as<const char*>())
    ) {
        return false;
    }

    JsonVariantConst arquivo = alarme["arquivo"];
    JsonVariantConst radioId = alarme["radioId"];
    JsonVariantConst radioFm = alarme["radioFm"];
    JsonVariantConst ultimaEstacaoWeb =
        alarme["usarUltimaEstacaoWeb"];
    JsonVariantConst somDfPlayer = alarme["somDfPlayer"];
    JsonVariantConst duracaoMinutos = alarme["duracaoMinutos"];
    bool usaRadioFm =
        radioFm.is<bool>() && radioFm.as<bool>();
    bool usaUltimaEstacaoWeb =
        ultimaEstacaoWeb.is<bool>() &&
        ultimaEstacaoWeb.as<bool>();

    if (!arquivo.isNull()) {
        return false;
    }

    if (
        !radioId.isNull() &&
        (
            !radioId.is<uint32_t>() ||
            radioId.as<uint32_t>() == 0
        )
    ) {
        return false;
    }

    if (
        !radioFm.isNull() &&
        (!radioFm.is<bool>() || !usaRadioFm)
    ) {
        return false;
    }

    if (
        !ultimaEstacaoWeb.isNull() &&
        (
            !ultimaEstacaoWeb.is<bool>() ||
            !usaUltimaEstacaoWeb
        )
    ) {
        return false;
    }

    if (
        !somDfPlayer.isNull() &&
        (
            !somDfPlayer.is<int>() ||
            somDfPlayer.as<int>() < 1 ||
            somDfPlayer.as<int>() > 255
        )
    ) {
        return false;
    }

    if (
        !duracaoMinutos.isNull() &&
        (
            !duracaoMinutos.is<int>() ||
            duracaoMinutos.as<int>() < 1 ||
            duracaoMinutos.as<int>() >
                DURACAO_MAXIMA_CONFIGURAVEL_ALARME_MINUTOS
        )
    ) {
        return false;
    }

    uint8_t quantidadeFontes = 0;
    quantidadeFontes += usaRadioFm ? 1 : 0;
    quantidadeFontes += radioId.is<uint32_t>() ? 1 : 0;
    quantidadeFontes += usaUltimaEstacaoWeb ? 1 : 0;
    quantidadeFontes += somDfPlayer.is<int>() ? 1 : 0;

    if (quantidadeFontes > 1) {
        return false;
    }

    return true;
}

bool configuracaoChimeValida(JsonVariantConst valor) {
    if (valor.isNull()) {
        return true;
    }
    if (!valor.is<JsonObjectConst>()) {
        return false;
    }

    JsonObjectConst chime = valor.as<JsonObjectConst>();
    JsonVariantConst ativo = chime["ativo"];
    JsonVariantConst som = chime["som"];
    JsonVariantConst volume = chime["volume"];
    JsonVariantConst intervalo = chime["intervaloMinutos"];
    JsonVariantConst silencioNoturno = chime["silencioNoturno"];
    JsonVariantConst inicioSilencio = chime["inicioSilencioHora"];
    JsonVariantConst fimSilencio = chime["fimSilencioHora"];
    int numeroSom = som | -1;
    int nivelVolume = volume | -1;

    return
        ativo.is<bool>() &&
        som.is<int>() &&
        numeroSom >= 1 && numeroSom <= 255 &&
        volume.is<int>() &&
        nivelVolume >= 1 && nivelVolume <= VOLUME_MAXIMO &&
        (
            intervalo.isNull() ||
            (
                intervalo.is<int>() &&
                intervalo.as<int>() >= 1 &&
                intervalo.as<int>() <= 60
            )
        ) &&
        (
            silencioNoturno.isNull() ||
            silencioNoturno.is<bool>()
        ) &&
        (
            inicioSilencio.isNull() ||
            (
                inicioSilencio.is<int>() &&
                inicioSilencio.as<int>() >= 0 &&
                inicioSilencio.as<int>() <= 23
            )
        ) &&
        (
            fimSilencio.isNull() ||
            (
                fimSilencio.is<int>() &&
                fimSilencio.as<int>() >= 0 &&
                fimSilencio.as<int>() <= 23
            )
        );
}

bool arquivoAlarmesValido(const char* caminho) {
    File arquivo = FFat.open(caminho, FILE_READ);

    if (!arquivo) {
        return false;
    }

    JsonDocument documento;
    DeserializationError erro =
        deserializeJson(documento, arquivo);

    arquivo.close();

    return !erro && documentoAlarmesValido(documento);
}

bool carregarDocumentoAlarmesLegado(
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

    if (
        erro ||
        (documento["versao"] | 0) != 1 ||
        !documento["alarmes"].is<JsonArray>()
    ) {
        return false;
    }

    bool documentoAlterado = false;
    JsonArray alarmes = documento["alarmes"].as<JsonArray>();

    for (JsonVariant item : alarmes) {
        if (!item.is<JsonObject>()) {
            return false;
        }

        JsonObject alarme = item.as<JsonObject>();

        if (!alarme["arquivo"].isNull()) {
            alarme.remove("arquivo");

            if (alarme["radioId"].isNull()) {
                alarme["radioFm"] = true;
            }

            documentoAlterado = true;
        }

        JsonVariant radioId = alarme["radioId"];

        if (
            radioId.is<uint32_t>() &&
            radioId.as<uint32_t>() == 0
        ) {
            alarme.remove("radioId");
            documentoAlterado = true;
        }
    }

    return
        documentoAlterado &&
        documentoAlarmesValido(documento);
}

void criarDocumentoAlarmesVazio(JsonDocument& documento) {
    documento.clear();
    documento["versao"] = 1;
    JsonObject chime = documento["chime"].to<JsonObject>();
    chime["ativo"] = false;
    chime["som"] = 1;
    chime["volume"] = VOLUME_PADRAO;
    chime["intervaloMinutos"] = 60;
    chime["silencioNoturno"] = false;
    chime["inicioSilencioHora"] = 22;
    chime["fimSilencioHora"] = 7;
    documento["alarmes"].to<JsonArray>();
}

}

bool documentoAlarmesValido(JsonDocument& documento) {
    if (
        (documento["versao"] | 0) != 1 ||
        !documento["alarmes"].is<JsonArray>() ||
        !configuracaoChimeValida(documento["chime"])
    ) {
        return false;
    }

    JsonArray alarmes = documento["alarmes"].as<JsonArray>();

    if (alarmes.size() > QUANTIDADE_MAXIMA_ALARMES) {
        return false;
    }

    for (size_t indice = 0; indice < alarmes.size(); indice++) {
        JsonObjectConst alarme = alarmes[indice].as<JsonObjectConst>();

        if (!alarmeValido(alarme)) {
            return false;
        }

        uint32_t id = alarme["id"] | 0;

        for (size_t anterior = 0; anterior < indice; anterior++) {
            if ((alarmes[anterior]["id"] | 0) == id) {
                return false;
            }
        }
    }

    return true;
}

bool carregarDocumentoAlarmesDoArquivo(
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

    return !erro && documentoAlarmesValido(documento);
}

OrigemArquivoAlarmes carregarDocumentoAlarmesPersistido(
    JsonDocument& documento
) {
    if (
        carregarDocumentoAlarmesDoArquivo(
            documento,
            CAMINHO_ALARMES_ATIVO
        )
    ) {
        return OrigemArquivoAlarmes::ATIVO;
    }

    if (
        carregarDocumentoAlarmesLegado(
            documento,
            CAMINHO_ALARMES_ATIVO
        )
    ) {
        Serial.println(
            "Alarmes: fonte Player antiga convertida para Radio FM."
        );
        return OrigemArquivoAlarmes::ATIVO;
    }

    if (
        carregarDocumentoAlarmesDoArquivo(
            documento,
            CAMINHO_ALARMES_BACKUP
        )
    ) {
        return OrigemArquivoAlarmes::BACKUP;
    }

    if (
        carregarDocumentoAlarmesLegado(
            documento,
            CAMINHO_ALARMES_BACKUP
        )
    ) {
        Serial.println(
            "Alarmes: backup antigo convertido para Radio FM."
        );
        return OrigemArquivoAlarmes::BACKUP;
    }

    return OrigemArquivoAlarmes::NENHUM_VALIDO;
}

bool carregarDocumentoAlarmesParaEdicao(
    JsonDocument& documento
) {
    OrigemArquivoAlarmes origem =
        carregarDocumentoAlarmesPersistido(documento);

    if (origem != OrigemArquivoAlarmes::NENHUM_VALIDO) {
        return true;
    }

    if (
        FFat.exists(CAMINHO_ALARMES_ATIVO) ||
        FFat.exists(CAMINHO_ALARMES_BACKUP)
    ) {
        return false;
    }

    criarDocumentoAlarmesVazio(documento);
    return true;
}

bool salvarDocumentoAlarmes(JsonDocument& documento) {
    if (!documentoAlarmesValido(documento)) {
        return false;
    }

    FFat.remove(CAMINHO_ALARMES_TEMPORARIO);

    File arquivo =
        FFat.open(
            CAMINHO_ALARMES_TEMPORARIO,
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
        !arquivoAlarmesValido(CAMINHO_ALARMES_TEMPORARIO)
    ) {
        FFat.remove(CAMINHO_ALARMES_TEMPORARIO);
        return false;
    }

    bool anteriorValido =
        arquivoAlarmesValido(CAMINHO_ALARMES_ATIVO);

    if (anteriorValido) {
        FFat.remove(CAMINHO_ALARMES_BACKUP);

        if (
            !FFat.rename(
                CAMINHO_ALARMES_ATIVO,
                CAMINHO_ALARMES_BACKUP
            )
        ) {
            FFat.remove(CAMINHO_ALARMES_TEMPORARIO);
            return false;
        }
    } else if (FFat.exists(CAMINHO_ALARMES_ATIVO)) {
        FFat.remove(CAMINHO_ALARMES_ATIVO);
    }

    if (
        FFat.rename(
            CAMINHO_ALARMES_TEMPORARIO,
            CAMINHO_ALARMES_ATIVO
        )
    ) {
        return true;
    }

    if (anteriorValido) {
        FFat.rename(
            CAMINHO_ALARMES_BACKUP,
            CAMINHO_ALARMES_ATIVO
        );
    }

    FFat.remove(CAMINHO_ALARMES_TEMPORARIO);
    return false;
}
