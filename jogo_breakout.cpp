#include "jogo_breakout.h"

#include "configuracao.h"
#include "display_radio.h"

#include <Arduino.h>
#include <math.h>

namespace {

constexpr int MARGEM_LATERAL_CAMPO_PX = 2;
constexpr int LIMITE_SUPERIOR_CAMPO_PX = 10;
constexpr int PASSO_RAQUETE_POR_TRANSICAO_PX = 4;

constexpr unsigned long DURACAO_INSTRUCOES_JOGO_MS = 1400;
constexpr unsigned long INTERVALO_QUADRO_JOGO_MS = 40;

bool ativo = false;
TelaJogoBreakout tela = TelaJogoBreakout::INSTRUCOES;

float bolaX = 63.0F;
float bolaY = 44.0F;
float velocidadeBolaX = 48.0F;
float velocidadeBolaY = -42.0F;

int raqueteX =
    (DISPLAY_LARGURA - LARGURA_RAQUETE_BREAKOUT_PX) / 2;
int pontuacao = 0;
int nivel = 1;

bool blocoVisivel[QUANTIDADE_BLOCOS_BREAKOUT] = {};

unsigned long momentoEntradaTelaMs = 0;
unsigned long momentoUltimoQuadroMs = 0;

int limitarInteiro(int valor, int minimo, int maximo) {
    if (valor < minimo) {
        return minimo;
    }

    if (valor > maximo) {
        return maximo;
    }

    return valor;
}

void prepararBlocos() {
    for (bool& visivel : blocoVisivel) {
        visivel = true;
    }
}

void reposicionarBola() {
    bolaX = DISPLAY_LARGURA / 2.0F;
    bolaY = 45.0F;

    float fatorNivel =
        1.0F + (nivel - 1) * 0.08F;

    velocidadeBolaX =
        (random(0, 2) == 0 ? -48.0F : 48.0F) *
        fatorNivel;
    velocidadeBolaY = -42.0F * fatorNivel;
}

void prepararNivel() {
    prepararBlocos();
    raqueteX =
        (DISPLAY_LARGURA - LARGURA_RAQUETE_BREAKOUT_PX) / 2;
    reposicionarBola();
}

bool todosBlocosForamDestruidos() {
    for (bool visivel : blocoVisivel) {
        if (visivel) {
            return false;
        }
    }

    return true;
}

void publicarQuadro() {
    QuadroJogoBreakout quadro;
    quadro.tela = tela;
    quadro.bolaX = static_cast<int>(lroundf(bolaX));
    quadro.bolaY = static_cast<int>(lroundf(bolaY));
    quadro.raqueteX = raqueteX;
    quadro.pontuacao = pontuacao;
    quadro.nivel = nivel;

    for (int indice = 0;
         indice < QUANTIDADE_BLOCOS_BREAKOUT;
         indice++) {
        quadro.blocoVisivel[indice] =
            blocoVisivel[indice];
    }

    mostrarJogoBreakout(quadro);
}

void tratarColisaoComParedes() {
    float limiteEsquerdo =
        static_cast<float>(MARGEM_LATERAL_CAMPO_PX);
    float limiteDireito =
        static_cast<float>(
            DISPLAY_LARGURA -
            MARGEM_LATERAL_CAMPO_PX -
            LARGURA_BOLA_BREAKOUT_PX
        );

    if (bolaX <= limiteEsquerdo) {
        bolaX = limiteEsquerdo;
        velocidadeBolaX = fabsf(velocidadeBolaX);
    } else if (bolaX >= limiteDireito) {
        bolaX = limiteDireito;
        velocidadeBolaX = -fabsf(velocidadeBolaX);
    }

    if (bolaY <= LIMITE_SUPERIOR_CAMPO_PX) {
        bolaY = LIMITE_SUPERIOR_CAMPO_PX;
        velocidadeBolaY = fabsf(velocidadeBolaY);
    }
}

void tratarColisaoComRaquete() {
    bool descendo = velocidadeBolaY > 0.0F;
    bool cruzouTopoRaquete =
        bolaY + ALTURA_BOLA_BREAKOUT_PX >=
            POSICAO_Y_RAQUETE_BREAKOUT_PX &&
        bolaY <
            POSICAO_Y_RAQUETE_BREAKOUT_PX +
            ALTURA_RAQUETE_BREAKOUT_PX;
    bool sobreRaquete =
        bolaX + LARGURA_BOLA_BREAKOUT_PX >= raqueteX &&
        bolaX <= raqueteX + LARGURA_RAQUETE_BREAKOUT_PX;

    if (!descendo || !cruzouTopoRaquete || !sobreRaquete) {
        return;
    }

    bolaY =
        POSICAO_Y_RAQUETE_BREAKOUT_PX -
        ALTURA_BOLA_BREAKOUT_PX;
    velocidadeBolaY = -fabsf(velocidadeBolaY);

    float centroRaquete =
        raqueteX + LARGURA_RAQUETE_BREAKOUT_PX / 2.0F;
    float distanciaNormalizada =
        (bolaX + LARGURA_BOLA_BREAKOUT_PX / 2.0F -
            centroRaquete) /
        (LARGURA_RAQUETE_BREAKOUT_PX / 2.0F);

    velocidadeBolaX += distanciaNormalizada * 18.0F;
    velocidadeBolaX = constrain(
        velocidadeBolaX,
        -78.0F,
        78.0F
    );
}

void tratarColisaoComBlocos() {
    for (int indice = 0;
         indice < QUANTIDADE_BLOCOS_BREAKOUT;
         indice++) {
        if (!blocoVisivel[indice]) {
            continue;
        }

        int coluna = indice % COLUNAS_BLOCOS_BREAKOUT;
        int linha = indice / COLUNAS_BLOCOS_BREAKOUT;

        int blocoX =
            POSICAO_X_PRIMEIRO_BLOCO_BREAKOUT_PX +
            coluna * (
                LARGURA_BLOCO_BREAKOUT_PX +
                ESPACO_HORIZONTAL_BLOCOS_BREAKOUT_PX
            );
        int blocoY =
            POSICAO_Y_PRIMEIRO_BLOCO_BREAKOUT_PX +
            linha * (
                ALTURA_BLOCO_BREAKOUT_PX +
                ESPACO_VERTICAL_BLOCOS_BREAKOUT_PX
            );

        bool sobrepoeHorizontalmente =
            bolaX + LARGURA_BOLA_BREAKOUT_PX >= blocoX &&
            bolaX <= blocoX + LARGURA_BLOCO_BREAKOUT_PX;
        bool sobrepoeVerticalmente =
            bolaY + ALTURA_BOLA_BREAKOUT_PX >= blocoY &&
            bolaY <= blocoY + ALTURA_BLOCO_BREAKOUT_PX;

        if (!sobrepoeHorizontalmente ||
            !sobrepoeVerticalmente) {
            continue;
        }

        blocoVisivel[indice] = false;
        pontuacao++;
        velocidadeBolaY = -velocidadeBolaY;
        return;
    }
}

void avancarPartida(unsigned long tempoDecorridoMs) {
    float tempoDecorridoSegundos =
        tempoDecorridoMs / 1000.0F;

    bolaX += velocidadeBolaX * tempoDecorridoSegundos;
    bolaY += velocidadeBolaY * tempoDecorridoSegundos;

    tratarColisaoComParedes();
    tratarColisaoComRaquete();
    tratarColisaoComBlocos();

    if (bolaY >= DISPLAY_ALTURA) {
        tela = TelaJogoBreakout::FIM_DE_JOGO;
        momentoEntradaTelaMs = millis();
        return;
    }

    if (todosBlocosForamDestruidos()) {
        nivel++;
        prepararNivel();
    }
}

}

void iniciarJogoBreakout() {
    ativo = true;
    tela = TelaJogoBreakout::INSTRUCOES;
    pontuacao = 0;
    nivel = 1;
    prepararNivel();

    momentoEntradaTelaMs = millis();
    momentoUltimoQuadroMs = momentoEntradaTelaMs;

    publicarQuadro();
    Serial.println(
        "Modo: teste do jogo Breakout"
    );
}

void encerrarJogoBreakout() {
    ativo = false;
}

void moverRaqueteJogoBreakout(
    long deslocamentoEncoder
) {
    if (!ativo || tela != TelaJogoBreakout::JOGANDO) {
        return;
    }

    long novaPosicao =
        raqueteX +
        deslocamentoEncoder *
            PASSO_RAQUETE_POR_TRANSICAO_PX;

    raqueteX = limitarInteiro(
        static_cast<int>(novaPosicao),
        MARGEM_LATERAL_CAMPO_PX,
        DISPLAY_LARGURA -
            MARGEM_LATERAL_CAMPO_PX -
            LARGURA_RAQUETE_BREAKOUT_PX
    );
}

void processarJogoBreakout() {
    if (!ativo) {
        return;
    }

    unsigned long agoraMs = millis();

    if (tela == TelaJogoBreakout::INSTRUCOES) {
        if (
            agoraMs - momentoEntradaTelaMs <
            DURACAO_INSTRUCOES_JOGO_MS
        ) {
            return;
        }

        tela = TelaJogoBreakout::JOGANDO;
        momentoUltimoQuadroMs = agoraMs;
        publicarQuadro();
        return;
    }

    if (tela == TelaJogoBreakout::FIM_DE_JOGO) {
        return;
    }

    unsigned long tempoDecorridoMs =
        agoraMs - momentoUltimoQuadroMs;

    if (tempoDecorridoMs < INTERVALO_QUADRO_JOGO_MS) {
        return;
    }

    // Uma pausa externa não deve fazer a bola atravessar a tela ao retornar.
    if (tempoDecorridoMs > 100) {
        tempoDecorridoMs = 100;
    }

    momentoUltimoQuadroMs = agoraMs;
    avancarPartida(tempoDecorridoMs);
    publicarQuadro();
}
