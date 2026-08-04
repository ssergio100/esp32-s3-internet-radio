#include "servidor_web.h"
#include "audio_radio.h"
#include "radios.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FFat.h>
#include <WiFi.h>
#include <WebServer.h>

namespace {

WebServer servidor(80);

const char* ARQUIVO_RADIOS = "/radios.json";
const char* ARQUIVO_RADIOS_TEMPORARIO = "/radios.tmp";
const char* ARQUIVO_RADIOS_BACKUP = "/radios.bak";
const char* ARQUIVO_UPLOAD_TEMPORARIO = "/.upload.tmp";
const char* ARQUIVO_UPLOAD_RESERVA = "/.upload.bak";

File arquivoUpload;

String destinoUpload;
String erroUpload;

bool uploadConcluido = false;

const char PAGINA_UPLOAD[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Arquivos do ESP32</title>

    <style>
        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            padding: 30px 16px;
            background: #101826;
            color: #edf3fa;
            font-family: Arial, sans-serif;
        }

        .container {
            width: 100%;
            max-width: 720px;
            margin: 0 auto;
        }

        .cabecalho {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 15px;
            margin-bottom: 22px;
        }

        h1 {
            margin: 0;
            font-size: 26px;
        }

        h2 {
            margin-top: 0;
            font-size: 20px;
        }

        .voltar {
            color: #9ecfff;
            text-decoration: none;
        }

        .card {
            padding: 22px;
            margin-bottom: 20px;
            border: 1px solid #2b3c52;
            border-radius: 14px;
            background: #182436;
        }

        input[type="file"] {
            display: block;
            width: 100%;
            padding: 14px;
            margin: 14px 0;
            color: #edf3fa;
            border: 1px solid #3b506a;
            border-radius: 9px;
            background: #101826;
        }

        button {
            width: 100%;
            padding: 13px;
            border: 0;
            border-radius: 9px;
            background: #2f8cff;
            color: white;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
        }

        button:disabled {
            opacity: 0.55;
            cursor: wait;
        }

        progress {
            display: none;
            width: 100%;
            height: 18px;
            margin-top: 16px;
        }

        #mensagem {
            min-height: 20px;
            margin-top: 14px;
        }

        .sucesso {
            color: #62df91;
        }

        .erro {
            color: #ff7c7c;
        }

        .arquivo {
            display: flex;
            justify-content: space-between;
            gap: 15px;
            padding: 11px 0;
            border-bottom: 1px solid #2b3c52;
        }

        .arquivo:last-child {
            border-bottom: 0;
        }

        .tamanho {
            color: #9eb0c4;
            white-space: nowrap;
        }

        .aviso {
            margin-bottom: 0;
            color: #b9c7d6;
            font-size: 14px;
            line-height: 1.5;
        }
    </style>
</head>

<body>
    <main class="container">
        <header class="cabecalho">
            <h1>Arquivos do ESP32</h1>
            <a class="voltar" href="/">Voltar ao rádio</a>
        </header>

        <section class="card">
            <form id="formUpload">
                <label for="arquivo">Selecione o arquivo</label>

                <input
                    type="file"
                    id="arquivo"
                    name="arquivo"
                    required
                >

                <button type="submit" id="btnEnviar">
                    Enviar arquivo
                </button>

                <progress
                    id="progresso"
                    max="100"
                    value="0"
                ></progress>

                <div id="mensagem"></div>
            </form>

            <p class="aviso">
                O arquivo será gravado na raiz do FFat com seu nome
                original. Um novo index.html substituirá o atual.
            </p>
        </section>

        <section class="card">
            <h2>Arquivos armazenados</h2>
            <div id="listaArquivos">Carregando...</div>
        </section>
    </main>

    <script>
        const formUpload =
            document.getElementById("formUpload");

        const campoArquivo =
            document.getElementById("arquivo");

        const btnEnviar =
            document.getElementById("btnEnviar");

        const progresso =
            document.getElementById("progresso");

        const mensagem =
            document.getElementById("mensagem");

        const listaArquivos =
            document.getElementById("listaArquivos");

        function formatarTamanho(bytes) {
            if (bytes < 1024) {
                return bytes + " bytes";
            }

            if (bytes < 1024 * 1024) {
                return (bytes / 1024).toFixed(1) + " KB";
            }

            return (
                bytes / (1024 * 1024)
            ).toFixed(1) + " MB";
        }

        async function carregarArquivos() {
            try {
                const resposta =
                    await fetch("/api/arquivos");

                if (!resposta.ok) {
                    throw new Error(
                        "Falha ao listar os arquivos"
                    );
                }

                const arquivos =
                    await resposta.json();

                if (!arquivos.length) {
                    listaArquivos.textContent =
                        "Nenhum arquivo encontrado.";

                    return;
                }

                listaArquivos.innerHTML = "";

                arquivos.forEach(function (arquivo) {
                    const linha =
                        document.createElement("div");

                    linha.className = "arquivo";

                    const nome =
                        document.createElement("span");

                    nome.textContent = arquivo.nome;

                    const tamanho =
                        document.createElement("span");

                    tamanho.className = "tamanho";
                    tamanho.textContent =
                        formatarTamanho(arquivo.tamanho);

                    linha.appendChild(nome);
                    linha.appendChild(tamanho);
                    listaArquivos.appendChild(linha);
                });
            } catch (erro) {
                listaArquivos.textContent = erro.message;
            }
        }

        formUpload.addEventListener(
            "submit",
            function (evento) {
                evento.preventDefault();

                if (!campoArquivo.files.length) {
                    return;
                }

                const arquivo = campoArquivo.files[0];

                if (
                    arquivo.name === "radios.json" &&
                    !confirm(
                        "Isso substituirá toda a lista de rádios. Continuar?"
                    )
                ) {
                    return;
                }

                const dados = new FormData();

                dados.append("arquivo", arquivo);

                const requisicao =
                    new XMLHttpRequest();

                requisicao.open("POST", "/upload");

                btnEnviar.disabled = true;
                btnEnviar.textContent = "Enviando...";

                progresso.style.display = "block";
                progresso.value = 0;

                mensagem.className = "";
                mensagem.textContent = "";

                requisicao.upload.addEventListener(
                    "progress",
                    function (eventoProgresso) {
                        if (!eventoProgresso.lengthComputable) {
                            return;
                        }

                        progresso.value =
                            eventoProgresso.loaded /
                            eventoProgresso.total *
                            100;
                    }
                );

                requisicao.addEventListener(
                    "load",
                    async function () {
                        let respostaJson = {};

                        try {
                            respostaJson =
                                JSON.parse(
                                    requisicao.responseText
                                );
                        } catch (erro) {
                        }

                        if (
                            requisicao.status >= 200 &&
                            requisicao.status < 300
                        ) {
                            mensagem.className = "sucesso";
                            mensagem.textContent =
                                "Arquivo enviado com sucesso.";

                            campoArquivo.value = "";
                            await carregarArquivos();
                        } else {
                            mensagem.className = "erro";
                            mensagem.textContent =
                                respostaJson.erro ||
                                "Falha ao enviar o arquivo.";
                        }

                        btnEnviar.disabled = false;
                        btnEnviar.textContent =
                            "Enviar arquivo";
                    }
                );

                requisicao.addEventListener(
                    "error",
                    function () {
                        mensagem.className = "erro";
                        mensagem.textContent =
                            "A conexão com o ESP32 foi interrompida.";

                        btnEnviar.disabled = false;
                        btnEnviar.textContent =
                            "Enviar arquivo";
                    }
                );

                requisicao.send(dados);
            }
        );

        carregarArquivos();
    </script>
</body>
</html>
)HTML";

bool documentoRadiosValido(
    DynamicJsonDocument& documento
) {
    if (!documento.is<JsonArray>()) {
        return false;
    }

    JsonArray radios =
        documento.as<JsonArray>();

    if (
        radios.size() == 0 ||
        radios.size() >
        QUANTIDADE_MAXIMA_RADIOS
    ) {
        return false;
    }

    for (JsonObject radio : radios) {
        const char* nome = radio["nome"];
        const char* url = radio["url"];

        if (!dadosRadioValidos(nome, url)) {
            return false;
        }
    }

    return true;
}

bool arquivoJsonValido(
    const char* caminho
) {
    File arquivo =
        FFat.open(
            caminho,
            FILE_READ
        );

    if (!arquivo) {
        return false;
    }

    DynamicJsonDocument documento(16384);

    DeserializationError erro =
        deserializeJson(
            documento,
            arquivo
        );

    arquivo.close();

    return
        !erro &&
        documentoRadiosValido(
            documento
        );
}

bool salvarDocumento(
    DynamicJsonDocument& documento
) {
    FFat.remove(
        ARQUIVO_RADIOS_TEMPORARIO
    );

    File arquivo =
        FFat.open(
            ARQUIVO_RADIOS_TEMPORARIO,
            FILE_WRITE
        );

    if (!arquivo) {
        return false;
    }

    bool salvo =
        serializeJson(
            documento,
            arquivo
        ) > 0;

    arquivo.flush();
    arquivo.close();

    if (
        !salvo ||
        !arquivoJsonValido(
            ARQUIVO_RADIOS_TEMPORARIO
        )
    ) {
        FFat.remove(
            ARQUIVO_RADIOS_TEMPORARIO
        );

        return false;
    }

    bool arquivoAnteriorValido =
        arquivoJsonValido(
            ARQUIVO_RADIOS
        );

    if (arquivoAnteriorValido) {
        FFat.remove(
            ARQUIVO_RADIOS_BACKUP
        );

        if (
            !FFat.rename(
                ARQUIVO_RADIOS,
                ARQUIVO_RADIOS_BACKUP
            )
        ) {
            FFat.remove(
                ARQUIVO_RADIOS_TEMPORARIO
            );

            return false;
        }
    } else if (
        FFat.exists(
            ARQUIVO_RADIOS
        )
    ) {
        FFat.remove(
            ARQUIVO_RADIOS
        );
    }

    if (
        FFat.rename(
            ARQUIVO_RADIOS_TEMPORARIO,
            ARQUIVO_RADIOS
        )
    ) {
        return true;
    }

    if (arquivoAnteriorValido) {
        FFat.rename(
            ARQUIVO_RADIOS_BACKUP,
            ARQUIVO_RADIOS
        );
    }

    FFat.remove(
        ARQUIVO_RADIOS_TEMPORARIO
    );

    return false;
}

bool carregarDocumentoArquivo(
    DynamicJsonDocument& documento,
    const char* caminho
) {
    File arquivo =
        FFat.open(
            caminho,
            FILE_READ
        );

    if (!arquivo) {
        return false;
    }

    documento.clear();

    DeserializationError erro =
        deserializeJson(
            documento,
            arquivo
        );

    arquivo.close();

    return
        !erro &&
        documentoRadiosValido(
            documento
        );
}

bool carregarDocumento(
    DynamicJsonDocument& documento
) {
    if (
        carregarDocumentoArquivo(
            documento,
            ARQUIVO_RADIOS
        )
    ) {
        return true;
    }

    if (
        carregarDocumentoArquivo(
            documento,
            ARQUIVO_RADIOS_BACKUP
        )
    ) {
        Serial.println(
            "radios.json invalido; usando radios.bak."
        );

        return true;
    }

    if (
        !FFat.exists(
            ARQUIVO_RADIOS
        ) &&
        !FFat.exists(
            ARQUIVO_RADIOS_BACKUP
        )
    ) {
        documento.to<JsonArray>();

        return true;
    }

    return false;
}

void listarRadios() {
    const char* caminho = nullptr;

    if (
        arquivoJsonValido(
            ARQUIVO_RADIOS
        )
    ) {
        caminho = ARQUIVO_RADIOS;
    } else if (
        arquivoJsonValido(
            ARQUIVO_RADIOS_BACKUP
        )
    ) {
        caminho = ARQUIVO_RADIOS_BACKUP;
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

    if (!carregarDocumento(documento)) {
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

    if (!salvarDocumento(documento)) {
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

    if (!carregarDocumento(documento)) {
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

    if (!salvarDocumento(documento)) {
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

void abrirPagina() {
    File arquivo =
        FFat.open(
            "/index.html",
            FILE_READ
        );

    if (!arquivo) {
        servidor.send(
            404,
            "text/plain",
            "Arquivo index.html não encontrado"
        );

        return;
    }

    servidor.streamFile(
        arquivo,
        "text/html; charset=utf-8"
    );

    arquivo.close();
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
    servidor.send_P(
        200,
        "text/html; charset=utf-8",
        PAGINA_UPLOAD
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

        if (destinoUpload == ARQUIVO_RADIOS) {
            DynamicJsonDocument documento(16384);

            if (
                !carregarDocumentoArquivo(
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

            if (!salvarDocumento(documento)) {
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

void obterStatusSistema() {
    StatusAudio audio =
        obterStatusAudio();

    DynamicJsonDocument documento(1536);

    documento["estado"] =
        obterTextoEstadoAudio(
            audio.estado
        );

    documento["radio"] = audio.radio;
    documento["titulo"] = audio.titulo;
    documento["codec"] = audio.codec;
    documento["bitrate"] = audio.bitrate;
    documento["bufferBytes"] =
        audio.bufferBytes;
    documento["bufferTotalBytes"] =
        audio.bufferTotalBytes;
    documento["bufferMilissegundos"] =
        audio.bufferMilissegundos;
    documento["eventosFluxoLento"] =
        audio.eventosFluxoLento;
    documento["tentativasReconexao"] =
        audio.tentativasReconexao;
    documento["stackMinimoBytes"] =
        audio.stackMinimoBytes;
    documento["ultimoErro"] =
        audio.ultimoErro;

    documento["wifiConectado"] =
        WiFi.status() == WL_CONNECTED;
    documento["rssi"] = WiFi.RSSI();
    documento["bssid"] =
        WiFi.BSSIDstr();
    documento["canalWifi"] =
        WiFi.channel();
    documento["ramLivre"] =
        ESP.getFreeHeap();
    documento["maiorBlocoRam"] =
        ESP.getMaxAllocHeap();
    documento["psramLivre"] =
        ESP.getFreePsram();
    documento["maiorBlocoPsram"] =
        ESP.getMaxAllocPsram();
    documento["uptimeMs"] = millis();

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
        obterStatusSistema
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
