#include "upload_arquivos.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FFat.h>
#include <WebServer.h>

#include "alarmes.h"
#include "persistencia_alarmes.h"
#include "persistencia_radios.h"

namespace {

const char* const CAMINHO_UPLOAD_TEMPORARIO =
    "/.upload.tmp";

const char* const CAMINHO_UPLOAD_BACKUP =
    "/.upload.bak";

File arquivoRecebido;

String caminhoDestino;
String mensagemErro;

bool uploadConcluido = false;

bool caracterePermitidoNoNome(char caractere) {
    return
        (
            caractere >= 'a' &&
            caractere <= 'z'
        ) ||
        (
            caractere >= 'A' &&
            caractere <= 'Z'
        ) ||
        (
            caractere >= '0' &&
            caractere <= '9'
        ) ||
        caractere == '.' ||
        caractere == '-' ||
        caractere == '_';
}

String obterNomeArquivoSeguro(
    const String& nomeOriginal
) {
    int ultimaBarraNormal =
        nomeOriginal.lastIndexOf('/');

    int ultimaBarraInvertida =
        nomeOriginal.lastIndexOf('\\');

    int ultimaBarra =
        ultimaBarraNormal > ultimaBarraInvertida
            ? ultimaBarraNormal
            : ultimaBarraInvertida;

    String nome =
        nomeOriginal.substring(ultimaBarra + 1);

    nome.trim();

    if (
        nome.length() == 0 ||
        nome == "." ||
        nome == ".." ||
        nome.startsWith(".") ||
        nome == "alarme_padrao.wav" ||
        nome == "alarmes.tmp" ||
        nome == "alarmes.bak" ||
        nome == "radios.tmp" ||
        nome == "radios.bak"
    ) {
        return "";
    }

    for (
        size_t indice = 0;
        indice < nome.length();
        indice++
    ) {
        if (!caracterePermitidoNoNome(nome[indice])) {
            return "";
        }
    }

    return nome;
}

void iniciarRecebimento(const HTTPUpload& upload) {
    uploadConcluido = false;
    mensagemErro = "";
    caminhoDestino = "";

    if (arquivoRecebido) {
        arquivoRecebido.close();
    }

    String nome =
        obterNomeArquivoSeguro(upload.filename);

    if (nome.length() == 0) {
        mensagemErro = "Nome de arquivo inválido";

        return;
    }

    caminhoDestino = "/" + nome;

    FFat.remove(CAMINHO_UPLOAD_TEMPORARIO);

    arquivoRecebido =
        FFat.open(
            CAMINHO_UPLOAD_TEMPORARIO,
            FILE_WRITE
        );

    if (!arquivoRecebido) {
        mensagemErro =
            "Não foi possível criar o arquivo temporário";
    }
}

void gravarParteRecebida(const HTTPUpload& upload) {
    if (
        mensagemErro.length() > 0 ||
        !arquivoRecebido
    ) {
        return;
    }

    size_t quantidadeGravada =
        arquivoRecebido.write(
            upload.buf,
            upload.currentSize
        );

    if (quantidadeGravada != upload.currentSize) {
        mensagemErro = "Falha ao gravar os dados";
    }
}

bool salvarListaRadiosRecebida() {
    JsonDocument documento;

    if (
        !carregarDocumentoRadiosDoArquivo(
            documento,
            CAMINHO_UPLOAD_TEMPORARIO
        )
    ) {
        mensagemErro =
            "O arquivo radios.json é inválido";

        return false;
    }

    if (!salvarDocumentoRadios(documento)) {
        mensagemErro =
            "Não foi possível salvar radios.json";

        return false;
    }

    Serial.println(
        "radios.json validado e atualizado."
    );

    return true;
}

bool salvarListaAlarmesRecebida() {
    JsonDocument documento;

    if (
        !carregarDocumentoAlarmesDoArquivo(
            documento,
            CAMINHO_UPLOAD_TEMPORARIO
        )
    ) {
        mensagemErro =
            "O arquivo alarmes.json é inválido";

        return false;
    }

    if (!salvarDocumentoAlarmes(documento)) {
        mensagemErro =
            "Não foi possível salvar alarmes.json";

        return false;
    }

    carregarAlarmes();
    Serial.println(
        "alarmes.json validado e atualizado."
    );

    return true;
}

bool substituirArquivoComum() {
    FFat.remove(CAMINHO_UPLOAD_BACKUP);

    bool arquivoAnteriorExiste =
        FFat.exists(caminhoDestino.c_str());

    if (
        arquivoAnteriorExiste &&
        !FFat.rename(
            caminhoDestino.c_str(),
            CAMINHO_UPLOAD_BACKUP
        )
    ) {
        mensagemErro =
            "Não foi possível preservar o arquivo anterior";

        return false;
    }

    if (
        !FFat.rename(
            CAMINHO_UPLOAD_TEMPORARIO,
            caminhoDestino.c_str()
        )
    ) {
        mensagemErro =
            "Não foi possível concluir a substituição";

        if (arquivoAnteriorExiste) {
            FFat.rename(
                CAMINHO_UPLOAD_BACKUP,
                caminhoDestino.c_str()
            );
        }

        return false;
    }

    FFat.remove(CAMINHO_UPLOAD_BACKUP);

    Serial.print("Arquivo enviado: ");
    Serial.println(caminhoDestino);

    return true;
}

void concluirRecebimento() {
    if (arquivoRecebido) {
        arquivoRecebido.close();
    }

    if (mensagemErro.length() > 0) {
        FFat.remove(CAMINHO_UPLOAD_TEMPORARIO);

        return;
    }

    bool salvo;

    if (caminhoDestino == CAMINHO_RADIOS_ATIVO) {
        salvo = salvarListaRadiosRecebida();
    } else if (caminhoDestino == CAMINHO_ALARMES_ATIVO) {
        salvo = salvarListaAlarmesRecebida();
    } else {
        salvo = substituirArquivoComum();
    }

    FFat.remove(CAMINHO_UPLOAD_TEMPORARIO);

    uploadConcluido = salvo;
}

void cancelarRecebimento() {
    if (arquivoRecebido) {
        arquivoRecebido.close();
    }

    FFat.remove(CAMINHO_UPLOAD_TEMPORARIO);

    mensagemErro = "Upload interrompido";
}

}

void processarDadosUpload(WebServer& servidor) {
    HTTPUpload& upload = servidor.upload();

    if (upload.status == UPLOAD_FILE_START) {
        iniciarRecebimento(upload);

        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        gravarParteRecebida(upload);

        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        concluirRecebimento();

        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        cancelarRecebimento();
    }
}

void responderResultadoUpload(WebServer& servidor) {
    if (
        uploadConcluido &&
        mensagemErro.length() == 0
    ) {
        servidor.send(
            200,
            "application/json",
            "{\"sucesso\":true}"
        );

        return;
    }

    String erro =
        mensagemErro.length() > 0
            ? mensagemErro
            : "Upload não concluído";

    JsonDocument documento;

    documento["erro"] = erro;

    String resposta;

    serializeJson(documento, resposta);

    servidor.send(
        500,
        "application/json",
        resposta
    );
}
