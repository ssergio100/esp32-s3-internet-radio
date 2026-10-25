#ifndef DFPLAYER_H
#define DFPLAYER_H

#include <Arduino.h>

enum class ErroDfPlayer : uint8_t {
    NENHUM,
    NAO_INICIADO,
    MODULO_NAO_RESPONDE,
    PASTA_SEM_SONS,
    NUMERO_DE_SOM_INVALIDO,
    ERRO_REPORTADO_PELO_MODULO,
    SAIDA_AUDIO_INDISPONIVEL
};

enum class FaseDfPlayer : uint8_t {
    INDISPONIVEL,
    PRONTO,
    INICIANDO,
    TOCANDO,
    CONCLUIDO,
    ERRO
};

enum class UsoDfPlayer : uint8_t {
    NENHUM,
    ALARME,
    CHIME,
    PREVIA
};

struct SomDfPlayer {
    uint8_t numero = 0;
    char arquivo[8] = {};
    char nome[16] = {};
};

struct CatalogoSonsDfPlayer {
    bool disponivel = false;
    uint8_t quantidade = 0;
    int quantidadeInformadaPasta = -1;
    int quantidadeInformadaCartao = -1;
    bool contagensDivergentes = false;
};

struct EstadoDfPlayer {
    bool moduloRespondendo = false;
    bool pronto = false;
    bool reproduzindo = false;
    uint8_t quantidadeSons = 0;
    uint8_t somAtual = 0;
    uint8_t volumeEquipamento = 0;
    uint8_t volumeModulo = 0;
    FaseDfPlayer fase = FaseDfPlayer::INDISPONIVEL;
    UsoDfPlayer uso = UsoDfPlayer::NENHUM;
    ErroDfPlayer erro = ErroDfPlayer::NAO_INICIADO;
    uint8_t ultimoEvento = 0;
    uint16_t detalheUltimoEvento = 0;
    uint32_t avisosComunicacao = 0;
};

// A inicialização compara a contagem de /01 com a contagem total do cartão,
// pois alguns DFPlayers retornam uma contagem incompleta para a pasta.
// Não chame a partir de ISR nem do caminho crítico de leitura dos encoders.
bool iniciarDfPlayer();

// Consome eventos assíncronos da UART e atualiza BUSY sem bloquear o loop.
void processarDfPlayer();

// Catálogo numérico da pasta /01. O protocolo serial do DFPlayer não fornece
// nomes reais; cada item é apresentado como 001.mp3 / Som 001.
CatalogoSonsDfPlayer obterCatalogoSonsDfPlayer();
bool obterSomDfPlayer(uint8_t numeroSom, SomDfPlayer& som);

// Habilita DAC_L/DAC_R e os NS8002 antes de reproduzir. O relé não participa.
// O volume recebido usa a faixa geral do equipamento (0..VOLUME_MAXIMO).
bool reproduzirSomDfPlayer(
    uint8_t numeroSom,
    uint8_t volumeEquipamento,
    UsoDfPlayer uso
);

// Avança ou retrocede um som por detente. Deslocamentos acumulados também são
// aceitos e preservados, para não perder passos quando o encoder gira rápido.
bool deslocarSomDfPlayer(
    long deslocamento,
    uint8_t volumeEquipamento,
    UsoDfPlayer uso
);

// Para a reprodução, desabilita o DAC interno do DFPlayer e restaura exatamente
// GPA7/GPA6/GPA5 ao estado fotografado antes de habilitar os NS8002.
void pararDfPlayer();
void definirVolumeDfPlayer(uint8_t volumeEquipamento);
EstadoDfPlayer obterEstadoDfPlayer();
const char* obterTextoFaseDfPlayer(FaseDfPlayer fase);

#endif
