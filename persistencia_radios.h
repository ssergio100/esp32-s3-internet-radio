#ifndef PERSISTENCIA_RADIOS_H
#define PERSISTENCIA_RADIOS_H

#include <ArduinoJson.h>

// Nomes únicos dos arquivos persistentes usados por todos os módulos.
extern const char* const CAMINHO_RADIOS_ATIVO;
extern const char* const CAMINHO_RADIOS_BACKUP;

enum class OrigemArquivoRadios {
    ATIVO,
    BACKUP,
    NENHUM_VALIDO
};

// Carrega e valida um documento de rádios a partir do caminho informado.
bool carregarDocumentoRadiosDoArquivo(
    DynamicJsonDocument& documento,
    const char* caminho
);

// Aplica a ordem única de recuperação: arquivo ativo e depois backup.
OrigemArquivoRadios carregarDocumentoRadiosPersistido(
    DynamicJsonDocument& documento
);

// Carrega a lista ativa, depois o backup. Se ambos não existirem, cria um
// array vazio para permitir o primeiro cadastro pela API.
bool carregarDocumentoRadiosParaEdicao(
    DynamicJsonDocument& documento
);

// Valida e promove o documento usando temporário e backup.
bool salvarDocumentoRadios(
    DynamicJsonDocument& documento
);

#endif
