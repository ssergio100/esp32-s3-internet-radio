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
        SELECAO_RADIO,
        VOLUME
    };

    TelaDisplay telaAtual = TelaDisplay::MENSAGEM;

    String nomeRadioAtual;
    int indiceRadioAtual = 0;
    int quantidadeRadiosAtual = 0;

    constexpr int MARGEM_NOME_RADIO = 2;
    constexpr int POSICAO_Y_NOME_RADIO = 24;
    constexpr uint8_t TAMANHO_NOME_RADIO = 2;
    constexpr unsigned long INTERVALO_ROLAGEM_MS = 50;
    constexpr int ESPACO_ENTRE_NOMES = 24;

    bool rolagemNomeAtiva = false;
    int posicaoXNomeRadio = MARGEM_NOME_RADIO;
    uint16_t larguraNomeRadio = 0;
    unsigned long ultimaMovimentacaoNome = 0;

    unsigned long ultimaAtualizacaoRelogio = 0;
    int ultimoMinutoExibido = -1;
    String dataHoraAtual = "--/--/---- --:--";

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
        display.setTextWrap(false);
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

    void reiniciarRolagemNome() {
        display.setTextSize(TAMANHO_NOME_RADIO);

        int16_t x1;
        int16_t y1;
        uint16_t altura;

        display.getTextBounds(
            nomeRadioAtual,
            0,
            0,
            &x1,
            &y1,
            &larguraNomeRadio,
            &altura
        );

        if (
            larguraNomeRadio <=
            DISPLAY_LARGURA - 2 * MARGEM_NOME_RADIO
        ) {
            posicaoXNomeRadio =
                (DISPLAY_LARGURA - larguraNomeRadio) / 2;
            rolagemNomeAtiva = false;
        } else {
            posicaoXNomeRadio = MARGEM_NOME_RADIO;
            rolagemNomeAtiva = true;
        }

        ultimaMovimentacaoNome = millis();
    }

    void desenharTelaRadio() {
        prepararTela();

        escreverCentralizado(
            dataHoraAtual,
            2,
            1
        );

        display.setTextSize(TAMANHO_NOME_RADIO);
        display.setCursor(
            posicaoXNomeRadio,
            POSICAO_Y_NOME_RADIO
        );
        display.print(nomeRadioAtual);

        if (rolagemNomeAtiva) {
            display.setCursor(
                posicaoXNomeRadio +
                    larguraNomeRadio +
                    ESPACO_ENTRE_NOMES,
                POSICAO_Y_NOME_RADIO
            );
            display.print(nomeRadioAtual);
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

void mostrarMensagem(
    const String& mensagem
) {
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

    bool radioMudou =
        nomeRadioAtual != nome ||
        indiceRadioAtual != indiceAtual;

    nomeRadioAtual = nome;
    indiceRadioAtual = indiceAtual;
    quantidadeRadiosAtual = quantidadeRadios;

    telaAtual = TelaDisplay::RADIO;
    ultimoMinutoExibido = -1;
    dataHoraAtual = obterDataHora();

    if (radioMudou || larguraNomeRadio == 0) {
        reiniciarRolagemNome();
    } else {
        ultimaMovimentacaoNome = millis();
    }

    desenharTelaRadio();
}

void mostrarSelecaoRadio(
    const String& nome,
    int indiceSelecionado,
    int quantidadeRadios
) {
    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::SELECAO_RADIO;

    prepararTela();

    escreverCentralizado(
        "<  ESTACOES  >",
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
            22,
            2
        );
    } else {
        escreverCentralizado(
            nome,
            27,
            1
        );
    }

    escreverCentralizado(
        String(indiceSelecionado + 1) +
            "/" +
            String(quantidadeRadios) +
            "  Clique: OK",
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

    unsigned long agora = millis();
    bool precisaRedesenhar = false;

    if (agora - ultimaAtualizacaoRelogio >= 1000) {
        ultimaAtualizacaoRelogio = agora;

        struct tm horario;

        if (
            getLocalTime(&horario, 10) &&
            horario.tm_min != ultimoMinutoExibido
        ) {
            ultimoMinutoExibido = horario.tm_min;
            dataHoraAtual = obterDataHora();
            precisaRedesenhar = true;
        }
    }

    if (
        rolagemNomeAtiva &&
        agora - ultimaMovimentacaoNome >=
            INTERVALO_ROLAGEM_MS
    ) {
        ultimaMovimentacaoNome = agora;
        posicaoXNomeRadio--;

        int comprimentoCiclo =
            larguraNomeRadio + ESPACO_ENTRE_NOMES;

        if (
            posicaoXNomeRadio <=
            MARGEM_NOME_RADIO - comprimentoCiclo
        ) {
            posicaoXNomeRadio += comprimentoCiclo;
        }

        precisaRedesenhar = true;
    }

    if (precisaRedesenhar) {
        desenharTelaRadio();
    }
}
