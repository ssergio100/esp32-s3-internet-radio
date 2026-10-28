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
    uint32_t radioId = 0;
    bool radioFm = false;
    bool radioWebUltimaEstacao = false;
    uint8_t somDfPlayer = 0;
    bool ativo = false;
    bool execucaoUnica = false;
    uint8_t hora = 0;
    uint8_t minuto = 0;
    uint8_t diasSemana = 0;
    uint16_t ano = 0;
    uint8_t mes = 0;
    uint8_t dia = 0;
    uint8_t volume = 0;
    uint8_t duracaoMinutos = DURACAO_MAXIMA_ALARME_MINUTOS;
    String nome;
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
        alarme.radioId = item["radioId"] | 0;
        alarme.radioFm = item["radioFm"] | false;
        alarme.radioWebUltimaEstacao =
            item["usarUltimaEstacaoWeb"] | false;
        alarme.somDfPlayer = item["somDfPlayer"] | 0;
        alarme.duracaoMinutos = item["duracaoMinutos"] |
            DURACAO_MAXIMA_ALARME_MINUTOS;

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
    disparo.radioId = escolhido.radioId;
    disparo.radioFm = escolhido.radioFm;
    disparo.radioWebUltimaEstacao =
        escolhido.radioWebUltimaEstacao ||
        (
            !escolhido.radioFm &&
            escolhido.radioId != 0
        );
    disparo.somDfPlayer = escolhido.somDfPlayer;
    disparo.nome = escolhido.nome;
    disparo.volume = escolhido.volume;
    disparo.duracaoMinutos = escolhido.duracaoMinutos;

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

bool obterProximoAlarme(ProximoAlarme& proximo) {
    proximo = ProximoAlarme{};
    struct tm agoraLocal = {};
    if (!obterDataHoraLocal(agoraLocal)) {
        return false;
    }

    time_t instanteAtual = mktime(&agoraLocal);
    time_t melhorInstante = 0;
    const AlarmeCarregado* melhor = nullptr;
    struct tm melhorDataLocal = {};

    for (size_t indice = 0; indice < quantidadeAlarmes; indice++) {
        const AlarmeCarregado& alarme = alarmes[indice];
        if (!alarme.ativo) {
            continue;
        }

        time_t instanteCandidato = 0;
        struct tm candidato = agoraLocal;
        candidato.tm_sec = 0;
        candidato.tm_isdst = -1;
        candidato.tm_hour = alarme.hora;
        candidato.tm_min = alarme.minuto;

        if (alarme.execucaoUnica) {
            candidato.tm_year = alarme.ano - 1900;
            candidato.tm_mon = alarme.mes - 1;
            candidato.tm_mday = alarme.dia;
            instanteCandidato = mktime(&candidato);
            if (instanteCandidato <= instanteAtual) {
                continue;
            }
        } else {
            instanteCandidato = 0;
            for (uint8_t deslocamentoDia = 0; deslocamentoDia < 8; deslocamentoDia++) {
                struct tm ocorrencia = candidato;
                ocorrencia.tm_mday += deslocamentoDia;
                time_t instante = mktime(&ocorrencia);
                if (
                    instante > instanteAtual &&
                    (alarme.diasSemana & (1U << ocorrencia.tm_wday)) != 0
                ) {
                    instanteCandidato = instante;
                    candidato = ocorrencia;
                    break;
                }
            }
            if (instanteCandidato == 0) {
                continue;
            }
        }

        if (
            melhor == nullptr ||
            instanteCandidato < melhorInstante
        ) {
            melhor = &alarme;
            melhorInstante = instanteCandidato;
            melhorDataLocal = candidato;
        }
    }

    if (melhor == nullptr) {
        return false;
    }

    proximo.disponivel = true;
    proximo.hora = melhor->hora;
    proximo.minuto = melhor->minuto;

    if (melhor->execucaoUnica) {
        snprintf(
            proximo.recorrencia,
            sizeof(proximo.recorrencia),
            "%02u/%02u",
            melhor->dia,
            melhor->mes
        );
    } else if (melhor->diasSemana == 0b00111110) {
        strlcpy(proximo.recorrencia, "SEG-SEX", sizeof(proximo.recorrencia));
    } else if (melhor->diasSemana == 0b01000001) {
        strlcpy(proximo.recorrencia, "FIM SEM.", sizeof(proximo.recorrencia));
    } else if (melhor->diasSemana == 0b01111111) {
        strlcpy(proximo.recorrencia, "TODOS", sizeof(proximo.recorrencia));
    } else {
        static constexpr const char* DIAS[] = {
            "DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"
        };
        strlcpy(
            proximo.recorrencia,
            DIAS[melhorDataLocal.tm_wday],
            sizeof(proximo.recorrencia)
        );
    }

    if (melhor->radioFm) {
        strlcpy(proximo.fonte, "RÁDIO FM", sizeof(proximo.fonte));
    } else if (
        melhor->radioWebUltimaEstacao ||
        melhor->radioId != 0
    ) {
        strlcpy(proximo.fonte, "RÁDIO WEB", sizeof(proximo.fonte));
    } else if (melhor->somDfPlayer != 0) {
        strlcpy(proximo.fonte, "DFPLAYER", sizeof(proximo.fonte));
    } else {
        strlcpy(proximo.fonte, "SOM PADRÃO", sizeof(proximo.fonte));
    }
    return true;
}

namespace {

FonteAlarmeConfiguravel obterFonteConfiguravel(
    const AlarmeCarregado& alarme
) {
    if (alarme.radioFm) {
        return FonteAlarmeConfiguravel::RADIO_FM;
    }
    if (alarme.radioWebUltimaEstacao || alarme.radioId != 0) {
        return FonteAlarmeConfiguravel::RADIO_WEB;
    }
    if (alarme.somDfPlayer != 0) {
        return FonteAlarmeConfiguravel::DFPLAYER;
    }
    return FonteAlarmeConfiguravel::SOM_PADRAO;
}

void copiarConfiguracao(
    const AlarmeCarregado& origem,
    ConfiguracaoAlarme& destino
) {
    destino = ConfiguracaoAlarme{};
    destino.id = origem.id;
    strlcpy(destino.nome, origem.nome.c_str(), sizeof(destino.nome));
    destino.ativo = origem.ativo;
    destino.execucaoUnica = origem.execucaoUnica;
    destino.hora = origem.hora;
    destino.minuto = origem.minuto;
    destino.diasSemana = origem.diasSemana;
    destino.ano = origem.ano;
    destino.mes = origem.mes;
    destino.dia = origem.dia;
    destino.fonte = obterFonteConfiguravel(origem);
    destino.radioId = origem.radioId;
    destino.usarUltimaEstacaoWeb =
        origem.radioWebUltimaEstacao;
    destino.somDfPlayer = origem.somDfPlayer == 0
        ? 1
        : origem.somDfPlayer;
    destino.volume = origem.volume;
    destino.duracaoMinutos = origem.duracaoMinutos;
}

void escreverDiasSemana(JsonArray dias, uint8_t mascara) {
    static constexpr const char* NOMES[] = {
        "dom", "seg", "ter", "qua", "qui", "sex", "sab"
    };
    for (uint8_t indice = 0; indice < 7; indice++) {
        if ((mascara & (1U << indice)) != 0) {
            dias.add(NOMES[indice]);
        }
    }
}

void escreverConfiguracao(
    JsonObject destino,
    const ConfiguracaoAlarme& configuracao
) {
    destino["id"] = configuracao.id;
    destino["nome"] = configuracao.nome;
    destino["ativo"] = configuracao.ativo;

    char horario[6];
    snprintf(
        horario,
        sizeof(horario),
        "%02u:%02u",
        configuracao.hora,
        configuracao.minuto
    );
    destino["horario"] = horario;
    destino["volume"] = configuracao.volume;
    destino["duracaoMinutos"] = configuracao.duracaoMinutos;

    if (configuracao.execucaoUnica) {
        char data[11];
        snprintf(
            data,
            sizeof(data),
            "%04u-%02u-%02u",
            configuracao.ano,
            configuracao.mes,
            configuracao.dia
        );
        destino["data"] = data;
    } else {
        escreverDiasSemana(
            destino["dias"].to<JsonArray>(),
            configuracao.diasSemana
        );
    }

    switch (configuracao.fonte) {
        case FonteAlarmeConfiguravel::RADIO_FM:
            destino["radioFm"] = true;
            break;
        case FonteAlarmeConfiguravel::RADIO_WEB:
            if (configuracao.usarUltimaEstacaoWeb) {
                destino["usarUltimaEstacaoWeb"] = true;
            } else if (configuracao.radioId != 0) {
                destino["radioId"] = configuracao.radioId;
            }
            break;
        case FonteAlarmeConfiguravel::DFPLAYER:
            destino["somDfPlayer"] = configuracao.somDfPlayer;
            break;
        case FonteAlarmeConfiguravel::SOM_PADRAO:
            break;
    }
}

}

size_t listarConfiguracoesAlarmes(
    ConfiguracaoAlarme* destino,
    size_t capacidade
) {
    if (destino == nullptr || capacidade == 0) {
        return 0;
    }

    size_t quantidade = min(quantidadeAlarmes, capacidade);
    for (size_t indice = 0; indice < quantidade; indice++) {
        copiarConfiguracao(alarmes[indice], destino[indice]);
    }
    return quantidade;
}

bool criarConfiguracaoAlarme(ConfiguracaoAlarme& configuracao) {
    if (quantidadeAlarmes >= QUANTIDADE_MAXIMA_ALARMES) {
        return false;
    }

    uint32_t maiorId = 0;
    for (size_t indice = 0; indice < quantidadeAlarmes; indice++) {
        maiorId = max(maiorId, alarmes[indice].id);
    }
    if (maiorId == UINT32_MAX) {
        return false;
    }

    configuracao = ConfiguracaoAlarme{};
    configuracao.id = maiorId + 1;
    configuracao.volume = VOLUME_PADRAO;
    configuracao.duracaoMinutos =
        DURACAO_MAXIMA_ALARME_MINUTOS;
    strlcpy(
        configuracao.nome,
        "Alarme",
        sizeof(configuracao.nome)
    );
    return true;
}

bool salvarConfiguracaoAlarme(
    const ConfiguracaoAlarme& configuracao
) {
    if (
        configuracao.id == 0 ||
        configuracao.nome[0] == '\0' ||
        configuracao.hora > 23 ||
        configuracao.minuto > 59 ||
        configuracao.volume < 1 ||
        configuracao.volume > VOLUME_MAXIMO ||
        configuracao.duracaoMinutos < 1 ||
        configuracao.duracaoMinutos >
            DURACAO_MAXIMA_CONFIGURAVEL_ALARME_MINUTOS ||
        (!configuracao.execucaoUnica && configuracao.diasSemana == 0)
    ) {
        return false;
    }

    JsonDocument documento;
    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        return false;
    }

    JsonArray lista = documento["alarmes"].as<JsonArray>();
    JsonObject destino;
    for (JsonObject item : lista) {
        if ((item["id"] | 0U) == configuracao.id) {
            destino = item;
            break;
        }
    }

    if (destino.isNull()) {
        if (lista.size() >= QUANTIDADE_MAXIMA_ALARMES) {
            return false;
        }
        destino = lista.add<JsonObject>();
    } else {
        destino.clear();
    }

    escreverConfiguracao(destino, configuracao);
    if (!salvarDocumentoAlarmes(documento)) {
        return false;
    }

    carregarAlarmes();
    return true;
}

bool excluirConfiguracaoAlarme(uint32_t id) {
    if (id == 0) {
        return false;
    }

    JsonDocument documento;
    if (!carregarDocumentoAlarmesParaEdicao(documento)) {
        return false;
    }

    JsonArray lista = documento["alarmes"].as<JsonArray>();
    bool encontrou = false;
    for (size_t indice = 0; indice < lista.size(); indice++) {
        if ((lista[indice]["id"] | 0U) == id) {
            lista.remove(indice);
            encontrou = true;
            break;
        }
    }

    if (!encontrou || !salvarDocumentoAlarmes(documento)) {
        return false;
    }

    carregarAlarmes();
    return true;
}
