#include "chime_audio.h"
#include "arquivos_audio.h"
#include "configuracao.h"

#include <Arduino.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>

namespace {

struct CabecalhoWavInfo {
    uint16_t formatoAudio = 0;
    uint16_t canais = 0;
    uint32_t taxaAmostragem = 0;
    uint16_t bitsPorAmostra = 0;
    uint32_t tamanhoDadosBytes = 0;
    uint32_t deslocamentoDados = 0;
};

// Amostras PCM em PSRAM (formato 16-bit com sinal)
int16_t* bufferPcmPsram = nullptr;
size_t totalQuadrosPcm = 0;
uint16_t canaisChime = 0;
uint32_t taxaAmostragemChime = 44100;
bool chimeCarregado = false;

// Estado da reprodução e do ducking
EstadoChime estadoAtual = EstadoChime::INATIVO;
uint64_t posicaoAmostraFp = 0; // Ponto fixo 32.32 ou 48.16 para interpolação
float fatorDuckingAtual = 1.0f;

uint32_t lerUint32Le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t lerUint16Le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

bool analisarCabecalhoWav(File& arquivo, CabecalhoWavInfo& info) {
    if (arquivo.size() < 44) {
        Serial.println("Chime: arquivo WAV muito curto.");
        return false;
    }

    uint8_t cabecalhoRiff[12];
    if (arquivo.read(cabecalhoRiff, sizeof(cabecalhoRiff)) != sizeof(cabecalhoRiff)) {
        Serial.println("Chime: erro ao ler cabecalho RIFF.");
        return false;
    }

    if (memcmp(&cabecalhoRiff[0], "RIFF", 4) != 0 ||
        memcmp(&cabecalhoRiff[8], "WAVE", 4) != 0) {
        Serial.println("Chime: arquivo nao possui assinatura RIFF/WAVE valida.");
        return false;
    }

    bool encontrouFmt = false;
    bool encontrouData = false;

    while (arquivo.available() >= 8) {
        uint8_t chunkHdr[8];
        if (arquivo.read(chunkHdr, 8) != 8) {
            break;
        }

        uint32_t chunkSize = lerUint32Le(&chunkHdr[4]);

        if (memcmp(chunkHdr, "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                Serial.println("Chime: bloco fmt invalido.");
                return false;
            }

            uint8_t fmtData[16];
            if (arquivo.read(fmtData, 16) != 16) {
                return false;
            }

            info.formatoAudio = lerUint16Le(&fmtData[0]);
            info.canais = lerUint16Le(&fmtData[2]);
            info.taxaAmostragem = lerUint32Le(&fmtData[4]);
            info.bitsPorAmostra = lerUint16Le(&fmtData[14]);

            encontrouFmt = true;

            if (chunkSize > 16) {
                arquivo.seek(arquivo.position() + (chunkSize - 16));
            }
        } else if (memcmp(chunkHdr, "data", 4) == 0) {
            info.tamanhoDadosBytes = chunkSize;
            info.deslocamentoDados = arquivo.position();
            encontrouData = true;
            break;
        } else {
            // Pula chunks desconhecidos (LIST, JUNK, etc.)
            arquivo.seek(arquivo.position() + chunkSize);
        }
    }

    if (!encontrouFmt || !encontrouData) {
        Serial.println("Chime: blocos 'fmt ' ou 'data' ausentes no WAV.");
        return false;
    }

    if (info.formatoAudio != 1) {
        Serial.printf("Chime: formato de audio %d nao suportado (apenas PCM linear).\n", info.formatoAudio);
        return false;
    }

    if (info.bitsPorAmostra != 16) {
        Serial.printf("Chime: resolucao de %d bits nao suportada (necessario 16-bit PCM).\n", info.bitsPorAmostra);
        return false;
    }

    if (info.canais < 1 || info.canais > 2) {
        Serial.printf("Chime: quantidade de canais (%d) invalida (apenas mono ou estereo).\n", info.canais);
        return false;
    }

    return true;
}

} // namespace

bool iniciarChimeAudio(const char* caminhoArquivo) {
    descarregarChimeAudio();

    const char* caminho = (caminhoArquivo != nullptr && caminhoArquivo[0] != '\0')
                              ? caminhoArquivo
                              : CAMINHO_ARQUIVO_CHIME;

    fs::FS* fs = obterSistemaArquivosAudio();
    if (fs == nullptr) {
        Serial.println("Chime: microSD indisponivel para carregamento do chime.");
        return false;
    }

    if (!fs->exists(caminho)) {
        Serial.printf("Chime: arquivo '%s' nao encontrado no microSD.\n", caminho);
        return false;
    }

    File arquivo = fs->open(caminho, "r");
    if (!arquivo) {
        Serial.printf("Chime: falha ao abrir '%s'.\n", caminho);
        return false;
    }

    CabecalhoWavInfo info;
    if (!analisarCabecalhoWav(arquivo, info)) {
        arquivo.close();
        return false;
    }

    // Aloca memória na PSRAM
    bufferPcmPsram = static_cast<int16_t*>(
        heap_caps_malloc(info.tamanhoDadosBytes, MALLOC_CAP_SPIRAM)
    );

    if (bufferPcmPsram == nullptr) {
        Serial.printf("Chime: memoria PSRAM insuficiente (%u bytes necessarios).\n",
                      info.tamanhoDadosBytes);
        arquivo.close();
        return false;
    }

    arquivo.seek(info.deslocamentoDados);

    size_t lidosTotal = 0;
    uint8_t* ponteiroDestino = reinterpret_cast<uint8_t*>(bufferPcmPsram);

    while (lidosTotal < info.tamanhoDadosBytes && arquivo.available()) {
        size_t bloco = std::min<size_t>(4096, info.tamanhoDadosBytes - lidosTotal);
        size_t lidos = arquivo.read(ponteiroDestino + lidosTotal, bloco);
        if (lidos == 0) {
            break;
        }
        lidosTotal += lidos;
    }

    arquivo.close();

    if (lidosTotal < info.tamanhoDadosBytes) {
        Serial.printf("Chime: aviso: leitura incompleta (%u de %u bytes).\n",
                      lidosTotal, info.tamanhoDadosBytes);
    }

    canaisChime = info.canais;
    taxaAmostragemChime = info.taxaAmostragem;
    totalQuadrosPcm = lidosTotal / (info.canais * sizeof(int16_t));
    chimeCarregado = true;
    estadoAtual = EstadoChime::INATIVO;
    fatorDuckingAtual = 1.0f;

    float duracaoSegundos = static_cast<float>(totalQuadrosPcm) / static_cast<float>(taxaAmostragemChime);

    Serial.printf("Chime carregado em PSRAM: %s (%u Hz, %s, %.2f s, %u KB).\n",
                  caminho,
                  taxaAmostragemChime,
                  canaisChime == 1 ? "mono" : "estereo",
                  duracaoSegundos,
                  static_cast<unsigned int>(info.tamanhoDadosBytes / 1024));

    return true;
}

bool chimeDisponivel() {
    return chimeCarregado && bufferPcmPsram != nullptr && totalQuadrosPcm > 0;
}

bool dispararChime() {
    if (!chimeDisponivel()) {
        Serial.println("Chime: solicitacao de disparo rejeitada (audio nao carregado).");
        return false;
    }

    posicaoAmostraFp = 0;
    estadoAtual = EstadoChime::TOCANDO;
    Serial.println("Chime: reproducao iniciada no mixer.");
    return true;
}

bool chimeEmReproducao() {
    return estadoAtual != EstadoChime::INATIVO || fatorDuckingAtual < 0.999f;
}

void pararChime() {
    estadoAtual = EstadoChime::INATIVO;
    posicaoAmostraFp = 0;
    fatorDuckingAtual = 1.0f;
}

void descarregarChimeAudio() {
    pararChime();
    chimeCarregado = false;
    totalQuadrosPcm = 0;

    if (bufferPcmPsram != nullptr) {
        free(bufferPcmPsram);
        bufferPcmPsram = nullptr;
    }
}

void processarMixagemChime(
    int32_t* outBuff,
    int16_t validSamples,
    uint32_t taxaAmostragemStream
) {
    if (outBuff == nullptr || validSamples <= 0) {
        return;
    }

    if (estadoAtual == EstadoChime::INATIVO && fatorDuckingAtual >= 0.999f) {
        return;
    }

    uint32_t taxaStream = (taxaAmostragemStream > 0) ? taxaAmostragemStream : 44100;

    // Passo de avanço no buffer do chime em ponto fixo 16.16
    uint64_t passoFp = (static_cast<uint64_t>(taxaAmostragemChime) << 16) / taxaStream;

    // Rampas de atenuação e recuperação do áudio da rádio (ducking suave)
    // Rampa de descida ~50ms; rampa de subida ~150ms
    float passoDuckingDescida = (1.0f - FATOR_DUCKING_RADIO_CHIME) / (static_cast<float>(taxaStream) * 0.050f);
    float passoDuckingSubida  = (1.0f - FATOR_DUCKING_RADIO_CHIME) / (static_cast<float>(taxaStream) * 0.150f);

    for (int16_t i = 0; i < validSamples; i++) {
        // Atualiza a máquina de estados do ducking
        if (estadoAtual == EstadoChime::TOCANDO) {
            if (fatorDuckingAtual > FATOR_DUCKING_RADIO_CHIME) {
                fatorDuckingAtual = std::max(FATOR_DUCKING_RADIO_CHIME, fatorDuckingAtual - passoDuckingDescida);
            }

            size_t indiceQuadro = static_cast<size_t>(posicaoAmostraFp >> 16);
            if (indiceQuadro >= totalQuadrosPcm) {
                estadoAtual = EstadoChime::FINALIZANDO;
                Serial.println("Chime: amostras PCM finalizadas; restaurando volume da radio...");
            }
        } else if (estadoAtual == EstadoChime::FINALIZANDO || estadoAtual == EstadoChime::INATIVO) {
            if (fatorDuckingAtual < 1.0f) {
                fatorDuckingAtual = std::min(1.0f, fatorDuckingAtual + passoDuckingSubida);
            }
            if (fatorDuckingAtual >= 0.999f) {
                fatorDuckingAtual = 1.0f;
                if (estadoAtual != EstadoChime::INATIVO) {
                    estadoAtual = EstadoChime::INATIVO;
                    Serial.println("Chime: ciclo concluido; volume nominal da radio restaurado.");
                }
            }
        }

        // 1. Atenua o som da rádio web
        int64_t radioL = static_cast<int64_t>(static_cast<float>(outBuff[i * 2]) * fatorDuckingAtual);
        int64_t radioR = static_cast<int64_t>(static_cast<float>(outBuff[i * 2 + 1]) * fatorDuckingAtual);

        // 2. Se o chime estiver tocando, sobrepõe as amostras PCM
        if (estadoAtual == EstadoChime::TOCANDO && bufferPcmPsram != nullptr) {
            size_t idx = static_cast<size_t>(posicaoAmostraFp >> 16);

            if (idx < totalQuadrosPcm) {
                int32_t chimeL32 = 0;
                int32_t chimeR32 = 0;

                if (canaisChime == 1) {
                    int16_t s = bufferPcmPsram[idx];
                    chimeL32 = static_cast<int32_t>(s) << 16;
                    chimeR32 = chimeL32;
                } else {
                    int16_t sL = bufferPcmPsram[idx * 2];
                    int16_t sR = bufferPcmPsram[idx * 2 + 1];
                    chimeL32 = static_cast<int32_t>(sL) << 16;
                    chimeR32 = static_cast<int32_t>(sR) << 16;
                }

                int64_t chimeL = static_cast<int64_t>(static_cast<float>(chimeL32) * VOLUME_CHIME_PADRAO);
                int64_t chimeR = static_cast<int64_t>(static_cast<float>(chimeR32) * VOLUME_CHIME_PADRAO);

                int64_t saidaL = radioL + chimeL;
                int64_t saidaR = radioR + chimeR;

                // Proteção contra saturação (clipping)
                saidaL = std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, saidaL));
                saidaR = std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, saidaR));

                outBuff[i * 2]     = static_cast<int32_t>(saidaL);
                outBuff[i * 2 + 1] = static_cast<int32_t>(saidaR);

                posicaoAmostraFp += passoFp;
            }
        } else {
            // Apenas aplica o ducking na rádio durante a rampa de retorno
            outBuff[i * 2]     = static_cast<int32_t>(radioL);
            outBuff[i * 2 + 1] = static_cast<int32_t>(radioR);
        }
    }
}

EstadoChime obterEstadoChime() {
    return estadoAtual;
}

const char* obterTextoEstadoChime() {
    if (!chimeCarregado) {
        return "descarregado";
    }
    switch (estadoAtual) {
        case EstadoChime::INATIVO:
            return "inativo";
        case EstadoChime::TOCANDO:
            return "tocando";
        case EstadoChime::FINALIZANDO:
            return "finalizando";
    }
    return "desconhecido";
}

float obterFatorDuckingAtual() {
    return fatorDuckingAtual;
}

uint32_t obterTaxaAmostragemChime() {
    return taxaAmostragemChime;
}

uint16_t obterCanaisChime() {
    return canaisChime;
}

float obterDuracaoChimeSegundos() {
    if (taxaAmostragemChime == 0) {
        return 0.0f;
    }
    return static_cast<float>(totalQuadrosPcm) / static_cast<float>(taxaAmostragemChime);
}

size_t obterMemoriaChimeBytes() {
    return totalQuadrosPcm * canaisChime * sizeof(int16_t);
}
