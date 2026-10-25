#include "ui_tft_poc.h"

#if RADIO_UI_TFT_POC

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "ui_tft_fontes_aa.h"
#include "ui_tft_paleta.h"

namespace {

    constexpr int ALTURA_TOPO = 27;
    constexpr int TOPO_RODAPE = 205;
    constexpr size_t QUANTIDADE_AMOSTRAS_LATENCIA = 64;

    Adafruit_ST7789 tft(
        &SPI,
        PIN_TFT_CS,
        PIN_TFT_DC,
        PIN_TFT_RST
    );

    bool disponivel = false;
    bool possuiEstadoAnterior = false;
    EstadoUiTftPoc estadoAnterior;
    MetricasUiTftPoc metricas;
    uint64_t somaDuracoesUs = 0;
    uint32_t ultimaSequenciaBuscandoMedida = 0;
    uint32_t ultimaSequenciaResultadoMedida = 0;

    struct HistoricoLatencia {
        uint32_t valores[QUANTIDADE_AMOSTRAS_LATENCIA] = {};
        size_t quantidade = 0;
        size_t proxima = 0;
    };

    HistoricoLatencia historicoBuscando;
    HistoricoLatencia historicoResultado;

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

    uint16_t larguraTexto(const char* texto, const FonteAa& fonte) {
        uint16_t largura = 0;
        const char* cursor = texto;

        while (*cursor != '\0') {
            GlifoFonteAa glifo;
            uint16_t codigo = proximoCodigoUtf8(cursor);
            if (localizarGlifo(fonte, codigo, glifo) != nullptr) {
                largura += glifo.avanco;
            }
        }

        return largura;
    }

    void desenharTexto(
        const char* texto,
        int x,
        int y,
        int largura,
        int altura,
        const FonteAa& fonte,
        uint16_t cor,
        uint16_t fundo = PALETA_UI_TFT_LARANJA.fundo,
        bool centralizar = false
    ) {
        if (
            largura <= 0 || altura <= 0 ||
            largura > TFT_LARGURA
        ) {
            return;
        }

        int cursorX = x;
        if (centralizar) {
            int medida = larguraTexto(texto, fonte);
            cursorX = x + max(0, (largura - medida) / 2);
        }

        int alturaLinha = fonte.ascendente + fonte.descendente;
        int linhaBase = y + max(0, (altura - alturaLinha) / 2) + fonte.ascendente;
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
            if (cursorX + glifo.avanco > x + largura) {
                break;
            }

            glifosCampo[quantidadeGlifos++] = {
                glifo,
                static_cast<int16_t>(cursorX + glifo.deslocamentoX),
                static_cast<int16_t>(linhaBase + glifo.deslocamentoY)
            };
            cursorX += glifo.avanco;
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

    const char* textoOuTraco(const char* texto) {
        return texto[0] == '\0' ? "-" : texto;
    }

    void desenharEstrutura(const EstadoUiTftPoc& estado) {
        tft.fillScreen(PALETA_UI_TFT_LARANJA.fundo);
        tft.fillRect(0, 0, TFT_LARGURA, ALTURA_TOPO, PALETA_UI_TFT_LARANJA.superficie);
        tft.fillRect(0, TOPO_RODAPE, TFT_LARGURA, TFT_ALTURA - TOPO_RODAPE, PALETA_UI_TFT_LARANJA.superficie);
        desenharTexto(
            textoOuTraco(estado.titulo),
            8, 0, 210, ALTURA_TOPO,
            FONTE_AA_PEQUENA,
            PALETA_UI_TFT_LARANJA.primaria,
            PALETA_UI_TFT_LARANJA.superficie
        );
    }

    void desenharVolume(int volume) {
        char texto[20];
        snprintf(texto, sizeof(texto), "VOL %d/%d", volume, VOLUME_MAXIMO);
        desenharTexto(
            texto,
            210, TOPO_RODAPE, 106, TFT_ALTURA - TOPO_RODAPE,
            FONTE_AA_NORMAL,
            PALETA_UI_TFT_LARANJA.primaria,
            PALETA_UI_TFT_LARANJA.superficie
        );
    }

    uint32_t desenharRadioWeb(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        if (completo || strcmp(atual.estacao, anterior->estacao) != 0) {
            desenharTexto(textoOuTraco(atual.estacao), 10, 38, 300, 34, FONTE_AA_DESTAQUE, PALETA_UI_TFT_LARANJA.primaria);
            campos++;
        }
        if (completo || strcmp(atual.faixa, anterior->faixa) != 0) {
            desenharTexto(textoOuTraco(atual.faixa), 10, 76, 300, 38, FONTE_AA_NORMAL, PALETA_UI_TFT_LARANJA.secundaria);
            campos++;
        }
        if (completo || strcmp(atual.codec, anterior->codec) != 0) {
            char texto[32];
            snprintf(texto, sizeof(texto), "CODEC  %s", textoOuTraco(atual.codec));
            desenharTexto(texto, 10, 124, 145, 25, FONTE_AA_PEQUENA, PALETA_UI_TFT_LARANJA.secundaria);
            campos++;
        }
        if (completo || atual.bitrate != anterior->bitrate) {
            char texto[32];
            if (atual.bitrate > 0) {
                snprintf(texto, sizeof(texto), "BITRATE  %lu kbps", (unsigned long)((atual.bitrate + 500) / 1000));
            } else {
                snprintf(texto, sizeof(texto), "BITRATE  -");
            }
            desenharTexto(texto, 165, 124, 145, 25, FONTE_AA_PEQUENA, PALETA_UI_TFT_LARANJA.secundaria);
            campos++;
        }
        if (completo || atual.bufferMs != anterior->bufferMs) {
            char texto[32];
            if (atual.bitrate > 0) {
                snprintf(texto, sizeof(texto), "BUFFER  %lu.%lus", (unsigned long)(atual.bufferMs / 1000), (unsigned long)((atual.bufferMs % 1000) / 100));
            } else {
                snprintf(texto, sizeof(texto), "BUFFER  -");
            }
            desenharTexto(texto, 10, 157, 145, 25, FONTE_AA_PEQUENA, PALETA_UI_TFT_LARANJA.secundaria);
            campos++;
        }
        if (completo || atual.wifiConectado != anterior->wifiConectado || atual.wifiRssi != anterior->wifiRssi) {
            char texto[32];
            if (atual.wifiConectado) {
                snprintf(texto, sizeof(texto), "WI-FI  %d dBm", atual.wifiRssi);
            } else {
                snprintf(texto, sizeof(texto), "WI-FI  -");
            }
            desenharTexto(texto, 165, 157, 145, 25, FONTE_AA_PEQUENA, PALETA_UI_TFT_LARANJA.secundaria);
            campos++;
        }
        return campos;
    }

    uint32_t desenharRadioFm(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo,
        bool& estadoOperacaoRedesenhado
    ) {
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
                PALETA_UI_TFT_LARANJA.primaria,
                PALETA_UI_TFT_LARANJA.fundo,
                true
            );
            campos++;
        }

        if (
            completo ||
            atual.fmBuscando != anterior->fmBuscando ||
            strcmp(atual.estadoFm, anterior->estadoFm) != 0
        ) {
            estadoOperacaoRedesenhado = true;
            char texto[80];
            if (atual.fmBuscando) {
                snprintf(texto, sizeof(texto), "MHz  ·  BUSCANDO");
            } else if (atual.estadoFm[0] != '\0') {
                snprintf(texto, sizeof(texto), "MHz  ·  %s", atual.estadoFm);
            } else {
                snprintf(texto, sizeof(texto), "MHz");
            }
            desenharTexto(
                texto,
                8, 95, 304, 18,
                FONTE_AA_PEQUENA,
                atual.fmBuscando
                    ? PALETA_UI_TFT_LARANJA.primaria
                    : PALETA_UI_TFT_LARANJA.secundaria,
                PALETA_UI_TFT_LARANJA.fundo,
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
                PALETA_UI_TFT_LARANJA.primaria,
                PALETA_UI_TFT_LARANJA.fundo,
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
                PALETA_UI_TFT_LARANJA.secundaria,
                PALETA_UI_TFT_LARANJA.fundo,
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
                PALETA_UI_TFT_LARANJA.secundaria,
                PALETA_UI_TFT_LARANJA.fundo,
                true
            );
            campos++;
        }

        return campos;
    }

    uint32_t desenharRelogio(
        const EstadoUiTftPoc& atual,
        const EstadoUiTftPoc* anterior,
        bool completo
    ) {
        uint32_t campos = 0;
        if (completo || atual.horarioDisponivel != anterior->horarioDisponivel || atual.hora != anterior->hora || atual.minuto != anterior->minuto) {
            char texto[12];
            if (atual.horarioDisponivel) {
                snprintf(texto, sizeof(texto), "%02u:%02u", atual.hora, atual.minuto);
            } else {
                snprintf(texto, sizeof(texto), "--:--");
            }
            desenharTexto(texto, 10, 49, 300, 72, FONTE_AA_FREQUENCIA, PALETA_UI_TFT_LARANJA.primaria, PALETA_UI_TFT_LARANJA.fundo, true);
            campos++;
        }
        if (completo || strcmp(atual.data, anterior->data) != 0) {
            desenharTexto(textoOuTraco(atual.data), 10, 132, 300, 34, FONTE_AA_NORMAL, PALETA_UI_TFT_LARANJA.secundaria, PALETA_UI_TFT_LARANJA.fundo, true);
            campos++;
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
        desenharTexto(textoOuTraco(atual.mensagem), 10, 82, 300, 52, FONTE_AA_DESTAQUE, PALETA_UI_TFT_LARANJA.primaria, PALETA_UI_TFT_LARANJA.fundo, true);
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
    tft.fillScreen(PALETA_UI_TFT_LARANJA.fundo);

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

    bool completo =
        !possuiEstadoAnterior ||
        estado.tela != estadoAnterior.tela;
    bool tituloMudou =
        completo ||
        strcmp(estado.titulo, estadoAnterior.titulo) != 0;
    bool volumeMudou =
        completo ||
        estado.volume != estadoAnterior.volume;

    if (!completo && !tituloMudou && !volumeMudou && estado.revisao == estadoAnterior.revisao) {
        return;
    }

    uint32_t inicioUs = micros();
    uint32_t campos = 0;
    bool estadoOperacaoFmRedesenhado = false;
    bool conteudoFmRedesenhado = false;

    if (completo) {
        desenharEstrutura(estado);
        campos += 3;
    } else if (tituloMudou) {
        desenharTexto(
            textoOuTraco(estado.titulo),
            8, 0, 210, ALTURA_TOPO,
            FONTE_AA_PEQUENA,
            PALETA_UI_TFT_LARANJA.primaria,
            PALETA_UI_TFT_LARANJA.superficie
        );
        campos++;
    }

    switch (estado.tela) {
        case TelaUiTftPoc::RADIO_WEB:
            campos += desenharRadioWeb(estado, &estadoAnterior, completo);
            break;
        case TelaUiTftPoc::RADIO_FM:
        {
            uint32_t camposAntes = campos;
            campos += desenharRadioFm(
                estado,
                &estadoAnterior,
                completo,
                estadoOperacaoFmRedesenhado
            );
            conteudoFmRedesenhado = campos > camposAntes;
            break;
        }
        case TelaUiTftPoc::RELOGIO:
            campos += desenharRelogio(estado, &estadoAnterior, completo);
            break;
        case TelaUiTftPoc::MENSAGEM:
        case TelaUiTftPoc::CONFIGURACAO_WIFI:
        case TelaUiTftPoc::ALARME:
            campos += desenharMensagem(estado, &estadoAnterior, completo);
            break;
    }

    if (
        volumeMudou &&
        estado.tela != TelaUiTftPoc::RADIO_FM
    ) {
        desenharVolume(estado.volume);
        campos++;
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

    estadoAnterior = estado;
    possuiEstadoAnterior = true;
}

MetricasUiTftPoc obterMetricasUiTftPoc() {
    return metricas;
}

#endif
