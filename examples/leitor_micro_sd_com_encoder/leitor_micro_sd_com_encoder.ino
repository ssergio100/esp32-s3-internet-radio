/*
 * Exemplo independente: navegador e reprodutor de arquivos do microSD.
 *
 * O sketch procura músicas na raiz e nas subpastas do cartão.
 *
 * Modo de seleção:
 *   - girar o encoder escolhe um arquivo;
 *   - clicar inicia a reprodução.
 *
 * Durante a reprodução:
 *   - girar o encoder ajusta o volume;
 *   - clicar para a música e retorna à seleção.
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <vector>
#include <algorithm>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "AiEsp32RotaryEncoder.h"
#include "Audio.h"

// Display OLED SSD1306
constexpr int PIN_DISPLAY_SDA = 9;
constexpr int PIN_DISPLAY_SCL = 10;
constexpr int DISPLAY_LARGURA = 128;
constexpr int DISPLAY_ALTURA = 64;
constexpr int DISPLAY_ENDERECO = 0x3C;

// Encoder principal
constexpr int PIN_ENCODER_CLK = 16;
constexpr int PIN_ENCODER_DT = 15;
constexpr int PIN_ENCODER_SW = 17;
constexpr int PIN_ENCODER_VCC = -1;
constexpr int TRANSICOES_ENCODER_POR_DETENTE = 4;

// Leitor microSD SPI
constexpr int PIN_CARTAO_SCK = 11;
constexpr int PIN_CARTAO_MISO = 12;
constexpr int PIN_CARTAO_MOSI = 13;
constexpr int PIN_CARTAO_CS = 14;
constexpr uint32_t FREQUENCIA_CARTAO_HZ = 1000000;
constexpr int TENTATIVAS_INICIALIZACAO_CARTAO = 3;
constexpr unsigned long INTERVALO_TENTATIVAS_CARTAO_MS = 500;

// Saída I2S para o MAX98357A
constexpr int PIN_I2S_BCLK = 5;
constexpr int PIN_I2S_LRC = 6;
constexpr int PIN_I2S_DIN = 7;

constexpr int VOLUME_MINIMO = 0;
constexpr int VOLUME_MAXIMO = 21;
constexpr int VOLUME_INICIAL = 10;

constexpr unsigned long TEMPO_DEBOUNCE_BOTAO_MS = 30;

enum class ModoExemplo {
    SELECAO,
    TOCANDO
};

Adafruit_SSD1306 display(
    DISPLAY_LARGURA,
    DISPLAY_ALTURA,
    &Wire,
    -1
);

AiEsp32RotaryEncoder encoder(
    PIN_ENCODER_CLK,
    PIN_ENCODER_DT,
    PIN_ENCODER_SW,
    PIN_ENCODER_VCC,
    TRANSICOES_ENCODER_POR_DETENTE,
    false
);

Audio audio;

std::vector<String> caminhosArquivos;

ModoExemplo modo = ModoExemplo::SELECAO;
int indiceSelecionado = 0;
int volumeAtual = VOLUME_INICIAL;

bool displayDisponivel = false;
bool exemploPronto = false;
volatile bool arquivoTerminou = false;

bool ultimaLeituraBrutaBotao = HIGH;
bool estadoEstavelBotao = HIGH;
unsigned long momentoMudancaBotaoMs = 0;

void IRAM_ATTR encoderISR() {
    encoder.readEncoder_ISR();
}

bool extensaoAudioSuportada(const char* caminho) {
    if (caminho == nullptr) {
        return false;
    }

    String caminhoNormalizado = caminho;
    caminhoNormalizado.toLowerCase();

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

String nomeVisivelArquivo(const String& caminho) {
    int ultimaBarra = caminho.lastIndexOf('/');

    if (ultimaBarra < 0) {
        return caminho;
    }

    return caminho.substring(ultimaBarra + 1);
}

String limitarTexto(const String& texto, size_t quantidadeCaracteres) {
    if (texto.length() <= quantidadeCaracteres) {
        return texto;
    }

    if (quantidadeCaracteres <= 3) {
        return texto.substring(0, quantidadeCaracteres);
    }

    return
        texto.substring(0, quantidadeCaracteres - 3) +
        "...";
}

void mostrarMensagem(const String& mensagem) {
    if (!displayDisponivel) {
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.println(mensagem);
    display.display();
}

void desenharListaArquivos() {
    if (!displayDisponivel) {
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("ARQUIVOS ");
    display.print(
        caminhosArquivos.empty()
            ? 0
            : indiceSelecionado + 1
    );
    display.print("/");
    display.println(caminhosArquivos.size());
    display.drawFastHLine(0, 9, DISPLAY_LARGURA, SSD1306_WHITE);

    if (caminhosArquivos.empty()) {
        display.setCursor(0, 25);
        display.println("Nenhum arquivo");
        display.setCursor(0, 37);
        display.println("Confira o cartao");
        display.display();
        return;
    }

    constexpr int QUANTIDADE_LINHAS = 5;
    int primeiroIndice =
        indiceSelecionado - QUANTIDADE_LINHAS / 2;

    if (primeiroIndice < 0) {
        primeiroIndice = 0;
    }

    int maiorPrimeiroIndice =
        static_cast<int>(caminhosArquivos.size()) -
        QUANTIDADE_LINHAS;

    if (maiorPrimeiroIndice < 0) {
        maiorPrimeiroIndice = 0;
    }

    if (primeiroIndice > maiorPrimeiroIndice) {
        primeiroIndice = maiorPrimeiroIndice;
    }

    for (int linha = 0; linha < QUANTIDADE_LINHAS; linha++) {
        int indice = primeiroIndice + linha;

        if (indice >= static_cast<int>(caminhosArquivos.size())) {
            break;
        }

        int posicaoY = 12 + linha * 10;
        bool selecionado = indice == indiceSelecionado;

        if (selecionado) {
            display.fillRect(
                0,
                posicaoY - 1,
                DISPLAY_LARGURA,
                9,
                SSD1306_WHITE
            );
            display.setTextColor(
                SSD1306_BLACK,
                SSD1306_WHITE
            );
        } else {
            display.setTextColor(SSD1306_WHITE);
        }

        display.setCursor(2, posicaoY);
        display.print(
            limitarTexto(
                nomeVisivelArquivo(caminhosArquivos[indice]),
                20
            )
        );
    }

    display.setTextColor(SSD1306_WHITE);
    display.display();
}

void desenharReproducao() {
    if (!displayDisponivel || caminhosArquivos.empty()) {
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("TOCANDO");
    display.drawFastHLine(0, 9, DISPLAY_LARGURA, SSD1306_WHITE);

    display.setCursor(0, 18);
    display.println(
        limitarTexto(
            nomeVisivelArquivo(
                caminhosArquivos[indiceSelecionado]
            ),
            20
        )
    );

    display.setCursor(0, 36);
    display.print("Volume: ");
    display.print(volumeAtual);
    display.print("/");
    display.println(VOLUME_MAXIMO);

    display.setCursor(0, 54);
    display.println("Clique: parar");
    display.display();
}

bool detectarCliqueConfirmado() {
    bool leituraAtual = digitalRead(PIN_ENCODER_SW);
    unsigned long agoraMs = millis();

    if (leituraAtual != ultimaLeituraBrutaBotao) {
        ultimaLeituraBrutaBotao = leituraAtual;
        momentoMudancaBotaoMs = agoraMs;
    }

    if (
        agoraMs - momentoMudancaBotaoMs <
            TEMPO_DEBOUNCE_BOTAO_MS ||
        leituraAtual == estadoEstavelBotao
    ) {
        return false;
    }

    estadoEstavelBotao = leituraAtual;

    // Confirma o clique somente depois que o botão for solto.
    return estadoEstavelBotao == HIGH;
}

bool adicionarArquivosDoDiretorio(
    const char* caminhoDiretorio,
    std::vector<String>& diretoriosPendentes
) {
    File diretorio = SD.open(caminhoDiretorio);

    if (!diretorio || !diretorio.isDirectory()) {
        diretorio.close();
        return false;
    }

    Serial.print("Diretorio: ");
    Serial.println(caminhoDiretorio);

    File arquivo = diretorio.openNextFile();

    while (arquivo) {
        if (arquivo.isDirectory()) {
            diretoriosPendentes.push_back(
                arquivo.path()
            );
            arquivo.close();
        } else {
            String caminho = arquivo.path();

            if (extensaoAudioSuportada(caminho.c_str())) {
                caminhosArquivos.push_back(caminho);
                Serial.print("  Audio: ");
            } else {
                Serial.print("  Ignorado: ");
            }

            Serial.println(caminho);
            arquivo.close();
        }

        arquivo = diretorio.openNextFile();
    }

    diretorio.close();

    return true;
}

bool carregarListaArquivos() {
    caminhosArquivos.clear();

    // A busca em largura mantém apenas um diretório aberto por vez, evitando
    // atingir o limite de arquivos simultâneos do driver SD.
    std::vector<String> diretoriosPendentes = {
        "/"
    };

    for (
        size_t indiceDiretorio = 0;
        indiceDiretorio < diretoriosPendentes.size();
        indiceDiretorio++
    ) {
        if (
            !adicionarArquivosDoDiretorio(
                diretoriosPendentes[indiceDiretorio].c_str(),
                diretoriosPendentes
            )
        ) {
            return false;
        }
    }

    std::sort(
        caminhosArquivos.begin(),
        caminhosArquivos.end(),
        [](const String& esquerda, const String& direita) {
            return esquerda.compareTo(direita) < 0;
        }
    );

    indiceSelecionado = 0;
    return true;
}

bool iniciarCartao() {
    pinMode(PIN_CARTAO_CS, OUTPUT);
    digitalWrite(PIN_CARTAO_CS, HIGH);

    for (
        int tentativa = 1;
        tentativa <= TENTATIVAS_INICIALIZACAO_CARTAO;
        tentativa++
    ) {
        SPI.begin(
            PIN_CARTAO_SCK,
            PIN_CARTAO_MISO,
            PIN_CARTAO_MOSI,
            PIN_CARTAO_CS
        );

        if (
            SD.begin(
                PIN_CARTAO_CS,
                SPI,
                FREQUENCIA_CARTAO_HZ
            ) &&
            SD.cardType() != CARD_NONE
        ) {
            Serial.printf(
                "microSD inicializado na tentativa %d.\n",
                tentativa
            );
            return true;
        }

        Serial.printf(
            "Tentativa %d de %d para iniciar o microSD falhou.\n",
            tentativa,
            TENTATIVAS_INICIALIZACAO_CARTAO
        );

        SD.end();
        SPI.end();
        digitalWrite(PIN_CARTAO_CS, HIGH);

        if (tentativa < TENTATIVAS_INICIALIZACAO_CARTAO) {
            delay(INTERVALO_TENTATIVAS_CARTAO_MS);
        }
    }

    return false;
}

void iniciarReproducaoSelecionada() {
    if (caminhosArquivos.empty()) {
        return;
    }

    audio.stopSong();
    arquivoTerminou = false;

    if (
        !audio.connecttoFS(
            SD,
            caminhosArquivos[indiceSelecionado].c_str()
        )
    ) {
        mostrarMensagem("Falha ao abrir arquivo");
        delay(1000);
        desenharListaArquivos();
        return;
    }

    modo = ModoExemplo::TOCANDO;
    desenharReproducao();

    Serial.print("Tocando: ");
    Serial.println(caminhosArquivos[indiceSelecionado]);
}

void pararReproducao() {
    audio.stopSong();
    arquivoTerminou = false;
    modo = ModoExemplo::SELECAO;
    desenharListaArquivos();
    Serial.println("Reproducao parada.");
}

void tratarEventoAudio(Audio::msg_t evento) {
    const char* mensagem =
        evento.msg != nullptr ? evento.msg : "";

    Serial.print("Audio: ");
    Serial.println(mensagem);

    if (evento.e == Audio::evt_eof) {
        arquivoTerminou = true;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(PIN_DISPLAY_SDA, PIN_DISPLAY_SCL);
    displayDisponivel = display.begin(
        SSD1306_SWITCHCAPVCC,
        DISPLAY_ENDERECO
    );

    if (!displayDisponivel) {
        Serial.println("Display OLED nao encontrado.");
    } else {
        mostrarMensagem("Inicializando...");
    }

    encoder.begin();
    encoder.setup(encoderISR);
    encoder.setBoundaries(-10000, 10000, false);
    encoder.setEncoderValue(0);
    encoder.disableAcceleration();

    if (!iniciarCartao()) {
        mostrarMensagem("Falha no microSD");
        Serial.println("Falha ao montar o microSD.");
        Serial.println(
            "Confira: SCK=11, MISO=12, MOSI=13, CS=14."
        );
        Serial.println(
            "Use GND comum e cartao formatado em FAT16/FAT32."
        );
        return;
    }

    if (!carregarListaArquivos()) {
        mostrarMensagem("Falha ao listar cartao");
        Serial.println("Nao foi possivel listar o cartao.");
        return;
    }

    Audio::audio_info_callback = tratarEventoAudio;

    if (!audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN)) {
        mostrarMensagem("Falha no audio I2S");
        Serial.println("Falha ao iniciar I2S ou PSRAM.");
        return;
    }

    audio.setVolume(volumeAtual);
    desenharListaArquivos();
    exemploPronto = true;

    Serial.printf(
        "%u arquivo(s) de audio encontrado(s).\n",
        static_cast<unsigned int>(caminhosArquivos.size())
    );
}

void loop() {
    if (!exemploPronto) {
        delay(100);
        return;
    }

    audio.loop();

    if (
        modo == ModoExemplo::TOCANDO &&
        arquivoTerminou
    ) {
        pararReproducao();
    }

    long deslocamento = encoder.encoderChanged();

    if (deslocamento != 0) {
        if (modo == ModoExemplo::SELECAO) {
            int quantidade =
                static_cast<int>(caminhosArquivos.size());

            if (quantidade > 0) {
                long novoIndice =
                    indiceSelecionado + deslocamento;

                novoIndice %= quantidade;

                if (novoIndice < 0) {
                    novoIndice += quantidade;
                }

                indiceSelecionado =
                    static_cast<int>(novoIndice);
                desenharListaArquivos();
            }
        } else {
            int novoVolume = constrain(
                volumeAtual + deslocamento,
                static_cast<long>(VOLUME_MINIMO),
                static_cast<long>(VOLUME_MAXIMO)
            );

            if (novoVolume != volumeAtual) {
                volumeAtual = novoVolume;
                audio.setVolume(volumeAtual);
                desenharReproducao();
            }
        }
    }

    if (detectarCliqueConfirmado()) {
        if (modo == ModoExemplo::SELECAO) {
            iniciarReproducaoSelecionada();
        } else {
            pararReproducao();
        }
    }

    delay(1);
}
