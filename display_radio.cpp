#include "display_radio.h"
#include "audio_radio.h"
#include "configuracao.h"

#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold9pt7b.h>


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
        RADIO
    };

    TelaDisplay telaAtual = TelaDisplay::MENSAGEM;

    enum class ModoFaixaCentral {
        ROLAGEM,
        SELECAO,
        ESTADO
    };

    ModoFaixaCentral modoFaixaCentral =
        ModoFaixaCentral::ROLAGEM;

    String nomeRadioAtual;
    int indiceRadioAtual = 0;
    int quantidadeRadiosAtual = 0;

    // Os valores também representam o passo horizontal de um pixel.
    enum class SentidoRolagem {
        PARA_ESQUERDA = -1,
        PARA_DIREITA = 1
    };

    struct ConfiguracaoFaixaRolante {
        int posicaoVerticalPx;
        uint8_t tamanhoTexto;
        int margemHorizontalPx;
        int espacoEntreRepeticoesPx;
        unsigned long intervaloPassoMs;
        SentidoRolagem sentido;
    };

    struct EstadoFaixaRolante {
        bool ativa = false;
        int posicaoHorizontalPx = 0;
        uint16_t larguraTextoPx = 0;
        unsigned long momentoUltimoPassoMs = 0;
    };

    constexpr ConfiguracaoFaixaRolante CONFIGURACAO_FAIXA_DIAGNOSTICO = {
        2,                                          // posição vertical, em px
        1,                                          // tamanho do texto
        2,                                          // margem horizontal, em px
        24,                                         // espaço entre cópias, em px
        INTERVALO_PASSO_ROLAGEM_DIAGNOSTICO_MS,     // velocidade
        SentidoRolagem::PARA_ESQUERDA                // sentido do movimento
    };

    constexpr ConfiguracaoFaixaRolante CONFIGURACAO_FAIXA_NOME_RADIO = {
        24,                                         // posição vertical, em px
        2,                                          // tamanho do texto
        2,                                          // margem horizontal, em px
        24,                                         // espaço entre cópias, em px
        INTERVALO_PASSO_ROLAGEM_NOME_MS,            // velocidade
        SentidoRolagem::PARA_ESQUERDA               // sentido do movimento
    };

    String textoDiagnostico;
    String textoBufferBarraInferior;
    bool exibindoVolumeNaBarraInferior = false;
    int volumeBarraInferior = VOLUME_PADRAO;
    EstadoFaixaRolante estadoFaixaDiagnostico;
    unsigned long momentoUltimaAtualizacaoDiagnosticoMs = 0;

    EstadoFaixaRolante estadoFaixaNomeRadio;

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

    String formatarBufferParaBarraInferior(
        const StatusAudio& status
    ) {
        if (status.bitrate == 0) {
            return "B--";
        }

        uint32_t decimosDeSegundo =
            (status.bufferMilissegundos + 50) / 100;

        return
            "B" +
            String(decimosDeSegundo / 10) +
            "." +
            String(decimosDeSegundo % 10) +
            "s";
    }

    String montarTextoDiagnostico(
        const StatusAudio& status
    ) {
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

    // Use esta função quando o texto de uma faixa mudar. O texto já deve
    // estar pronto: esta função não conhece rádio, áudio nem Wi-Fi.
    // Passe reiniciarPosicao=true ao trocar completamente o conteúdo.
    void configurarFaixaRolante(
        const String& texto,
        const ConfiguracaoFaixaRolante& configuracao,
        EstadoFaixaRolante& estado,
        bool reiniciarPosicao
    ) {
        display.setTextSize(configuracao.tamanhoTexto);

        int16_t x1;
        int16_t y1;
        uint16_t altura;

        display.getTextBounds(
            texto,
            0,
            0,
            &x1,
            &y1,
            &estado.larguraTextoPx,
            &altura
        );

        if (
            estado.larguraTextoPx <=
            DISPLAY_LARGURA -
                2 * configuracao.margemHorizontalPx
        ) {
            estado.posicaoHorizontalPx =
                (DISPLAY_LARGURA - estado.larguraTextoPx) / 2;
            estado.ativa = false;
        } else {
            estado.ativa = true;

            if (reiniciarPosicao) {
                estado.posicaoHorizontalPx =
                    configuracao.margemHorizontalPx;
            }

            int larguraCicloRolagemPx =
                estado.larguraTextoPx +
                configuracao.espacoEntreRepeticoesPx;

            int passoHorizontalPx =
                static_cast<int>(configuracao.sentido);

            while (
                (
                    estado.posicaoHorizontalPx -
                    configuracao.margemHorizontalPx
                ) * passoHorizontalPx >= larguraCicloRolagemPx
            ) {
                estado.posicaoHorizontalPx -=
                    passoHorizontalPx * larguraCicloRolagemPx;
            }
        }

        if (reiniciarPosicao) {
            estado.momentoUltimoPassoMs = millis();
        }
    }

    // Desenha o texto e, quando necessário, uma segunda cópia fora da tela.
    // Essa cópia entra logo após a primeira e torna a rolagem contínua.
    void desenharFaixaRolante(
        const String& texto,
        const ConfiguracaoFaixaRolante& configuracao,
        const EstadoFaixaRolante& estado
    ) {
        display.setTextSize(configuracao.tamanhoTexto);
        display.setCursor(
            estado.posicaoHorizontalPx,
            configuracao.posicaoVerticalPx
        );
        display.print(texto);

        if (!estado.ativa) {
            return;
        }

        int larguraCicloRolagemPx =
            estado.larguraTextoPx +
            configuracao.espacoEntreRepeticoesPx;

        int passoHorizontalPx =
            static_cast<int>(configuracao.sentido);

        display.setCursor(
            estado.posicaoHorizontalPx -
                passoHorizontalPx * larguraCicloRolagemPx,
            configuracao.posicaoVerticalPx
        );
        display.print(texto);
    }

    // Chame periodicamente no loop. A função respeita o intervalo configurado,
    // move um pixel no sentido escolhido e informa se a tela deve ser redesenhada.
    bool avancarFaixaRolante(
        const ConfiguracaoFaixaRolante& configuracao,
        EstadoFaixaRolante& estado,
        unsigned long agoraMs
    ) {
        if (
            !estado.ativa ||
            agoraMs - estado.momentoUltimoPassoMs <
                configuracao.intervaloPassoMs
        ) {
            return false;
        }

        estado.momentoUltimoPassoMs = agoraMs;

        int larguraCicloRolagemPx =
            estado.larguraTextoPx +
            configuracao.espacoEntreRepeticoesPx;

        int passoHorizontalPx =
            static_cast<int>(configuracao.sentido);

        estado.posicaoHorizontalPx += passoHorizontalPx;

        if (
            (
                estado.posicaoHorizontalPx -
                configuracao.margemHorizontalPx
            ) * passoHorizontalPx >= larguraCicloRolagemPx
        ) {
            estado.posicaoHorizontalPx -=
                passoHorizontalPx * larguraCicloRolagemPx;
        }

        return true;
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
        StatusAudio status = obterStatusAudio();
        String novoTextoDiagnostico =
            montarTextoDiagnostico(status);
        String novoTextoBuffer =
            formatarBufferParaBarraInferior(status);

        bool barraInferiorMudou =
            novoTextoBuffer != textoBufferBarraInferior;

        textoBufferBarraInferior = novoTextoBuffer;

        if (novoTextoDiagnostico == textoDiagnostico) {
            return barraInferiorMudou;
        }

        textoDiagnostico = novoTextoDiagnostico;
        configurarFaixaRolante(
            textoDiagnostico,
            CONFIGURACAO_FAIXA_DIAGNOSTICO,
            estadoFaixaDiagnostico,
            false
        );

        return true;
    }

    void desenharBarraInferior() {
        constexpr int topoBarraPx = 44;
        constexpr int alturaBarraPx =
            DISPLAY_ALTURA - topoBarraPx;
        constexpr int margemHorizontalPx = 2;

        String textoDireita = exibindoVolumeNaBarraInferior
            ? String(volumeBarraInferior) +
                "/" + String(VOLUME_MAXIMO)
            : String(indiceRadioAtual + 1) +
                "/" + String(quantidadeRadiosAtual);

        display.fillRect(
            0,
            topoBarraPx,
            DISPLAY_LARGURA,
            alturaBarraPx,
            SSD1306_WHITE
        );
        display.setFont(&FreeSansBold9pt7b);
        display.setTextSize(1);
        display.setTextColor(SSD1306_BLACK);

        int16_t x1;
        int16_t y1;
        uint16_t largura;
        uint16_t altura;

        if (exibindoVolumeNaBarraInferior) {
            constexpr int barraX = 2;
            constexpr int barraY = 49;
            constexpr int barraLargura = 70;
            constexpr int barraAltura = 10;

            display.drawRect(
                barraX,
                barraY,
                barraLargura,
                barraAltura,
                SSD1306_BLACK
            );

            int larguraPreenchida = map(
                volumeBarraInferior,
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
                    SSD1306_BLACK
                );
            }
        } else {
            display.getTextBounds(
                textoBufferBarraInferior,
                0,
                0,
                &x1,
                &y1,
                &largura,
                &altura
            );
            int linhaBaseBufferPx =
                topoBarraPx +
                (alturaBarraPx - altura) / 2 -
                y1;
            display.setCursor(
                margemHorizontalPx - x1,
                linhaBaseBufferPx
            );
            display.print(textoBufferBarraInferior);
        }

        display.getTextBounds(
            textoDireita,
            0,
            0,
            &x1,
            &y1,
            &largura,
            &altura
        );
        int linhaBaseTextoPx =
            topoBarraPx +
            (alturaBarraPx - altura) / 2 -
            y1;
        display.setCursor(
            DISPLAY_LARGURA - margemHorizontalPx - largura - x1,
            linhaBaseTextoPx
        );
        display.print(textoDireita);

        display.setFont();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
    }

    void desenharSelecaoNaFaixaCentral() {
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
                22,
                2
            );
        } else {
            escreverCentralizado(
                nomeRadioAtual,
                27,
                1
            );
        }
    }

    void desenharTelaRadio() {
        prepararTela();

        desenharFaixaRolante(
            textoDiagnostico,
            CONFIGURACAO_FAIXA_DIAGNOSTICO,
            estadoFaixaDiagnostico
        );

        if (modoFaixaCentral == ModoFaixaCentral::SELECAO) {
            desenharSelecaoNaFaixaCentral();
        } else if (modoFaixaCentral == ModoFaixaCentral::ESTADO) {
            escreverCentralizado(
                nomeRadioAtual,
                27,
                1
            );
        } else {
            desenharFaixaRolante(
                nomeRadioAtual,
                CONFIGURACAO_FAIXA_NOME_RADIO,
                estadoFaixaNomeRadio
            );
        }

        desenharBarraInferior();

        display.display();
    }

    void mostrarTextoEstaticoNaFaixaCentral(
        const String& texto,
        int indiceAtual,
        int quantidadeRadios,
        ModoFaixaCentral modo
    ) {
        if (!disponivel) {
            return;
        }

        nomeRadioAtual = texto;
        indiceRadioAtual = indiceAtual;
        quantidadeRadiosAtual = quantidadeRadios;

        bool entrandoNaTelaRadio =
            telaAtual != TelaDisplay::RADIO;

        telaAtual = TelaDisplay::RADIO;
        exibindoVolumeNaBarraInferior = false;
        modoFaixaCentral = modo;
        atualizarTextoDiagnostico(true);

        if (
            entrandoNaTelaRadio ||
            estadoFaixaDiagnostico.larguraTextoPx == 0
        ) {
            configurarFaixaRolante(
                textoDiagnostico,
                CONFIGURACAO_FAIXA_DIAGNOSTICO,
                estadoFaixaDiagnostico,
                true
            );
        }

        desenharTelaRadio();
    }

}

void iniciarDisplay() {
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

        return;
    }

    prepararTela();
    mostrarMensagem("Inicializando");
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
    bool faixaCentralEstavaEstatica =
        modoFaixaCentral != ModoFaixaCentral::ROLAGEM;

    telaAtual = TelaDisplay::RADIO;
    exibindoVolumeNaBarraInferior = false;
    modoFaixaCentral = ModoFaixaCentral::ROLAGEM;
    atualizarTextoDiagnostico(true);

    if (
        entrandoNaTelaRadio ||
        estadoFaixaDiagnostico.larguraTextoPx == 0
    ) {
        configurarFaixaRolante(
            textoDiagnostico,
            CONFIGURACAO_FAIXA_DIAGNOSTICO,
            estadoFaixaDiagnostico,
            true
        );
    }

    if (
        radioMudou ||
        faixaCentralEstavaEstatica ||
        estadoFaixaNomeRadio.larguraTextoPx == 0
    ) {
        configurarFaixaRolante(
            nomeRadioAtual,
            CONFIGURACAO_FAIXA_NOME_RADIO,
            estadoFaixaNomeRadio,
            true
        );
    } else {
        estadoFaixaNomeRadio.momentoUltimoPassoMs = millis();
    }

    desenharTelaRadio();
}

void mostrarSelecaoRadio(
    const String& nome,
    int indiceSelecionado,
    int quantidadeRadios
) {
    mostrarTextoEstaticoNaFaixaCentral(
        nome,
        indiceSelecionado,
        quantidadeRadios,
        ModoFaixaCentral::SELECAO
    );
}

void mostrarEstadoRadio(
    const String& estado,
    int indiceAtual,
    int quantidadeRadios
) {
    mostrarTextoEstaticoNaFaixaCentral(
        estado,
        indiceAtual,
        quantidadeRadios,
        ModoFaixaCentral::ESTADO
    );
}

void mostrarVolume(
    int volume
) {
    if (
        !disponivel ||
        telaAtual != TelaDisplay::RADIO
    ) {
        return;
    }

    volumeBarraInferior = volume;
    exibindoVolumeNaBarraInferior = true;
    desenharTelaRadio();
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
        avancarFaixaRolante(
            CONFIGURACAO_FAIXA_DIAGNOSTICO,
            estadoFaixaDiagnostico,
            agoraMs
        )
    ) {
        precisaRedesenhar = true;
    }

    if (
        modoFaixaCentral == ModoFaixaCentral::ROLAGEM &&
        avancarFaixaRolante(
            CONFIGURACAO_FAIXA_NOME_RADIO,
            estadoFaixaNomeRadio,
            agoraMs
        )
    ) {
        precisaRedesenhar = true;
    }

    if (precisaRedesenhar) {
        desenharTelaRadio();
    }
}
