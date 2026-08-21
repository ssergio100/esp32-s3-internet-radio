#include "alarmes.h"

#include <ArduinoJson.h>
#include <FFat.h>
#include <cmath>
#include <cstring>

#include "persistencia_alarmes.h"
#include "relogio.h"

const char* const CAMINHO_SOM_PADRAO_ALARME =
    "/alarme_padrao.wav";

namespace {

constexpr uint32_t TAXA_SOM_PADRAO_HZ = 16000;
constexpr uint32_t DURACAO_SOM_PADRAO_MS = 1000;
constexpr uint32_t AMOSTRAS_SOM_PADRAO =
    TAXA_SOM_PADRAO_HZ * DURACAO_SOM_PADRAO_MS / 1000;
constexpr uint32_t TAMANHO_SOM_PADRAO =
    44 + AMOSTRAS_SOM_PADRAO * 2;
constexpr unsigned long INTERVALO_VERIFICACAO_ALARMES_MS = 500;

struct AlarmeCarregado {
    uint32_t id = 0;
    bool ativo = false;
    bool execucaoUnica = false;
    uint8_t hora = 0;
    uint8_t minuto = 0;
    uint8_t diasSemana = 0;
    uint16_t ano = 0;
    uint8_t mes = 0;
    uint8_t dia = 0;
    uint8_t volume = 0;
    String nome;
    String arquivo;
};

AlarmeCarregado alarmes[QUANTIDADE_MAXIMA_ALARMES];
size_t quantidadeAlarmes = 0;
uint64_t ultimaChaveMinuto = UINT64_MAX;
bool somPadraoDisponivel = false;
unsigned long momentoUltimaVerificacaoMs = 0;

void escreverUint16(File& arquivo, uint16_t valor) {
    uint8_t bytes[] = {
        static_cast<uint8_t>(valor & 0xFF),
        static_cast<uint8_t>((valor >> 8) & 0xFF)
    };

    arquivo.write(bytes, sizeof(bytes));
}

void escreverUint32(File& arquivo, uint32_t valor) {
    uint8_t bytes[] = {
        static_cast<uint8_t>(valor & 0xFF),
        static_cast<uint8_t>((valor >> 8) & 0xFF),
        static_cast<uint8_t>((valor >> 16) & 0xFF),
        static_cast<uint8_t>((valor >> 24) & 0xFF)
    };

    arquivo.write(bytes, sizeof(bytes));
}

bool somPadraoValido() {
    File arquivo = FFat.open(CAMINHO_SOM_PADRAO_ALARME, FILE_READ);

    if (!arquivo || arquivo.size() != TAMANHO_SOM_PADRAO) {
        arquivo.close();
        return false;
    }

    char assinatura[12] = {};
    bool leuAssinatura = arquivo.readBytes(
        assinatura,
        sizeof(assinatura)
    ) == sizeof(assinatura);

    arquivo.close();

    return
        leuAssinatura &&
        memcmp(assinatura, "RIFF", 4) == 0 &&
        memcmp(assinatura + 8, "WAVE", 4) == 0;
}

bool gerarSomPadrao() {
    FFat.remove(CAMINHO_SOM_PADRAO_ALARME);

    File arquivo = FFat.open(
        CAMINHO_SOM_PADRAO_ALARME,
        FILE_WRITE
    );

    if (!arquivo) {
        return false;
    }

    arquivo.write(reinterpret_cast<const uint8_t*>("RIFF"), 4);
    escreverUint32(arquivo, TAMANHO_SOM_PADRAO - 8);
    arquivo.write(reinterpret_cast<const uint8_t*>("WAVEfmt "), 8);
    escreverUint32(arquivo, 16);
    escreverUint16(arquivo, 1);
    escreverUint16(arquivo, 1);
    escreverUint32(arquivo, TAXA_SOM_PADRAO_HZ);
    escreverUint32(arquivo, TAXA_SOM_PADRAO_HZ * 2);
    escreverUint16(arquivo, 2);
    escreverUint16(arquivo, 16);
    arquivo.write(reinterpret_cast<const uint8_t*>("data"), 4);
    escreverUint32(arquivo, AMOSTRAS_SOM_PADRAO * 2);

    constexpr size_t AMOSTRAS_POR_BLOCO = 256;
    uint8_t bloco[AMOSTRAS_POR_BLOCO * 2];
    uint32_t amostraAtual = 0;

    while (amostraAtual < AMOSTRAS_SOM_PADRAO) {
        size_t quantidade = min(
            static_cast<uint32_t>(AMOSTRAS_POR_BLOCO),
            AMOSTRAS_SOM_PADRAO - amostraAtual
        );

        for (size_t indice = 0; indice < quantidade; indice++) {
            uint32_t instante = amostraAtual + indice;
            uint32_t milissegundos =
                instante * 1000 / TAXA_SOM_PADRAO_HZ;
            float frequencia = 0.0F;

            if (milissegundos < 180) {
                frequencia = 880.0F;
            } else if (
                milissegundos >= 300 &&
                milissegundos < 480
            ) {
                frequencia = 1046.5F;
            }

            int16_t valor = 0;

            if (frequencia > 0.0F) {
                constexpr float DOIS_PI = 6.28318530718F;
                float fase =
                    DOIS_PI * frequencia * instante /
                    TAXA_SOM_PADRAO_HZ;
                valor = static_cast<int16_t>(sinf(fase) * 9000.0F);
            }

            bloco[indice * 2] =
                static_cast<uint8_t>(valor & 0xFF);
            bloco[indice * 2 + 1] =
                static_cast<uint8_t>((valor >> 8) & 0xFF);
        }

        if (
            arquivo.write(bloco, quantidade * 2) !=
            quantidade * 2
        ) {
            arquivo.close();
            FFat.remove(CAMINHO_SOM_PADRAO_ALARME);
            return false;
        }

        amostraAtual += quantidade;
    }

    arquivo.flush();
    arquivo.close();
    return somPadraoValido();
}

int indiceDiaSemana(const char* dia) {
    static constexpr const char* DIAS[] = {
        "dom", "seg", "ter", "qua", "qui", "sex", "sab"
    };

    for (int indice = 0; indice < 7; indice++) {
        if (strcmp(dia, DIAS[indice]) == 0) {
            return indice;
        }
    }

    return -1;
}

void interpretarHorario(
    const char* horario,
    uint8_t& hora,
    uint8_t& minuto
) {
    hora = static_cast<uint8_t>(atoi(horario));
    minuto = static_cast<uint8_t>(atoi(horario + 3));
}

void interpretarData(
    const char* data,
    AlarmeCarregado& alarme
) {
    alarme.ano = static_cast<uint16_t>(atoi(data));
    alarme.mes = static_cast<uint8_t>(atoi(data + 5));
    alarme.dia = static_cast<uint8_t>(atoi(data + 8));
}

bool correspondeAoMinuto(
    const AlarmeCarregado& alarme,
    const struct tm& dataHora
) {
    if (
        !alarme.ativo ||
        alarme.hora != dataHora.tm_hour ||
        alarme.minuto != dataHora.tm_min
    ) {
        return false;
    }

    if (alarme.execucaoUnica) {
        return
            alarme.ano == dataHora.tm_year + 1900 &&
            alarme.mes == dataHora.tm_mon + 1 &&
            alarme.dia == dataHora.tm_mday;
    }

    return
        (alarme.diasSemana & (1U << dataHora.tm_wday)) != 0;
}

bool alarmeUnicoVencido(
    const AlarmeCarregado& alarme,
    const struct tm& dataHora
) {
    if (!alarme.ativo || !alarme.execucaoUnica) {
        return false;
    }

    uint64_t instanteAlarme =
        static_cast<uint64_t>(alarme.ano) * 100000000ULL +
        static_cast<uint64_t>(alarme.mes) * 1000000ULL +
        static_cast<uint64_t>(alarme.dia) * 10000ULL +
        static_cast<uint64_t>(alarme.hora) * 100ULL +
        alarme.minuto;
    uint64_t instanteAtual =
        static_cast<uint64_t>(dataHora.tm_year + 1900) * 100000000ULL +
        static_cast<uint64_t>(dataHora.tm_mon + 1) * 1000000ULL +
        static_cast<uint64_t>(dataHora.tm_mday) * 10000ULL +
        static_cast<uint64_t>(dataHora.tm_hour) * 100ULL +
        dataHora.tm_min;

    return instanteAlarme <= instanteAtual;
}

bool persistirAlarmesUnicosVencidos(
    const struct tm& dataHora
) {
    JsonDocument documento;

    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        return false;
    }

    bool alterou = false;
    JsonArray lista = documento["alarmes"].as<JsonArray>();

    for (JsonObject item : lista) {
        if (!item["data"].is<const char*>()) {
            continue;
        }

        uint32_t id = item["id"] | 0;

        for (size_t indice = 0; indice < quantidadeAlarmes; indice++) {
            if (
                alarmes[indice].id == id &&
                alarmeUnicoVencido(alarmes[indice], dataHora)
            ) {
                item["ativo"] = false;
                alterou = true;
                break;
            }
        }
    }

    if (!alterou) {
        return true;
    }

    if (!salvarDocumentoAlarmes(documento)) {
        return false;
    }

    // A fotografia em RAM só acompanha a mudança depois que a transação na
    // FFat terminou. Em caso de falha, o próximo minuto tentará novamente.
    for (size_t indice = 0; indice < quantidadeAlarmes; indice++) {
        if (alarmeUnicoVencido(alarmes[indice], dataHora)) {
            alarmes[indice].ativo = false;
        }
    }

    return true;
}

}

void iniciarAlarmes() {
    somPadraoDisponivel =
        somPadraoValido() || gerarSomPadrao();

    Serial.println(
        somPadraoDisponivel
            ? "Alarmes: som padrao disponivel."
            : "Alarmes: falha ao preparar o som padrao."
    );

    carregarAlarmes();
}

void carregarAlarmes() {
    quantidadeAlarmes = 0;

    JsonDocument documento;
    OrigemArquivoAlarmes origem =
        carregarDocumentoAlarmesPersistido(documento);

    if (origem == OrigemArquivoAlarmes::NENHUM_VALIDO) {
        Serial.println("Alarmes: nenhum cadastro persistido valido.");
        return;
    }

    JsonArray lista = documento["alarmes"].as<JsonArray>();

    for (JsonObject item : lista) {
        AlarmeCarregado& alarme = alarmes[quantidadeAlarmes];

        alarme = AlarmeCarregado{};
        alarme.id = item["id"] | 0;
        alarme.ativo = item["ativo"] | false;
        alarme.nome = item["nome"].as<const char*>();
        alarme.volume = item["volume"] | 1;

        interpretarHorario(
            item["horario"].as<const char*>(),
            alarme.hora,
            alarme.minuto
        );

        if (item["data"].is<const char*>()) {
            alarme.execucaoUnica = true;
            interpretarData(
                item["data"].as<const char*>(),
                alarme
            );
        } else {
            JsonArray dias = item["dias"].as<JsonArray>();

            for (JsonVariant dia : dias) {
                int indice =
                    indiceDiaSemana(dia.as<const char*>());

                alarme.diasSemana |=
                    static_cast<uint8_t>(1U << indice);
            }
        }

        if (item["arquivo"].is<const char*>()) {
            alarme.arquivo = item["arquivo"].as<const char*>();
        }

        quantidadeAlarmes++;
    }

    Serial.printf(
        "Alarmes: %u cadastro(s) carregado(s).\n",
        static_cast<unsigned int>(quantidadeAlarmes)
    );
}

bool verificarDisparoAlarme(DisparoAlarme& disparo) {
    unsigned long agoraMs = millis();

    if (
        agoraMs - momentoUltimaVerificacaoMs <
        INTERVALO_VERIFICACAO_ALARMES_MS
    ) {
        return false;
    }

    momentoUltimaVerificacaoMs = agoraMs;
    struct tm dataHora = {};

    if (!obterDataHoraLocal(dataHora)) {
        return false;
    }

    uint64_t chaveMinuto =
        static_cast<uint64_t>(dataHora.tm_year + 1900) * 100000000ULL +
        static_cast<uint64_t>(dataHora.tm_yday) * 10000ULL +
        static_cast<uint64_t>(dataHora.tm_hour) * 100ULL +
        static_cast<uint64_t>(dataHora.tm_min);

    if (chaveMinuto == ultimaChaveMinuto) {
        return false;
    }

    ultimaChaveMinuto = chaveMinuto;
    int indiceEscolhido = -1;
    bool possuiExecucaoUnicaVencida = false;

    for (size_t indice = 0; indice < quantidadeAlarmes; indice++) {
        possuiExecucaoUnicaVencida =
            possuiExecucaoUnicaVencida ||
            alarmeUnicoVencido(alarmes[indice], dataHora);

        if (!correspondeAoMinuto(alarmes[indice], dataHora)) {
            continue;
        }

        if (
            indiceEscolhido < 0 ||
            alarmes[indice].id > alarmes[indiceEscolhido].id
        ) {
            indiceEscolhido = static_cast<int>(indice);
        }
    }

    if (
        possuiExecucaoUnicaVencida &&
        !persistirAlarmesUnicosVencidos(dataHora)
    ) {
        Serial.println(
            "Alarmes: falha ao desativar execucao unica."
        );
    }

    if (indiceEscolhido < 0) {
        return false;
    }

    AlarmeCarregado escolhido = alarmes[indiceEscolhido];

    disparo.id = escolhido.id;
    disparo.nome = escolhido.nome;
    disparo.arquivo = escolhido.arquivo;
    disparo.volume = escolhido.volume;

    return true;
}

StatusAlarmes obterStatusAlarmes() {
    StatusAlarmes status;
    status.quantidade = quantidadeAlarmes;
    status.somPadraoDisponivel = somPadraoDisponivel;

    for (size_t indice = 0; indice < quantidadeAlarmes; indice++) {
        if (alarmes[indice].ativo) {
            status.quantidadeAtivos++;
        }
    }

    return status;
}
