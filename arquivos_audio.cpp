#include "arquivos_audio.h"

#include "configuracao.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <algorithm>

const char* const DIRETORIO_ARQUIVOS_AUDIO =
    "/sons";

namespace {

bool cartaoMontado = false;

bool pinosCartaoConfigurados() {
    return
        PIN_CARTAO_MICRO_SD_SCK >= 0 &&
        PIN_CARTAO_MICRO_SD_MISO >= 0 &&
        PIN_CARTAO_MICRO_SD_MOSI >= 0 &&
        PIN_CARTAO_MICRO_SD_CS >= 0;
}

}

bool caminhoArquivoAudioSuportado(
    const char* caminho
) {
    if (
        caminho == nullptr ||
        caminho[0] != '/'
    ) {
        return false;
    }

    String caminhoNormalizado = caminho;
    caminhoNormalizado.toLowerCase();

    String prefixoDiretorio =
        String(DIRETORIO_ARQUIVOS_AUDIO) + "/";

    if (
        !caminhoNormalizado.startsWith(prefixoDiretorio) ||
        caminhoNormalizado.indexOf("..") >= 0
    ) {
        return false;
    }

    return
        caminhoNormalizado.endsWith(".mp3") ||
        caminhoNormalizado.endsWith(".m4a") ||
        caminhoNormalizado.endsWith(".aac") ||
        caminhoNormalizado.endsWith(".wav") ||
        caminhoNormalizado.endsWith(".flac") ||
        caminhoNormalizado.endsWith(".ogg") ||
        caminhoNormalizado.endsWith(".oga") ||
        caminhoNormalizado.endsWith(".opus");
}

bool iniciarArquivosAudio() {
    if (cartaoMontado) {
        return true;
    }

    if (!pinosCartaoConfigurados()) {
        Serial.println(
            "Cartao microSD ainda sem pinos configurados."
        );

        return false;
    }

    pinMode(PIN_CARTAO_MICRO_SD_CS, OUTPUT);
    digitalWrite(PIN_CARTAO_MICRO_SD_CS, HIGH);

    SPI.begin(
        PIN_CARTAO_MICRO_SD_SCK,
        PIN_CARTAO_MICRO_SD_MISO,
        PIN_CARTAO_MICRO_SD_MOSI,
        PIN_CARTAO_MICRO_SD_CS
    );

    if (
        !SD.begin(
            PIN_CARTAO_MICRO_SD_CS,
            SPI,
            FREQUENCIA_CARTAO_MICRO_SD_HZ
        )
    ) {
        Serial.println(
            "Falha ao montar o cartao microSD."
        );
        Serial.println(
            "Confira SCK=11, MISO=12, MOSI=13, CS=14 e FAT32."
        );
        SPI.end();

        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        Serial.println(
            "Nenhum cartao microSD encontrado."
        );
        SD.end();
        SPI.end();

        return false;
    }

    if (
        !SD.exists(DIRETORIO_ARQUIVOS_AUDIO) &&
        !SD.mkdir(DIRETORIO_ARQUIVOS_AUDIO)
    ) {
        Serial.println(
            "Falha ao criar o diretorio /sons no microSD."
        );
        SD.end();
        SPI.end();

        return false;
    }

    File diretorio =
        SD.open(DIRETORIO_ARQUIVOS_AUDIO);

    bool diretorioValido =
        diretorio && diretorio.isDirectory();

    diretorio.close();

    if (!diretorioValido) {
        Serial.println(
            "/sons existe, mas nao e um diretorio valido."
        );
        SD.end();
        SPI.end();

        return false;
    }

    cartaoMontado = true;

    Serial.println(
        "Cartao microSD pronto; arquivos de audio em /sons."
    );

    return true;
}

bool arquivosAudioDisponiveis() {
    return cartaoMontado;
}

bool obterListaArquivosAudio(
    std::vector<ArquivoAudioDisponivel>& arquivos
) {
    arquivos.clear();

    if (!cartaoMontado) {
        return false;
    }

    File diretorio =
        SD.open(DIRETORIO_ARQUIVOS_AUDIO);

    if (!diretorio || !diretorio.isDirectory()) {
        diretorio.close();
        return false;
    }

    File arquivo = diretorio.openNextFile();

    while (arquivo) {
        if (
            !arquivo.isDirectory() &&
            caminhoArquivoAudioSuportado(
                arquivo.path()
            )
        ) {
            ArquivoAudioDisponivel item;
            item.caminho = arquivo.path();
            item.tamanhoBytes = arquivo.size();
            arquivos.push_back(item);
        }

        arquivo.close();
        arquivo = diretorio.openNextFile();
    }

    diretorio.close();

    std::sort(
        arquivos.begin(),
        arquivos.end(),
        [](
            const ArquivoAudioDisponivel& esquerda,
            const ArquivoAudioDisponivel& direita
        ) {
            return
                esquerda.caminho.compareTo(
                    direita.caminho
                ) < 0;
        }
    );

    return true;
}

fs::FS* obterSistemaArquivosAudio() {
    if (!cartaoMontado) {
        return nullptr;
    }

    return &SD;
}
