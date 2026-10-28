#ifndef UI_TFT_POC_H
#define UI_TFT_POC_H

#include <Arduino.h>

#include "configuracao.h"
#include "ui_tft_paleta.h"

#if RADIO_UI_TFT_POC

enum class TelaUiTftPoc : uint8_t {
    MENSAGEM,
    CONFIGURACAO_WIFI,
    RADIO_WEB,
    RADIO_FM,
    RELOGIO,
    ALARME,
    CONFIGURACAO_ALARMES,
    CONFIGURACOES
};

enum class VistaAlarmesUiTft : uint8_t {
    LISTA,
    EDITOR,
    DIAS_PERSONALIZADOS,
    EDITAR_NOME,
    CONFIRMAR_EXCLUSAO
};

constexpr size_t MAXIMO_ITENS_ALARMES_UI_TFT = 4;

struct ItemAlarmesUiTft {
    char titulo[36] = {};
    char subtitulo[56] = {};
    char valor[24] = {};
};

struct EstadoUiTftPoc {
    TelaUiTftPoc tela = TelaUiTftPoc::MENSAGEM;
    uint32_t revisao = 0;
    uint32_t momentoAlteracaoMs = 0;
    char titulo[24] = {};
    char indicadorTopoDireito[32] = {};
    char mensagem[96] = {};
    char estacao[64] = {};
    char faixa[128] = {};
    char codec[16] = {};
    char estadoWeb[48] = {};
    char data[64] = {};
    VistaAlarmesUiTft vistaAlarmes = VistaAlarmesUiTft::LISTA;
    ItemAlarmesUiTft itensAlarmes[MAXIMO_ITENS_ALARMES_UI_TFT];
    uint8_t quantidadeItensAlarmes = 0;
    uint8_t itemAlarmeSelecionado = 0;
    char horarioResumoAlarme[6] = {};
    char resumoAlarme[64] = {};
    char estadoResumoAlarme[5] = {};
    char rotuloCampoAlarme[24] = {};
    char valorCampoAlarme[40] = {};
    char ajudaCampoAlarme[72] = {};
    uint8_t campoAtualAlarme = 0;
    uint8_t quantidadeCamposAlarme = 0;
    uint8_t mascaraDiasAlarme = 0;
    uint8_t diaSelecionadoAlarme = 0;
    bool confirmarExclusaoAlarme = false;
    bool erroPersistenciaAlarme = false;
    uint32_t sequenciaEventoEncoderAlarme = 0;
    uint32_t momentoEventoEncoderAlarmeUs = 0;
    char psFm[25] = {};
    char radioTextoFm[193] = {};
    char estadoFm[64] = {};
    uint16_t frequenciaFm10kHz = 0;
    int16_t sinalFmDecimosDbuv = 0;
    uint16_t larguraBandaFmDecimosKHz = 0;
    bool sinalFmValido = false;
    bool larguraBandaFmValida = false;
    bool pilotoEstereoFm = false;
    bool pilotoEstereoFmValido = false;
    bool detalhesTecnicosFm = false;
    bool psFmValido = false;
    bool radioTextoFmValido = false;
    bool fmBuscando = false;
    bool fmBuscaConcluida = false;
    uint32_t sequenciaEventoEncoderFm = 0;
    uint32_t momentoEventoEncoderFmUs = 0;
    uint32_t bitrate = 0;
    uint32_t bufferBytes = 0;
    uint32_t bufferTotalBytes = 0;
    uint32_t bufferMs = 0;
    bool wifiConectado = false;
    bool dadosWebValidos = false;
    bool selecaoWeb = false;
    bool detalhesRedeWeb = false;
    int16_t wifiRssi = 0;
    uint8_t wifiCanal = 0;
    char wifiSsid[33] = {};
    char wifiBssid[18] = {};
    char wifiIp[16] = {};
    bool horarioDisponivel = false;
    uint8_t hora = 0;
    uint8_t minuto = 0;
    uint8_t segundo = 0;
    bool proximoAlarmeDisponivel = false;
    char proximoAlarmeHorario[6] = {};
    char proximoAlarmeRecorrencia[16] = {};
    char proximoAlarmeFonte[16] = {};
    bool rtcRespondendo = false;
    bool sincronizacaoNtpValida = false;
    uint8_t quantidadeAlarmes = 0;
    int16_t volume = 0;
    bool volumeSobreposto = false;
    TemaUiTft tema = TemaUiTft::LARANJA;
    bool tftLigado = true;
    bool configuracoesEmLista = true;
    ItemAlarmesUiTft itensConfiguracoes[MAXIMO_ITENS_ALARMES_UI_TFT];
    uint8_t quantidadeItensConfiguracoes = 0;
    uint8_t itemConfiguracaoSelecionado = 0;
    char rotuloConfiguracao[28] = {};
    char valorConfiguracao[40] = {};
    char ajudaConfiguracao[72] = {};
};

struct MetricasUiTftPoc {
    bool disponivel = false;
    uint32_t atualizacoes = 0;
    uint32_t camposRedesenhados = 0;
    uint32_t duracaoUltimaUs = 0;
    uint32_t duracaoMaximaUs = 0;
    uint32_t duracaoMediaUs = 0;
    uint32_t latenciaUltimaMs = 0;
    uint32_t latenciaBuscandoUltimaUs = 0;
    uint32_t latenciaBuscandoMaximaUs = 0;
    uint32_t latenciaBuscandoP95Us = 0;
    uint32_t amostrasLatenciaBuscando = 0;
    uint32_t latenciaResultadoUltimaUs = 0;
    uint32_t latenciaResultadoMaximaUs = 0;
    uint32_t latenciaResultadoP95Us = 0;
    uint32_t amostrasLatenciaResultado = 0;
    uint32_t latenciaAlarmesUltimaUs = 0;
    uint32_t latenciaAlarmesMaximaUs = 0;
    uint32_t latenciaAlarmesP95Us = 0;
    uint32_t amostrasLatenciaAlarmes = 0;
    int32_t variacaoHeapBytes = 0;
    int32_t variacaoPsramBytes = 0;
};

bool iniciarUiTftPoc();
void processarUiTftPoc(const EstadoUiTftPoc& estado);
MetricasUiTftPoc obterMetricasUiTftPoc();

#endif

#endif
