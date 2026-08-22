#include "display_radio.h"
#include "audio_radio.h"
#include "configuracao.h"
#include "relogio.h"

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
        RADIO,
        RELOGIO,
        PLAYER,
        ALARME,
        OPCAO_ESTADO
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
        22,                                         // posição vertical, em px
        2,                                          // tamanho do texto
        2,                                          // margem horizontal, em px
        24,                                         // espaço entre cópias, em px
        INTERVALO_PASSO_ROLAGEM_NOME_MS,            // velocidade
        SentidoRolagem::PARA_ESQUERDA               // sentido do movimento
    };

    constexpr ConfiguracaoFaixaRolante CONFIGURACAO_DATA_RELOGIO = {
        55,                                         // rodapé, em px
        1,                                          // tamanho do texto
        2,                                          // margem horizontal, em px
        24,                                         // espaço entre cópias, em px
        INTERVALO_PASSO_ROLAGEM_DATA_RELOGIO_MS,    // velocidade
        SentidoRolagem::PARA_ESQUERDA               // sentido do movimento
    };

    constexpr ConfiguracaoFaixaRolante CONFIGURACAO_NOME_PLAYER = {
        21,
        2,
        2,
        24,
        INTERVALO_PASSO_ROLAGEM_PLAYER_MS,
        SentidoRolagem::PARA_ESQUERDA
    };

    String textoDiagnostico;
    String textoBufferBarraInferior;
    bool exibindoVolumeNaBarraInferior = false;
    int volumeBarraInferior = VOLUME_PADRAO;
    EstadoFaixaRolante estadoFaixaDiagnostico;
    unsigned long momentoUltimaAtualizacaoDiagnosticoMs = 0;

    EstadoFaixaRolante estadoFaixaNomeRadio;

    String textoDataRelogio;
    EstadoFaixaRolante estadoFaixaDataRelogio;
    int minutoRelogioExibido = -1;
    int diaRelogioExibido = -1;
    unsigned long momentoUltimaConsultaRelogioMs = 0;
    struct tm ultimaDataHoraRelogio = {};
    bool horarioRelogioDisponivel = false;

    String nomeArquivoPlayer;
    int indiceArquivoPlayer = 0;
    int quantidadeArquivosPlayer = 0;
    bool arquivoPlayerEmSelecao = false;
    EstadoFaixaRolante estadoFaixaNomePlayer;

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
        display.setFont();
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

    String formatarDataHoraParaDiagnostico() {
        struct tm dataHora = {};

        if (!obterDataHoraLocal(dataHora)) {
            return "--/--/---- --:--:--";
        }

        char textoDataHora[20];

        snprintf(
            textoDataHora,
            sizeof(textoDataHora),
            "%02d/%02d/%04d %02d:%02d:%02d",
            dataHora.tm_mday,
            dataHora.tm_mon + 1,
            dataHora.tm_year + 1900,
            dataHora.tm_hour,
            dataHora.tm_min,
            dataHora.tm_sec
        );

        return String(textoDataHora);
    }

    String montarTextoDiagnostico(
        const StatusAudio& status
    ) {
        String texto;
        texto.reserve(136);

        texto += formatarDataHoraParaDiagnostico();
        texto += " | ";

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

    // Chame periodicamente no loop. A posição considera todos os intervalos
    // transcorridos, mesmo quando a transferência do OLED demora mais que um
    // passo. Assim, a velocidade não fica limitada a um pixel por quadro.
    bool avancarFaixaRolante(
        const ConfiguracaoFaixaRolante& configuracao,
        EstadoFaixaRolante& estado,
        unsigned long agoraMs
    ) {
        if (!estado.ativa) {
            return false;
        }

        unsigned long intervaloPassoMs =
            max(
                configuracao.intervaloPassoMs,
                1UL
            );
        unsigned long tempoDecorridoMs =
            agoraMs - estado.momentoUltimoPassoMs;
        unsigned long quantidadePassos =
            tempoDecorridoMs / intervaloPassoMs;

        if (quantidadePassos == 0) {
            return false;
        }

        // Preserva a fração de intervalo que ainda não completou um passo.
        estado.momentoUltimoPassoMs +=
            quantidadePassos * intervaloPassoMs;

        int larguraCicloRolagemPx =
            estado.larguraTextoPx +
            configuracao.espacoEntreRepeticoesPx;

        int passoHorizontalPx =
            static_cast<int>(configuracao.sentido);

        int passosDentroDoCiclo =
            quantidadePassos % larguraCicloRolagemPx;

        if (passosDentroDoCiclo == 0) {
            return false;
        }

        estado.posicaoHorizontalPx +=
            passoHorizontalPx * passosDentroDoCiclo;

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

    String formatarDataRelogio(
        const struct tm& dataHora
    ) {
        static const char* DIAS_SEMANA[] = {
            "domingo",
            "segunda",
            "terca",
            "quarta",
            "quinta",
            "sexta",
            "sabado"
        };

        static const char* MESES[] = {
            "janeiro",
            "fevereiro",
            "marco",
            "abril",
            "maio",
            "junho",
            "julho",
            "agosto",
            "setembro",
            "outubro",
            "novembro",
            "dezembro"
        };

        return
            String(DIAS_SEMANA[dataHora.tm_wday]) +
            ", " +
            String(dataHora.tm_mday) +
            " de " +
            MESES[dataHora.tm_mon] +
            " de " +
            String(dataHora.tm_year + 1900);
    }

    void desenharDigitoFlip(
        char digito,
        int posicaoX
    ) {
        constexpr int POSICAO_Y = 1;
        constexpr int LARGURA = 24;
        constexpr int ALTURA = 44;

        display.drawRoundRect(
            posicaoX,
            POSICAO_Y,
            LARGURA,
            ALTURA,
            2,
            SSD1306_WHITE
        );

        display.setFont(&FreeSansBold9pt7b);
        display.setTextSize(2);

        String texto(digito);
        int16_t x1;
        int16_t y1;
        uint16_t largura;
        uint16_t altura;

        display.getTextBounds(
            texto,
            0,
            0,
            &x1,
            &y1,
            &largura,
            &altura
        );

        int cursorX =
            posicaoX +
            (LARGURA - largura) / 2 -
            x1;
        int linhaBase =
            POSICAO_Y +
            (ALTURA - altura) / 2 -
            y1;

        display.setCursor(cursorX, linhaBase);
        display.print(texto);

        // O corte no glifo reproduz a divisão horizontal da placa flip.
        int posicaoDivisaoY = POSICAO_Y + ALTURA / 2;
        display.drawFastHLine(
            posicaoX + 1,
            posicaoDivisaoY,
            LARGURA - 2,
            SSD1306_BLACK
        );
        display.drawPixel(
            posicaoX,
            posicaoDivisaoY,
            SSD1306_WHITE
        );
        display.drawPixel(
            posicaoX + LARGURA - 1,
            posicaoDivisaoY,
            SSD1306_WHITE
        );

        display.setFont();
        display.setTextSize(1);
    }

    void desenharTelaRelogio() {
        prepararTela();

        char horario[] = "--:--";

        if (horarioRelogioDisponivel) {
            snprintf(
                horario,
                sizeof(horario),
                "%02d:%02d",
                ultimaDataHoraRelogio.tm_hour,
                ultimaDataHoraRelogio.tm_min
            );
        }

        desenharDigitoFlip(horario[0], 9);
        desenharDigitoFlip(horario[1], 35);

        display.fillCircle(64, 15, 2, SSD1306_WHITE);
        display.fillCircle(64, 31, 2, SSD1306_WHITE);

        desenharDigitoFlip(horario[3], 69);
        desenharDigitoFlip(horario[4], 95);

        display.drawFastHLine(
            2,
            49,
            DISPLAY_LARGURA - 4,
            SSD1306_WHITE
        );

        desenharFaixaRolante(
            textoDataRelogio,
            CONFIGURACAO_DATA_RELOGIO,
            estadoFaixaDataRelogio
        );

        display.display();
    }

    void atualizarTelaRelogio(
        bool forcarAtualizacao
    ) {
        unsigned long agoraMs = millis();
        bool dataRolou = avancarFaixaRolante(
            CONFIGURACAO_DATA_RELOGIO,
            estadoFaixaDataRelogio,
            agoraMs
        );
        bool deveConsultarHorario =
            forcarAtualizacao ||
            agoraMs - momentoUltimaConsultaRelogioMs >= 500;
        bool horarioMudou = false;

        if (deveConsultarHorario) {
            struct tm dataHora = {};
            bool disponivelAgora =
                obterDataHoraLocal(dataHora);

            momentoUltimaConsultaRelogioMs = agoraMs;

            if (!disponivelAgora) {
                horarioMudou = horarioRelogioDisponivel;
                horarioRelogioDisponivel = false;

                if (textoDataRelogio != "horario indisponivel") {
                    textoDataRelogio = "horario indisponivel";
                    configurarFaixaRolante(
                        textoDataRelogio,
                        CONFIGURACAO_DATA_RELOGIO,
                        estadoFaixaDataRelogio,
                        true
                    );
                    horarioMudou = true;
                }
            } else {
                bool disponibilidadeMudou =
                    !horarioRelogioDisponivel;
                bool minutoMudou =
                    dataHora.tm_min != minutoRelogioExibido;
                bool diaMudou =
                    dataHora.tm_yday != diaRelogioExibido;

                ultimaDataHoraRelogio = dataHora;
                horarioRelogioDisponivel = true;

                if (diaMudou || textoDataRelogio.isEmpty()) {
                    textoDataRelogio =
                        formatarDataRelogio(dataHora);
                    configurarFaixaRolante(
                        textoDataRelogio,
                        CONFIGURACAO_DATA_RELOGIO,
                        estadoFaixaDataRelogio,
                        true
                    );
                }

                minutoRelogioExibido = dataHora.tm_min;
                diaRelogioExibido = dataHora.tm_yday;
                horarioMudou =
                    disponibilidadeMudou ||
                    minutoMudou ||
                    diaMudou;
            }
        }

        if (forcarAtualizacao || horarioMudou || dataRolou) {
            desenharTelaRelogio();
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

    void desenharBarraPlayer() {
        constexpr int TOPO = 46;
        constexpr int ALTURA = DISPLAY_ALTURA - TOPO;

        display.fillRect(
            0,
            TOPO,
            DISPLAY_LARGURA,
            ALTURA,
            SSD1306_WHITE
        );
        display.setTextColor(SSD1306_BLACK);
        display.setTextSize(1);

        if (exibindoVolumeNaBarraInferior) {
            constexpr int BARRA_X = 3;
            constexpr int BARRA_Y = 51;
            constexpr int BARRA_LARGURA = 76;
            constexpr int BARRA_ALTURA = 9;

            display.drawRect(
                BARRA_X,
                BARRA_Y,
                BARRA_LARGURA,
                BARRA_ALTURA,
                SSD1306_BLACK
            );

            int preenchimento = map(
                volumeBarraInferior,
                VOLUME_MINIMO,
                VOLUME_MAXIMO,
                0,
                BARRA_LARGURA - 4
            );

            if (preenchimento > 0) {
                display.fillRect(
                    BARRA_X + 2,
                    BARRA_Y + 2,
                    preenchimento,
                    BARRA_ALTURA - 4,
                    SSD1306_BLACK
                );
            }

            display.setCursor(88, 51);
            display.printf(
                "%d/%d",
                volumeBarraInferior,
                VOLUME_MAXIMO
            );
        } else {
            display.setCursor(3, 51);
            display.print("MP3");

            String posicao =
                String(indiceArquivoPlayer + 1) +
                "/" +
                String(quantidadeArquivosPlayer);
            int largura = posicao.length() * 6;

            display.setCursor(
                DISPLAY_LARGURA - largura - 3,
                51
            );
            display.print(posicao);
        }

        display.setTextColor(SSD1306_WHITE);
    }

    void desenharTelaPlayer() {
        prepararTela();
        escreverCentralizado("PLAYER", 2, 1);

        if (arquivoPlayerEmSelecao) {
            display.setTextSize(2);

            int16_t x1;
            int16_t y1;
            uint16_t largura;
            uint16_t altura;

            display.getTextBounds(
                nomeArquivoPlayer,
                0,
                0,
                &x1,
                &y1,
                &largura,
                &altura
            );

            escreverCentralizado(
                nomeArquivoPlayer,
                largura <= DISPLAY_LARGURA - 4 ? 21 : 26,
                largura <= DISPLAY_LARGURA - 4 ? 2 : 1
            );
        } else {
            desenharFaixaRolante(
                nomeArquivoPlayer,
                CONFIGURACAO_NOME_PLAYER,
                estadoFaixaNomePlayer
            );
        }

        desenharBarraPlayer();
        display.display();
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
        PIN_BARRAMENTO_I2C_SDA,
        PIN_BARRAMENTO_I2C_SCL
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
    if (!disponivel) {
        return;
    }

    volumeBarraInferior = volume;
    exibindoVolumeNaBarraInferior = true;

    if (telaAtual == TelaDisplay::RADIO) {
        desenharTelaRadio();
    } else if (telaAtual == TelaDisplay::PLAYER) {
        desenharTelaPlayer();
    }
}

void mostrarTelaRelogio() {
    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::RELOGIO;
    minutoRelogioExibido = -1;
    diaRelogioExibido = -1;
    momentoUltimaConsultaRelogioMs = 0;
    horarioRelogioDisponivel = false;

    atualizarTelaRelogio(true);
}

void mostrarOpcaoEstado(const String& opcao) {
    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::OPCAO_ESTADO;
    prepararTela();

    if (opcao == "RADIO WEB") {
        escreverCentralizado("RADIO", 7, 3);
        escreverCentralizado("WEB", 37, 3);
    } else {
        escreverCentralizado(opcao, 21, 3);
    }

    display.display();
}

void mostrarArquivoPlayer(
    const String& nome,
    int indiceAtual,
    int quantidadeArquivos,
    bool emSelecao
) {
    if (!disponivel) {
        return;
    }

    bool arquivoMudou =
        nomeArquivoPlayer != nome ||
        indiceArquivoPlayer != indiceAtual;

    nomeArquivoPlayer = nome;
    indiceArquivoPlayer = indiceAtual;
    quantidadeArquivosPlayer = quantidadeArquivos;
    arquivoPlayerEmSelecao = emSelecao;
    exibindoVolumeNaBarraInferior = false;
    telaAtual = TelaDisplay::PLAYER;

    if (arquivoMudou || estadoFaixaNomePlayer.larguraTextoPx == 0) {
        configurarFaixaRolante(
            nomeArquivoPlayer,
            CONFIGURACAO_NOME_PLAYER,
            estadoFaixaNomePlayer,
            true
        );
    }

    desenharTelaPlayer();
}

void mostrarAlarme(const String& nome) {
    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::ALARME;
    prepararTela();

    escreverCentralizado("ALARME", 2, 2);

    String nomeExibido = nome;

    if (nomeExibido.length() > 19) {
        nomeExibido = nomeExibido.substring(0, 16) + "...";
    }

    escreverCentralizado(nomeExibido, 27, 1);

    display.fillRect(
        0,
        46,
        DISPLAY_LARGURA,
        DISPLAY_ALTURA - 46,
        SSD1306_WHITE
    );
    display.setTextColor(SSD1306_BLACK);
    escreverCentralizado("CLIQUE PARA PARAR", 51, 1);
    display.setTextColor(SSD1306_WHITE);

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

    if (telaAtual == TelaDisplay::RELOGIO) {
        atualizarTelaRelogio(false);
        return;
    }

    if (telaAtual == TelaDisplay::PLAYER) {
        if (
            !arquivoPlayerEmSelecao &&
            avancarFaixaRolante(
                CONFIGURACAO_NOME_PLAYER,
                estadoFaixaNomePlayer,
                millis()
            )
        ) {
            desenharTelaPlayer();
        }

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
