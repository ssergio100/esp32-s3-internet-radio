#include "display_radio.h"
#include "audio_radio.h"
#include "configuracao.h"

#include <Wire.h>
#include <WiFi.h>
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

    constexpr int MARGEM_HORIZONTAL_DIAGNOSTICO_PX = 2;
    constexpr int POSICAO_VERTICAL_DIAGNOSTICO_PX = 2;
    constexpr uint8_t TAMANHO_TEXTO_DIAGNOSTICO = 1;
    constexpr int ESPACO_ENTRE_REPETICOES_DIAGNOSTICO_PX = 24;

    constexpr int MARGEM_HORIZONTAL_NOME_RADIO_PX = 2;
    constexpr int POSICAO_VERTICAL_NOME_RADIO_PX = 24;
    constexpr uint8_t TAMANHO_TEXTO_NOME_RADIO = 2;
    constexpr int ESPACO_ENTRE_REPETICOES_NOME_PX = 24;

    String textoDiagnostico;
    bool rolagemDiagnosticoAtiva = false;
    int posicaoHorizontalDiagnosticoPx =
        MARGEM_HORIZONTAL_DIAGNOSTICO_PX;
    uint16_t larguraDiagnosticoPx = 0;
    unsigned long momentoUltimoPassoRolagemDiagnosticoMs = 0;
    unsigned long momentoUltimaAtualizacaoDiagnosticoMs = 0;

    bool rolagemNomeAtiva = false;
    int posicaoHorizontalNomeRadioPx =
        MARGEM_HORIZONTAL_NOME_RADIO_PX;
    uint16_t larguraNomeRadioPx = 0;
    unsigned long momentoUltimoPassoRolagemNomeMs = 0;

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

    String formatarBufferParaDiagnostico(
        const StatusAudio& status
    ) {
        if (status.bitrate == 0) {
            return "--";
        }

        uint32_t decimosDeSegundo =
            (status.bufferMilissegundos + 50) / 100;

        return
            String(decimosDeSegundo / 10) +
            "." +
            String(decimosDeSegundo % 10) +
            " s";
    }

    String montarTextoDiagnostico() {
        StatusAudio status = obterStatusAudio();
        String texto;
        texto.reserve(112);

        texto += (
            status.codec[0] != '\0'
                ? status.codec
                : "--"
        );
        texto += " | ";

        if (status.bitrate > 0) {
            texto += String((status.bitrate + 500) / 1000);
            texto += " kbps";
        } else {
            texto += "-- kbps";
        }

        texto += " | BUF ";
        texto += formatarBufferParaDiagnostico(status);

        if (WiFi.status() == WL_CONNECTED) {
            texto += " | RSSI ";
            texto += String(WiFi.RSSI());
            texto += " dBm | BSSID ";
            texto += WiFi.BSSIDstr();
        } else {
            texto += " | Wi-Fi desconectado";
        }

        return texto;
    }

    void medirTextoDiagnostico(
        bool reiniciarPosicao
    ) {
        display.setTextSize(TAMANHO_TEXTO_DIAGNOSTICO);

        int16_t x1;
        int16_t y1;
        uint16_t altura;

        display.getTextBounds(
            textoDiagnostico,
            0,
            0,
            &x1,
            &y1,
            &larguraDiagnosticoPx,
            &altura
        );

        if (
            larguraDiagnosticoPx <=
            DISPLAY_LARGURA -
                2 * MARGEM_HORIZONTAL_DIAGNOSTICO_PX
        ) {
            posicaoHorizontalDiagnosticoPx =
                (DISPLAY_LARGURA - larguraDiagnosticoPx) / 2;
            rolagemDiagnosticoAtiva = false;
        } else {
            rolagemDiagnosticoAtiva = true;

            if (reiniciarPosicao) {
                posicaoHorizontalDiagnosticoPx =
                    MARGEM_HORIZONTAL_DIAGNOSTICO_PX;
            }

            int larguraCicloRolagemPx =
                larguraDiagnosticoPx +
                ESPACO_ENTRE_REPETICOES_DIAGNOSTICO_PX;

            while (
                posicaoHorizontalDiagnosticoPx >=
                MARGEM_HORIZONTAL_DIAGNOSTICO_PX +
                    larguraCicloRolagemPx
            ) {
                posicaoHorizontalDiagnosticoPx -=
                    larguraCicloRolagemPx;
            }
        }

        if (reiniciarPosicao) {
            momentoUltimoPassoRolagemDiagnosticoMs = millis();
        }
    }

    bool atualizarTextoDiagnostico(
        bool forcarAtualizacao = false
    ) {
        unsigned long agoraMs = millis();

        if (
            !forcarAtualizacao &&
            agoraMs - momentoUltimaAtualizacaoDiagnosticoMs <
                INTERVALO_ATUALIZACAO_DIAGNOSTICO_DISPLAY_MS
        ) {
            return false;
        }

        momentoUltimaAtualizacaoDiagnosticoMs = agoraMs;
        String novoTexto = montarTextoDiagnostico();

        if (novoTexto == textoDiagnostico) {
            return false;
        }

        textoDiagnostico = novoTexto;
        medirTextoDiagnostico(false);

        return true;
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

        display.setTextSize(TAMANHO_TEXTO_DIAGNOSTICO);
        display.setCursor(
            posicaoHorizontalDiagnosticoPx,
            POSICAO_VERTICAL_DIAGNOSTICO_PX
        );
        display.print(textoDiagnostico);

        if (rolagemDiagnosticoAtiva) {
            display.setCursor(
                posicaoHorizontalDiagnosticoPx -
                    larguraDiagnosticoPx -
                    ESPACO_ENTRE_REPETICOES_DIAGNOSTICO_PX,
                POSICAO_VERTICAL_DIAGNOSTICO_PX
            );
            display.print(textoDiagnostico);
        }

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

    bool entrandoNaTelaRadio =
        telaAtual != TelaDisplay::RADIO;

    telaAtual = TelaDisplay::RADIO;
    atualizarTextoDiagnostico(true);

    if (entrandoNaTelaRadio || larguraDiagnosticoPx == 0) {
        medirTextoDiagnostico(true);
    }

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

    if (atualizarTextoDiagnostico()) {
        precisaRedesenhar = true;
    }

    if (
        rolagemDiagnosticoAtiva &&
        agoraMs - momentoUltimoPassoRolagemDiagnosticoMs >=
            INTERVALO_PASSO_ROLAGEM_DIAGNOSTICO_MS
    ) {
        momentoUltimoPassoRolagemDiagnosticoMs = agoraMs;
        posicaoHorizontalDiagnosticoPx++;

        int larguraCicloRolagemPx =
            larguraDiagnosticoPx +
            ESPACO_ENTRE_REPETICOES_DIAGNOSTICO_PX;

        if (
            posicaoHorizontalDiagnosticoPx >=
            MARGEM_HORIZONTAL_DIAGNOSTICO_PX +
                larguraCicloRolagemPx
        ) {
            posicaoHorizontalDiagnosticoPx -=
                larguraCicloRolagemPx;
        }

        precisaRedesenhar = true;
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
