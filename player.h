#ifndef PLAYER_H
#define PLAYER_H

#include <Arduino.h>
#include <FS.h>

constexpr size_t TAMANHO_MAXIMO_CAMINHO_PLAYER = 256;

struct ArquivoPlayer {
    String nome;
    String caminho;
};

// Monta o cartão e cria uma única fotografia do catálogo MP3 em /sons.
bool prepararPlayer();

int obterQuantidadeArquivosPlayer();
const ArquivoPlayer* obterArquivoPlayer(int indice);

bool caminhoPlayerValido(const char* caminho);

// Uso exclusivo do serviço de áudio durante a reprodução.
fs::FS* obterSistemaArquivosPlayer();

#endif
