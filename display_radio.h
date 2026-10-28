#ifndef DISPLAY_RADIO_H
#define DISPLAY_RADIO_H

#include <Arduino.h>

#include "ui_tft_paleta.h"

void iniciarDisplay();

void mostrarConfiguracaoWifi();

void mostrarMensagem(const String& mensagem);

void mostrarNomeRadio(
    const String& nome,
    int indiceAtual,
    int quantidadeRadios
);

void mostrarSelecaoRadio(
    const String& nome,
    int indiceSelecionado,
    int quantidadeRadios
);

void mostrarEstadoRadio(
    const String& estado,
    const char* nomeRadio,
    int indiceAtual,
    int quantidadeRadios
);

// Alterna apenas a apresentação TFT entre a tela principal do Rádio Web e
// os dados da rede. Não reinicia Wi-Fi, streaming ou servidor web.
void configurarDetalhesRadioWeb(bool exibir);

struct DadosDisplayRadioFm {
    uint16_t frequencia10kHz = 0;
    const char* nomeEstacao = "";
    const char* radioTexto = "";
    const char* estado = "";
    bool nomeEstacaoValido = false;
    bool radioTextoValido = false;
    bool buscando = false;
    bool buscaConcluida = false;
    bool qualidadeValida = false;
    int16_t nivelDecimosDbuv = 0;
    bool larguraBandaValida = false;
    uint16_t larguraBandaDecimosKHz = 0;
    bool pilotoEstereo = false;
    bool pilotoEstereoValido = false;
    bool detalhesTecnicos = false;
    uint32_t sequenciaEventoEncoder = 0;
    uint32_t momentoEventoEncoderUs = 0;
};

// Atualiza o modelo da tela FM. A implementação do display não acessa o
// driver TEF6686; recebe apenas dados já tratados pelo coordenador principal.
void mostrarRadioFm(const DadosDisplayRadioFm& dados);

void mostrarVolume(int volume);

enum class VistaConfiguracaoAlarmes : uint8_t {
    LISTA,
    EDITOR,
    DIAS_PERSONALIZADOS,
    EDITAR_NOME,
    CONFIRMAR_EXCLUSAO
};

constexpr size_t MAXIMO_ITENS_ALARMES_VISIVEIS = 4;

struct ItemListaAlarmesDisplay {
    char titulo[36] = {};
    char subtitulo[56] = {};
    char valor[24] = {};
};

struct DadosDisplayConfiguracaoAlarmes {
    VistaConfiguracaoAlarmes vista =
        VistaConfiguracaoAlarmes::LISTA;
    char indicadorTopoDireito[32] = {};
    ItemListaAlarmesDisplay itens[MAXIMO_ITENS_ALARMES_VISIVEIS];
    uint8_t quantidadeItens = 0;
    uint8_t itemSelecionado = 0;
    char horarioResumo[6] = {};
    char resumo[64] = {};
    char estadoResumo[5] = {};
    char rotuloCampo[24] = {};
    char valorCampo[40] = {};
    char ajudaCampo[72] = {};
    uint8_t campoAtual = 0;
    uint8_t quantidadeCampos = 0;
    uint8_t mascaraDias = 0;
    uint8_t diaSelecionado = 0;
    bool confirmarExclusao = false;
    bool erroPersistencia = false;
    uint32_t sequenciaEventoEncoder = 0;
    uint32_t momentoEventoEncoderUs = 0;
};

// Atualiza somente o modelo. A tarefa do display executa o desenho e mantém
// transferências SPI fora do caminho crítico dos encoders.
void mostrarConfiguracaoAlarmes(
    const DadosDisplayConfiguracaoAlarmes& dados
);

enum class VistaConfiguracoes : uint8_t {
    LISTA,
    VALOR
};

struct DadosDisplayConfiguracoes {
    VistaConfiguracoes vista = VistaConfiguracoes::LISTA;
    char titulo[24] = {};
    char indicadorTopoDireito[32] = {};
    ItemListaAlarmesDisplay itens[MAXIMO_ITENS_ALARMES_VISIVEIS];
    uint8_t quantidadeItens = 0;
    uint8_t itemSelecionado = 0;
    char rotulo[28] = {};
    char valor[40] = {};
    char ajuda[72] = {};
};

void mostrarConfiguracoes(const DadosDisplayConfiguracoes& dados);

// Apenas atualizam o modelo; a tarefa dedicada executa os comandos SPI.
void configurarTemaInterfaceTft(TemaUiTft tema);
void configurarTftLigado(bool ligado);

// Troca o TFT para a tela permanente do estado Relógio.
void mostrarTelaRelogio();

// Sobrepõe temporariamente a interface enquanto um alarme está tocando.
void mostrarAlarme(const String& nome, int volume);

/*
 * Mantém compatibilidade como caminho de contingência no loop(). Quando o
 * serviço dedicado foi criado, somente a tarefa do display faz a atualização.
 */
void processarDisplay();

#endif
