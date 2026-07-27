#include "display_radio.h"
#include "configuracao.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>


namespace {

    
    Adafruit_SSD1306 display(
        DISPLAY_LARGURA,
        DISPLAY_ALTURA,
        &Wire,
        -1
    );

    bool disponivel = false;

    enum class TelaDisplay {
        MENSAGEM,
        RADIO,
        VOLUME
    };

    TelaDisplay telaAtual = TelaDisplay::MENSAGEM;

    String nomeRadioAtual;
    int indiceRadioAtual = 0;
    int quantidadeRadiosAtual = 0;

    unsigned long ultimaAtualizacaoRelogio = 0;
    int ultimoMinutoExibido = -1;

    void escreverCentralizado(
        const String& texto,
        int posicaoY,
        uint8_t tamanhoTexto
    ) {
        display.setTextSize(tamanhoTexto);

        int16_t x1;
        int16_t y1;
        uint16_t largura;
        uint16_t altura;

        display.getTextBounds(
            texto,
            0,
            posicaoY,
            &x1,
            &y1,
            &largura,
            &altura
        );

        int posicaoX =
            (DISPLAY_LARGURA - largura) / 2;

        if (posicaoX < 0) {
            posicaoX = 0;
        }

        display.setCursor(
            posicaoX,
            posicaoY
        );

        display.print(texto);
    }

    void prepararTela() {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
    }

    String obterDataHora() {
    struct tm horario;

    if (!getLocalTime(&horario, 10)) {
        return "--/--/---- --:--";
    }

    char texto[17];

    strftime(
        texto,
        sizeof(texto),
        "%d/%m/%Y %H:%M",
        &horario
    );

    return String(texto);
}



void desenharTelaRadio() {
    prepararTela();

    escreverCentralizado(
        obterDataHora(),
        2,
        1
    );

    display.setTextSize(2);

    int16_t x1;
    int16_t y1;
    uint16_t largura;
    uint16_t altura;

    display.getTextBounds(
        nomeRadioAtual,
        0,
        0,
        &x1,
        &y1,
        &largura,
        &altura
    );

    if (largura <= DISPLAY_LARGURA - 4) {
        escreverCentralizado(
            nomeRadioAtual,
            24,
            2
        );
    } else {
        escreverCentralizado(
            nomeRadioAtual,
            29,
            1
        );
    }

    escreverCentralizado(
        String(indiceRadioAtual + 1) +
            "/" +
            String(quantidadeRadiosAtual),
        54,
        1
    );

    display.display();
}

}

bool iniciarDisplay() {
    Wire.begin(
        PIN_DISPLAY_SDA,
        PIN_DISPLAY_SCL
    );

    disponivel = display.begin(
        SSD1306_SWITCHCAPVCC,
        DISPLAY_ENDERECO
    );

    if (!disponivel) {
        Serial.println(
            "Display OLED não encontrado."
        );

        return false;
    }

    prepararTela();
    mostrarMensagem("Inicializando");

    return true;
}

void mostrarMensagem( const String& mensagem ) 
{
    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::MENSAGEM;

    prepararTela();

    escreverCentralizado(
        mensagem,
        28,
        1
    );

    display.display();
}

void mostrarNomeRadio(
    const String& nome,
    int indiceAtual,
    int quantidadeRadios
) {
    if (!disponivel) {
        return;
    }

    nomeRadioAtual = nome;
    indiceRadioAtual = indiceAtual;
    quantidadeRadiosAtual = quantidadeRadios;

    telaAtual = TelaDisplay::RADIO;
    ultimoMinutoExibido = -1;

    desenharTelaRadio();
}

void mostrarVolume(
    int volume
) {
    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::VOLUME;

    prepararTela();

    // escreverCentralizado(
    //     "VOLUME",
    //     2,
    //     1
    // );

  String volumeExibido = (volume == 0)
    ? "Mudo"
    : String(volume) + "/" + String(VOLUME_MAXIMO);

    escreverCentralizado(
        volumeExibido,
        17,
        3
    );

    constexpr int barraX = 9;
    constexpr int barraY = 51;
    constexpr int barraLargura = 110;
    constexpr int barraAltura = 11;

    display.drawRect(
        barraX,
        barraY,
        barraLargura,
        barraAltura,
        SSD1306_WHITE
    );

    int larguraPreenchida = map(
        volume,
        VOLUME_MINIMO,
        VOLUME_MAXIMO,
        0,
        barraLargura - 4
    );

    if (larguraPreenchida > 0) {
        display.fillRect(
            barraX + 2,
            barraY + 2,
            larguraPreenchida,
            barraAltura - 4,
            SSD1306_WHITE
        );
    }

    display.display();
}

void mostrarConfiguracaoWifi() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(9, 0);
    display.println("CONECTE-SE A REDE");

    display.setTextSize(2);
    display.setCursor(10, 12);
    display.println("RADIO-WEB");

    display.setTextSize(1);
    display.setCursor(12, 34);
    display.println("ABRA NO NAVEGADOR");

    display.setTextSize(1);
    display.setCursor(31, 50);
    display.println("192.168.4.1");

    display.display();
}

void processarDisplay() {
    if (!disponivel) {
        return;
    }

    if (telaAtual != TelaDisplay::RADIO) {
        return;
    }

    if (millis() - ultimaAtualizacaoRelogio < 1000) {
        return;
    }

    ultimaAtualizacaoRelogio = millis();

    struct tm horario;

    if (!getLocalTime(&horario, 10)) {
        return;
    }

    if (horario.tm_min == ultimoMinutoExibido) {
        return;
    }

    ultimoMinutoExibido = horario.tm_min;

    desenharTelaRadio();
}