#ifndef ARQUIVOS_AUDIO_H
#define ARQUIVOS_AUDIO_H

#include <Arduino.h>
#include <FS.h>
#include <vector>

extern const char* const DIRETORIO_ARQUIVOS_AUDIO;
constexpr size_t TAMANHO_MAXIMO_CAMINHO_ARQUIVO_AUDIO = 256;

struct ArquivoAudioDisponivel {
    String caminho;
    uint64_t tamanhoBytes = 0;
};

// Monta o cartão e cria /sons quando necessário. Uma configuração de pinos
// incompleta apenas mantém o recurso indisponível; não impede o rádio de ligar.
bool iniciarArquivosAudio();

bool arquivosAudioDisponiveis();

// Substitui o conteúdo de "arquivos" pela lista alfabética dos arquivos de
// áudio diretamente dentro de /sons. Retorna false quando o cartão não está
// disponível ou o diretório não pode ser lido.
bool obterListaArquivosAudio(
    std::vector<ArquivoAudioDisponivel>& arquivos
);

bool caminhoArquivoAudioSuportado(
    const char* caminho
);

// Uso interno do serviço dedicado de áudio. Os demais módulos não devem operar
// diretamente o sistema de arquivos enquanto uma faixa estiver tocando.
fs::FS* obterSistemaArquivosAudio();

#endif
