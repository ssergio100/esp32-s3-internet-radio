#ifndef CONTROLES_H
#define CONTROLES_H

enum class EventoEncoder {
    NENHUM,
    ESQUERDA,
    DIREITA,
    CLIQUE
};

void iniciarControles();

EventoEncoder lerControles();

#endif