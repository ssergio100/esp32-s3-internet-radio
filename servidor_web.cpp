#include "servidor_web.h"
#include "api_alarmes.h"
#include "api_radios.h"
#include "api_status.h"
#include "upload_arquivos.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FFat.h>
#include <WiFi.h>
#include <WebServer.h>

namespace {

WebServer servidor(80);
bool servidorAtivo = false;

const char* ARQUIVO_PAGINA_PRINCIPAL = "/index.html";
const char* ARQUIVO_PAGINA_UPLOAD = "/upload.html";
const char* ARQUIVO_PAGINA_ALARMES = "/alarmes.html";

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

void abrirPaginaAlarmes() {
    if (enviarPaginaHtmlDaFfat(ARQUIVO_PAGINA_ALARMES)) {
        return;
    }

    servidor.send(
        404,
        "text/plain",
        "Arquivo alarmes.html não encontrado"
    );
}

void listarArquivos() {
    JsonDocument documento;

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
                    lista.add<JsonObject>();

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

void responderStatusSistema() {
    servidor.send(
        200,
        "application/json",
        criarStatusSistemaJson()
    );
}

}

bool iniciarArmazenamentoEServidorWeb() {
    // Um ESP32 novo ainda não possui um sistema de arquivos na partição.
    // A formatação na falha torna o formulário incorporado de /upload
    // acessível para restaurar index.html, alarmes.html e upload.html.
    if (!FFat.begin(true)) {
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
        []() {
            responderListaRadios(servidor);
        }
    );

    servidor.on(
        "/alarmes",
        HTTP_GET,
        abrirPaginaAlarmes
    );

    servidor.on(
        "/api/alarmes",
        HTTP_GET,
        []() {
            responderListaAlarmes(servidor);
        }
    );

    servidor.on(
        "/api/alarmes",
        HTTP_POST,
        []() {
            adicionarAlarmePelaApi(servidor);
        }
    );

    servidor.on(
        "/api/alarmes",
        HTTP_PUT,
        []() {
            atualizarAlarmePelaApi(servidor);
        }
    );

    servidor.on(
        "/api/alarmes",
        HTTP_DELETE,
        []() {
            excluirAlarmePelaApi(servidor);
        }
    );

    servidor.on(
        "/api/arquivos-player",
        HTTP_GET,
        []() {
            responderArquivosPlayer(servidor);
        }
    );

    servidor.on(
        "/api/radios-alarmes",
        HTTP_GET,
        []() {
            responderRadiosParaAlarmes(servidor);
        }
    );

    servidor.on(
        "/api/radios",
        HTTP_POST,
        []() {
            adicionarRadioPelaApi(servidor);
        }
    );

    servidor.on(
        "/api/radios",
        HTTP_DELETE,
        []() {
            excluirRadioPelaApi(servidor);
        }
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
        []() {
            responderResultadoUpload(servidor);
        },
        []() {
            processarDadosUpload(servidor);
        }
    );

    servidor.onNotFound([]() {
        servidor.send(
            404,
            "application/json",
            "{\"erro\":\"Endereço não encontrado\"}"
        );
    });

    servidor.begin();
    servidorAtivo = true;

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

void desativarServidorWeb() {
    if (!servidorAtivo) {
        return;
    }

    servidor.stop();
    servidorAtivo = false;

    Serial.println("Servidor web desativado.");
}

void reativarServidorWeb() {
    if (servidorAtivo) {
        return;
    }

    servidor.begin();
    servidorAtivo = true;

    Serial.println("Servidor web reativado.");
}

void processarServidorWeb() {
    if (!servidorAtivo) {
        return;
    }

    servidor.handleClient();
}
