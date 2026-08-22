#include "player.h"

#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <vector>

#include "configuracao.h"

namespace {

constexpr const char* DIRETORIO_PLAYER = "/sons";
constexpr size_t QUANTIDADE_MAXIMA_ARQUIVOS = 100;

bool cartaoMontado = false;
bool catalogoCarregado = false;
std::vector<ArquivoPlayer> arquivos;

bool montarCartao() {
    if (cartaoMontado && SD.cardType() != CARD_NONE) {
        return true;
    }

    SD.end();
    SPI.end();

    pinMode(PIN_CARTAO_PLAYER_CS, OUTPUT);
    digitalWrite(PIN_CARTAO_PLAYER_CS, HIGH);

    SPI.begin(
        PIN_CARTAO_PLAYER_SCK,
        PIN_CARTAO_PLAYER_MISO,
        PIN_CARTAO_PLAYER_MOSI,
        PIN_CARTAO_PLAYER_CS
    );

    if (!SD.begin(
        PIN_CARTAO_PLAYER_CS,
        SPI,
        FREQUENCIA_CARTAO_PLAYER_HZ
    )) {
        Serial.println("Player: falha ao montar o cartao microSD.");
        SPI.end();
        cartaoMontado = false;
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        Serial.println("Player: cartao microSD ausente.");
        SD.end();
        SPI.end();
        cartaoMontado = false;
        return false;
    }

    if (
        !SD.exists(DIRETORIO_PLAYER) &&
        !SD.mkdir(DIRETORIO_PLAYER)
    ) {
        Serial.println("Player: nao foi possivel criar /sons.");
        SD.end();
        SPI.end();
        cartaoMontado = false;
        return false;
    }

    cartaoMontado = true;
    Serial.println("Player: cartao microSD montado.");
    return true;
}

bool carregarCatalogo() {
    arquivos.clear();
    arquivos.reserve(QUANTIDADE_MAXIMA_ARQUIVOS);

    File diretorio = SD.open(DIRETORIO_PLAYER);

    if (!diretorio || !diretorio.isDirectory()) {
        diretorio.close();
        Serial.println("Player: diretorio /sons indisponivel.");
        return false;
    }

    File arquivo = diretorio.openNextFile();

    while (arquivo) {
        if (
            !arquivo.isDirectory() &&
            caminhoPlayerValido(arquivo.path()) &&
            arquivos.size() < QUANTIDADE_MAXIMA_ARQUIVOS
        ) {
            ArquivoPlayer item;
            item.caminho = arquivo.path();

            int ultimaBarra = item.caminho.lastIndexOf('/');
            item.nome = item.caminho.substring(ultimaBarra + 1);
            arquivos.push_back(item);
        }

        arquivo.close();
        arquivo = diretorio.openNextFile();
    }

    diretorio.close();

    std::sort(
        arquivos.begin(),
        arquivos.end(),
        [](const ArquivoPlayer& esquerda, const ArquivoPlayer& direita) {
            return esquerda.nome.compareTo(direita.nome) < 0;
        }
    );

    Serial.printf(
        "Player: %u arquivo(s) MP3 encontrado(s) em /sons.\n",
        static_cast<unsigned int>(arquivos.size())
    );

    if (arquivos.size() == QUANTIDADE_MAXIMA_ARQUIVOS) {
        Serial.println("Player: catalogo limitado aos primeiros 100 MP3.");
    }

    return true;
}

}

bool prepararPlayer() {
    if (catalogoCarregado) {
        return true;
    }

    if (!montarCartao() || !carregarCatalogo()) {
        return false;
    }

    catalogoCarregado = true;
    return true;
}

int obterQuantidadeArquivosPlayer() {
    return static_cast<int>(arquivos.size());
}

const ArquivoPlayer* obterArquivoPlayer(int indice) {
    if (indice < 0 || indice >= obterQuantidadeArquivosPlayer()) {
        return nullptr;
    }

    return &arquivos[indice];
}

bool caminhoPlayerValido(const char* caminho) {
    if (caminho == nullptr || caminho[0] != '/') {
        return false;
    }

    String normalizado = caminho;
    normalizado.toLowerCase();

    return
        normalizado.startsWith("/sons/") &&
        normalizado.indexOf("..") < 0 &&
        normalizado.endsWith(".mp3");
}

fs::FS* obterSistemaArquivosPlayer() {
    return cartaoMontado ? &SD : nullptr;
}
