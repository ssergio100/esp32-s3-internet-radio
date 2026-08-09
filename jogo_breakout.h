#ifndef JOGO_BREAKOUT_H
#define JOGO_BREAKOUT_H

#include <Arduino.h>

constexpr int COLUNAS_BLOCOS_BREAKOUT = 8;
constexpr int LINHAS_BLOCOS_BREAKOUT = 3;
constexpr int QUANTIDADE_BLOCOS_BREAKOUT =
    COLUNAS_BLOCOS_BREAKOUT * LINHAS_BLOCOS_BREAKOUT;

// Geometria compartilhada entre a física e o desenho do jogo.
constexpr int LARGURA_BOLA_BREAKOUT_PX = 2;
constexpr int ALTURA_BOLA_BREAKOUT_PX = 2;
constexpr int LARGURA_RAQUETE_BREAKOUT_PX = 24;
constexpr int ALTURA_RAQUETE_BREAKOUT_PX = 3;
constexpr int POSICAO_Y_RAQUETE_BREAKOUT_PX = 60;
constexpr int LARGURA_BLOCO_BREAKOUT_PX = 13;
constexpr int ALTURA_BLOCO_BREAKOUT_PX = 4;
constexpr int ESPACO_HORIZONTAL_BLOCOS_BREAKOUT_PX = 2;
constexpr int ESPACO_VERTICAL_BLOCOS_BREAKOUT_PX = 2;
constexpr int POSICAO_X_PRIMEIRO_BLOCO_BREAKOUT_PX = 5;
constexpr int POSICAO_Y_PRIMEIRO_BLOCO_BREAKOUT_PX = 13;

enum class TelaJogoBreakout {
    INSTRUCOES,
    JOGANDO,
    FIM_DE_JOGO
};

// Fotografia somente com os dados necessários para desenhar uma partida.
// A lógica do jogo permanece independente da biblioteca do display.
struct QuadroJogoBreakout {
    TelaJogoBreakout tela = TelaJogoBreakout::INSTRUCOES;
    int bolaX = 0;
    int bolaY = 0;
    int raqueteX = 0;
    int pontuacao = 0;
    int nivel = 1;
    bool blocoVisivel[QUANTIDADE_BLOCOS_BREAKOUT] = {};
};

void iniciarJogoBreakout();

void encerrarJogoBreakout();

void moverRaqueteJogoBreakout(long deslocamentoEncoder);

// Atualiza física e apresentação sem bloquear o loop principal.
void processarJogoBreakout();

#endif
