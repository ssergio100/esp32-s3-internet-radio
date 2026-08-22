#ifndef PERSISTENCIA_ALARMES_H
#define PERSISTENCIA_ALARMES_H

#include <ArduinoJson.h>

extern const char* const CAMINHO_ALARMES_ATIVO;
extern const char* const CAMINHO_ALARMES_BACKUP;

enum class OrigemArquivoAlarmes {
    ATIVO,
    BACKUP,
    NENHUM_VALIDO
};

bool documentoAlarmesValido(JsonDocument& documento);

bool carregarDocumentoAlarmesDoArquivo(
    JsonDocument& documento,
    const char* caminho
);

OrigemArquivoAlarmes carregarDocumentoAlarmesPersistido(
    JsonDocument& documento
);

// Se os dois arquivos ainda não existirem, cria um documento vazio válido.
bool carregarDocumentoAlarmesParaEdicao(
    JsonDocument& documento
);

bool salvarDocumentoAlarmes(JsonDocument& documento);

#endif
