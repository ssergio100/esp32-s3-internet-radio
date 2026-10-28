#ifndef ALARMES_H
#define ALARMES_H

#include <Arduino.h>

#include "configuracao.h"

constexpr size_t QUANTIDADE_MAXIMA_ALARMES = 20;
constexpr size_t TAMANHO_MAXIMO_NOME_ALARME = 63;
constexpr uint8_t DURACAO_MAXIMA_CONFIGURAVEL_ALARME_MINUTOS = 60;

extern const char* const CAMINHO_SOM_PADRAO_ALARME;

enum class FonteAlarmeConfiguravel : uint8_t {
    RADIO_FM,
    RADIO_WEB,
    DFPLAYER,
    SOM_PADRAO
};

struct ConfiguracaoAlarme {
    uint32_t id = 0;
    char nome[TAMANHO_MAXIMO_NOME_ALARME + 1] = {};
    bool ativo = true;
    bool execucaoUnica = false;
    uint8_t hora = 7;
    uint8_t minuto = 0;
    uint8_t diasSemana = 0b00111110;
    uint16_t ano = 0;
    uint8_t mes = 0;
    uint8_t dia = 0;
    FonteAlarmeConfiguravel fonte = FonteAlarmeConfiguravel::RADIO_FM;
    uint32_t radioId = 0;
    bool usarUltimaEstacaoWeb = false;
    uint8_t somDfPlayer = 1;
    uint8_t volume = 1;
    uint8_t duracaoMinutos = DURACAO_MAXIMA_ALARME_MINUTOS;
};

struct DisparoAlarme {
    uint32_t id = 0;
    uint32_t radioId = 0;
    bool radioFm = false;
    bool radioWebUltimaEstacao = false;
    uint8_t somDfPlayer = 0;
    uint8_t volume = 0;
    uint8_t duracaoMinutos = DURACAO_MAXIMA_ALARME_MINUTOS;
    String nome;
};

struct StatusAlarmes {
    size_t quantidade = 0;
    size_t quantidadeAtivos = 0;
    bool somPadraoDisponivel = false;
};

struct ProximoAlarme {
    bool disponivel = false;
    uint8_t hora = 0;
    uint8_t minuto = 0;
    char recorrencia[16] = {};
    char fonte[16] = {};
};

// Garante o som padrão na FFat e carrega as regras persistidas para a RAM.
void iniciarAlarmes();

// Recarrega a fotografia em RAM depois de uma alteração feita pela API.
void carregarAlarmes();

// Verifica somente uma vez cada minuto local. Se vários alarmes coincidirem,
// devolve o de maior ID e considera os demais substituídos por ele.
bool verificarDisparoAlarme(DisparoAlarme& disparo);

StatusAlarmes obterStatusAlarmes();

// Calcula a próxima ocorrência usando somente a fotografia já carregada em
// RAM. Não lê a FFat e não modifica o agendador.
bool obterProximoAlarme(ProximoAlarme& proximo);

// A interface local trabalha sobre uma fotografia em RAM. Somente salvar e
// excluir escrevem na FFat; girar os encoders nunca acessa o armazenamento.
size_t listarConfiguracoesAlarmes(
    ConfiguracaoAlarme* destino,
    size_t capacidade
);
bool criarConfiguracaoAlarme(ConfiguracaoAlarme& configuracao);
bool salvarConfiguracaoAlarme(const ConfiguracaoAlarme& configuracao);
bool excluirConfiguracaoAlarme(uint32_t id);

#endif
