#include "ui_tft_poc.h"

#if RADIO_UI_TFT_POC

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "icones_bitmap.h"
#include "ui_tft_fontes_aa.h"
#include "ui_tft_paleta.h"

namespace {

    constexpr int ALTURA_TOPO = 27;
    constexpr int TOPO_RODAPE = 205;
    constexpr int CABECALHO_FM_TITULO_X = 8;
    constexpr int CABECALHO_FM_TITULO_LARGURA = 90;
    constexpr int CABECALHO_FM_DIREITO_X = 106;
    constexpr int CABECALHO_FM_DIREITO_LARGURA =
        TFT_LARGURA - CABECALHO_FM_DIREITO_X - 8;
    constexpr int WEB_CARTAO_X = 14;
    constexpr int WEB_CARTAO_Y = 112;
    constexpr int WEB_CARTAO_LARGURA = TFT_LARGURA - 28;
    constexpr int WEB_CARTAO_ALTURA = 48;
    constexpr int WEB_COLUNA_LARGURA = WEB_CARTAO_LARGURA / 3;
    constexpr int WEB_BUFFER_X = 16;
    constexpr int WEB_BUFFER_Y = 188;
    constexpr int WEB_BUFFER_LARGURA = TFT_LARGURA - 32;
    constexpr int WEB_BUFFER_ALTURA = 8;
    constexpr int VOLUME_SOBREPOSTO_X = 92;
    constexpr int VOLUME_SOBREPOSTO_Y = 56;
    constexpr int VOLUME_SOBREPOSTO_LARGURA = 136;
    constexpr int VOLUME_SOBREPOSTO_ALTURA = 108;
    constexpr size_t QUANTIDADE_AMOSTRAS_LATENCIA = 64;

    Adafruit_ST7789 tft(
        &SPI,
        PIN_TFT_CS,
        PIN_TFT_DC,
        PIN_TFT_RST
    );

    bool disponivel = false;
    bool possuiEstadoAnterior = false;
    bool tftLigadoAplicado = true;
    PaletaUiTft paletaAtual = PALETA_UI_TFT_LARANJA;
    EstadoUiTftPoc estadoAnterior;
    MetricasUiTftPoc metricas;
    uint64_t somaDuracoesUs = 0;
    uint32_t ultimaSequenciaBuscandoMedida = 0;
    uint32_t ultimaSequenciaResultadoMedida = 0;
    uint32_t ultimaSequenciaAlarmesMedida = 0;

    struct HistoricoLatencia {
        uint32_t valores[QUANTIDADE_AMOSTRAS_LATENCIA] = {};
        size_t quantidade = 0;
        size_t proxima = 0;
    };

    HistoricoLatencia historicoBuscando;
    HistoricoLatencia historicoResultado;
    HistoricoLatencia historicoAlarmes;

    struct GlifoPosicionado {
        GlifoFonteAa glifo;
        int16_t x;
        int16_t y;
    };

    constexpr size_t MAXIMO_GLIFOS_CAMPO = 96;
    GlifoPosicionado glifosCampo[MAXIMO_GLIFOS_CAMPO];
    uint16_t linhaCampo[TFT_LARGURA];

    uint32_t calcularPercentil95(const HistoricoLatencia& historico) {
        if (historico.quantidade == 0) {
            return 0;
        }

        uint32_t ordenadas[QUANTIDADE_AMOSTRAS_LATENCIA];
        for (size_t i = 0; i < historico.quantidade; i++) {
            ordenadas[i] = historico.valores[i];
        }

        for (size_t i = 1; i < historico.quantidade; i++) {
            uint32_t valor = ordenadas[i];
            size_t posicao = i;
            while (posicao > 0 && ordenadas[posicao - 1] > valor) {
                ordenadas[posicao] = ordenadas[posicao - 1];
                --posicao;
            }
            ordenadas[posicao] = valor;
        }

        size_t ordem =
            (historico.quantidade * 95 + 99) / 100;
        return ordenadas[ordem - 1];
    }

    void registrarAmostraLatencia(
        HistoricoLatencia& historico,
        uint32_t valorUs,
        uint32_t& ultimaUs,
        uint32_t& maximaUs,
        uint32_t& percentil95Us,
        uint32_t& quantidadeTotal
    ) {
        historico.valores[historico.proxima] = valorUs;
        historico.proxima =
            (historico.proxima + 1) % QUANTIDADE_AMOSTRAS_LATENCIA;
        if (historico.quantidade < QUANTIDADE_AMOSTRAS_LATENCIA) {
            ++historico.quantidade;
        }

        ultimaUs = valorUs;
        if (valorUs > maximaUs) {
            maximaUs = valorUs;
        }
        ++quantidadeTotal;
        percentil95Us = calcularPercentil95(historico);
    }

    const GlifoFonteAa* localizarGlifo(
        const FonteAa& fonte,
        uint16_t codigo,
        GlifoFonteAa& destino
    ) {
        for (uint16_t indice = 0; indice < fonte.quantidadeGlifos; indice++) {
            memcpy_P(
                &destino,
                fonte.glifos + indice,
                sizeof(destino)
            );

            if (destino.codigo == codigo) {
                return fonte.glifos + indice;
            }
        }

        if (codigo != '?') {
            return localizarGlifo(fonte, '?', destino);
        }

        return nullptr;
    }

    uint16_t proximoCodigoUtf8(const char*& texto) {
        uint8_t primeiro = static_cast<uint8_t>(*texto++);

        if (primeiro < 0x80) {
            return primeiro;
        }

        if ((primeiro & 0xE0) == 0xC0 && *texto != '\0') {
            uint8_t segundo = static_cast<uint8_t>(*texto++);
            return static_cast<uint16_t>(
                ((primeiro & 0x1F) << 6) |
                (segundo & 0x3F)
            );
        }

        // O conjunto da PoC cobre Latin-1; caracteres fora dele usam '?'.
        while ((*texto & 0xC0) == 0x80) {
            texto++;
        }
        return '?';
    }

    uint16_t misturarCor(
        uint16_t fundo,
        uint16_t frente,
        uint8_t alfa4
    ) {
        uint8_t fundoR = (fundo >> 11) & 0x1F;
        uint8_t fundoG = (fundo >> 5) & 0x3F;
        uint8_t fundoB = fundo & 0x1F;
        uint8_t frenteR = (frente >> 11) & 0x1F;
        uint8_t frenteG = (frente >> 5) & 0x3F;
        uint8_t frenteB = frente & 0x1F;

        uint8_t r = (fundoR * (15 - alfa4) + frenteR * alfa4 + 7) / 15;
        uint8_t g = (fundoG * (15 - alfa4) + frenteG * alfa4 + 7) / 15;
        uint8_t b = (fundoB * (15 - alfa4) + frenteB * alfa4 + 7) / 15;
        return static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }

    uint16_t larguraTexto(
        const char* texto,
        const FonteAa& fonte,
        int ajusteAvanco = 0
    ) {
        uint16_t largura = 0;
        const char* cursor = texto;

        while (*cursor != '\0') {
            GlifoFonteAa glifo;
            uint16_t codigo = proximoCodigoUtf8(cursor);
            if (localizarGlifo(fonte, codigo, glifo) != nullptr) {
                largura += max(1, glifo.avanco + ajusteAvanco);
            }
        }

        return largura;
    }

    struct CaixaTextoVisivel {
        int esquerda = 0;
        int topo = 0;
        int direita = 0;
        int base = 0;
        bool valida = false;
    };

    CaixaTextoVisivel medirTextoVisivel(
        const char* texto,
        const FonteAa& fonte,
        int ajusteAvanco = 0
    ) {
        CaixaTextoVisivel caixa;
        int cursorX = 0;
        const char* cursor = texto;

        while (*cursor != '\0') {
            GlifoFonteAa glifo;
            uint16_t codigo = proximoCodigoUtf8(cursor);
            if (localizarGlifo(fonte, codigo, glifo) == nullptr) {
                continue;
            }
            if (glifo.largura > 0 && glifo.altura > 0) {
                int esquerda = cursorX + glifo.deslocamentoX;
                int direita = esquerda + glifo.largura;
                int topo = glifo.deslocamentoY;
                int base = topo + glifo.altura;
                if (!caixa.valida) {
                    caixa.esquerda = esquerda;
                    caixa.topo = topo;
                    caixa.direita = direita;
                    caixa.base = base;
                    caixa.valida = true;
                } else {
                    caixa.esquerda = min(caixa.esquerda, esquerda);
                    caixa.topo = min(caixa.topo, topo);
                    caixa.direita = max(caixa.direita, direita);
                    caixa.base = max(caixa.base, base);
                }
            }
            cursorX += max(1, glifo.avanco + ajusteAvanco);
        }
        return caixa;
    }

    const FonteAa& maiorFonteTexto(
        const char* texto,
        int largura,
        int altura
    ) {
        if (
            FONTE_AA_DESTAQUE.ascendente +
                FONTE_AA_DESTAQUE.descendente <= altura &&
            larguraTexto(texto, FONTE_AA_DESTAQUE) <= largura
        ) {
            return FONTE_AA_DESTAQUE;
        }
        if (
            FONTE_AA_NORMAL.ascendente +
                FONTE_AA_NORMAL.descendente <= altura &&
            larguraTexto(texto, FONTE_AA_NORMAL) <= largura
        ) {
            return FONTE_AA_NORMAL;
        }
        return FONTE_AA_PEQUENA;
    }

    const FonteAa& maiorFonteValorConfiguracao(
        const char* texto,
        int largura,
        int altura
    ) {
        if (
            FONTE_AA_VALOR.ascendente +
                FONTE_AA_VALOR.descendente <= altura &&
            larguraTexto(texto, FONTE_AA_VALOR) <= largura
        ) {
            return FONTE_AA_VALOR;
        }
        return maiorFonteTexto(texto, largura, altura);
    }

    void desenharTexto(
        const char* texto,
        int x,
        int y,
        int largura,
        int altura,
        const FonteAa& fonte,
        uint16_t cor,
        uint16_t fundo = paletaAtual.fundo,
        bool centralizar = false,
        bool alinharDireita = false,
        int deslocamentoVertical = 0,
        int deslocamentoHorizontal = 0,
        int ajusteAvanco = 0
    ) {
        if (
            largura <= 0 || altura <= 0 ||
            largura > TFT_LARGURA
        ) {
            return;
        }

        int cursorX = x + deslocamentoHorizontal;
        if (centralizar) {
            int medida = larguraTexto(texto, fonte, ajusteAvanco);
            cursorX = x + max(0, (largura - medida) / 2);
        } else if (alinharDireita) {
            int medida = larguraTexto(texto, fonte, ajusteAvanco);
            cursorX = x + max(0, largura - medida);
        }

        int alturaLinha = fonte.ascendente + fonte.descendente;
        int linhaBase =
            y + max(0, (altura - alturaLinha) / 2) +
            fonte.ascendente + deslocamentoVertical;
        const char* cursor = texto;
        size_t quantidadeGlifos = 0;
        uint16_t paletaAlfa[16];

        for (uint8_t alfa = 0; alfa < 16; alfa++) {
            paletaAlfa[alfa] = misturarCor(fundo, cor, alfa);
        }

        while (
            *cursor != '\0' &&
            quantidadeGlifos < MAXIMO_GLIFOS_CAMPO
        ) {
            GlifoFonteAa glifo;
            uint16_t codigo = proximoCodigoUtf8(cursor);
            if (localizarGlifo(fonte, codigo, glifo) == nullptr) {
                continue;
            }
            int avanco = max(1, glifo.avanco + ajusteAvanco);
            if (cursorX + avanco > x + largura) {
                break;
            }

            glifosCampo[quantidadeGlifos++] = {
                glifo,
                static_cast<int16_t>(cursorX + glifo.deslocamentoX),
                static_cast<int16_t>(linhaBase + glifo.deslocamentoY)
            };
            cursorX += avanco;
        }

        // Um unico retangulo e uma unica transacao SPI por campo: a limpeza e
        // os glifos antialiasing seguem juntos e nao duplicam pixels no fio.
        tft.startWrite();
        tft.setAddrWindow(x, y, largura, altura);

        for (int linha = 0; linha < altura; linha++) {
            for (int coluna = 0; coluna < largura; coluna++) {
                linhaCampo[coluna] = fundo;
            }

            int yAbsoluto = y + linha;
            for (size_t indice = 0; indice < quantidadeGlifos; indice++) {
                const GlifoPosicionado& posicionado = glifosCampo[indice];
                const GlifoFonteAa& glifo = posicionado.glifo;
                int linhaGlifo = yAbsoluto - posicionado.y;

                if (linhaGlifo < 0 || linhaGlifo >= glifo.altura) {
                    continue;
                }

                for (int coluna = 0; coluna < glifo.largura; coluna++) {
                    int colunaDestino = posicionado.x - x + coluna;
                    if (colunaDestino < 0 || colunaDestino >= largura) {
                        continue;
                    }

                    uint32_t pixel =
                        static_cast<uint32_t>(linhaGlifo) * glifo.largura +
                        coluna;
                    uint8_t empacotado = pgm_read_byte(
                        fonte.bitmap + glifo.offset + pixel / 2
                    );
                    uint8_t alfa = (pixel & 1)
                        ? empacotado & 0x0F
                        : empacotado >> 4;
                    linhaCampo[colunaDestino] = paletaAlfa[alfa];
                }
            }

            tft.writePixels(linhaCampo, largura, true);
        }

        tft.endWrite();
    }

    void desenharTextoVisivel(
        const char* texto,
        int x,
        int y,
        int largura,
        int altura,
        const FonteAa& fonte,
        uint16_t cor,
        uint16_t fundo,
        bool centralizar = false,
        bool alinharDireita = false,
        int ajusteAvanco = 0
    ) {
        CaixaTextoVisivel caixa = medirTextoVisivel(
            texto,
            fonte,
            ajusteAvanco
        );
        if (!caixa.valida) {
            tft.fillRect(x, y, largura, altura, fundo);
            return;
        }

        int larguraVisivel = caixa.direita - caixa.esquerda;
        int esquerdaVisivel = x;
        if (centralizar) {
            esquerdaVisivel = x + max(0, (largura - larguraVisivel) / 2);
        } else if (alinharDireita) {
            esquerdaVisivel = x + max(0, largura - larguraVisivel);
        }

        int alturaLinha = fonte.ascendente + fonte.descendente;
        int basePadrao =
            max(0, (altura - alturaLinha) / 2) + fonte.ascendente;
        int deslocamentoVertical = -caixa.topo - basePadrao;
        int deslocamentoHorizontal =
            esquerdaVisivel - x - caixa.esquerda;

        desenharTexto(
            texto,
            x,
            y,
            largura,
            altura,
            fonte,
            cor,
            fundo,
            false,
            false,
            deslocamentoVertical,
            deslocamentoHorizontal,
            ajusteAvanco
        );
    }

    void desenharIconeMono1Bpp(
        const uint8_t* bitmap,
        int largura,
        int altura,
        int stride,
        int x,
        int y,
        uint16_t cor,
        uint16_t fundo
    ) {
        tft.startWrite();
        tft.setAddrWindow(x, y, largura, altura);
        for (int linha = 0; linha < altura; linha++) {
            for (int coluna = 0; coluna < largura; coluna++) {
                uint8_t bits = pgm_read_byte(
                    bitmap + linha * stride + coluna / 8
                );
                linhaCampo[coluna] =
                    bits & (0x80 >> (coluna % 8))
                        ? cor
                        : fundo;
            }
            tft.writePixels(linhaCampo, largura, true);
        }
        tft.endWrite();
    }

    void desenharIconeAlpha4(
        const uint8_t* bitmap,
        int largura,
        int altura,
        int x,
        int y,
        uint16_t cor,
        uint16_t fundo
    ) {
        const int bytesPorLinha = (largura + 1) / 2;
        uint16_t paletaAlfa[16];
        for (uint8_t alfa = 0; alfa < 16; alfa++) {
            paletaAlfa[alfa] = misturarCor(fundo, cor, alfa);
        }

        tft.startWrite();
        tft.setAddrWindow(x, y, largura, altura);
        for (int linha = 0; linha < altura; linha++) {
            for (int coluna = 0; coluna < largura; coluna++) {
                const int indice =
                    linha * bytesPorLinha + coluna / 2;
                const uint8_t empacotado = pgm_read_byte(bitmap + indice);
                const uint8_t alfa = (coluna & 1)
                    ? empacotado & 0x0F
                    : empacotado >> 4;
                linhaCampo[coluna] = paletaAlfa[alfa];
            }
            tft.writePixels(linhaCampo, largura, true);
        }
        tft.endWrite();
    }

    uint8_t coberturaRetanguloArredondado(
        int coluna,
        int linha,
        int largura,
        int altura,
        int raio
    ) {
        if (
            coluna < 0 || linha < 0 ||
            coluna >= largura || linha >= altura
        ) {
            return 0;
        }

        int raioLimitado = min(raio, min(largura, altura) / 2);
        if (raioLimitado <= 0) {
            return 15;
        }

        bool colunaNoCanto =
            coluna < raioLimitado ||
            coluna >= largura - raioLimitado;
        bool linhaNoCanto =
            linha < raioLimitado ||
            linha >= altura - raioLimitado;
        if (!colunaNoCanto || !linhaNoCanto) {
            return 15;
        }

        // Quatro amostras por eixo. As coordenadas ímpares representam o
        // centro de cada subpixel em uma grade com oito unidades por pixel.
        int centroX8 = coluna < raioLimitado
            ? raioLimitado * 8
            : (largura - raioLimitado) * 8;
        int centroY8 = linha < raioLimitado
            ? raioLimitado * 8
            : (altura - raioLimitado) * 8;
        int raio8 = raioLimitado * 8;
        int raioQuadrado = raio8 * raio8;
        uint8_t amostrasInternas = 0;

        for (int amostraY = 1; amostraY < 8; amostraY += 2) {
            int distanciaY = linha * 8 + amostraY - centroY8;
            for (int amostraX = 1; amostraX < 8; amostraX += 2) {
                int distanciaX = coluna * 8 + amostraX - centroX8;
                if (
                    distanciaX * distanciaX +
                    distanciaY * distanciaY <= raioQuadrado
                ) {
                    amostrasInternas++;
                }
            }
        }

        return static_cast<uint8_t>(
            (amostrasInternas * 15U + 8U) / 16U
        );
    }

    // Cartão e borda são compostos em uma única transferência. Apenas os
    // cantos usam superamostragem; as regiões retas têm cobertura integral.
    void desenharCartao(
        int x,
        int y,
        int largura,
        int altura,
        int raio,
        uint16_t corBorda,
        uint16_t corInterior,
        int espessura = 1,
        uint16_t corExterior = paletaAtual.fundo
    ) {
        if (
            largura <= 0 || altura <= 0 ||
            largura > TFT_LARGURA
        ) {
            return;
        }

        int raioExterno = max(
            0,
            min(raio, min(largura, altura) / 2)
        );
        int borda = max(1, espessura);
        int larguraInterior = largura - borda * 2;
        int alturaInterior = altura - borda * 2;
        int raioInterior = max(0, raioExterno - borda);

        tft.startWrite();
        tft.setAddrWindow(x, y, largura, altura);

        for (int linha = 0; linha < altura; linha++) {
            for (int coluna = 0; coluna < largura; coluna++) {
                uint8_t coberturaExterna =
                    coberturaRetanguloArredondado(
                        coluna,
                        linha,
                        largura,
                        altura,
                        raioExterno
                    );
                uint16_t corPixel = misturarCor(
                    corExterior,
                    corBorda,
                    coberturaExterna
                );

                if (larguraInterior > 0 && alturaInterior > 0) {
                    uint8_t coberturaInterna =
                        coberturaRetanguloArredondado(
                            coluna - borda,
                            linha - borda,
                            larguraInterior,
                            alturaInterior,
                            raioInterior
                        );
                    if (coberturaInterna > 0) {
                        corPixel = misturarCor(
                            corPixel,
                            corInterior,
                            coberturaInterna
                        );
                    }
                }

                linhaCampo[coluna] = corPixel;
            }

            tft.writePixels(linhaCampo, largura, true);
        }

        tft.endWrite();
    }

    const char* textoOuTraco(const char* texto) {
        return texto[0] == '\0' ? "-" : texto;
    }

    void desenharTituloCabecalho(const EstadoUiTftPoc& estado) {
        bool radioComIndicador =
            estado.tela == TelaUiTftPoc::RADIO_FM ||
            estado.tela == TelaUiTftPoc::RADIO_WEB ||
            estado.tela == TelaUiTftPoc::CONFIGURACAO_ALARMES;
        int x = 8;
        int largura = radioComIndicador
            ? CABECALHO_FM_TITULO_LARGURA
            : TFT_LARGURA - 16;
        const FonteAa& fonte = maiorFonteTexto(
            textoOuTraco(estado.titulo),
            largura,
            ALTURA_TOPO
        );
        desenharTexto(
            textoOuTraco(estado.titulo),
            radioComIndicador ? CABECALHO_FM_TITULO_X : x,
            0,
            largura,
            ALTURA_TOPO,
            fonte,
            paletaAtual.primaria,
            paletaAtual.superficie
        );
    }

    void desenharIndicadorDireitoCabecalho(
        const EstadoUiTftPoc& estado
    ) {
        int x = CABECALHO_FM_DIREITO_X;
        int largura = TFT_LARGURA - x - 8;
        desenharTexto(
            estado.indicadorTopoDireito,
            x,
            0,
            largura,
            ALTURA_TOPO,
            FONTE_AA_PEQUENA,
            paletaAtual.atenuada,
            paletaAtual.superficie,
            false,
            true
        );
    }

    void desenharEstrutura(const EstadoUiTftPoc& estado) {
        if (estado.tela == TelaUiTftPoc::RELOGIO) {
            tft.fillScreen(paletaAtual.fundo);
            return;
        }

        tft.fillScreen(paletaAtual.fundo);
        tft.fillRect(0, 0, TFT_LARGURA, ALTURA_TOPO, paletaAtual.superficie);
        tft.fillRect(
            0,
            TOPO_RODAPE,
            TFT_LARGURA,
            TFT_ALTURA - TOPO_RODAPE,
            paletaAtual.superficie
        );
        desenharTituloCabecalho(estado);
        if (
            estado.tela == TelaUiTftPoc::RADIO_FM ||
            estado.tela == TelaUiTftPoc::RADIO_WEB ||
            estado.tela == TelaUiTftPoc::CONFIGURACAO_ALARMES
        ) {
            desenharIndicadorDireitoCabecalho(estado);
        }
    }

    void desenharVolumeSobreposto(int volume) {
        desenharCartao(
            VOLUME_SOBREPOSTO_X,
            VOLUME_SOBREPOSTO_Y,
            VOLUME_SOBREPOSTO_LARGURA,
            VOLUME_SOBREPOSTO_ALTURA,
            8,
            paletaAtual.primaria,
            paletaAtual.superficie,
            2,
            paletaAtual.fundo
        );

        desenharTexto(
            "VOLUME",
            VOLUME_SOBREPOSTO_X + 8,
            VOLUME_SOBREPOSTO_Y + 6,
            VOLUME_SOBREPOSTO_LARGURA - 16,
            20,
            FONTE_AA_PEQUENA,
            paletaAtual.atenuada,
            paletaAtual.superficie,
            true
        );

        char texto[8];
        snprintf(texto, sizeof(texto), "%d", volume);
        desenharTextoVisivel(
            texto,
            VOLUME_SOBREPOSTO_X + 8,
            VOLUME_SOBREPOSTO_Y + 27,
            VOLUME_SOBREPOSTO_LARGURA - 16,
            VOLUME_SOBREPOSTO_ALTURA - 33,
            FONTE_AA_FREQUENCIA,
            paletaAtual.primaria,
            paletaAtual.superficie,
            true,
            false,
            -2
        );
    }

    void limparAreaVolumeSobreposto() {
        tft.fillRect(
            VOLUME_SOBREPOSTO_X,
            VOLUME_SOBREPOSTO_Y,
            VOLUME_SOBREPOSTO_LARGURA,
            VOLUME_SOBREPOSTO_ALTURA,
            paletaAtual.fundo
        );
    }

    bool telaAceitaVolumeSobreposto(const EstadoUiTftPoc& estado) {
        return
            estado.tela == TelaUiTftPoc::RADIO_FM ||
            estado.tela == TelaUiTftPoc::RADIO_WEB;
    }

    void desenharRodapeAlarmes(const EstadoUiTftPoc& estado) {
        const char* esquerda = "A: GIRAR · CAMPO";
        const char* direita = "B: VALOR · CLIQUE VOLTA";

        switch (estado.vistaAlarmes) {
            case VistaAlarmesUiTft::LISTA:
                esquerda = "A: GIRAR · NAVEGAR";
                direita = "A: ENTRAR · B: VOLTAR";
                break;
            case VistaAlarmesUiTft::DIAS_PERSONALIZADOS:
                esquerda = "A: GIRAR · MARCAR";
                direita = "B: VOLTAR";
                break;
            case VistaAlarmesUiTft::EDITAR_NOME:
                esquerda = "A: POSIÇÃO · AVANÇA";
                direita = "B: LETRA · VOLTAR";
                break;
            case VistaAlarmesUiTft::CONFIRMAR_EXCLUSAO:
                esquerda = "A: GIRAR · ESCOLHER";
                direita = "A: CONFIRMAR · B: VOLTAR";
                break;
            case VistaAlarmesUiTft::EDITOR:
                break;
        }

        if (estado.erroPersistenciaAlarme) {
            esquerda = "ERRO AO SALVAR";
        }

        desenharTexto(
            esquerda,
            8,
            TOPO_RODAPE,
            150,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            estado.erroPersistenciaAlarme
                ? paletaAtual.primaria
                : paletaAtual.atenuada,
            paletaAtual.superficie
        );
        desenharTexto(
            direita,
            162,
            TOPO_RODAPE,
            TFT_LARGURA - 170,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            paletaAtual.primaria,
            paletaAtual.superficie,
            false,
            true
        );
    }

    void desenharItemListaAlarmes(
        const EstadoUiTftPoc& estado,
        uint8_t indice
    ) {
        constexpr int ALTURA_ITEM = 43;
        const int y = ALTURA_TOPO + 2 + indice * ALTURA_ITEM;
        const bool selecionado =
            indice < estado.quantidadeItensAlarmes &&
            indice == estado.itemAlarmeSelecionado;
        const uint16_t fundo = selecionado
            ? paletaAtual.superficie
            : paletaAtual.fundo;

        tft.fillRect(0, y, TFT_LARGURA, ALTURA_ITEM - 1, fundo);
        if (selecionado) {
            tft.fillRect(
                0,
                y,
                4,
                ALTURA_ITEM - 1,
                paletaAtual.primaria
            );
        }
        if (indice >= estado.quantidadeItensAlarmes) {
            return;
        }

        const ItemAlarmesUiTft& item = estado.itensAlarmes[indice];
        desenharTexto(
            item.titulo,
            10,
            y,
            222,
            23,
            FONTE_AA_NORMAL,
            selecionado
                ? paletaAtual.primaria
                : paletaAtual.secundaria,
            fundo
        );
        desenharTexto(
            item.valor,
            235,
            y,
            75,
            23,
            FONTE_AA_PEQUENA,
            selecionado
                ? paletaAtual.primaria
                : paletaAtual.atenuada,
            fundo,
            false,
            true
        );
        desenharTexto(
            item.subtitulo,
            10,
            y + 22,
            300,
            18,
            FONTE_AA_PEQUENA,
            paletaAtual.atenuada,
            fundo
        );
    }

    uint32_t desenharListaAlarmes(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        uint8_t quantidadeAnterior = completo
            ? 0
            : anterior->quantidadeItensAlarmes;
        uint8_t quantidade = max(
            atual.quantidadeItensAlarmes,
            quantidadeAnterior
        );
        quantidade = min(
            quantidade,
            static_cast<uint8_t>(MAXIMO_ITENS_ALARMES_UI_TFT)
        );

        for (uint8_t indice = 0; indice < quantidade; indice++) {
            bool selecaoAfetaLinha =
                !completo &&
                atual.itemAlarmeSelecionado !=
                    anterior->itemAlarmeSelecionado &&
                (
                    indice == atual.itemAlarmeSelecionado ||
                    indice == anterior->itemAlarmeSelecionado
                );
            bool mudou =
                completo ||
                indice >= atual.quantidadeItensAlarmes ||
                indice >= quantidadeAnterior ||
                memcmp(
                    &atual.itensAlarmes[indice],
                    &anterior->itensAlarmes[indice],
                    sizeof(ItemAlarmesUiTft)
                ) != 0 ||
                selecaoAfetaLinha;
            if (mudou) {
                desenharItemListaAlarmes(atual, indice);
                campos++;
            }
        }

        if (
            completo ||
            atual.erroPersistenciaAlarme !=
                anterior->erroPersistenciaAlarme
        ) {
            desenharRodapeAlarmes(atual);
            campos += 2;
        }
        return campos;
    }

    uint32_t desenharResumoEditorAlarme(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        constexpr int Y = 32;
        constexpr int ALTURA = 30;
        if (completo) {
            tft.fillRoundRect(
                10,
                Y,
                300,
                ALTURA,
                4,
                paletaAtual.superficie
            );
        }
        if (
            completo ||
            strcmp(
                atual.horarioResumoAlarme,
                anterior->horarioResumoAlarme
            ) != 0
        ) {
            desenharTexto(
                atual.horarioResumoAlarme,
                18,
                Y,
                75,
                ALTURA,
                FONTE_AA_DESTAQUE,
                paletaAtual.primaria,
                paletaAtual.superficie
            );
            campos++;
        }
        if (
            completo ||
            strcmp(atual.resumoAlarme, anterior->resumoAlarme) != 0
        ) {
            desenharTexto(
                atual.resumoAlarme,
                98,
                Y,
                150,
                ALTURA,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            campos++;
        }
        if (
            completo ||
            strcmp(
                atual.estadoResumoAlarme,
                anterior->estadoResumoAlarme
            ) != 0
        ) {
            desenharTexto(
                atual.estadoResumoAlarme,
                252,
                Y,
                48,
                ALTURA,
                FONTE_AA_PEQUENA,
                paletaAtual.primaria,
                paletaAtual.superficie,
                false,
                true
            );
            campos++;
        }
        return campos;
    }

    uint32_t desenharEditorAlarme(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = desenharResumoEditorAlarme(
            atual,
            anterior,
            completo
        );
        const bool valorHorarioGrande =
            strcmp(atual.rotuloCampoAlarme, "HORAS") == 0 ||
            strcmp(atual.rotuloCampoAlarme, "MINUTOS") == 0;
        constexpr int CARTAO_X = 10;
        constexpr int CARTAO_Y = 68;
        constexpr int CARTAO_LARGURA = 300;
        constexpr int CARTAO_ALTURA = 112;

        if (completo) {
            desenharCartao(
                CARTAO_X,
                CARTAO_Y,
                CARTAO_LARGURA,
                CARTAO_ALTURA,
                5,
                paletaAtual.primaria,
                paletaAtual.fundo
            );
            campos++;
        }

        if (
            completo ||
            strcmp(
                atual.rotuloCampoAlarme,
                anterior->rotuloCampoAlarme
            ) != 0
        ) {
            desenharTexto(
                atual.rotuloCampoAlarme,
                22,
                valorHorarioGrande ? 70 : 75,
                276,
                20,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            strcmp(
                atual.valorCampoAlarme,
                anterior->valorCampoAlarme
            ) != 0 ||
            strcmp(
                atual.rotuloCampoAlarme,
                anterior->rotuloCampoAlarme
            ) != 0
        ) {
            const FonteAa& fonteValor = valorHorarioGrande
                ? FONTE_AA_FREQUENCIA
                : maiorFonteTexto(
                    atual.valorCampoAlarme,
                    276,
                    52
                );
            desenharTexto(
                atual.valorCampoAlarme,
                22,
                valorHorarioGrande ? 88 : 96,
                276,
                valorHorarioGrande ? 72 : 52,
                fonteValor,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            strcmp(
                atual.ajudaCampoAlarme,
                anterior->ajudaCampoAlarme
            ) != 0
        ) {
            desenharTexto(
                atual.ajudaCampoAlarme,
                22,
                valorHorarioGrande ? 160 : 150,
                276,
                valorHorarioGrande ? 18 : 22,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            atual.campoAtualAlarme != anterior->campoAtualAlarme ||
            atual.quantidadeCamposAlarme !=
                anterior->quantidadeCamposAlarme
        ) {
            tft.fillRect(
                10,
                186,
                300,
                12,
                paletaAtual.fundo
            );
            uint8_t quantidade = min(
                atual.quantidadeCamposAlarme,
                static_cast<uint8_t>(12)
            );
            int larguraTotal = quantidade == 0
                ? 0
                : quantidade * 17 - 4;
            int x = (TFT_LARGURA - larguraTotal) / 2;
            for (uint8_t indice = 0; indice < quantidade; indice++) {
                tft.fillRoundRect(
                    x + indice * 17,
                    190,
                    13,
                    3,
                    2,
                    indice == atual.campoAtualAlarme
                        ? paletaAtual.primaria
                        : paletaAtual.superficie
                );
            }
            campos++;
        }

        if (
            completo ||
            atual.erroPersistenciaAlarme !=
                anterior->erroPersistenciaAlarme
        ) {
            desenharRodapeAlarmes(atual);
            campos += 2;
        }
        return campos;
    }

    uint32_t desenharDiasAlarme(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = desenharResumoEditorAlarme(
            atual,
            anterior,
            completo
        );
        static constexpr const char* DIAS[] = {
            "DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"
        };
        if (
            completo ||
            atual.mascaraDiasAlarme != anterior->mascaraDiasAlarme ||
            atual.diaSelecionadoAlarme !=
                anterior->diaSelecionadoAlarme
        ) {
            desenharCartao(
                10,
                68,
                300,
                112,
                5,
                paletaAtual.primaria,
                paletaAtual.fundo
            );
            desenharTexto(
                "DIAS PERSONALIZADOS",
                20,
                75,
                280,
                20,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            for (uint8_t indice = 0; indice < 7; indice++) {
                int x = 18 + indice * 41;
                bool ativo =
                    (atual.mascaraDiasAlarme & (1U << indice)) != 0;
                bool selecionado =
                    atual.diaSelecionadoAlarme == indice;
                uint16_t fundo = ativo
                    ? paletaAtual.superficie
                    : paletaAtual.fundo;
                tft.fillRect(x, 105, 37, 32, fundo);
                tft.drawRect(
                    x,
                    105,
                    37,
                    32,
                    selecionado
                        ? paletaAtual.primaria
                        : paletaAtual.atenuada
                );
                desenharTexto(
                    DIAS[indice],
                    x + 2,
                    107,
                    33,
                    27,
                    FONTE_AA_PEQUENA,
                    ativo
                        ? paletaAtual.primaria
                        : paletaAtual.atenuada,
                    fundo,
                    true
                );
                if (ativo) {
                    // Mantém a seleção visível mesmo quando o cursor vai
                    // para outro dia; o contorno continua indicando apenas
                    // o dia atualmente focalizado pelo encoder A.
                    tft.fillRect(
                        x + 7,
                        133,
                        23,
                        3,
                        paletaAtual.primaria
                    );
                }
            }
            desenharTexto(
                "A gira e pressiona para marcar · B volta",
                20,
                146,
                280,
                22,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos += 10;
        }
        if (
            completo ||
            atual.erroPersistenciaAlarme !=
                anterior->erroPersistenciaAlarme
        ) {
            desenharRodapeAlarmes(atual);
            campos += 2;
        }
        return campos;
    }

    uint32_t desenharConfirmacaoExclusao(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        if (completo) {
            desenharTexto(
                "EXCLUIR ESTE ALARME?",
                20,
                55,
                280,
                28,
                FONTE_AA_DESTAQUE,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );
            desenharTexto(
                atual.resumoAlarme,
                20,
                84,
                280,
                22,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos += 2;
        }
        if (
            completo ||
            atual.confirmarExclusaoAlarme !=
                anterior->confirmarExclusaoAlarme
        ) {
            for (uint8_t indice = 0; indice < 2; indice++) {
                bool selecionado =
                    atual.confirmarExclusaoAlarme == (indice == 1);
                uint16_t fundo = selecionado
                    ? paletaAtual.superficie
                    : paletaAtual.fundo;
                int x = indice == 0 ? 42 : 166;
                desenharCartao(
                    x,
                    120,
                    112,
                    40,
                    5,
                    selecionado
                        ? paletaAtual.primaria
                        : paletaAtual.atenuada,
                    fundo
                );
                desenharTexto(
                    indice == 0 ? "CANCELAR" : "EXCLUIR",
                    x + 4,
                    122,
                    104,
                    36,
                    FONTE_AA_NORMAL,
                    selecionado
                        ? paletaAtual.primaria
                        : paletaAtual.atenuada,
                    fundo,
                    true
                );
            }
            campos += 2;
        }
        if (
            completo ||
            atual.erroPersistenciaAlarme !=
                anterior->erroPersistenciaAlarme
        ) {
            desenharRodapeAlarmes(atual);
            campos += 2;
        }
        return campos;
    }

    uint32_t desenharConfiguracaoAlarmes(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        bool vistaMudou =
            completo ||
            atual.vistaAlarmes != anterior->vistaAlarmes;
        if (vistaMudou && !completo) {
            tft.fillRect(
                0,
                ALTURA_TOPO,
                TFT_LARGURA,
                TOPO_RODAPE - ALTURA_TOPO,
                paletaAtual.fundo
            );
        }

        switch (atual.vistaAlarmes) {
            case VistaAlarmesUiTft::LISTA:
                return desenharListaAlarmes(
                    atual,
                    anterior,
                    vistaMudou
                );
            case VistaAlarmesUiTft::EDITOR:
                return desenharEditorAlarme(
                    atual,
                    anterior,
                    vistaMudou
                );
            case VistaAlarmesUiTft::DIAS_PERSONALIZADOS:
                return desenharDiasAlarme(
                    atual,
                    anterior,
                    vistaMudou
                );
            case VistaAlarmesUiTft::EDITAR_NOME:
                return desenharEditorAlarme(
                    atual,
                    anterior,
                    vistaMudou
                );
            case VistaAlarmesUiTft::CONFIRMAR_EXCLUSAO:
                return desenharConfirmacaoExclusao(
                    atual,
                    anterior,
                    vistaMudou
                );
        }
        return 0;
    }

    void copiarConfiguracoesParaCamposAlarmes(
        const EstadoUiTftPoc& origem,
        EstadoUiTftPoc& destino
    ) {
        destino.quantidadeItensAlarmes =
            origem.quantidadeItensConfiguracoes;
        destino.itemAlarmeSelecionado =
            origem.itemConfiguracaoSelecionado;
        for (
            uint8_t indice = 0;
            indice < origem.quantidadeItensConfiguracoes;
            indice++
        ) {
            destino.itensAlarmes[indice] =
                origem.itensConfiguracoes[indice];
        }
        strlcpy(
            destino.rotuloCampoAlarme,
            origem.rotuloConfiguracao,
            sizeof(destino.rotuloCampoAlarme)
        );
        strlcpy(
            destino.valorCampoAlarme,
            origem.valorConfiguracao,
            sizeof(destino.valorCampoAlarme)
        );
        strlcpy(
            destino.ajudaCampoAlarme,
            origem.ajudaConfiguracao,
            sizeof(destino.ajudaCampoAlarme)
        );
        destino.vistaAlarmes = origem.configuracoesEmLista
            ? VistaAlarmesUiTft::LISTA
            : VistaAlarmesUiTft::EDITOR;
        destino.quantidadeCamposAlarme = 0;
        destino.campoAtualAlarme = 0;
        destino.erroPersistenciaAlarme = false;
    }

    void desenharRodapeValorConfiguracao() {
        desenharTexto(
            "A: CONFIRMAR",
            8,
            TOPO_RODAPE,
            150,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            paletaAtual.atenuada,
            paletaAtual.superficie
        );
        desenharTexto(
            "B: GIRAR · CLIQUE VOLTA",
            162,
            TOPO_RODAPE,
            TFT_LARGURA - 170,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            paletaAtual.primaria,
            paletaAtual.superficie,
            false,
            true
        );
    }

    uint32_t desenharValorConfiguracao(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        constexpr int CARTAO_X = 10;
        constexpr int CARTAO_Y = 34;
        constexpr int CARTAO_LARGURA = 300;
        constexpr int CARTAO_ALTURA = 162;
        constexpr int CONTEUDO_X = 20;
        constexpr int CONTEUDO_LARGURA = 280;
        uint32_t campos = 0;

        if (completo) {
            desenharCartao(
                CARTAO_X,
                CARTAO_Y,
                CARTAO_LARGURA,
                CARTAO_ALTURA,
                5,
                paletaAtual.primaria,
                paletaAtual.fundo
            );
            campos++;
        }

        const bool rotuloMudou =
            completo ||
            anterior == nullptr ||
            strcmp(
                atual.rotuloConfiguracao,
                anterior->rotuloConfiguracao
            ) != 0;
        const bool valorMudou =
            completo ||
            anterior == nullptr ||
            strcmp(
                atual.valorConfiguracao,
                anterior->valorConfiguracao
            ) != 0;
        const bool ajudaMudou =
            completo ||
            anterior == nullptr ||
            strcmp(
                atual.ajudaConfiguracao,
                anterior->ajudaConfiguracao
            ) != 0;

        if (rotuloMudou) {
            desenharTexto(
                atual.rotuloConfiguracao,
                CONTEUDO_X,
                40,
                CONTEUDO_LARGURA,
                26,
                maiorFonteTexto(
                    atual.rotuloConfiguracao,
                    CONTEUDO_LARGURA,
                    26
                ),
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (valorMudou || rotuloMudou) {
            desenharTexto(
                atual.valorConfiguracao,
                CONTEUDO_X,
                68,
                CONTEUDO_LARGURA,
                72,
                maiorFonteValorConfiguracao(
                    atual.valorConfiguracao,
                    CONTEUDO_LARGURA,
                    72
                ),
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (ajudaMudou) {
            desenharTexto(
                atual.ajudaConfiguracao,
                CONTEUDO_X,
                148,
                CONTEUDO_LARGURA,
                38,
                maiorFonteTexto(
                    atual.ajudaConfiguracao,
                    CONTEUDO_LARGURA,
                    38
                ),
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (completo) {
            desenharRodapeValorConfiguracao();
            campos += 2;
        }
        return campos;
    }

    uint32_t desenharConfiguracoes(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        // EstadoUiTftPoc contém textos, listas e todos os dados das telas.
        // Duas instâncias locais excediam a pilha da DisplayService ao abrir
        // Configurações. Estes adaptadores pertencem somente à tarefa do
        // display e ficam em armazenamento estático, fora da pilha.
        static EstadoUiTftPoc atualMapeado;
        static EstadoUiTftPoc anteriorMapeado;
        atualMapeado = atual;
        copiarConfiguracoesParaCamposAlarmes(atual, atualMapeado);
        if (anterior != nullptr) {
            anteriorMapeado = *anterior;
            copiarConfiguracoesParaCamposAlarmes(
                *anterior,
                anteriorMapeado
            );
        }

        bool vistaMudou =
            completo ||
            anterior == nullptr ||
            atual.configuracoesEmLista !=
                anterior->configuracoesEmLista;
        if (vistaMudou && !completo) {
            tft.fillRect(
                0,
                ALTURA_TOPO,
                TFT_LARGURA,
                TOPO_RODAPE - ALTURA_TOPO,
                paletaAtual.fundo
            );
        }

        if (atual.configuracoesEmLista) {
            return desenharListaAlarmes(
                atualMapeado,
                anterior != nullptr ? &anteriorMapeado : nullptr,
                vistaMudou
            );
        }

        return desenharValorConfiguracao(
            atual,
            anterior,
            vistaMudou
        );
    }

    uint32_t desenharRodapeRadioWeb(
        bool selecao,
        bool detalhesRede
    ) {
        desenharTexto(
            "A: VOL · CLIQUE MENU",
            8,
            TOPO_RODAPE,
            148,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            paletaAtual.atenuada,
            paletaAtual.superficie
        );
        desenharTexto(
            detalhesRede
                ? "B: CLIQUE VOLTA"
                : selecao
                    ? "B: GIRA · CLIQUE OK"
                    : "B: GIRA · CLIQUE DADOS",
            160,
            TOPO_RODAPE,
            TFT_LARGURA - 168,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            paletaAtual.primaria,
            paletaAtual.superficie,
            false,
            true
        );
        return 2;
    }

    uint32_t desenharDetalhesRadioWeb(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        constexpr int LINHAS_Y[] = {58, 91, 124};

        if (completo) {
            desenharTexto(
                "REDE E INTERFACE",
                12, 31, 296, 24,
                FONTE_AA_NORMAL,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );

            for (int y : LINHAS_Y) {
                desenharCartao(
                    10, y, 300, 29, 5,
                    paletaAtual.atenuada,
                    paletaAtual.superficie
                );
            }
            desenharCartao(
                10, 157, 300, 39, 5,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            tft.drawFastVLine(
                160, 164, 25, paletaAtual.atenuada
            );

            desenharTexto(
                "REDE", 18, 58, 72, 29,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            desenharTexto(
                "MAC DO AP", 18, 91, 72, 29,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            desenharTexto(
                "INTERFACE", 18, 124, 72, 29,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            desenharTexto(
                "CANAL", 18, 157, 66, 39,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            desenharTexto(
                "SINAL", 168, 157, 55, 39,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            campos += 12;
        }

        if (
            completo ||
            atual.wifiConectado != anterior->wifiConectado ||
            strcmp(atual.wifiSsid, anterior->wifiSsid) != 0
        ) {
            const char* texto = atual.wifiConectado
                ? textoOuTraco(atual.wifiSsid)
                : "SEM CONEXÃO";
            desenharTexto(
                texto,
                94, 58, 208, 29,
                maiorFonteTexto(texto, 208, 29),
                paletaAtual.primaria,
                paletaAtual.superficie
            );
            campos++;
        }

        if (
            completo ||
            atual.wifiConectado != anterior->wifiConectado ||
            strcmp(atual.wifiBssid, anterior->wifiBssid) != 0
        ) {
            const char* texto = atual.wifiConectado
                ? textoOuTraco(atual.wifiBssid)
                : "-";
            desenharTexto(
                texto,
                94, 91, 208, 29,
                maiorFonteTexto(texto, 208, 29),
                paletaAtual.primaria,
                paletaAtual.superficie
            );
            campos++;
        }

        if (
            completo ||
            atual.wifiConectado != anterior->wifiConectado ||
            strcmp(atual.wifiIp, anterior->wifiIp) != 0
        ) {
            char texto[32];
            if (atual.wifiConectado && atual.wifiIp[0] != '\0') {
                snprintf(texto, sizeof(texto), "http://%s", atual.wifiIp);
            } else {
                snprintf(texto, sizeof(texto), "-");
            }
            desenharTexto(
                texto,
                94, 124, 208, 29,
                maiorFonteTexto(texto, 208, 29),
                paletaAtual.primaria,
                paletaAtual.superficie
            );
            campos++;
        }

        if (
            completo ||
            atual.wifiConectado != anterior->wifiConectado ||
            atual.wifiCanal != anterior->wifiCanal
        ) {
            char texto[12];
            if (atual.wifiConectado && atual.wifiCanal > 0) {
                snprintf(
                    texto,
                    sizeof(texto),
                    "%u",
                    static_cast<unsigned int>(atual.wifiCanal)
                );
            } else {
                snprintf(texto, sizeof(texto), "-");
            }
            desenharTexto(
                texto,
                84, 157, 66, 39,
                FONTE_AA_NORMAL,
                paletaAtual.primaria,
                paletaAtual.superficie,
                false,
                true
            );
            campos++;
        }

        if (
            completo ||
            atual.wifiConectado != anterior->wifiConectado ||
            atual.wifiRssi != anterior->wifiRssi
        ) {
            char texto[20];
            if (atual.wifiConectado) {
                snprintf(texto, sizeof(texto), "%d dBm", atual.wifiRssi);
            } else {
                snprintf(texto, sizeof(texto), "-");
            }
            desenharTexto(
                texto,
                221, 157, 81, 39,
                FONTE_AA_NORMAL,
                paletaAtual.primaria,
                paletaAtual.superficie,
                false,
                true
            );
            campos++;
        }

        if (completo) {
            campos += desenharRodapeRadioWeb(false, true);
        }
        return campos;
    }

    uint32_t desenharRadioWeb(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;

        bool vistaMudou =
            anterior == nullptr ||
            atual.detalhesRedeWeb != anterior->detalhesRedeWeb;
        if (vistaMudou && !completo) {
            tft.fillRect(
                0,
                ALTURA_TOPO,
                TFT_LARGURA,
                TOPO_RODAPE - ALTURA_TOPO,
                paletaAtual.fundo
            );
            tft.fillRect(
                0,
                TOPO_RODAPE,
                TFT_LARGURA,
                TFT_ALTURA - TOPO_RODAPE,
                paletaAtual.superficie
            );
        }

        completo = completo || vistaMudou;
        if (atual.detalhesRedeWeb) {
            return desenharDetalhesRadioWeb(
                atual,
                anterior,
                completo
            );
        }

        if (completo) {
            desenharCartao(
                WEB_CARTAO_X,
                WEB_CARTAO_Y,
                WEB_CARTAO_LARGURA,
                WEB_CARTAO_ALTURA,
                6,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            tft.drawFastVLine(
                WEB_CARTAO_X + WEB_COLUNA_LARGURA,
                WEB_CARTAO_Y + 8,
                WEB_CARTAO_ALTURA - 16,
                paletaAtual.atenuada
            );
            tft.drawFastVLine(
                WEB_CARTAO_X + WEB_COLUNA_LARGURA * 2,
                WEB_CARTAO_Y + 8,
                WEB_CARTAO_ALTURA - 16,
                paletaAtual.atenuada
            );
            desenharTexto(
                "CODEC",
                WEB_CARTAO_X + 1,
                WEB_CARTAO_Y + 2,
                WEB_COLUNA_LARGURA - 2,
                17,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            desenharTexto(
                "BITRATE",
                WEB_CARTAO_X + WEB_COLUNA_LARGURA + 1,
                WEB_CARTAO_Y + 2,
                WEB_COLUNA_LARGURA - 2,
                17,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            desenharTexto(
                "Wi-Fi",
                WEB_CARTAO_X + WEB_COLUNA_LARGURA * 2 + 1,
                WEB_CARTAO_Y + 2,
                WEB_COLUNA_LARGURA - 3,
                17,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            desenharTexto(
                "BUFFER DE ÁUDIO",
                16, 165, 190, 18,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada
            );
            campos += 8;
            campos += desenharRodapeRadioWeb(
                atual.selecaoWeb,
                false
            );
        } else if (atual.selecaoWeb != anterior->selecaoWeb) {
            campos += desenharRodapeRadioWeb(
                atual.selecaoWeb,
                false
            );
        }

        if (completo || strcmp(atual.estacao, anterior->estacao) != 0) {
            desenharTexto(
                textoOuTraco(atual.estacao),
                12, 36, 296, 34,
                FONTE_AA_DESTAQUE,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }
        if (completo || strcmp(atual.faixa, anterior->faixa) != 0) {
            desenharTexto(
                textoOuTraco(atual.faixa),
                12, 70, 296, 19,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos++;
        }
        if (
            completo ||
            strcmp(atual.estadoWeb, anterior->estadoWeb) != 0
        ) {
            desenharTexto(
                textoOuTraco(atual.estadoWeb),
                12, 91, 296, 18,
                FONTE_AA_PEQUENA,
                paletaAtual.secundaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }
        if (
            completo ||
            atual.dadosWebValidos != anterior->dadosWebValidos ||
            strcmp(atual.codec, anterior->codec) != 0
        ) {
            desenharTexto(
                atual.dadosWebValidos
                    ? textoOuTraco(atual.codec)
                    : "-",
                WEB_CARTAO_X + 1,
                WEB_CARTAO_Y + 19,
                WEB_COLUNA_LARGURA - 2,
                26,
                FONTE_AA_NORMAL,
                paletaAtual.primaria,
                paletaAtual.superficie,
                true
            );
            campos++;
        }
        if (
            completo ||
            atual.dadosWebValidos != anterior->dadosWebValidos ||
            atual.bitrate != anterior->bitrate
        ) {
            char texto[24];
            if (atual.dadosWebValidos && atual.bitrate > 0) {
                snprintf(
                    texto,
                    sizeof(texto),
                    "%lu kbps",
                    static_cast<unsigned long>(
                        (atual.bitrate + 500) / 1000
                    )
                );
            } else {
                snprintf(texto, sizeof(texto), "-");
            }
            desenharTexto(
                texto,
                WEB_CARTAO_X + WEB_COLUNA_LARGURA + 1,
                WEB_CARTAO_Y + 19,
                WEB_COLUNA_LARGURA - 2,
                26,
                FONTE_AA_NORMAL,
                paletaAtual.primaria,
                paletaAtual.superficie,
                true
            );
            campos++;
        }
        if (
            completo ||
            atual.dadosWebValidos != anterior->dadosWebValidos ||
            atual.bufferMs != anterior->bufferMs ||
            atual.bufferBytes != anterior->bufferBytes ||
            atual.bufferTotalBytes != anterior->bufferTotalBytes
        ) {
            char texto[24];
            if (atual.dadosWebValidos && atual.bitrate > 0) {
                snprintf(
                    texto,
                    sizeof(texto),
                    "%lu,%lu s",
                    static_cast<unsigned long>(atual.bufferMs / 1000),
                    static_cast<unsigned long>(
                        (atual.bufferMs % 1000) / 100
                    )
                );
            } else {
                snprintf(texto, sizeof(texto), "-");
            }
            desenharTexto(
                texto,
                206, 165, 98, 18,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                false,
                true
            );

            tft.fillRoundRect(
                WEB_BUFFER_X,
                WEB_BUFFER_Y,
                WEB_BUFFER_LARGURA,
                WEB_BUFFER_ALTURA,
                4,
                paletaAtual.superficie
            );

            if (
                atual.dadosWebValidos &&
                atual.bufferTotalBytes > 0
            ) {
                // A biblioteca reserva mais memória do que o fluxo alcança
                // em operação normal. Para a apresentação, metade da
                // capacidade física representa 100% da barra; telemetria e
                // duração em segundos continuam usando os valores reais.
                uint32_t capacidadeVisualBytes =
                    atual.bufferTotalBytes / 2;
                if (capacidadeVisualBytes == 0) {
                    capacidadeVisualBytes = 1;
                }
                uint32_t bytesPreenchidos = min(
                    atual.bufferBytes,
                    capacidadeVisualBytes
                );
                uint32_t larguraPreenchida =
                    static_cast<uint32_t>(
                        (
                            static_cast<uint64_t>(bytesPreenchidos) *
                            WEB_BUFFER_LARGURA
                        ) / capacidadeVisualBytes
                    );
                if (larguraPreenchida > 0) {
                    if (larguraPreenchida >= WEB_BUFFER_ALTURA) {
                        tft.fillRoundRect(
                            WEB_BUFFER_X,
                            WEB_BUFFER_Y,
                            static_cast<int>(larguraPreenchida),
                            WEB_BUFFER_ALTURA,
                            4,
                            paletaAtual.primaria
                        );
                    } else {
                        tft.fillRect(
                            WEB_BUFFER_X,
                            WEB_BUFFER_Y,
                            static_cast<int>(larguraPreenchida),
                            WEB_BUFFER_ALTURA,
                            paletaAtual.primaria
                        );
                    }
                }
            }
            campos++;
        }
        if (
            completo ||
            atual.dadosWebValidos != anterior->dadosWebValidos ||
            atual.wifiConectado != anterior->wifiConectado ||
            atual.wifiRssi != anterior->wifiRssi
        ) {
            char texto[32];
            if (atual.dadosWebValidos && atual.wifiConectado) {
                snprintf(texto, sizeof(texto), "%d dBm", atual.wifiRssi);
            } else {
                snprintf(texto, sizeof(texto), "-");
            }
            desenharTexto(
                texto,
                WEB_CARTAO_X + WEB_COLUNA_LARGURA * 2 + 1,
                WEB_CARTAO_Y + 19,
                WEB_COLUNA_LARGURA - 3,
                26,
                FONTE_AA_NORMAL,
                paletaAtual.primaria,
                paletaAtual.superficie,
                true
            );
            campos++;
        }
        return campos;
    }

    uint32_t desenharRodapeRadioFm(bool detalhesTecnicos) {
        desenharTexto(
            "A: VOL · CLIQUE MENU",
            8,
            TOPO_RODAPE,
            148,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            paletaAtual.atenuada,
            paletaAtual.superficie
        );
        desenharTexto(
            detalhesTecnicos
                ? "B: CLIQUE VOLTA"
                : "B: BUSCA · CLIQUE DADOS",
            160,
            TOPO_RODAPE,
            TFT_LARGURA - 168,
            TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_PEQUENA,
            paletaAtual.primaria,
            paletaAtual.superficie,
            false,
            true
        );
        return 2;
    }

    uint32_t desenharDetalhesRadioFm(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        if (completo) {
            desenharTexto(
                "DADOS DE RECEPÇÃO",
                8,
                32,
                304,
                28,
                FONTE_AA_DESTAQUE,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );

            constexpr int CARTAO_Y = 68;
            constexpr int CARTAO_ALTURA = 112;
            constexpr int CARTAO_LARGURA = 140;
            desenharCartao(
                12, CARTAO_Y, CARTAO_LARGURA, CARTAO_ALTURA,
                5, paletaAtual.primaria, paletaAtual.superficie
            );
            desenharCartao(
                168, CARTAO_Y, CARTAO_LARGURA, CARTAO_ALTURA,
                5, paletaAtual.primaria, paletaAtual.superficie
            );
            desenharTexto(
                "DX",
                20, 74, 124, 22,
                FONTE_AA_NORMAL,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            desenharTexto(
                "-",
                20, 99, 124, 52,
                FONTE_AA_VALOR,
                paletaAtual.primaria,
                paletaAtual.superficie,
                true
            );
            desenharTexto(
                "NÃO DISP.",
                20, 152, 124, 20,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            desenharTexto(
                "BW",
                176, 74, 124, 22,
                FONTE_AA_NORMAL,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            campos += 8;
        }

        if (
            completo ||
            atual.larguraBandaFmValida !=
                anterior->larguraBandaFmValida ||
            atual.larguraBandaFmDecimosKHz !=
                anterior->larguraBandaFmDecimosKHz
        ) {
            char texto[16];
            if (atual.larguraBandaFmValida) {
                snprintf(
                    texto,
                    sizeof(texto),
                    "%u.%u",
                    static_cast<unsigned>(
                        atual.larguraBandaFmDecimosKHz / 10
                    ),
                    static_cast<unsigned>(
                        atual.larguraBandaFmDecimosKHz % 10
                    )
                );
            } else {
                strlcpy(texto, "-", sizeof(texto));
            }
            desenharTexto(
                texto,
                176, 99, 124, 52,
                maiorFonteValorConfiguracao(texto, 124, 52),
                paletaAtual.primaria,
                paletaAtual.superficie,
                true
            );
            desenharTexto(
                atual.larguraBandaFmValida ? "kHz" : "NÃO DISP.",
                176, 152, 124, 20,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                true
            );
            campos += 2;
        }

        if (completo) {
            campos += desenharRodapeRadioFm(true);
        }
        return campos;
    }

    uint32_t desenharRadioFm(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo,
        bool& estadoOperacaoRedesenhado
    ) {
        const bool vistaMudou =
            completo ||
            atual.detalhesTecnicosFm !=
                anterior->detalhesTecnicosFm;
        if (vistaMudou && !completo) {
            tft.fillRect(
                0,
                ALTURA_TOPO,
                TFT_LARGURA,
                TOPO_RODAPE - ALTURA_TOPO,
                paletaAtual.fundo
            );
            tft.fillRect(
                0,
                TOPO_RODAPE,
                TFT_LARGURA,
                TFT_ALTURA - TOPO_RODAPE,
                paletaAtual.superficie
            );
        }
        completo = vistaMudou;
        if (atual.detalhesTecnicosFm) {
            return desenharDetalhesRadioFm(
                atual,
                anterior,
                completo
            );
        }

        uint32_t campos = 0;
        if (completo || atual.frequenciaFm10kHz != anterior->frequenciaFm10kHz) {
            char texto[16];
            if (atual.frequenciaFm10kHz > 0) {
                snprintf(texto, sizeof(texto), "%u.%02u", atual.frequenciaFm10kHz / 100, atual.frequenciaFm10kHz % 100);
            } else {
                snprintf(texto, sizeof(texto), "-.-");
            }
            desenharTexto(
                texto,
                8, 28, 304, 67,
                FONTE_AA_FREQUENCIA,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            atual.fmBuscando != anterior->fmBuscando ||
            atual.pilotoEstereoFm != anterior->pilotoEstereoFm ||
            atual.pilotoEstereoFmValido != anterior->pilotoEstereoFmValido ||
            strcmp(atual.estadoFm, anterior->estadoFm) != 0
        ) {
            estadoOperacaoRedesenhado = true;
            char texto[80];
            if (atual.fmBuscando) {
                snprintf(texto, sizeof(texto), "MHz  ·  BUSCANDO");
            } else if (atual.estadoFm[0] != '\0') {
                snprintf(texto, sizeof(texto), "MHz  ·  %s", atual.estadoFm);
            } else if (atual.pilotoEstereoFmValido) {
                snprintf(
                    texto,
                    sizeof(texto),
                    "MHz  ·  %s",
                    atual.pilotoEstereoFm ? "ESTÉREO" : "MONO"
                );
            } else {
                snprintf(texto, sizeof(texto), "MHz  ·  -");
            }
            desenharTexto(
                texto,
                8, 95, 304, 18,
                FONTE_AA_PEQUENA,
                atual.fmBuscando
                    ? paletaAtual.primaria
                    : paletaAtual.secundaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            atual.psFmValido != anterior->psFmValido ||
            strcmp(atual.psFm, anterior->psFm) != 0
        ) {
            desenharTexto(
                atual.psFmValido ? textoOuTraco(atual.psFm) : "-",
                8, 114, 304, 28,
                FONTE_AA_DESTAQUE,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            atual.radioTextoFmValido != anterior->radioTextoFmValido ||
            strcmp(atual.radioTextoFm, anterior->radioTextoFm) != 0
        ) {
            desenharTexto(
                atual.radioTextoFmValido
                    ? textoOuTraco(atual.radioTextoFm)
                    : "-",
                8, 143, 304, 24,
                FONTE_AA_PEQUENA,
                paletaAtual.secundaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            atual.sinalFmValido != anterior->sinalFmValido ||
            atual.sinalFmDecimosDbuv != anterior->sinalFmDecimosDbuv
        ) {
            char texto[40];
            if (atual.sinalFmValido) {
                int32_t sinal = atual.sinalFmDecimosDbuv;
                bool negativo = sinal < 0;
                uint32_t absoluto = static_cast<uint32_t>(
                    negativo ? -sinal : sinal
                );
                snprintf(
                    texto,
                    sizeof(texto),
                    "SINAL  %s%lu.%01lu dBµV",
                    negativo ? "-" : "",
                    static_cast<unsigned long>(absoluto / 10),
                    static_cast<unsigned long>(absoluto % 10)
                );
            } else {
                snprintf(texto, sizeof(texto), "SINAL  -");
            }
            desenharTexto(
                texto,
                8, 174, 304, 25,
                FONTE_AA_NORMAL,
                paletaAtual.secundaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        if (completo) {
            campos += desenharRodapeRadioFm(false);
        }

        return campos;
    }

    uint32_t desenharRelogio(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        if (completo) {
            tft.drawFastHLine(10, 53, 300, paletaAtual.atenuada);
            tft.drawFastHLine(10, 202, 300, paletaAtual.atenuada);
            desenharCartao(
                9,
                136,
                302,
                58,
                5,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                1,
                paletaAtual.fundo
            );
            tft.fillRect(130, 154, 1, 33, paletaAtual.atenuada);
            tft.fillRect(240, 154, 1, 33, paletaAtual.atenuada);
            tft.fillRect(108, 210, 1, 25, paletaAtual.atenuada);
            tft.fillRect(211, 210, 1, 25, paletaAtual.atenuada);
            campos += 7;
        }

        if (
            completo ||
            strcmp(atual.titulo, anterior->titulo) != 0
        ) {
            desenharTextoVisivel(
                textoOuTraco(atual.titulo),
                13, 8, 294, 18,
                FONTE_AA_DESTAQUE,
                paletaAtual.secundaria,
                paletaAtual.fundo
            );
            campos++;
        }

        if (
            completo ||
            strcmp(atual.data, anterior->data) != 0
        ) {
            desenharTextoVisivel(
                textoOuTraco(atual.data),
                13, 30, 294, 14,
                FONTE_AA_NORMAL,
                paletaAtual.atenuada,
                paletaAtual.fundo
            );
            campos++;
        }

        if (
            completo ||
            atual.horarioDisponivel != anterior->horarioDisponivel ||
            atual.hora != anterior->hora ||
            atual.minuto != anterior->minuto
        ) {
            char texto[12];
            if (atual.horarioDisponivel) {
                snprintf(texto, sizeof(texto), "%02u:%02u", atual.hora, atual.minuto);
            } else {
                snprintf(texto, sizeof(texto), "--:--");
            }
            desenharTextoVisivel(
                texto,
                14,
                59,
                228,
                71,
                FONTE_AA_RELOGIO,
                paletaAtual.primaria,
                paletaAtual.fundo,
                true,
                false,
                -5
            );
            campos++;
        }

        if (completo) {
            tft.fillRect(250, 66, 1, 63, paletaAtual.atenuada);
        }
        if (
            completo ||
            atual.horarioDisponivel != anterior->horarioDisponivel ||
            atual.segundo != anterior->segundo
        ) {
            char segundos[4];
            if (atual.horarioDisponivel) {
                snprintf(
                    segundos,
                    sizeof(segundos),
                    "%02u",
                    static_cast<unsigned>(atual.segundo)
                );
            } else {
                strlcpy(segundos, "--", sizeof(segundos));
            }
            desenharTextoVisivel(
                segundos,
                258, 77, 48, 31,
                FONTE_AA_VALOR,
                paletaAtual.secundaria,
                paletaAtual.fundo,
                true
            );
            campos++;
        }
        if (completo) {
            desenharTextoVisivel(
                "SEG",
                258, 112, 48, 11,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo,
                true
            );
            campos++;
        }

        bool proximoMudou =
            completo ||
            atual.proximoAlarmeDisponivel !=
                anterior->proximoAlarmeDisponivel ||
            strcmp(
                atual.proximoAlarmeHorario,
                anterior->proximoAlarmeHorario
            ) != 0 ||
            strcmp(
                atual.proximoAlarmeRecorrencia,
                anterior->proximoAlarmeRecorrencia
            ) != 0 ||
            strcmp(
                atual.proximoAlarmeFonte,
                anterior->proximoAlarmeFonte
            ) != 0;
        if (proximoMudou) {
            desenharCartao(
                9, 136, 302, 58, 5,
                paletaAtual.atenuada,
                paletaAtual.superficie,
                1,
                paletaAtual.fundo
            );
            tft.fillRect(130, 154, 1, 33, paletaAtual.atenuada);
            tft.fillRect(240, 154, 1, 33, paletaAtual.atenuada);

            desenharTextoVisivel(
                "PRÓXIMO ALARME",
                18, 143, 104, 11,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.superficie
            );
            if (atual.proximoAlarmeDisponivel) {
                desenharTextoVisivel(
                    atual.proximoAlarmeHorario,
                    18, 158, 104, 29,
                    FONTE_AA_VALOR,
                    paletaAtual.primaria,
                    paletaAtual.superficie
                );
                desenharTextoVisivel(
                    atual.proximoAlarmeRecorrencia,
                    137, 155, 96, 16,
                    FONTE_AA_NORMAL,
                    paletaAtual.secundaria,
                    paletaAtual.superficie,
                    true
                );
                desenharTextoVisivel(
                    atual.proximoAlarmeFonte,
                    137, 174, 96, 13,
                    FONTE_AA_PEQUENA,
                    paletaAtual.atenuada,
                    paletaAtual.superficie,
                    true
                );
                desenharIconeAlpha4(
                    ICON_BELL_A4,
                    ICON_BELL_W,
                    ICON_BELL_H,
                    268, 144,
                    paletaAtual.primaria,
                    paletaAtual.superficie
                );
                desenharTextoVisivel(
                    "ATIVO",
                    247, 174, 56, 13,
                    FONTE_AA_PEQUENA,
                    paletaAtual.secundaria,
                    paletaAtual.superficie,
                    true
                );
                campos += 6;
            } else {
                desenharTextoVisivel(
                    "--:--",
                    18, 158, 104, 29,
                    FONTE_AA_VALOR,
                    paletaAtual.atenuada,
                    paletaAtual.superficie
                );
                desenharTextoVisivel(
                    "NENHUM",
                    137, 155, 96, 16,
                    FONTE_AA_NORMAL,
                    paletaAtual.atenuada,
                    paletaAtual.superficie,
                    true
                );
                desenharTextoVisivel(
                    "ALARME ATIVO",
                    137, 174, 96, 13,
                    FONTE_AA_PEQUENA,
                    paletaAtual.atenuada,
                    paletaAtual.superficie,
                    true
                );
                campos += 4;
            }
        }

        if (
            completo ||
            atual.rtcRespondendo != anterior->rtcRespondendo ||
            atual.sincronizacaoNtpValida !=
                anterior->sincronizacaoNtpValida ||
            atual.quantidadeAlarmes != anterior->quantidadeAlarmes
        ) {
            tft.fillRect(10, 203, 300, 37, paletaAtual.fundo);
            tft.fillRect(108, 210, 1, 25, paletaAtual.atenuada);
            tft.fillRect(211, 210, 1, 25, paletaAtual.atenuada);

            desenharIconeAlpha4(
                ICON_CLOCK_A4,
                ICON_CLOCK_W,
                ICON_CLOCK_H,
                18, 212,
                paletaAtual.atenuada,
                paletaAtual.fundo
            );
            desenharTextoVisivel(
                "RTC",
                42, 208, 58, 12,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo
            );
            desenharTextoVisivel(
                atual.rtcRespondendo ? "OK" : "--",
                42, 224, 58, 12,
                FONTE_AA_PEQUENA,
                paletaAtual.secundaria,
                paletaAtual.fundo
            );

            desenharIconeAlpha4(
                ICON_GLOBE_A4,
                ICON_GLOBE_W,
                ICON_GLOBE_H,
                119, 211,
                paletaAtual.atenuada,
                paletaAtual.fundo
            );
            desenharTextoVisivel(
                "NTP",
                144, 208, 58, 12,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo
            );
            desenharTextoVisivel(
                atual.sincronizacaoNtpValida ? "OK" : "--",
                144, 224, 58, 12,
                FONTE_AA_PEQUENA,
                paletaAtual.secundaria,
                paletaAtual.fundo
            );

            desenharIconeAlpha4(
                ICON_ALARM_CLOCK_A4,
                ICON_ALARM_CLOCK_W,
                ICON_ALARM_CLOCK_H,
                220, 210,
                paletaAtual.atenuada,
                paletaAtual.fundo
            );
            desenharTextoVisivel(
                "ALARMES",
                245, 208, 62, 12,
                FONTE_AA_PEQUENA,
                paletaAtual.atenuada,
                paletaAtual.fundo
            );
            char quantidade[5];
            snprintf(
                quantidade,
                sizeof(quantidade),
                "%u",
                static_cast<unsigned>(atual.quantidadeAlarmes)
            );
            desenharTextoVisivel(
                quantidade,
                245, 224, 62, 12,
                FONTE_AA_PEQUENA,
                paletaAtual.primaria,
                paletaAtual.fundo
            );
            campos += 9;
        }
        return campos;
    }

    uint32_t desenharMensagem(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        if (!completo && strcmp(atual.mensagem, anterior->mensagem) == 0) {
            return 0;
        }
        const char* texto = textoOuTraco(atual.mensagem);
        desenharTexto(
            texto,
            10,
            82,
            300,
            52,
            maiorFonteTexto(texto, 300, 52),
            paletaAtual.primaria,
            paletaAtual.fundo,
            true
        );
        return 1;
    }

}

bool iniciarUiTftPoc() {
    uint32_t heapAntes = ESP.getFreeHeap();
    uint32_t psramAntes = ESP.getFreePsram();

    SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
    tft.init(240, 320);
    tft.setSPISpeed(FREQUENCIA_SPI_TFT_HZ);
    tft.setRotation(1);
    tft.setTextWrap(false);
    tft.fillScreen(paletaAtual.fundo);

    disponivel = tft.width() == TFT_LARGURA && tft.height() == TFT_ALTURA;
    metricas.disponivel = disponivel;
    metricas.variacaoHeapBytes = static_cast<int32_t>(ESP.getFreeHeap()) - static_cast<int32_t>(heapAntes);
    metricas.variacaoPsramBytes = static_cast<int32_t>(ESP.getFreePsram()) - static_cast<int32_t>(psramAntes);

    Serial.printf(
        "TFT PoC ST7789: %s, %dx%d, heap %+ld bytes, PSRAM %+ld bytes.\n",
        disponivel ? "iniciado" : "dimensoes inesperadas",
        tft.width(),
        tft.height(),
        static_cast<long>(metricas.variacaoHeapBytes),
        static_cast<long>(metricas.variacaoPsramBytes)
    );
    return disponivel;
}

void processarUiTftPoc(const EstadoUiTftPoc& estado) {
    if (!disponivel) {
        return;
    }

    bool temaMudou =
        !possuiEstadoAnterior ||
        estado.tema != estadoAnterior.tema;
    if (temaMudou) {
        paletaAtual = obterPaletaUiTft(estado.tema);
    }

    bool estadoTftMudou = estado.tftLigado != tftLigadoAplicado;
    if (estadoTftMudou) {
        tft.enableDisplay(estado.tftLigado);
        tftLigadoAplicado = estado.tftLigado;
    }
    if (!estado.tftLigado) {
        estadoAnterior = estado;
        possuiEstadoAnterior = true;
        return;
    }

    bool completo =
        !possuiEstadoAnterior ||
        estado.tela != estadoAnterior.tela ||
        temaMudou ||
        estadoTftMudou;
    bool tituloMudou =
        completo ||
        strcmp(estado.titulo, estadoAnterior.titulo) != 0;
    bool indicadorTopoDireitoMudou =
        completo ||
        strcmp(
            estado.indicadorTopoDireito,
            estadoAnterior.indicadorTopoDireito
        ) != 0;
    bool volumeMudou =
        completo ||
        estado.volume != estadoAnterior.volume;
    bool volumeSobrepostoMudou =
        completo ||
        estado.volumeSobreposto != estadoAnterior.volumeSobreposto;
    bool ocultandoVolumeSobreposto =
        !completo &&
        estadoAnterior.volumeSobreposto &&
        !estado.volumeSobreposto &&
        telaAceitaVolumeSobreposto(estado);
    bool redesenharConteudo = completo || ocultandoVolumeSobreposto;

    if (
        !completo &&
        !tituloMudou &&
        !indicadorTopoDireitoMudou &&
        !volumeMudou &&
        !volumeSobrepostoMudou &&
        estado.revisao == estadoAnterior.revisao
    ) {
        return;
    }

    uint32_t inicioUs = micros();
    uint32_t campos = 0;
    bool estadoOperacaoFmRedesenhado = false;
    bool conteudoFmRedesenhado = false;

    if (ocultandoVolumeSobreposto) {
        limparAreaVolumeSobreposto();
    }

    if (completo) {
        desenharEstrutura(estado);
        campos +=
            estado.tela == TelaUiTftPoc::RADIO_FM ||
            estado.tela == TelaUiTftPoc::RADIO_WEB ||
            estado.tela == TelaUiTftPoc::CONFIGURACAO_ALARMES
                ? 4
                : 3;
    } else if (
        tituloMudou &&
        estado.tela != TelaUiTftPoc::RELOGIO
    ) {
        desenharTituloCabecalho(estado);
        campos++;
    }

    if (
        !completo &&
        (
            estado.tela == TelaUiTftPoc::RADIO_FM ||
            estado.tela == TelaUiTftPoc::RADIO_WEB ||
            estado.tela == TelaUiTftPoc::CONFIGURACAO_ALARMES
        ) &&
        indicadorTopoDireitoMudou
    ) {
        desenharIndicadorDireitoCabecalho(estado);
        campos++;
    }

    switch (estado.tela) {
        case TelaUiTftPoc::RADIO_WEB:
            campos += desenharRadioWeb(
                estado,
                &estadoAnterior,
                redesenharConteudo
            );
            break;
        case TelaUiTftPoc::RADIO_FM:
        {
            uint32_t camposAntes = campos;
            campos += desenharRadioFm(
                estado,
                &estadoAnterior,
                redesenharConteudo,
                estadoOperacaoFmRedesenhado
            );
            conteudoFmRedesenhado = campos > camposAntes;
            break;
        }
        case TelaUiTftPoc::RELOGIO:
            campos += desenharRelogio(estado, &estadoAnterior, completo);
            break;
        case TelaUiTftPoc::CONFIGURACAO_ALARMES:
            campos += desenharConfiguracaoAlarmes(
                estado,
                &estadoAnterior,
                completo
            );
            break;
        case TelaUiTftPoc::CONFIGURACOES:
            campos += desenharConfiguracoes(
                estado,
                &estadoAnterior,
                completo
            );
            break;
        case TelaUiTftPoc::MENSAGEM:
        case TelaUiTftPoc::CONFIGURACAO_WIFI:
        case TelaUiTftPoc::ALARME:
            campos += desenharMensagem(estado, &estadoAnterior, completo);
            break;
    }

    if (
        estado.volumeSobreposto &&
        telaAceitaVolumeSobreposto(estado)
    ) {
        // A sobreposição é sempre o último desenho. Assim, atualizações
        // periódicas de sinal, RDS ou buffer não passam sobre a janela.
        desenharVolumeSobreposto(estado.volume);
        campos += 3;
    }

    // writePixels() usa o caminho bloqueante do Adafruit_SPITFT no ESP32;
    // o HAL espera cada transacao terminar. Portanto este instante ocorre
    // depois de os ultimos pixels terem sido enviados ao controlador.
    uint32_t fimUs = micros();
    uint32_t duracaoUs = fimUs - inicioUs;
    metricas.atualizacoes++;
    metricas.camposRedesenhados += campos;
    metricas.duracaoUltimaUs = duracaoUs;
    metricas.duracaoMaximaUs = max(metricas.duracaoMaximaUs, duracaoUs);
    somaDuracoesUs += duracaoUs;
    metricas.duracaoMediaUs = somaDuracoesUs / metricas.atualizacoes;
    metricas.latenciaUltimaMs = millis() - estado.momentoAlteracaoMs;

    if (
        estado.tela == TelaUiTftPoc::RADIO_FM &&
        estado.sequenciaEventoEncoderFm != 0
    ) {
        if (
            estado.fmBuscando &&
            estadoOperacaoFmRedesenhado &&
            estado.sequenciaEventoEncoderFm !=
                ultimaSequenciaBuscandoMedida
        ) {
            ultimaSequenciaBuscandoMedida =
                estado.sequenciaEventoEncoderFm;
            registrarAmostraLatencia(
                historicoBuscando,
                fimUs - estado.momentoEventoEncoderFmUs,
                metricas.latenciaBuscandoUltimaUs,
                metricas.latenciaBuscandoMaximaUs,
                metricas.latenciaBuscandoP95Us,
                metricas.amostrasLatenciaBuscando
            );
        }

        if (
            estado.fmBuscaConcluida &&
            conteudoFmRedesenhado &&
            estado.sequenciaEventoEncoderFm !=
                ultimaSequenciaResultadoMedida
        ) {
            ultimaSequenciaResultadoMedida =
                estado.sequenciaEventoEncoderFm;
            registrarAmostraLatencia(
                historicoResultado,
                fimUs - estado.momentoEventoEncoderFmUs,
                metricas.latenciaResultadoUltimaUs,
                metricas.latenciaResultadoMaximaUs,
                metricas.latenciaResultadoP95Us,
                metricas.amostrasLatenciaResultado
            );
        }
    }

    if (
        estado.tela == TelaUiTftPoc::CONFIGURACAO_ALARMES &&
        campos > 0 &&
        estado.sequenciaEventoEncoderAlarme != 0 &&
        estado.sequenciaEventoEncoderAlarme !=
            ultimaSequenciaAlarmesMedida
    ) {
        ultimaSequenciaAlarmesMedida =
            estado.sequenciaEventoEncoderAlarme;
        registrarAmostraLatencia(
            historicoAlarmes,
            fimUs - estado.momentoEventoEncoderAlarmeUs,
            metricas.latenciaAlarmesUltimaUs,
            metricas.latenciaAlarmesMaximaUs,
            metricas.latenciaAlarmesP95Us,
            metricas.amostrasLatenciaAlarmes
        );
    }

    estadoAnterior = estado;
    possuiEstadoAnterior = true;
}

MetricasUiTftPoc obterMetricasUiTftPoc() {
    return metricas;
}

#endif
