#include "display_radio.h"
#include "configuracao.h"
#include "relogio.h"

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

    constexpr int MARGEM_HORIZONTAL_NOME_RADIO_PX = 2;
    constexpr int POSICAO_VERTICAL_NOME_RADIO_PX = 24;
    constexpr uint8_t TAMANHO_TEXTO_NOME_RADIO = 2;
    constexpr int ESPACO_ENTRE_REPETICOES_NOME_PX = 24;
    constexpr unsigned long INTERVALO_VERIFICACAO_RELOGIO_MS =
        1000;

    bool rolagemNomeAtiva = false;
    int posicaoHorizontalNomeRadioPx =
        MARGEM_HORIZONTAL_NOME_RADIO_PX;
    uint16_t larguraNomeRadioPx = 0;
    unsigned long momentoUltimoPassoRolagemNomeMs = 0;

    unsigned long momentoUltimaVerificacaoRelogioMs = 0;
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

    String formatarDataHoraParaDisplay() {
        struct tm dataHora;

        if (!obterDataHoraLocal(dataHora)) {
            return "--/--/---- --:--";
        }

        char texto[17];

        strftime(
            texto,
            sizeof(texto),
            "%d/%m/%Y %H:%M",
            &dataHora
        );

        return String(texto);
    }

    void reiniciarRolagemNome() {
        display.setTextSize(TAMANHO_TEXTO_NOME_RADIO);

        int16_t x1;
        int16_t y1;
        uint16_t altura;

        display.getTextBounds(
            nomeRadioAtual,
            0,
            0,
            &x1,
            &y1,
            &larguraNomeRadioPx,
            &altura
        );

        if (
            larguraNomeRadioPx <=
            DISPLAY_LARGURA -
                2 * MARGEM_HORIZONTAL_NOME_RADIO_PX
        ) {
            posicaoHorizontalNomeRadioPx =
                (DISPLAY_LARGURA - larguraNomeRadioPx) / 2;
            rolagemNomeAtiva = false;
        } else {
            posicaoHorizontalNomeRadioPx =
                MARGEM_HORIZONTAL_NOME_RADIO_PX;
            rolagemNomeAtiva = true;
        }

        momentoUltimoPassoRolagemNomeMs = millis();
    }

    void desenharTelaRadio() {
        prepararTela();

        escreverCentralizado(
            dataHoraAtual,
            2,
            1
        );

        display.setTextSize(TAMANHO_TEXTO_NOME_RADIO);
        display.setCursor(
            posicaoHorizontalNomeRadioPx,
            POSICAO_VERTICAL_NOME_RADIO_PX
        );
        display.print(nomeRadioAtual);

        if (rolagemNomeAtiva) {
            display.setCursor(
                posicaoHorizontalNomeRadioPx +
                    larguraNomeRadioPx +
                    ESPACO_ENTRE_REPETICOES_NOME_PX,
                POSICAO_VERTICAL_NOME_RADIO_PX
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
    dataHoraAtual = formatarDataHoraParaDisplay();

    if (radioMudou || larguraNomeRadioPx == 0) {
        reiniciarRolagemNome();
    } else {
        momentoUltimoPassoRolagemNomeMs = millis();
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

void desligarDisplay() {
    if (!disponivel) {
        return;
    }

    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);

    disponivel = false;
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

    unsigned long agoraMs = millis();
    bool precisaRedesenhar = false;

    if (
        agoraMs - momentoUltimaVerificacaoRelogioMs >=
        INTERVALO_VERIFICACAO_RELOGIO_MS
    ) {
        momentoUltimaVerificacaoRelogioMs = agoraMs;

        struct tm dataHora;

        if (
            obterDataHoraLocal(dataHora) &&
            dataHora.tm_min != ultimoMinutoExibido
        ) {
            ultimoMinutoExibido = dataHora.tm_min;
            dataHoraAtual = formatarDataHoraParaDisplay();
            precisaRedesenhar = true;
        }
    }

    if (
        rolagemNomeAtiva &&
        agoraMs - momentoUltimoPassoRolagemNomeMs >=
            INTERVALO_PASSO_ROLAGEM_NOME_MS
    ) {
        momentoUltimoPassoRolagemNomeMs = agoraMs;
        posicaoHorizontalNomeRadioPx--;

        int larguraCicloRolagemPx =
            larguraNomeRadioPx +
            ESPACO_ENTRE_REPETICOES_NOME_PX;

        if (
            posicaoHorizontalNomeRadioPx <=
            MARGEM_HORIZONTAL_NOME_RADIO_PX -
                larguraCicloRolagemPx
        ) {
            posicaoHorizontalNomeRadioPx +=
                larguraCicloRolagemPx;
        }

        precisaRedesenhar = true;
    }

    if (precisaRedesenhar) {
        desenharTelaRadio();
    }
}
