#include "servidor_web.h"
#include "api_status.h"
#include "persistencia_radios.h"
#include "radios.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FFat.h>
#include <WiFi.h>
#include <WebServer.h>

namespace {

WebServer servidor(80);

const char* ARQUIVO_PAGINA_PRINCIPAL = "/index.html";
const char* ARQUIVO_PAGINA_UPLOAD = "/upload.html";
const char* ARQUIVO_UPLOAD_TEMPORARIO = "/.upload.tmp";
const char* ARQUIVO_UPLOAD_RESERVA = "/.upload.bak";

File arquivoUpload;

String destinoUpload;
String erroUpload;

bool uploadConcluido = false;

const char PAGINA_RECUPERACAO_UPLOAD[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Recuperação de arquivos</title>
</head>
<body>
    <h1>Recuperação de arquivos</h1>

    <p>
        O arquivo upload.html não foi encontrado na FFat.
        Use este formulário mínimo para restaurá-lo.
    </p>

    <form
        method="post"
        action="/upload"
        enctype="multipart/form-data"
    >
        <input
            type="file"
            name="arquivo"
            required
        >

        <button type="submit">Enviar arquivo</button>
    </form>
</body>
</html>
)HTML";

void listarRadios() {
    const char* caminho = nullptr;

    if (
        arquivoRadiosValido(
            CAMINHO_RADIOS_ATIVO
        )
    ) {
        caminho = CAMINHO_RADIOS_ATIVO;
    } else if (
        arquivoRadiosValido(
            CAMINHO_RADIOS_BACKUP
        )
    ) {
        caminho = CAMINHO_RADIOS_BACKUP;
    }

    if (caminho == nullptr) {
        servidor.send(
            200,
            "application/json",
            "[]"
        );

        return;
    }

    File arquivo =
        FFat.open(
            caminho,
            FILE_READ
        );

    if (!arquivo) {
        servidor.send(
            200,
            "application/json",
            "[]"
        );

        return;
    }

    servidor.streamFile(
        arquivo,
        "application/json"
    );

    arquivo.close();
}

void adicionarRadio() {
    if (!servidor.hasArg("plain")) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"Dados não enviados\"}"
        );

        return;
    }

    DynamicJsonDocument novaRadio(1024);

    DeserializationError erro =
        deserializeJson(
            novaRadio,
            servidor.arg("plain")
        );

    if (erro) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"JSON inválido\"}"
        );

        return;
    }

    const char* nome =
        novaRadio["nome"];

    const char* url =
        novaRadio["url"];

    int estrelas =
        novaRadio["estrelas"] | 0;

    if (
        !dadosRadioValidos(nome, url) ||
        estrelas < 1 ||
        estrelas > 5
    ) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"Dados inválidos\"}"
        );

        return;
    }

    DynamicJsonDocument documento(16384);

    if (!carregarDocumentoRadiosParaEdicao(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao ler as rádios\"}"
        );

        return;
    }

    JsonArray radios =
        documento.as<JsonArray>();

    if (
        radios.size() >=
        QUANTIDADE_MAXIMA_RADIOS
    ) {
        servidor.send(
            409,
            "application/json",
            "{\"erro\":\"Limite de rádios atingido\"}"
        );

        return;
    }

    unsigned long maiorId = 0;

    for (JsonObject radio : radios) {
        unsigned long id =
            radio["id"] | 0;

        if (id > maiorId) {
            maiorId = id;
        }
    }

    JsonObject radio =
        radios.createNestedObject();

    radio["id"] = maiorId + 1;
    radio["nome"] = nome;
    radio["url"] = url;
    radio["estrelas"] = estrelas;

    if (!salvarDocumentoRadios(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao salvar a rádio\"}"
        );

        return;
    }

    servidor.send(
        201,
        "application/json",
        "{\"sucesso\":true}"
    );
}

void excluirRadio() {
    if (!servidor.hasArg("id")) {
        servidor.send(
            400,
            "application/json",
            "{\"erro\":\"ID não informado\"}"
        );

        return;
    }

    unsigned long idProcurado =
        servidor.arg("id").toInt();

    DynamicJsonDocument documento(16384);

    if (!carregarDocumentoRadiosParaEdicao(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao ler as rádios\"}"
        );

        return;
    }

    JsonArray radios =
        documento.as<JsonArray>();

    bool encontrou = false;

    for (
        size_t indice = 0;
        indice < radios.size();
        indice++
    ) {
        unsigned long id =
            radios[indice]["id"] | 0;

        if (id == idProcurado) {
            radios.remove(indice);
            encontrou = true;

            break;
        }
    }

    if (!encontrou) {
        servidor.send(
            404,
            "application/json",
            "{\"erro\":\"Rádio não encontrada\"}"
        );

        return;
    }

    if (radios.size() == 0) {
        servidor.send(
            409,
            "application/json",
            "{\"erro\":\"O rádio precisa manter ao menos uma estação\"}"
        );

        return;
    }

    if (!salvarDocumentoRadios(documento)) {
        servidor.send(
            500,
            "application/json",
            "{\"erro\":\"Falha ao salvar as alterações\"}"
        );

        return;
    }

    servidor.send(
        200,
        "application/json",
        "{\"sucesso\":true}"
    );
}

bool enviarPaginaHtmlDaFfat(const char* caminho) {
    File arquivo =
        FFat.open(
            caminho,
            FILE_READ
        );

    if (!arquivo) {
        return false;
    }

    servidor.streamFile(
        arquivo,
        "text/html; charset=utf-8"
    );

    arquivo.close();

    return true;
}

void abrirPagina() {
    if (enviarPaginaHtmlDaFfat(ARQUIVO_PAGINA_PRINCIPAL)) {
        return;
    }

    servidor.send(
        404,
        "text/plain",
        "Arquivo index.html não encontrado"
    );
}

bool caracterePermitido(char caractere) {
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
        ultimaBarraNormal >
        ultimaBarraInvertida
            ? ultimaBarraNormal
            : ultimaBarraInvertida;

    String nome =
        nomeOriginal.substring(
            ultimaBarra + 1
        );

    nome.trim();

    if (
        nome.length() == 0 ||
        nome == "." ||
        nome == ".." ||
        nome.startsWith(".") ||
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
        if (!caracterePermitido(nome[indice])) {
            return "";
        }
    }

    return nome;
}

void abrirPaginaUpload() {
    if (enviarPaginaHtmlDaFfat(ARQUIVO_PAGINA_UPLOAD)) {
        return;
    }

    servidor.send_P(
        200,
        "text/html; charset=utf-8",
        PAGINA_RECUPERACAO_UPLOAD
    );
}

void listarArquivos() {
    DynamicJsonDocument documento(4096);

    JsonArray lista =
        documento.to<JsonArray>();

    File raiz =
        FFat.open("/");

    if (raiz) {
        File arquivo =
            raiz.openNextFile();

        while (arquivo) {
            if (!arquivo.isDirectory()) {
                JsonObject item =
                    lista.createNestedObject();

                item["nome"] =
                    arquivo.name();

                item["tamanho"] =
                    arquivo.size();
            }

            arquivo.close();
            arquivo = raiz.openNextFile();
        }

        raiz.close();
    }

    String resposta;

    serializeJson(
        documento,
        resposta
    );

    servidor.send(
        200,
        "application/json",
        resposta
    );
}

void receberUpload() {
    HTTPUpload& upload =
        servidor.upload();

    if (
        upload.status ==
        UPLOAD_FILE_START
    ) {
        uploadConcluido = false;
        erroUpload = "";
        destinoUpload = "";

        if (arquivoUpload) {
            arquivoUpload.close();
        }

        String nome =
            obterNomeArquivoSeguro(
                upload.filename
            );

        if (nome.length() == 0) {
            erroUpload =
                "Nome de arquivo inválido";

            return;
        }

        destinoUpload =
            "/" + nome;

        FFat.remove(
            ARQUIVO_UPLOAD_TEMPORARIO
        );

        arquivoUpload =
            FFat.open(
                ARQUIVO_UPLOAD_TEMPORARIO,
                FILE_WRITE
            );

        if (!arquivoUpload) {
            erroUpload =
                "Não foi possível criar o arquivo temporário";
        }

        return;
    }

    if (
        upload.status ==
        UPLOAD_FILE_WRITE
    ) {
        if (
            erroUpload.length() > 0 ||
            !arquivoUpload
        ) {
            return;
        }

        size_t gravados =
            arquivoUpload.write(
                upload.buf,
                upload.currentSize
            );

        if (
            gravados !=
            upload.currentSize
        ) {
            erroUpload =
                "Falha ao gravar os dados";
        }

        return;
    }

    if (
        upload.status ==
        UPLOAD_FILE_END
    ) {
        if (arquivoUpload) {
            arquivoUpload.close();
        }

        if (erroUpload.length() > 0) {
            FFat.remove(
                ARQUIVO_UPLOAD_TEMPORARIO
            );

            return;
        }

        if (destinoUpload == CAMINHO_RADIOS_ATIVO) {
            DynamicJsonDocument documento(16384);

            if (
                !carregarDocumentoRadiosDoArquivo(
                    documento,
                    ARQUIVO_UPLOAD_TEMPORARIO
                )
            ) {
                erroUpload =
                    "O arquivo radios.json é inválido";

                FFat.remove(
                    ARQUIVO_UPLOAD_TEMPORARIO
                );

                return;
            }

            if (!salvarDocumentoRadios(documento)) {
                erroUpload =
                    "Não foi possível salvar radios.json";

                FFat.remove(
                    ARQUIVO_UPLOAD_TEMPORARIO
                );

                return;
            }

            FFat.remove(
                ARQUIVO_UPLOAD_TEMPORARIO
            );

            uploadConcluido = true;

            Serial.println(
                "radios.json validado e atualizado."
            );

            return;
        }

        FFat.remove(
            ARQUIVO_UPLOAD_RESERVA
        );

        bool arquivoAnteriorExiste =
            FFat.exists(
                destinoUpload.c_str()
            );

        if (
            arquivoAnteriorExiste &&
            !FFat.rename(
                destinoUpload.c_str(),
                ARQUIVO_UPLOAD_RESERVA
            )
        ) {
            erroUpload =
                "Não foi possível preservar o arquivo anterior";

            FFat.remove(
                ARQUIVO_UPLOAD_TEMPORARIO
            );

            return;
        }

        if (
            !FFat.rename(
                ARQUIVO_UPLOAD_TEMPORARIO,
                destinoUpload.c_str()
            )
        ) {
            erroUpload =
                "Não foi possível concluir a substituição";

            if (arquivoAnteriorExiste) {
                FFat.rename(
                    ARQUIVO_UPLOAD_RESERVA,
                    destinoUpload.c_str()
                );
            }

            FFat.remove(
                ARQUIVO_UPLOAD_TEMPORARIO
            );

            return;
        }

        FFat.remove(
            ARQUIVO_UPLOAD_RESERVA
        );

        uploadConcluido = true;

        Serial.print(
            "Arquivo enviado: "
        );

        Serial.println(
            destinoUpload
        );

        return;
    }

    if (
        upload.status ==
        UPLOAD_FILE_ABORTED
    ) {
        if (arquivoUpload) {
            arquivoUpload.close();
        }

        FFat.remove(
            ARQUIVO_UPLOAD_TEMPORARIO
        );

        erroUpload =
            "Upload interrompido";
    }
}

void finalizarUpload() {
    if (
        uploadConcluido &&
        erroUpload.length() == 0
    ) {
        servidor.send(
            200,
            "application/json",
            "{\"sucesso\":true}"
        );

        return;
    }

    String mensagemErro =
        erroUpload.length() > 0
            ? erroUpload
            : "Upload não concluído";

    DynamicJsonDocument documento(256);

    documento["erro"] =
        mensagemErro;

    String resposta;

    serializeJson(
        documento,
        resposta
    );

    servidor.send(
        500,
        "application/json",
        resposta
    );
}

void responderStatusSistema() {
    servidor.send(
        200,
        "application/json",
        criarStatusSistemaJson()
    );
}

}

bool iniciarServidorWeb() {
    if (!FFat.begin(false)) {
        Serial.println(
            "Falha ao iniciar o FFat."
        );

        return false;
    }

    servidor.on(
        "/",
        HTTP_GET,
        abrirPagina
    );

    servidor.on(
        "/api/radios",
        HTTP_GET,
        listarRadios
    );

    servidor.on(
        "/api/radios",
        HTTP_POST,
        adicionarRadio
    );

    servidor.on(
        "/api/radios",
        HTTP_DELETE,
        excluirRadio
    );

    servidor.on(
        "/api/v1/status",
        HTTP_GET,
        responderStatusSistema
    );

    servidor.on(
        "/upload",
        HTTP_GET,
        abrirPaginaUpload
    );

    servidor.on(
        "/api/arquivos",
        HTTP_GET,
        listarArquivos
    );

    servidor.on(
        "/upload",
        HTTP_POST,
        finalizarUpload,
        receberUpload
    );

    servidor.onNotFound([]() {
        servidor.send(
            404,
            "application/json",
            "{\"erro\":\"Endereço não encontrado\"}"
        );
    });

    servidor.begin();

    Serial.println(
        "Servidor web iniciado."
    );

    Serial.print(
        "Acesse: http://"
    );

    Serial.println(
        WiFi.localIP()
    );

    Serial.print(
        "Upload: http://"
    );

    Serial.print(
        WiFi.localIP()
    );

    Serial.println(
        "/upload"
    );

    return true;
}

void processarServidorWeb() {
    servidor.handleClient();
}
