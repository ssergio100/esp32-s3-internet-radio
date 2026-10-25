#include <Arduino.h>
#include <SHA2Builder.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_idf_version.h>
#include <esp_timer.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Este sketch nao contem SSID nem senha. WiFi.begin() reutiliza a configuracao
// salva na NVS pelo firmware principal.

#ifndef PROBE_ICY_METADATA
#define PROBE_ICY_METADATA 1
#endif

namespace {

constexpr char HOST_RADIO[] = "65.109.81.84";
constexpr uint16_t PORTA_RADIO = 9851;
constexpr char CAMINHO_RADIO[] = "/stream";

// Quantidade solicitada para permitir comparar exatamente duas execucoes.
constexpr uint32_t BYTES_HASH_SHA256 = 262144;

// O NetworkClient do core ESP32 3.3.x usa 1436 bytes no buffer de recepcao.
// Usar o mesmo tamanho evita introduzir outra granularidade artificial aqui.
constexpr size_t TAMANHO_BLOCO_REDE = 1436;

// O maior quadro Layer III calculavel pelas tabelas MPEG abaixo tem 1441
// bytes. Duas molduras completas e outro cabecalho confirmam sincronismo.
constexpr size_t MAXIMO_QUADRO_MP3 = 1441;
constexpr size_t CAPACIDADE_ANALISADOR_MP3 =
    MAXIMO_QUADRO_MP3 * 2 + 4;

constexpr size_t MAXIMO_LINHA_CHUNK = 255;
constexpr size_t MAXIMO_LINHA_HTTP = 1024;

enum class EstadoExecucao : uint8_t {
    CONECTANDO_WIFI,
    CONECTANDO_STREAM,
    LENDO_CABECALHO,
    LENDO_CORPO,
    FINALIZADO,
    ERRO
};

struct Metricas {
    uint64_t bytesCorpoBruto = 0;
    uint64_t bytesEntidadeHttp = 0;
    uint64_t bytesAudio = 0;
    uint64_t bytesMetadata = 0;

    uint64_t chamadasLeitura = 0;
    uint64_t bytesLidos = 0;
    uint64_t duracaoLeiturasUs = 0;
    uint32_t menorLeitura = UINT32_MAX;
    uint32_t maiorLeitura = 0;
    uint32_t maiorDuracaoLeituraUs = 0;

    uint64_t consultasSemDados = 0;
    uint64_t lacunasConcluidas = 0;
    uint64_t duracaoTotalLacunasUs = 0;
    uint64_t maiorLacunaUs = 0;

    uint64_t blocosChunk = 0;
    uint64_t blocosMetadata = 0;
    uint64_t quadrosMp3 = 0;
    uint64_t bytesEmQuadros = 0;
    uint64_t amostrasMp3 = 0;
    uint64_t quebrasDeContinuidade = 0;
    uint64_t bytesDescartadosRessincronizacao = 0;
};

struct CabecalhosResposta {
    int codigoStatus = 0;
    String linhaStatus;
    String contentType;
    String contentEncoding;
    String transferEncoding;
    String connection;
    String location;
    int64_t contentLength = -1;
    uint32_t icyMetaInt = 0;
    uint32_t icyBitrateKbps = 0;
    uint32_t icySampleRate = 0;
};

struct CabecalhoMp3 {
    bool valido = false;
    uint8_t versao = 0;
    uint32_t bitrate = 0;
    uint32_t sampleRate = 0;
    uint16_t tamanhoQuadro = 0;
    uint16_t amostrasPorQuadro = 0;
    uint8_t modoCanal = 0;
    bool padding = false;
};

NetworkClient cliente;
EstadoExecucao estadoExecucao =
    EstadoExecucao::CONECTANDO_WIFI;
Metricas metricas;
CabecalhosResposta cabecalhos;

uint8_t blocoRede[TAMANHO_BLOCO_REDE];

String linhaCabecalho;
bool primeiraLinhaCabecalho = true;
bool cabecalhoCompleto = false;

uint64_t instantePrimeiroByteCorpoUs = 0;
bool lacunaAtiva = false;
uint64_t inicioLacunaUs = 0;

SHA256Builder sha256;
uint32_t bytesNoHash = 0;
bool hashConcluido = false;
String hashCalculado;

// Estado do decodificador de Transfer-Encoding: chunked.
enum class EstadoChunk : uint8_t {
    TAMANHO,
    DADOS,
    CR_DEPOIS_DADOS,
    LF_DEPOIS_DADOS,
    TRAILER,
    CONCLUIDO
};

bool respostaChunked = false;
EstadoChunk estadoChunk = EstadoChunk::TAMANHO;
char linhaChunk[MAXIMO_LINHA_CHUNK + 1] = "";
size_t tamanhoLinhaChunk = 0;
uint64_t bytesRestantesChunk = 0;

// Estado do separador ICY.
uint32_t intervaloMetadata = 0;
uint32_t bytesAudioAteMetadata = 0;
uint32_t bytesMetadataRestantes = 0;
uint32_t tamanhoMetadataAtual = 0;
uint64_t inicioMetadataEntidade = 0;

// Estado do validador de quadros MP3.
uint8_t bufferMp3[CAPACIDADE_ANALISADOR_MP3];
size_t bytesBufferMp3 = 0;
uint64_t offsetInicioBufferMp3 = 0;
bool mp3Sincronizado = false;
CabecalhoMp3 ultimoFormatoMp3;
bool possuiUltimoFormatoMp3 = false;
uint32_t menorBitrateQuadro = UINT32_MAX;
uint32_t maiorBitrateQuadro = 0;
double duracaoMidiaSegundos = 0.0;

bool execucaoEncerrada = false;

uint64_t agoraUs() {
    return static_cast<uint64_t>(esp_timer_get_time());
}

const char* textoEstado(EstadoExecucao estado) {
    switch (estado) {
        case EstadoExecucao::CONECTANDO_WIFI:
            return "conectando_wifi";
        case EstadoExecucao::CONECTANDO_STREAM:
            return "conectando_stream";
        case EstadoExecucao::LENDO_CABECALHO:
            return "lendo_cabecalho";
        case EstadoExecucao::LENDO_CORPO:
            return "lendo_corpo";
        case EstadoExecucao::FINALIZADO:
            return "finalizado";
        case EstadoExecucao::ERRO:
            return "erro";
    }

    return "desconhecido";
}

void imprimirHex4(const uint8_t* dados) {
    Serial.printf(
        "%02X%02X%02X%02X",
        dados[0],
        dados[1],
        dados[2],
        dados[3]
    );
}

void imprimirResumo(const char* motivo) {
    const uint64_t agora = agoraUs();
    uint64_t decorridoUs = 0;

    if (instantePrimeiroByteCorpoUs != 0) {
        decorridoUs = agora - instantePrimeiroByteCorpoUs;
    }

    uint64_t lacunaAtualUs = 0;
    if (lacunaAtiva) {
        lacunaAtualUs = agora - inicioLacunaUs;
    }

    const double taxaCorpoBps =
        decorridoUs > 0
            ? static_cast<double>(metricas.bytesCorpoBruto) *
                8000000.0 /
                static_cast<double>(decorridoUs)
            : 0.0;

    const double taxaAudioBps =
        decorridoUs > 0
            ? static_cast<double>(metricas.bytesAudio) *
                8000000.0 /
                static_cast<double>(decorridoUs)
            : 0.0;

    const double bitrateEstruturalBps =
        duracaoMidiaSegundos > 0.0
            ? static_cast<double>(metricas.bytesEmQuadros) *
                8.0 /
                duracaoMidiaSegundos
            : 0.0;

    const uint32_t menorLeitura =
        metricas.menorLeitura == UINT32_MAX
            ? 0
            : metricas.menorLeitura;

    const uint32_t menorBitrate =
        menorBitrateQuadro == UINT32_MAX
            ? 0
            : menorBitrateQuadro;

    Serial.printf(
        "RESUMO motivo=%s estado=%s decorrido_us=%llu "
        "corpo_bruto=%llu entidade_http=%llu audio=%llu metadata=%llu\n",
        motivo,
        textoEstado(estadoExecucao),
        static_cast<unsigned long long>(decorridoUs),
        static_cast<unsigned long long>(metricas.bytesCorpoBruto),
        static_cast<unsigned long long>(metricas.bytesEntidadeHttp),
        static_cast<unsigned long long>(metricas.bytesAudio),
        static_cast<unsigned long long>(metricas.bytesMetadata)
    );

    Serial.printf(
        "TAXAS media_corpo_bps=%.1f media_audio_bps=%.1f "
        "bitrate_quadros_bps=%.1f icy_br_bps=%lu\n",
        taxaCorpoBps,
        taxaAudioBps,
        bitrateEstruturalBps,
        static_cast<unsigned long>(cabecalhos.icyBitrateKbps) * 1000UL
    );

    Serial.printf(
        "LEITURAS chamadas=%llu bytes=%llu menor=%lu maior=%lu "
        "tempo_total_us=%llu maior_tempo_us=%lu\n",
        static_cast<unsigned long long>(metricas.chamadasLeitura),
        static_cast<unsigned long long>(metricas.bytesLidos),
        static_cast<unsigned long>(menorLeitura),
        static_cast<unsigned long>(metricas.maiorLeitura),
        static_cast<unsigned long long>(metricas.duracaoLeiturasUs),
        static_cast<unsigned long>(metricas.maiorDuracaoLeituraUs)
    );

    Serial.printf(
        "GAPS consultas_sem_dados=%llu concluidos=%llu "
        "total_us=%llu maior_us=%llu atual_us=%llu\n",
        static_cast<unsigned long long>(metricas.consultasSemDados),
        static_cast<unsigned long long>(metricas.lacunasConcluidas),
        static_cast<unsigned long long>(metricas.duracaoTotalLacunasUs),
        static_cast<unsigned long long>(metricas.maiorLacunaUs),
        static_cast<unsigned long long>(lacunaAtualUs)
    );

    Serial.printf(
        "PROTOCOLO chunks=%llu blocos_icy=%llu "
        "quadros=%llu quebras=%llu descartados_ressync=%llu "
        "bitrate_min=%lu bitrate_max=%lu\n",
        static_cast<unsigned long long>(metricas.blocosChunk),
        static_cast<unsigned long long>(metricas.blocosMetadata),
        static_cast<unsigned long long>(metricas.quadrosMp3),
        static_cast<unsigned long long>(metricas.quebrasDeContinuidade),
        static_cast<unsigned long long>(metricas.bytesDescartadosRessincronizacao),
        static_cast<unsigned long>(menorBitrate),
        static_cast<unsigned long>(maiorBitrateQuadro)
    );

    Serial.printf(
        "SHA256 alvo=%lu processados=%lu completo=%s valor=%s\n",
        static_cast<unsigned long>(BYTES_HASH_SHA256),
        static_cast<unsigned long>(bytesNoHash),
        hashConcluido ? "sim" : "nao",
        hashConcluido ? hashCalculado.c_str() : "-"
    );

    Serial.printf(
        "WIFI rssi=%d canal=%d bssid=%s sleep=%d heap_livre=%lu\n",
        WiFi.RSSI(),
        WiFi.channel(),
        WiFi.BSSIDstr().c_str(),
        static_cast<int>(WiFi.getSleep()),
        static_cast<unsigned long>(ESP.getFreeHeap())
    );
}

void falhar(const char* motivo) {
    if (
        estadoExecucao == EstadoExecucao::ERRO ||
        estadoExecucao == EstadoExecucao::FINALIZADO
    ) {
        return;
    }

    Serial.printf("ERRO motivo=%s\n", motivo);
    estadoExecucao = EstadoExecucao::ERRO;
    cliente.stop();
    imprimirResumo("erro");
    execucaoEncerrada = true;
}

bool textoContemSemDiferenciarCaso(
    const String& texto,
    const char* trecho
) {
    String copia = texto;
    copia.toLowerCase();
    String trechoMinusculo = trecho;
    trechoMinusculo.toLowerCase();
    return copia.indexOf(trechoMinusculo) >= 0;
}

uint32_t converterInteiroCabecalho(const String& valor) {
    char* fim = nullptr;
    unsigned long numero = strtoul(
        valor.c_str(),
        &fim,
        10
    );

    if (fim == valor.c_str()) {
        return 0;
    }

    return static_cast<uint32_t>(numero);
}

void analisarLinhaCabecalho(const String& linha) {
    Serial.print("HTTP_RX|");
    Serial.println(linha);

    if (primeiraLinhaCabecalho) {
        primeiraLinhaCabecalho = false;
        cabecalhos.linhaStatus = linha;

        int primeiroEspaco = linha.indexOf(' ');
        if (primeiroEspaco >= 0) {
            cabecalhos.codigoStatus =
                linha.substring(primeiroEspaco + 1).toInt();
        }

        return;
    }

    int separador = linha.indexOf(':');
    if (separador < 0) {
        return;
    }

    String nome = linha.substring(0, separador);
    String valor = linha.substring(separador + 1);
    nome.trim();
    nome.toLowerCase();
    valor.trim();

    if (nome == "content-type") {
        cabecalhos.contentType = valor;
    } else if (nome == "content-encoding") {
        cabecalhos.contentEncoding = valor;
    } else if (nome == "transfer-encoding") {
        cabecalhos.transferEncoding = valor;
    } else if (nome == "connection") {
        cabecalhos.connection = valor;
    } else if (nome == "location") {
        cabecalhos.location = valor;
    } else if (nome == "content-length") {
        cabecalhos.contentLength = strtoll(
            valor.c_str(),
            nullptr,
            10
        );
    } else if (nome == "icy-metaint") {
        cabecalhos.icyMetaInt =
            converterInteiroCabecalho(valor);
    } else if (nome == "icy-br") {
        cabecalhos.icyBitrateKbps =
            converterInteiroCabecalho(valor);
    } else if (nome == "icy-sr") {
        cabecalhos.icySampleRate =
            converterInteiroCabecalho(valor);
    }
}

void prepararCorpoHttp() {
    if (cabecalhos.codigoStatus != 200) {
        falhar("status_http_nao_200");
        return;
    }

    if (
        cabecalhos.contentEncoding.length() > 0 &&
        !textoContemSemDiferenciarCaso(
            cabecalhos.contentEncoding,
            "identity"
        )
    ) {
        falhar("content_encoding_nao_suportado");
        return;
    }

    if (
        cabecalhos.transferEncoding.length() > 0 &&
        !textoContemSemDiferenciarCaso(
            cabecalhos.transferEncoding,
            "chunked"
        )
    ) {
        falhar("transfer_encoding_nao_suportado");
        return;
    }

    respostaChunked = textoContemSemDiferenciarCaso(
        cabecalhos.transferEncoding,
        "chunked"
    );

    estadoChunk =
        respostaChunked
            ? EstadoChunk::TAMANHO
            : EstadoChunk::DADOS;

    intervaloMetadata = cabecalhos.icyMetaInt;
    bytesAudioAteMetadata = intervaloMetadata;
    bytesMetadataRestantes = 0;
    tamanhoMetadataAtual = 0;

    Serial.printf(
        "HTTP_RESUMO status=%d content_type=\"%s\" "
        "transfer_encoding=\"%s\" connection=\"%s\" "
        "content_length=%lld icy_metaint=%lu icy_br_kbps=%lu "
        "icy_sr=%lu\n",
        cabecalhos.codigoStatus,
        cabecalhos.contentType.c_str(),
        cabecalhos.transferEncoding.c_str(),
        cabecalhos.connection.c_str(),
        static_cast<long long>(cabecalhos.contentLength),
        static_cast<unsigned long>(cabecalhos.icyMetaInt),
        static_cast<unsigned long>(cabecalhos.icyBitrateKbps),
        static_cast<unsigned long>(cabecalhos.icySampleRate)
    );

    if (
        PROBE_ICY_METADATA != 0 &&
        intervaloMetadata == 0
    ) {
        Serial.println(
            "AVISO icy_solicitado_sem_icy_metaint"
        );
    }

    sha256.begin();
    estadoExecucao = EstadoExecucao::LENDO_CORPO;
}

CabecalhoMp3 decodificarCabecalhoMp3(const uint8_t* dados) {
    static constexpr uint32_t BITRATES_MPEG1_LAYER3[] = {
        0,
        32000,
        40000,
        48000,
        56000,
        64000,
        80000,
        96000,
        112000,
        128000,
        160000,
        192000,
        224000,
        256000,
        320000
    };

    static constexpr uint32_t BITRATES_MPEG2_LAYER3[] = {
        0,
        8000,
        16000,
        24000,
        32000,
        40000,
        48000,
        56000,
        64000,
        80000,
        96000,
        112000,
        128000,
        144000,
        160000
    };

    static constexpr uint32_t SAMPLE_RATES_MPEG1[] = {
        44100,
        48000,
        32000
    };

    CabecalhoMp3 resultado;

    uint32_t palavra =
        static_cast<uint32_t>(dados[0]) << 24 |
        static_cast<uint32_t>(dados[1]) << 16 |
        static_cast<uint32_t>(dados[2]) << 8 |
        static_cast<uint32_t>(dados[3]);

    if ((palavra & 0xFFE00000UL) != 0xFFE00000UL) {
        return resultado;
    }

    uint8_t versao = (palavra >> 19) & 0x03;
    uint8_t camada = (palavra >> 17) & 0x03;
    uint8_t indiceBitrate = (palavra >> 12) & 0x0F;
    uint8_t indiceSampleRate = (palavra >> 10) & 0x03;

    // 01 e reservado; este probe valida Layer III (bits de camada 01).
    // Bitrate livre (indice zero) e valido em MPEG, mas nao permite calcular
    // o proximo offset sem uma busca especifica e por isso e informado como
    // nao suportado pelo validador.
    if (
        versao == 1 ||
        camada != 1 ||
        indiceBitrate == 0 ||
        indiceBitrate == 15 ||
        indiceSampleRate == 3
    ) {
        return resultado;
    }

    const bool mpeg1 = versao == 3;
    uint32_t bitrate =
        mpeg1
            ? BITRATES_MPEG1_LAYER3[indiceBitrate]
            : BITRATES_MPEG2_LAYER3[indiceBitrate];

    uint32_t sampleRate =
        SAMPLE_RATES_MPEG1[indiceSampleRate];

    if (versao == 2) {
        sampleRate /= 2;
    } else if (versao == 0) {
        sampleRate /= 4;
    }

    const bool padding = ((palavra >> 9) & 0x01) != 0;
    const uint32_t multiplicador = mpeg1 ? 144 : 72;
    uint32_t tamanhoQuadro =
        multiplicador * bitrate / sampleRate +
        (padding ? 1U : 0U);

    if (
        tamanhoQuadro < 4 ||
        tamanhoQuadro > MAXIMO_QUADRO_MP3
    ) {
        return resultado;
    }

    resultado.valido = true;
    resultado.versao = versao;
    resultado.bitrate = bitrate;
    resultado.sampleRate = sampleRate;
    resultado.tamanhoQuadro =
        static_cast<uint16_t>(tamanhoQuadro);
    resultado.amostrasPorQuadro =
        mpeg1 ? 1152 : 576;
    resultado.modoCanal = (palavra >> 6) & 0x03;
    resultado.padding = padding;

    return resultado;
}

void removerDoBufferMp3(size_t quantidade) {
    if (quantidade == 0) {
        return;
    }

    if (quantidade >= bytesBufferMp3) {
        offsetInicioBufferMp3 += bytesBufferMp3;
        bytesBufferMp3 = 0;
        return;
    }

    memmove(
        bufferMp3,
        bufferMp3 + quantidade,
        bytesBufferMp3 - quantidade
    );

    bytesBufferMp3 -= quantidade;
    offsetInicioBufferMp3 += quantidade;
}

void registrarQuadroMp3(
    const CabecalhoMp3& quadro,
    uint64_t offset
) {
    metricas.quadrosMp3++;
    metricas.bytesEmQuadros += quadro.tamanhoQuadro;
    metricas.amostrasMp3 += quadro.amostrasPorQuadro;
    duracaoMidiaSegundos +=
        static_cast<double>(quadro.amostrasPorQuadro) /
        static_cast<double>(quadro.sampleRate);

    menorBitrateQuadro = std::min(
        menorBitrateQuadro,
        quadro.bitrate
    );

    maiorBitrateQuadro = std::max(
        maiorBitrateQuadro,
        quadro.bitrate
    );

    if (
        !possuiUltimoFormatoMp3 ||
        quadro.versao != ultimoFormatoMp3.versao ||
        quadro.bitrate != ultimoFormatoMp3.bitrate ||
        quadro.sampleRate != ultimoFormatoMp3.sampleRate ||
        quadro.modoCanal != ultimoFormatoMp3.modoCanal
    ) {
        Serial.printf(
            "MP3_FORMAT offset=%llu versao_bits=%u layer=3 "
            "bitrate=%lu sample_rate=%lu modo_canal=%u "
            "tamanho_quadro=%u\n",
            static_cast<unsigned long long>(offset),
            quadro.versao,
            static_cast<unsigned long>(quadro.bitrate),
            static_cast<unsigned long>(quadro.sampleRate),
            quadro.modoCanal,
            quadro.tamanhoQuadro
        );

        ultimoFormatoMp3 = quadro;
        possuiUltimoFormatoMp3 = true;
    }
}

void procurarSincronismoMp3();

void processarQuadrosSincronizados() {
    while (mp3Sincronizado) {
        if (bytesBufferMp3 < 4) {
            return;
        }

        CabecalhoMp3 atual =
            decodificarCabecalhoMp3(bufferMp3);

        if (!atual.valido) {
            metricas.quebrasDeContinuidade++;
            Serial.printf(
                "MP3_BREAK offset=%llu motivo=cabecalho_atual_invalido bytes=",
                static_cast<unsigned long long>(offsetInicioBufferMp3)
            );
            imprimirHex4(bufferMp3);
            Serial.println();

            mp3Sincronizado = false;
            removerDoBufferMp3(1);
            procurarSincronismoMp3();
            return;
        }

        const size_t necessario =
            static_cast<size_t>(atual.tamanhoQuadro) + 4;

        if (bytesBufferMp3 < necessario) {
            return;
        }

        CabecalhoMp3 seguinte = decodificarCabecalhoMp3(
            bufferMp3 + atual.tamanhoQuadro
        );

        if (!seguinte.valido) {
            metricas.quebrasDeContinuidade++;
            Serial.printf(
                "MP3_BREAK offset=%llu motivo=proximo_cabecalho_invalido "
                "tamanho_previsto=%u atual=",
                static_cast<unsigned long long>(offsetInicioBufferMp3),
                atual.tamanhoQuadro
            );
            imprimirHex4(bufferMp3);
            Serial.print(" proximo=");
            imprimirHex4(bufferMp3 + atual.tamanhoQuadro);
            Serial.println();

            mp3Sincronizado = false;
            removerDoBufferMp3(1);
            procurarSincronismoMp3();
            return;
        }

        registrarQuadroMp3(
            atual,
            offsetInicioBufferMp3
        );

        removerDoBufferMp3(atual.tamanhoQuadro);
    }
}

void procurarSincronismoMp3() {
    if (mp3Sincronizado) {
        processarQuadrosSincronizados();
        return;
    }

    size_t primeiroCandidatoIncompleto = SIZE_MAX;

    for (size_t posicao = 0; posicao + 4 <= bytesBufferMp3; ++posicao) {
        CabecalhoMp3 primeiro =
            decodificarCabecalhoMp3(bufferMp3 + posicao);

        if (!primeiro.valido) {
            continue;
        }

        size_t posicaoSeguinte =
            posicao + primeiro.tamanhoQuadro;

        if (posicaoSeguinte + 4 > bytesBufferMp3) {
            primeiroCandidatoIncompleto = posicao;
            break;
        }

        CabecalhoMp3 segundo = decodificarCabecalhoMp3(
            bufferMp3 + posicaoSeguinte
        );

        if (!segundo.valido) {
            continue;
        }

        if (posicao > 0) {
            metricas.bytesDescartadosRessincronizacao +=
                posicao;
            Serial.printf(
                "MP3_RESSYNC descartados=%lu novo_offset=%llu\n",
                static_cast<unsigned long>(posicao),
                static_cast<unsigned long long>(
                    offsetInicioBufferMp3 + posicao
                )
            );
            removerDoBufferMp3(posicao);
        }

        mp3Sincronizado = true;
        Serial.printf(
            "MP3_SYNC offset=%llu primeiro_cabecalho=",
            static_cast<unsigned long long>(offsetInicioBufferMp3)
        );
        imprimirHex4(bufferMp3);
        Serial.println();
        processarQuadrosSincronizados();
        return;
    }

    if (primeiroCandidatoIncompleto != SIZE_MAX) {
        if (primeiroCandidatoIncompleto > 0) {
            metricas.bytesDescartadosRessincronizacao +=
                primeiroCandidatoIncompleto;
            removerDoBufferMp3(
                primeiroCandidatoIncompleto
            );
        }
        return;
    }

    const size_t preservar = MAXIMO_QUADRO_MP3 + 3;
    if (bytesBufferMp3 > preservar) {
        const size_t descartar =
            bytesBufferMp3 - preservar;
        metricas.bytesDescartadosRessincronizacao +=
            descartar;
        removerDoBufferMp3(descartar);
    }
}

void adicionarByteAoValidadorMp3(
    uint8_t byte,
    uint64_t offsetAudio
) {
    if (bytesBufferMp3 == 0) {
        offsetInicioBufferMp3 = offsetAudio;
    }

    if (bytesBufferMp3 >= CAPACIDADE_ANALISADOR_MP3) {
        metricas.quebrasDeContinuidade++;
        Serial.printf(
            "MP3_BREAK offset=%llu motivo=buffer_validador_cheio\n",
            static_cast<unsigned long long>(offsetInicioBufferMp3)
        );
        mp3Sincronizado = false;
        removerDoBufferMp3(1);
    }

    bufferMp3[bytesBufferMp3++] = byte;

    if (mp3Sincronizado) {
        processarQuadrosSincronizados();
    } else {
        procurarSincronismoMp3();
    }
}

void processarByteAudio(uint8_t byte) {
    const uint64_t offsetAudio = metricas.bytesAudio;
    metricas.bytesAudio++;

    if (bytesNoHash < BYTES_HASH_SHA256) {
        sha256.add(&byte, 1);
        bytesNoHash++;

        if (bytesNoHash == BYTES_HASH_SHA256) {
            sha256.calculate();
            hashCalculado = sha256.toString();
            hashConcluido = true;

            Serial.printf(
                "SHA256_COMPLETO bytes=%lu valor=%s\n",
                static_cast<unsigned long>(bytesNoHash),
                hashCalculado.c_str()
            );

            imprimirResumo("hash_completo");
        }
    }

    adicionarByteAoValidadorMp3(
        byte,
        offsetAudio
    );
}

void processarByteEntidadeHttp(uint8_t byte) {
    const uint64_t offsetEntidade =
        metricas.bytesEntidadeHttp;
    metricas.bytesEntidadeHttp++;

    if (intervaloMetadata == 0) {
        processarByteAudio(byte);
        return;
    }

    if (bytesMetadataRestantes > 0) {
        metricas.bytesMetadata++;
        bytesMetadataRestantes--;

        if (bytesMetadataRestantes == 0) {
            Serial.printf(
                "ICY_FIM bloco=%llu inicio_entidade=%llu tamanho=%lu "
                "proximo_audio_offset=%llu\n",
                static_cast<unsigned long long>(metricas.blocosMetadata),
                static_cast<unsigned long long>(inicioMetadataEntidade),
                static_cast<unsigned long>(tamanhoMetadataAtual),
                static_cast<unsigned long long>(metricas.bytesAudio)
            );
            bytesAudioAteMetadata = intervaloMetadata;
        }

        return;
    }

    if (bytesAudioAteMetadata == 0) {
        tamanhoMetadataAtual =
            static_cast<uint32_t>(byte) * 16U;
        bytesMetadataRestantes = tamanhoMetadataAtual;
        inicioMetadataEntidade = offsetEntidade;
        metricas.blocosMetadata++;

        Serial.printf(
            "ICY_INICIO bloco=%llu offset_entidade=%llu "
            "offset_audio=%llu tamanho=%lu unidade=0x%02X\n",
            static_cast<unsigned long long>(metricas.blocosMetadata),
            static_cast<unsigned long long>(offsetEntidade),
            static_cast<unsigned long long>(metricas.bytesAudio),
            static_cast<unsigned long>(tamanhoMetadataAtual),
            byte
        );

        if (bytesMetadataRestantes == 0) {
            bytesAudioAteMetadata = intervaloMetadata;
        }

        return;
    }

    processarByteAudio(byte);
    bytesAudioAteMetadata--;
}

bool converterTamanhoChunk(uint64_t& tamanho) {
    linhaChunk[tamanhoLinhaChunk] = '\0';

    char* extensao = strchr(linhaChunk, ';');
    if (extensao != nullptr) {
        *extensao = '\0';
    }

    char* inicio = linhaChunk;
    while (*inicio != '\0' && isspace(static_cast<unsigned char>(*inicio))) {
        inicio++;
    }

    char* fimTexto = inicio + strlen(inicio);
    while (
        fimTexto > inicio &&
        isspace(static_cast<unsigned char>(fimTexto[-1]))
    ) {
        *--fimTexto = '\0';
    }

    if (*inicio == '\0') {
        return false;
    }

    for (const char* atual = inicio; *atual != '\0'; ++atual) {
        if (!isxdigit(static_cast<unsigned char>(*atual))) {
            return false;
        }
    }

    char* fimConversao = nullptr;
    unsigned long long valor = strtoull(
        inicio,
        &fimConversao,
        16
    );

    if (
        fimConversao == inicio ||
        *fimConversao != '\0'
    ) {
        return false;
    }

    tamanho = static_cast<uint64_t>(valor);
    return true;
}

void processarByteChunked(uint8_t byte) {
    switch (estadoChunk) {
        case EstadoChunk::TAMANHO: {
            if (byte == '\r') {
                return;
            }

            if (byte == '\n') {
                uint64_t tamanho = 0;
                if (!converterTamanhoChunk(tamanho)) {
                    falhar("linha_de_tamanho_chunk_invalida");
                    return;
                }

                tamanhoLinhaChunk = 0;
                linhaChunk[0] = '\0';
                bytesRestantesChunk = tamanho;

                if (tamanho == 0) {
                    estadoChunk = EstadoChunk::TRAILER;
                } else {
                    metricas.blocosChunk++;
                    estadoChunk = EstadoChunk::DADOS;
                }
                return;
            }

            if (tamanhoLinhaChunk >= MAXIMO_LINHA_CHUNK) {
                falhar("linha_de_chunk_excedeu_limite");
                return;
            }

            linhaChunk[tamanhoLinhaChunk++] =
                static_cast<char>(byte);
            return;
        }

        case EstadoChunk::DADOS:
            if (bytesRestantesChunk == 0) {
                falhar("estado_chunk_sem_bytes_restantes");
                return;
            }

            processarByteEntidadeHttp(byte);
            bytesRestantesChunk--;

            if (bytesRestantesChunk == 0) {
                estadoChunk = EstadoChunk::CR_DEPOIS_DADOS;
            }
            return;

        case EstadoChunk::CR_DEPOIS_DADOS:
            if (byte != '\r') {
                falhar("chunk_sem_cr_final");
                return;
            }
            estadoChunk = EstadoChunk::LF_DEPOIS_DADOS;
            return;

        case EstadoChunk::LF_DEPOIS_DADOS:
            if (byte != '\n') {
                falhar("chunk_sem_lf_final");
                return;
            }
            estadoChunk = EstadoChunk::TAMANHO;
            return;

        case EstadoChunk::TRAILER:
            if (byte == '\r') {
                return;
            }

            if (byte == '\n') {
                if (tamanhoLinhaChunk == 0) {
                    estadoChunk = EstadoChunk::CONCLUIDO;
                    estadoExecucao = EstadoExecucao::FINALIZADO;
                    imprimirResumo("fim_chunked");
                    cliente.stop();
                    execucaoEncerrada = true;
                } else {
                    linhaChunk[tamanhoLinhaChunk] = '\0';
                    Serial.printf(
                        "HTTP_TRAILER|%s\n",
                        linhaChunk
                    );
                    tamanhoLinhaChunk = 0;
                }
                return;
            }

            if (tamanhoLinhaChunk >= MAXIMO_LINHA_CHUNK) {
                falhar("trailer_http_excedeu_limite");
                return;
            }

            linhaChunk[tamanhoLinhaChunk++] =
                static_cast<char>(byte);
            return;

        case EstadoChunk::CONCLUIDO:
            falhar("dados_apos_fim_chunked");
            return;
    }
}

void processarByteCorpoBruto(uint8_t byte) {
    if (instantePrimeiroByteCorpoUs == 0) {
        instantePrimeiroByteCorpoUs = agoraUs();
    }

    metricas.bytesCorpoBruto++;

    if (respostaChunked) {
        processarByteChunked(byte);
    } else {
        processarByteEntidadeHttp(byte);
    }
}

void processarByteRecebido(uint8_t byte) {
    if (!cabecalhoCompleto) {
        if (byte == '\n') {
            if (
                linhaCabecalho.length() > 0 &&
                linhaCabecalho[
                    linhaCabecalho.length() - 1
                ] == '\r'
            ) {
                linhaCabecalho.remove(
                    linhaCabecalho.length() - 1
                );
            }

            if (linhaCabecalho.length() == 0) {
                cabecalhoCompleto = true;
                Serial.println("HTTP_RX_FIM");
                prepararCorpoHttp();
            } else {
                analisarLinhaCabecalho(
                    linhaCabecalho
                );
            }

            linhaCabecalho = "";
            return;
        }

        if (linhaCabecalho.length() >= MAXIMO_LINHA_HTTP) {
            falhar("linha_http_excedeu_limite");
            return;
        }

        linhaCabecalho += static_cast<char>(byte);
        return;
    }

    if (estadoExecucao == EstadoExecucao::LENDO_CORPO) {
        processarByteCorpoBruto(byte);
    }
}

void encerrarLacunaSeNecessario(uint64_t instanteUs) {
    if (!lacunaAtiva) {
        return;
    }

    const uint64_t duracao =
        instanteUs - inicioLacunaUs;
    lacunaAtiva = false;
    metricas.lacunasConcluidas++;
    metricas.duracaoTotalLacunasUs += duracao;
    metricas.maiorLacunaUs = std::max(
        metricas.maiorLacunaUs,
        duracao
    );
}

void registrarAusenciaDeDados(uint64_t instanteUs) {
    // Nao classifica a ausencia como falha. Apenas mede cada periodo continuo
    // entre o fim do cabecalho e o proximo byte disponivel.
    if (estadoExecucao != EstadoExecucao::LENDO_CORPO) {
        return;
    }

    metricas.consultasSemDados++;

    if (!lacunaAtiva) {
        lacunaAtiva = true;
        inicioLacunaUs = instanteUs;
    }
}

void lerStream() {
    int disponiveis = cliente.available();

    if (disponiveis > 0) {
        const uint64_t instante = agoraUs();
        encerrarLacunaSeNecessario(instante);

        size_t solicitados = std::min(
            static_cast<size_t>(disponiveis),
            sizeof(blocoRede)
        );

        uint64_t inicioLeitura = agoraUs();
        int lidos = cliente.read(
            blocoRede,
            solicitados
        );
        uint32_t duracaoLeitura =
            static_cast<uint32_t>(
                agoraUs() - inicioLeitura
            );

        metricas.chamadasLeitura++;
        metricas.duracaoLeiturasUs +=
            duracaoLeitura;
        metricas.maiorDuracaoLeituraUs = std::max(
            metricas.maiorDuracaoLeituraUs,
            duracaoLeitura
        );

        if (lidos < 0) {
            falhar("NetworkClient_read_negativo");
            return;
        }

        if (lidos == 0) {
            registrarAusenciaDeDados(agoraUs());
            return;
        }

        metricas.bytesLidos += lidos;
        metricas.menorLeitura = std::min(
            metricas.menorLeitura,
            static_cast<uint32_t>(lidos)
        );
        metricas.maiorLeitura = std::max(
            metricas.maiorLeitura,
            static_cast<uint32_t>(lidos)
        );

        for (int indice = 0; indice < lidos; ++indice) {
            processarByteRecebido(
                blocoRede[indice]
            );

            if (execucaoEncerrada) {
                return;
            }
        }

        if (
            !respostaChunked &&
            cabecalhos.contentLength >= 0 &&
            metricas.bytesEntidadeHttp >=
                static_cast<uint64_t>(
                    cabecalhos.contentLength
                )
        ) {
            estadoExecucao = EstadoExecucao::FINALIZADO;
            cliente.stop();
            imprimirResumo("content_length_completo");
            execucaoEncerrada = true;
        }

        return;
    }

    if (cliente.connected()) {
        registrarAusenciaDeDados(agoraUs());
        delay(1);
        return;
    }

    encerrarLacunaSeNecessario(agoraUs());

    if (
        cabecalhos.contentLength >= 0 &&
        metricas.bytesEntidadeHttp <
            static_cast<uint64_t>(cabecalhos.contentLength)
    ) {
        falhar("conexao_fechada_antes_do_content_length");
        return;
    }

    estadoExecucao = EstadoExecucao::FINALIZADO;
    imprimirResumo("conexao_fechada");
    execucaoEncerrada = true;
}

String montarRequisicao() {
    String requisicao;
    requisicao.reserve(512);
    requisicao += "GET ";
    requisicao += CAMINHO_RADIO;
    requisicao += " HTTP/1.1\r\n";
    requisicao += "Host: ";
    requisicao += HOST_RADIO;
    requisicao += ':';
    requisicao += PORTA_RADIO;
    requisicao += "\r\n";

    if (PROBE_ICY_METADATA != 0) {
        requisicao += "Icy-MetaData:1\r\n";
    }

    requisicao += "Pragma: no-cache\r\n";
    requisicao += "Cache-Control: no-cache\r\n";
    requisicao += "Range: bytes=0-\r\n";
    requisicao += "Accept: */*\r\n";
    requisicao +=
        "User-Agent: VLC/3.0.21 LibVLC/3.0.21 "
        "AppleWebKit/537.36 (KHTML, like Gecko)\r\n";
    requisicao += "Accept-Encoding: identity;q=1,*;q=0\r\n";
    requisicao += "Connection: keep-alive\r\n\r\n";
    return requisicao;
}

void conectarWifiSalvo() {
    Serial.println(
        "WIFI inicio=credenciais_salvas_nvs"
    );

    WiFi.mode(WIFI_STA);
    WiFi.begin();

    wl_status_t ultimoStatus = WL_NO_SHIELD;

    while (WiFi.status() != WL_CONNECTED) {
        wl_status_t atual = WiFi.status();
        if (atual != ultimoStatus) {
            Serial.printf(
                "WIFI_STATUS codigo=%d\n",
                static_cast<int>(atual)
            );
            ultimoStatus = atual;
        }
        delay(100);
    }

    Serial.printf(
        "WIFI_CONECTADO ip=%s gateway=%s rssi=%d canal=%d "
        "bssid=%s sleep=%d\n",
        WiFi.localIP().toString().c_str(),
        WiFi.gatewayIP().toString().c_str(),
        WiFi.RSSI(),
        WiFi.channel(),
        WiFi.BSSIDstr().c_str(),
        static_cast<int>(WiFi.getSleep())
    );
}

void conectarStream() {
    estadoExecucao =
        EstadoExecucao::CONECTANDO_STREAM;

    Serial.printf(
        "STREAM_CONECTANDO host=%s porta=%u\n",
        HOST_RADIO,
        PORTA_RADIO
    );

    if (!cliente.connect(HOST_RADIO, PORTA_RADIO)) {
        falhar("falha_connect_stream");
        return;
    }

    String requisicao = montarRequisicao();

    Serial.println("HTTP_TX_INICIO");
    Serial.print(requisicao);
    Serial.println("HTTP_TX_FIM");

    size_t enviados = cliente.print(requisicao);
    if (enviados != requisicao.length()) {
        falhar("requisicao_http_incompleta");
        return;
    }

    linhaCabecalho.reserve(256);
    estadoExecucao =
        EstadoExecucao::LENDO_CABECALHO;
}

void processarComandosSeriais() {
    while (Serial.available() > 0) {
        int comando = Serial.read();

        if (comando == 's' || comando == 'S') {
            imprimirResumo("comando_s");
        } else if (comando == 'q' || comando == 'Q') {
            if (!execucaoEncerrada) {
                estadoExecucao =
                    EstadoExecucao::FINALIZADO;
                cliente.stop();
                imprimirResumo("comando_q");
                execucaoEncerrada = true;
            }
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("PROBE_STREAM_MP3 versao=1");
    Serial.printf(
        "AMBIENTE arduino_esp32=%s esp_idf=%s chip=%s revisao=%u "
        "flash=%lu psram=%lu\n",
        ESP_ARDUINO_VERSION_STR,
        esp_get_idf_version(),
        ESP.getChipModel(),
        ESP.getChipRevision(),
        static_cast<unsigned long>(ESP.getFlashChipSize()),
        static_cast<unsigned long>(ESP.getPsramSize())
    );

    Serial.printf(
        "CONFIG icy_metadata=%d sha256_bytes=%lu "
        "decoder=nenhum i2s=desativado display=ausente\n",
        PROBE_ICY_METADATA != 0 ? 1 : 0,
        static_cast<unsigned long>(BYTES_HASH_SHA256)
    );

    Serial.println(
        "COMANDOS s=resumo q=encerrar"
    );

    conectarWifiSalvo();
    conectarStream();
}

void loop() {
    processarComandosSeriais();

    if (execucaoEncerrada) {
        delay(10);
        return;
    }

    if (
        estadoExecucao == EstadoExecucao::LENDO_CABECALHO ||
        estadoExecucao == EstadoExecucao::LENDO_CORPO
    ) {
        lerStream();
    }
}
