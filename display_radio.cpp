#include "display_radio.h"
#include "configuracao.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {

Adafruit_SSD1306 display(
    DISPLAY_LARGURA,
    DISPLAY_ALTURA,
    &Wire,
    -1
);

bool disponivel = false;

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

void mostrarMensagem(
    const String& mensagem
) {
    if (!disponivel) {
        return;
    }

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

    prepararTela();

    escreverCentralizado(
        "ESTACAO",
        2,
        1
    );

    display.setTextSize(2);

    int16_t x1;
    int16_t y1;
    uint16_t largura;
    uint16_t altura;

    display.getTextBounds(
        nome,
        0,
        0,
        &x1,
        &y1,
        &largura,
        &altura
    );

    if (largura <= DISPLAY_LARGURA - 4) {
        escreverCentralizado(
            nome,
            24,
            2
        );
    } else {
        escreverCentralizado(
            nome,
            29,
            1
        );
    }

    escreverCentralizado(
        String(indiceAtual + 1) +
            "/" +
            String(quantidadeRadios),
        54,
        1
    );

    display.display();
}

void mostrarVolume(
    int volume
) {
    if (!disponivel) {
        return;
    }

    prepararTela();

    escreverCentralizado(
        "VOLUME",
        2,
        1
    );

    escreverCentralizado(
        String(volume) + "%",
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