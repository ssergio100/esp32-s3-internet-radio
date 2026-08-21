#ifndef ALARMES_H
#define ALARMES_H

#include <Arduino.h>

constexpr size_t QUANTIDADE_MAXIMA_ALARMES = 20;
constexpr size_t TAMANHO_MAXIMO_NOME_ALARME = 63;

extern const char* const CAMINHO_SOM_PADRAO_ALARME;

struct DisparoAlarme {
    uint32_t id = 0;
    uint8_t volume = 0;
    String nome;
    String arquivo;
};

struct StatusAlarmes {
    size_t quantidade = 0;
    size_t quantidadeAtivos = 0;
    bool somPadraoDisponivel = false;
};

// Garante o som padrão na FFat e carrega as regras persistidas para a RAM.
void iniciarAlarmes();

// Recarrega a fotografia em RAM depois de uma alteração feita pela API.
void carregarAlarmes();

// Verifica somente uma vez cada minuto local. Se vários alarmes coincidirem,
// devolve o de maior ID e considera os demais substituídos por ele.
bool verificarDisparoAlarme(DisparoAlarme& disparo);

StatusAlarmes obterStatusAlarmes();

#endif
