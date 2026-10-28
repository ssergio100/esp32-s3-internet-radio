#include "display_radio.h"
#include "audio_radio.h"
#include "alarmes.h"
#include "configuracao.h"
#include "relogio.h"
#include "relogio_rtc.h"
#include "ui_tft_poc.h"
#include "wifi_radio.h"

#include <WiFi.h>
#include <cstring>

namespace {

    bool disponivel = false;
    SemaphoreHandle_t mutexDisplay = nullptr;
    TaskHandle_t tarefaDisplayHandle = nullptr;

#if RADIO_UI_TFT_POC
    bool tftPocDisponivel = false;
    uint32_t revisaoModeloTft = 1;
    uint32_t revisaoTftProcessada = 0;
    uint32_t momentoAlteracaoModeloTftMs = 0;
    StatusAudio ultimoStatusAudioTft = {};
    bool statusAudioTftDisponivel = false;
    bool wifiTftConectado = false;
    int16_t wifiTftRssi = 0;
    uint8_t wifiTftCanal = 0;
    char wifiTftSsid[33] = {};
    char wifiTftBssid[18] = {};
    char wifiTftIp[16] = {};
    bool detalhesRedeWebTft = false;
    bool horarioTopoWebDisponivel = false;
    struct tm ultimaDataHoraTopoWeb = {};
    unsigned long momentoUltimaConsultaTopoWebMs = 0;
    bool horarioTopoFmDisponivel = false;
    struct tm ultimaDataHoraTopoFm = {};
    unsigned long momentoUltimaConsultaTopoFmMs = 0;
    TemaUiTft temaTftAtual = TemaUiTft::LARANJA;
    bool tftLigadoAtual = true;

    void marcarModeloTftAlterado() {
        revisaoModeloTft++;
        momentoAlteracaoModeloTftMs = millis();
    }

    bool statusAudioTftMudou(const StatusAudio& atual) {
        return
            !statusAudioTftDisponivel ||
            atual.bitrate != ultimoStatusAudioTft.bitrate ||
            atual.bufferBytes != ultimoStatusAudioTft.bufferBytes ||
            atual.bufferTotalBytes != ultimoStatusAudioTft.bufferTotalBytes ||
            atual.bufferMilissegundos != ultimoStatusAudioTft.bufferMilissegundos ||
            strcmp(atual.radio, ultimoStatusAudioTft.radio) != 0 ||
            strcmp(atual.titulo, ultimoStatusAudioTft.titulo) != 0 ||
            strcmp(atual.codec, ultimoStatusAudioTft.codec) != 0;
    }
#endif

    class BloqueioDisplay {
    public:
        BloqueioDisplay() {
            if (mutexDisplay == nullptr) {
                adquirido = true;
                return;
            }

            adquirido =
                xSemaphoreTake(
                    mutexDisplay,
                    portMAX_DELAY
                ) == pdTRUE;
        }

        ~BloqueioDisplay() {
            if (
                adquirido &&
                mutexDisplay != nullptr
            ) {
                xSemaphoreGive(mutexDisplay);
            }
        }

        bool foiAdquirido() const {
            return adquirido;
        }

        BloqueioDisplay(const BloqueioDisplay&) = delete;
        BloqueioDisplay& operator=(const BloqueioDisplay&) = delete;

    private:
        bool adquirido = false;
    };

    enum class TelaDisplay {
        MENSAGEM,
        CONFIGURACAO_WIFI,
        RADIO,
        RADIO_FM,
        RELOGIO,
        ALARME,
        CONFIGURACAO_ALARMES,
        CONFIGURACOES
    };

    TelaDisplay telaAtual = TelaDisplay::MENSAGEM;
    bool atualizacaoPendente = false;
    String mensagemAtual = "Inicializando";
    String nomeAlarmeAtual;
    int volumeAlarmeAtual = VOLUME_PADRAO;
    DadosDisplayConfiguracaoAlarmes dadosConfiguracaoAlarmesAtual;
    DadosDisplayConfiguracoes dadosConfiguracoesAtual;

    enum class ModoFaixaCentral {
        ROLAGEM,
        SELECAO,
        ESTADO
    };

    ModoFaixaCentral modoFaixaCentral =
        ModoFaixaCentral::ROLAGEM;

    String nomeRadioAtual;
    String nomeRadioWebTftAtual;
    String estadoRadioWebTftAtual;
    int indiceRadioAtual = 0;
    int quantidadeRadiosAtual = 0;

    uint16_t frequenciaRadioFm10kHz = 0;
    int16_t nivelRadioFmDecimosDbuv = 0;
    bool qualidadeRadioFmValida = false;
    uint16_t larguraBandaRadioFmDecimosKHz = 0;
    bool larguraBandaRadioFmValida = false;
    bool pilotoEstereoRadioFm = false;
    bool pilotoEstereoRadioFmValido = false;
    bool detalhesTecnicosRadioFm = false;
    String nomeEstacaoRadioFm;
    char radioTextoRadioFm[193] = {};
    String estadoRadioFm;
    bool nomeEstacaoRadioFmValido = false;
    bool radioTextoRadioFmValido = false;
    bool radioFmBuscando = false;
    bool buscaRadioFmConcluida = false;
    uint32_t sequenciaEventoEncoderFm = 0;
    uint32_t momentoEventoEncoderFmUs = 0;

    bool exibindoVolumeNaBarraInferior = false;
    int volumeBarraInferior = VOLUME_PADRAO;
    unsigned long momentoUltimaAtualizacaoDadosWebMs = 0;

    int minutoRelogioExibido = -1;
    int segundoRelogioExibido = -1;
    int diaRelogioExibido = -1;
    unsigned long momentoUltimaConsultaRelogioMs = 0;
    struct tm ultimaDataHoraRelogio = {};
    bool horarioRelogioDisponivel = false;
    ProximoAlarme proximoAlarmeRelogio;
    StatusAlarmes statusAlarmesRelogio;
    bool rtcRelogioRespondendo = false;
    bool sincronizacaoNtpRelogioValida = false;

#if RADIO_UI_TFT_POC
    void copiarTextoTft(char* destino, size_t tamanho, const String& origem) {
        strlcpy(destino, origem.c_str(), tamanho);
    }

    void copiarTextoTft(char* destino, size_t tamanho, const char* origem) {
        strlcpy(destino, origem != nullptr ? origem : "", tamanho);
    }

    bool atualizarHorarioTopoFm() {
        unsigned long agoraMs = millis();
        if (
            agoraMs - momentoUltimaConsultaTopoFmMs < 500 &&
            momentoUltimaConsultaTopoFmMs != 0
        ) {
            return false;
        }

        momentoUltimaConsultaTopoFmMs = agoraMs;
        struct tm dataHora = {};
        bool disponivelAgora = obterDataHoraLocal(dataHora);
        bool mudou =
            disponivelAgora != horarioTopoFmDisponivel;

        if (disponivelAgora) {
            mudou =
                mudou ||
                dataHora.tm_hour != ultimaDataHoraTopoFm.tm_hour ||
                dataHora.tm_min != ultimaDataHoraTopoFm.tm_min;
            ultimaDataHoraTopoFm = dataHora;
        }

        horarioTopoFmDisponivel = disponivelAgora;
        return mudou;
    }

    bool atualizarHorarioTopoWeb() {
        unsigned long agoraMs = millis();
        if (
            agoraMs - momentoUltimaConsultaTopoWebMs < 500 &&
            momentoUltimaConsultaTopoWebMs != 0
        ) {
            return false;
        }

        momentoUltimaConsultaTopoWebMs = agoraMs;
        struct tm dataHora = {};
        bool disponivelAgora = obterDataHoraLocal(dataHora);
        bool mudou = disponivelAgora != horarioTopoWebDisponivel;

        if (disponivelAgora) {
            mudou =
                mudou ||
                dataHora.tm_hour != ultimaDataHoraTopoWeb.tm_hour ||
                dataHora.tm_min != ultimaDataHoraTopoWeb.tm_min;
            ultimaDataHoraTopoWeb = dataHora;
        }

        horarioTopoWebDisponivel = disponivelAgora;
        return mudou;
    }

    void formatarDiaSemanaRelogioTft(
        const struct tm& dataHora,
        char* destino,
        size_t tamanho
    ) {
        static constexpr const char* DIAS[] = {
            "DOMINGO", "SEGUNDA-FEIRA", "TERÇA-FEIRA",
            "QUARTA-FEIRA", "QUINTA-FEIRA", "SEXTA-FEIRA", "SÁBADO"
        };
        strlcpy(destino, DIAS[dataHora.tm_wday], tamanho);
    }

    void formatarDataRelogioTft(
        const struct tm& dataHora,
        char* destino,
        size_t tamanho
    ) {
        static constexpr const char* MESES[] = {
            "JANEIRO", "FEVEREIRO", "MARÇO", "ABRIL", "MAIO", "JUNHO",
            "JULHO", "AGOSTO", "SETEMBRO", "OUTUBRO", "NOVEMBRO", "DEZEMBRO"
        };
        snprintf(
            destino,
            tamanho,
            "%02d %s %04d",
            dataHora.tm_mday,
            MESES[dataHora.tm_mon],
            dataHora.tm_year + 1900
        );
    }

    void preencherEstadoUiTftPoc(EstadoUiTftPoc& estado) {
        // Esta estrutura concentra todos os textos e dados das telas. Limpa-la
        // no armazenamento estatico evita colocar outra copia grande na pilha
        // da DisplayService.
        memset(&estado, 0, sizeof(estado));
        estado.revisao = revisaoModeloTft;
        estado.momentoAlteracaoMs = momentoAlteracaoModeloTftMs;
        estado.volume = volumeBarraInferior;
        estado.volumeSobreposto = exibindoVolumeNaBarraInferior;
        estado.tema = temaTftAtual;
        estado.tftLigado = tftLigadoAtual;

        switch (telaAtual) {
            case TelaDisplay::MENSAGEM:
                estado.tela = TelaUiTftPoc::MENSAGEM;
                copiarTextoTft(estado.titulo, sizeof(estado.titulo), "SISTEMA");
                copiarTextoTft(estado.mensagem, sizeof(estado.mensagem), mensagemAtual);
                break;

            case TelaDisplay::CONFIGURACAO_WIFI:
                estado.tela = TelaUiTftPoc::CONFIGURACAO_WIFI;
                copiarTextoTft(estado.titulo, sizeof(estado.titulo), "CONFIGURAÇÃO WI-FI");
                copiarTextoTft(estado.mensagem, sizeof(estado.mensagem), "RADIO-WEB  |  192.168.4.1");
                break;

            case TelaDisplay::RADIO:
                estado.tela = TelaUiTftPoc::RADIO_WEB;
                copiarTextoTft(
                    estado.titulo,
                    sizeof(estado.titulo),
                    "RÁDIO WEB"
                );
                {
                    int volumeLimitado = constrain(
                        volumeBarraInferior,
                        VOLUME_MINIMO,
                        VOLUME_MAXIMO
                    );
                    int volumePercentual =
                        (volumeLimitado * 100 + VOLUME_MAXIMO / 2) /
                        VOLUME_MAXIMO;

                    if (horarioTopoWebDisponivel) {
                        snprintf(
                            estado.indicadorTopoDireito,
                            sizeof(estado.indicadorTopoDireito),
                            "WEB · %d%% · %02d:%02d",
                            volumePercentual,
                            ultimaDataHoraTopoWeb.tm_hour,
                            ultimaDataHoraTopoWeb.tm_min
                        );
                    } else {
                        snprintf(
                            estado.indicadorTopoDireito,
                            sizeof(estado.indicadorTopoDireito),
                            "WEB · %d%% · --:--",
                            volumePercentual
                        );
                    }
                }
                copiarTextoTft(
                    estado.estacao,
                    sizeof(estado.estacao),
                    nomeRadioWebTftAtual
                );
                copiarTextoTft(
                    estado.estadoWeb,
                    sizeof(estado.estadoWeb),
                    estadoRadioWebTftAtual
                );
                estado.dadosWebValidos =
                    modoFaixaCentral == ModoFaixaCentral::ROLAGEM &&
                    statusAudioTftDisponivel &&
                    (
                        ultimoStatusAudioTft.estado == EstadoAudio::TOCANDO ||
                        ultimoStatusAudioTft.estado == EstadoAudio::DEGRADADO
                    );
                estado.selecaoWeb =
                    modoFaixaCentral == ModoFaixaCentral::SELECAO;
                estado.detalhesRedeWeb = detalhesRedeWebTft;
                if (estado.dadosWebValidos) {
                    copiarTextoTft(estado.faixa, sizeof(estado.faixa), ultimoStatusAudioTft.titulo);
                    copiarTextoTft(estado.codec, sizeof(estado.codec), ultimoStatusAudioTft.codec);
                    estado.bitrate = ultimoStatusAudioTft.bitrate;
                    estado.bufferBytes = ultimoStatusAudioTft.bufferBytes;
                    estado.bufferTotalBytes =
                        ultimoStatusAudioTft.bufferTotalBytes;
                    estado.bufferMs = ultimoStatusAudioTft.bufferMilissegundos;
                }
                estado.wifiConectado = wifiTftConectado;
                estado.wifiRssi = wifiTftRssi;
                estado.wifiCanal = wifiTftCanal;
                copiarTextoTft(
                    estado.wifiSsid,
                    sizeof(estado.wifiSsid),
                    wifiTftSsid
                );
                copiarTextoTft(
                    estado.wifiBssid,
                    sizeof(estado.wifiBssid),
                    wifiTftBssid
                );
                copiarTextoTft(
                    estado.wifiIp,
                    sizeof(estado.wifiIp),
                    wifiTftIp
                );
                break;

            case TelaDisplay::RADIO_FM:
                estado.tela = TelaUiTftPoc::RADIO_FM;
                copiarTextoTft(
                    estado.titulo,
                    sizeof(estado.titulo),
                    "RÁDIO FM"
                );
                {
                    int volumeLimitado = constrain(
                        volumeBarraInferior,
                        VOLUME_MINIMO,
                        VOLUME_MAXIMO
                    );
                    int volumePercentual =
                        (volumeLimitado * 100 + VOLUME_MAXIMO / 2) /
                        VOLUME_MAXIMO;

                    if (horarioTopoFmDisponivel) {
                        snprintf(
                            estado.indicadorTopoDireito,
                            sizeof(estado.indicadorTopoDireito),
                            "FM · %d%% · %02d:%02d",
                            volumePercentual,
                            ultimaDataHoraTopoFm.tm_hour,
                            ultimaDataHoraTopoFm.tm_min
                        );
                    } else {
                        snprintf(
                            estado.indicadorTopoDireito,
                            sizeof(estado.indicadorTopoDireito),
                            "FM · %d%% · --:--",
                            volumePercentual
                        );
                    }
                }
                estado.frequenciaFm10kHz = frequenciaRadioFm10kHz;
                estado.sinalFmDecimosDbuv = nivelRadioFmDecimosDbuv;
                estado.sinalFmValido = qualidadeRadioFmValida;
                estado.larguraBandaFmDecimosKHz =
                    larguraBandaRadioFmDecimosKHz;
                estado.larguraBandaFmValida = larguraBandaRadioFmValida;
                estado.pilotoEstereoFm = pilotoEstereoRadioFm;
                estado.pilotoEstereoFmValido = pilotoEstereoRadioFmValido;
                estado.detalhesTecnicosFm = detalhesTecnicosRadioFm;
                estado.psFmValido = nomeEstacaoRadioFmValido;
                estado.radioTextoFmValido = radioTextoRadioFmValido;
                estado.fmBuscando = radioFmBuscando;
                estado.fmBuscaConcluida = buscaRadioFmConcluida;
                estado.sequenciaEventoEncoderFm = sequenciaEventoEncoderFm;
                estado.momentoEventoEncoderFmUs = momentoEventoEncoderFmUs;
                copiarTextoTft(estado.psFm, sizeof(estado.psFm), nomeEstacaoRadioFm);
                copiarTextoTft(estado.radioTextoFm, sizeof(estado.radioTextoFm), radioTextoRadioFm);
                copiarTextoTft(estado.estadoFm, sizeof(estado.estadoFm), estadoRadioFm);
                break;

            case TelaDisplay::RELOGIO:
                estado.tela = TelaUiTftPoc::RELOGIO;
                estado.horarioDisponivel = horarioRelogioDisponivel;
                if (horarioRelogioDisponivel) {
                    estado.hora = ultimaDataHoraRelogio.tm_hour;
                    estado.minuto = ultimaDataHoraRelogio.tm_min;
                    estado.segundo = ultimaDataHoraRelogio.tm_sec;
                    formatarDiaSemanaRelogioTft(
                        ultimaDataHoraRelogio,
                        estado.titulo,
                        sizeof(estado.titulo)
                    );
                    formatarDataRelogioTft(
                        ultimaDataHoraRelogio,
                        estado.data,
                        sizeof(estado.data)
                    );
                } else {
                    copiarTextoTft(
                        estado.titulo,
                        sizeof(estado.titulo),
                        "HORÁRIO INDISPONÍVEL"
                    );
                }
                estado.proximoAlarmeDisponivel =
                    proximoAlarmeRelogio.disponivel;
                if (proximoAlarmeRelogio.disponivel) {
                    snprintf(
                        estado.proximoAlarmeHorario,
                        sizeof(estado.proximoAlarmeHorario),
                        "%02u:%02u",
                        proximoAlarmeRelogio.hora,
                        proximoAlarmeRelogio.minuto
                    );
                    const char* recorrencia =
                        strcmp(proximoAlarmeRelogio.recorrencia, "SEG-SEX") == 0
                            ? "SEG - SEX"
                            : proximoAlarmeRelogio.recorrencia;
                    copiarTextoTft(
                        estado.proximoAlarmeRecorrencia,
                        sizeof(estado.proximoAlarmeRecorrencia),
                        recorrencia
                    );
                    copiarTextoTft(
                        estado.proximoAlarmeFonte,
                        sizeof(estado.proximoAlarmeFonte),
                        proximoAlarmeRelogio.fonte
                    );
                }
                estado.rtcRespondendo = rtcRelogioRespondendo;
                estado.sincronizacaoNtpValida =
                    sincronizacaoNtpRelogioValida;
                estado.quantidadeAlarmes = static_cast<uint8_t>(min(
                    statusAlarmesRelogio.quantidade,
                    static_cast<size_t>(UINT8_MAX)
                ));
                break;

            case TelaDisplay::ALARME:
                estado.tela = TelaUiTftPoc::ALARME;
                copiarTextoTft(estado.titulo, sizeof(estado.titulo), "ALARME");
                snprintf(
                    estado.mensagem,
                    sizeof(estado.mensagem),
                    "%s  |  VOLUME %d/%d",
                    nomeAlarmeAtual.c_str(),
                    volumeAlarmeAtual,
                    VOLUME_MAXIMO
                );
                break;

            case TelaDisplay::CONFIGURACAO_ALARMES:
                estado.tela = TelaUiTftPoc::CONFIGURACAO_ALARMES;
                copiarTextoTft(
                    estado.titulo,
                    sizeof(estado.titulo),
                    dadosConfiguracaoAlarmesAtual.vista ==
                            VistaConfiguracaoAlarmes::LISTA
                        ? "ALARMES"
                        : "ALARME"
                );
                copiarTextoTft(
                    estado.indicadorTopoDireito,
                    sizeof(estado.indicadorTopoDireito),
                    dadosConfiguracaoAlarmesAtual.indicadorTopoDireito
                );
                estado.vistaAlarmes = static_cast<VistaAlarmesUiTft>(
                    dadosConfiguracaoAlarmesAtual.vista
                );
                estado.quantidadeItensAlarmes = min(
                    dadosConfiguracaoAlarmesAtual.quantidadeItens,
                    static_cast<uint8_t>(MAXIMO_ITENS_ALARMES_UI_TFT)
                );
                estado.itemAlarmeSelecionado = min(
                    dadosConfiguracaoAlarmesAtual.itemSelecionado,
                    estado.quantidadeItensAlarmes > 0
                        ? static_cast<uint8_t>(
                            estado.quantidadeItensAlarmes - 1
                        )
                        : static_cast<uint8_t>(0)
                );
                for (
                    uint8_t indice = 0;
                    indice < estado.quantidadeItensAlarmes;
                    indice++
                ) {
                    copiarTextoTft(
                        estado.itensAlarmes[indice].titulo,
                        sizeof(estado.itensAlarmes[indice].titulo),
                        dadosConfiguracaoAlarmesAtual.itens[indice].titulo
                    );
                    copiarTextoTft(
                        estado.itensAlarmes[indice].subtitulo,
                        sizeof(estado.itensAlarmes[indice].subtitulo),
                        dadosConfiguracaoAlarmesAtual.itens[indice].subtitulo
                    );
                    copiarTextoTft(
                        estado.itensAlarmes[indice].valor,
                        sizeof(estado.itensAlarmes[indice].valor),
                        dadosConfiguracaoAlarmesAtual.itens[indice].valor
                    );
                }
                copiarTextoTft(
                    estado.horarioResumoAlarme,
                    sizeof(estado.horarioResumoAlarme),
                    dadosConfiguracaoAlarmesAtual.horarioResumo
                );
                copiarTextoTft(
                    estado.resumoAlarme,
                    sizeof(estado.resumoAlarme),
                    dadosConfiguracaoAlarmesAtual.resumo
                );
                copiarTextoTft(
                    estado.estadoResumoAlarme,
                    sizeof(estado.estadoResumoAlarme),
                    dadosConfiguracaoAlarmesAtual.estadoResumo
                );
                copiarTextoTft(
                    estado.rotuloCampoAlarme,
                    sizeof(estado.rotuloCampoAlarme),
                    dadosConfiguracaoAlarmesAtual.rotuloCampo
                );
                copiarTextoTft(
                    estado.valorCampoAlarme,
                    sizeof(estado.valorCampoAlarme),
                    dadosConfiguracaoAlarmesAtual.valorCampo
                );
                copiarTextoTft(
                    estado.ajudaCampoAlarme,
                    sizeof(estado.ajudaCampoAlarme),
                    dadosConfiguracaoAlarmesAtual.ajudaCampo
                );
                estado.campoAtualAlarme =
                    dadosConfiguracaoAlarmesAtual.campoAtual;
                estado.quantidadeCamposAlarme =
                    dadosConfiguracaoAlarmesAtual.quantidadeCampos;
                estado.mascaraDiasAlarme =
                    dadosConfiguracaoAlarmesAtual.mascaraDias;
                estado.diaSelecionadoAlarme =
                    dadosConfiguracaoAlarmesAtual.diaSelecionado;
                estado.confirmarExclusaoAlarme =
                    dadosConfiguracaoAlarmesAtual.confirmarExclusao;
                estado.erroPersistenciaAlarme =
                    dadosConfiguracaoAlarmesAtual.erroPersistencia;
                estado.sequenciaEventoEncoderAlarme =
                    dadosConfiguracaoAlarmesAtual.sequenciaEventoEncoder;
                estado.momentoEventoEncoderAlarmeUs =
                    dadosConfiguracaoAlarmesAtual.momentoEventoEncoderUs;
                break;

            case TelaDisplay::CONFIGURACOES:
                estado.tela = TelaUiTftPoc::CONFIGURACOES;
                copiarTextoTft(
                    estado.titulo,
                    sizeof(estado.titulo),
                    dadosConfiguracoesAtual.titulo
                );
                copiarTextoTft(
                    estado.indicadorTopoDireito,
                    sizeof(estado.indicadorTopoDireito),
                    dadosConfiguracoesAtual.indicadorTopoDireito
                );
                estado.configuracoesEmLista =
                    dadosConfiguracoesAtual.vista ==
                    VistaConfiguracoes::LISTA;
                estado.quantidadeItensConfiguracoes = min(
                    dadosConfiguracoesAtual.quantidadeItens,
                    static_cast<uint8_t>(MAXIMO_ITENS_ALARMES_UI_TFT)
                );
                estado.itemConfiguracaoSelecionado = min(
                    dadosConfiguracoesAtual.itemSelecionado,
                    estado.quantidadeItensConfiguracoes > 0
                        ? static_cast<uint8_t>(
                            estado.quantidadeItensConfiguracoes - 1
                        )
                        : static_cast<uint8_t>(0)
                );
                for (
                    uint8_t indice = 0;
                    indice < estado.quantidadeItensConfiguracoes;
                    indice++
                ) {
                    copiarTextoTft(
                        estado.itensConfiguracoes[indice].titulo,
                        sizeof(estado.itensConfiguracoes[indice].titulo),
                        dadosConfiguracoesAtual.itens[indice].titulo
                    );
                    copiarTextoTft(
                        estado.itensConfiguracoes[indice].subtitulo,
                        sizeof(estado.itensConfiguracoes[indice].subtitulo),
                        dadosConfiguracoesAtual.itens[indice].subtitulo
                    );
                    copiarTextoTft(
                        estado.itensConfiguracoes[indice].valor,
                        sizeof(estado.itensConfiguracoes[indice].valor),
                        dadosConfiguracoesAtual.itens[indice].valor
                    );
                }
                copiarTextoTft(
                    estado.rotuloConfiguracao,
                    sizeof(estado.rotuloConfiguracao),
                    dadosConfiguracoesAtual.rotulo
                );
                copiarTextoTft(
                    estado.valorConfiguracao,
                    sizeof(estado.valorConfiguracao),
                    dadosConfiguracoesAtual.valor
                );
                copiarTextoTft(
                    estado.ajudaConfiguracao,
                    sizeof(estado.ajudaConfiguracao),
                    dadosConfiguracoesAtual.ajuda
                );
                break;
        }

    }
#endif



    bool atualizarTelaRelogio(bool forcarAtualizacao) {
        const unsigned long agoraMs = millis();
        if (
            !forcarAtualizacao &&
            agoraMs - momentoUltimaConsultaRelogioMs < 500
        ) {
            return false;
        }

        struct tm dataHora = {};
        const bool disponivelAgora = obterDataHoraLocal(dataHora);
        momentoUltimaConsultaRelogioMs = agoraMs;

        bool horarioMudou =
            disponivelAgora != horarioRelogioDisponivel;
        if (disponivelAgora) {
            horarioMudou =
                horarioMudou ||
                dataHora.tm_min != minutoRelogioExibido ||
                dataHora.tm_sec != segundoRelogioExibido ||
                dataHora.tm_yday != diaRelogioExibido;
            ultimaDataHoraRelogio = dataHora;
            minutoRelogioExibido = dataHora.tm_min;
            segundoRelogioExibido = dataHora.tm_sec;
            diaRelogioExibido = dataHora.tm_yday;
        }
        horarioRelogioDisponivel = disponivelAgora;

        ProximoAlarme proximoAgora;
        obterProximoAlarme(proximoAgora);
        const bool proximoMudou =
            proximoAgora.disponivel != proximoAlarmeRelogio.disponivel ||
            proximoAgora.hora != proximoAlarmeRelogio.hora ||
            proximoAgora.minuto != proximoAlarmeRelogio.minuto ||
            strcmp(proximoAgora.recorrencia, proximoAlarmeRelogio.recorrencia) != 0 ||
            strcmp(proximoAgora.fonte, proximoAlarmeRelogio.fonte) != 0;
        if (proximoMudou) {
            proximoAlarmeRelogio = proximoAgora;
        }

        const StatusAlarmes statusAgora = obterStatusAlarmes();
        const bool statusMudou =
            statusAgora.quantidade != statusAlarmesRelogio.quantidade ||
            statusAgora.quantidadeAtivos !=
                statusAlarmesRelogio.quantidadeAtivos;
        if (statusMudou) {
            statusAlarmesRelogio = statusAgora;
        }

        const bool rtcAgora = relogioRtcDisponivel();
        const bool rtcMudou = rtcAgora != rtcRelogioRespondendo;
        rtcRelogioRespondendo = rtcAgora;

        const bool ntpAgora = ultimaSincronizacaoNtpValida();
        const bool ntpMudou = ntpAgora != sincronizacaoNtpRelogioValida;
        sincronizacaoNtpRelogioValida = ntpAgora;

        if (
            forcarAtualizacao || horarioMudou || proximoMudou ||
            statusMudou || rtcMudou || ntpMudou
        ) {
            marcarModeloTftAlterado();
            return true;
        }
        return false;
    }

    bool atualizarDadosRadioWeb(bool forcarAtualizacao = false) {
        constexpr unsigned long INTERVALO_ATUALIZACAO_MS = 1000;
        const unsigned long agoraMs = millis();
        if (
            !forcarAtualizacao &&
            agoraMs - momentoUltimaAtualizacaoDadosWebMs <
                INTERVALO_ATUALIZACAO_MS
        ) {
            return false;
        }
        momentoUltimaAtualizacaoDadosWebMs = agoraMs;

        const StatusAudio status = obterStatusAudio();
        const bool conectado = WiFi.status() == WL_CONNECTED;
        const int16_t rssi = conectado ? WiFi.RSSI() : 0;
        const uint8_t canal = conectado
            ? static_cast<uint8_t>(WiFi.channel())
            : 0;
        const String ssid = conectado ? WiFi.SSID() : String();
        const String bssid = conectado ? WiFi.BSSIDstr() : String();
        const String ip = conectado
            ? WiFi.localIP().toString()
            : String();

        const bool mudou =
            statusAudioTftMudou(status) ||
            conectado != wifiTftConectado ||
            rssi != wifiTftRssi ||
            canal != wifiTftCanal ||
            strcmp(ssid.c_str(), wifiTftSsid) != 0 ||
            strcmp(bssid.c_str(), wifiTftBssid) != 0 ||
            strcmp(ip.c_str(), wifiTftIp) != 0;

        ultimoStatusAudioTft = status;
        statusAudioTftDisponivel = true;
        wifiTftConectado = conectado;
        wifiTftRssi = rssi;
        wifiTftCanal = canal;
        strlcpy(wifiTftSsid, ssid.c_str(), sizeof(wifiTftSsid));
        strlcpy(wifiTftBssid, bssid.c_str(), sizeof(wifiTftBssid));
        strlcpy(wifiTftIp, ip.c_str(), sizeof(wifiTftIp));

        if (mudou) {
            marcarModeloTftAlterado();
        }
        return mudou;
    }

    void solicitarApresentacao() {
        atualizacaoPendente = true;
        marcarModeloTftAlterado();
        if (tarefaDisplayHandle != nullptr) {
            xTaskNotifyGive(tarefaDisplayHandle);
        }
    }

    void mostrarTextoEstaticoNaFaixaCentral(
        const String& texto,
        const char* nomeRadioTft,
        const char* estadoRadioTft,
        int indiceAtual,
        int quantidadeRadios,
        ModoFaixaCentral modo
    ) {
        if (!disponivel) {
            return;
        }

        nomeRadioAtual = texto;
        nomeRadioWebTftAtual = nomeRadioTft != nullptr ? nomeRadioTft : "";
        estadoRadioWebTftAtual =
            estadoRadioTft != nullptr ? estadoRadioTft : "";
        indiceRadioAtual = indiceAtual;
        quantidadeRadiosAtual = quantidadeRadios;
        telaAtual = TelaDisplay::RADIO;
        exibindoVolumeNaBarraInferior = false;
        modoFaixaCentral = modo;
        solicitarApresentacao();
    }

    void tarefaDisplay(void* parametro) {
        (void)parametro;

        while (true) {
            processarDisplay();

            ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(
                    INTERVALO_SERVICO_DISPLAY_MS
                )
            );
        }
    }

}

void iniciarDisplay() {
    mutexDisplay = xSemaphoreCreateMutex();
    if (mutexDisplay == nullptr) {
        Serial.println(
            "Falha ao criar a protecao do TFT; usando o loop principal."
        );
    }

#if RADIO_UI_TFT_POC
    tftPocDisponivel = iniciarUiTftPoc();
    disponivel = tftPocDisponivel;
    momentoAlteracaoModeloTftMs = millis();
#else
    disponivel = false;
#endif

    if (!disponivel) {
        Serial.println("Falha ao inicializar o display TFT.");
        return;
    }

    mostrarMensagem("Inicializando");

    if (mutexDisplay == nullptr) {
        return;
    }

    BaseType_t criada =
        xTaskCreatePinnedToCore(
            tarefaDisplay,
            "DisplayService",
            getArduinoLoopTaskStackSize(),
            nullptr,
            tskIDLE_PRIORITY + 1,
            &tarefaDisplayHandle,
            ARDUINO_RUNNING_CORE
        );

    if (criada != pdPASS) {
        tarefaDisplayHandle = nullptr;
        Serial.println(
            "Falha ao criar o servico do TFT; usando o loop principal."
        );
    } else {
        Serial.printf(
            "Servico do TFT iniciado no nucleo %d, intervalo %lu ms.\n",
            ARDUINO_RUNNING_CORE,
            static_cast<unsigned long>(
                INTERVALO_SERVICO_DISPLAY_MS
            )
        );
    }
}

void mostrarMensagem(
    const String& mensagem
) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido()) {
        return;
    }

    if (!disponivel) {
        return;
    }

    mensagemAtual = mensagem;
    telaAtual = TelaDisplay::MENSAGEM;
    exibindoVolumeNaBarraInferior = false;
    solicitarApresentacao();
}

void mostrarNomeRadio(
    const String& nome,
    int indiceAtual,
    int quantidadeRadios
) {
    BloqueioDisplay bloqueio;
    if (!bloqueio.foiAdquirido() || !disponivel) {
        return;
    }

    nomeRadioAtual = nome;
    nomeRadioWebTftAtual = nome;
    estadoRadioWebTftAtual = "REPRODUZINDO";
    indiceRadioAtual = indiceAtual;
    quantidadeRadiosAtual = quantidadeRadios;
    telaAtual = TelaDisplay::RADIO;
    exibindoVolumeNaBarraInferior = false;
    modoFaixaCentral = ModoFaixaCentral::ROLAGEM;
    solicitarApresentacao();
}

void mostrarSelecaoRadio(
    const String& nome,
    int indiceSelecionado,
    int quantidadeRadios
) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido()) {
        return;
    }

    mostrarTextoEstaticoNaFaixaCentral(
        nome,
        nome.c_str(),
        "PRÉ-SELEÇÃO · B PARA CONECTAR",
        indiceSelecionado,
        quantidadeRadios,
        ModoFaixaCentral::SELECAO
    );
}

void mostrarEstadoRadio(
    const String& estado,
    const char* nomeRadio,
    int indiceAtual,
    int quantidadeRadios
) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido()) {
        return;
    }

    mostrarTextoEstaticoNaFaixaCentral(
        estado,
        nomeRadio,
        estado.c_str(),
        indiceAtual,
        quantidadeRadios,
        ModoFaixaCentral::ESTADO
    );
}

void configurarDetalhesRadioWeb(bool exibir) {
#if RADIO_UI_TFT_POC
    BloqueioDisplay bloqueio;

    if (
        !bloqueio.foiAdquirido() ||
        !disponivel ||
        detalhesRedeWebTft == exibir
    ) {
        return;
    }

    detalhesRedeWebTft = exibir;
    marcarModeloTftAlterado();
    if (tarefaDisplayHandle != nullptr) {
        xTaskNotifyGive(tarefaDisplayHandle);
    }
#else
    (void)exibir;
#endif
}

void mostrarRadioFm(const DadosDisplayRadioFm& dados) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido() || !disponivel) {
        return;
    }

    frequenciaRadioFm10kHz = dados.frequencia10kHz;
    nomeEstacaoRadioFm = dados.nomeEstacaoValido
        ? dados.nomeEstacao
        : "";
    strlcpy(
        radioTextoRadioFm,
        dados.radioTextoValido ? dados.radioTexto : "",
        sizeof(radioTextoRadioFm)
    );
    estadoRadioFm = dados.estado;
    nomeEstacaoRadioFmValido = dados.nomeEstacaoValido;
    radioTextoRadioFmValido = dados.radioTextoValido;
    radioFmBuscando = dados.buscando;
    buscaRadioFmConcluida = dados.buscaConcluida;
    qualidadeRadioFmValida = dados.qualidadeValida;
    nivelRadioFmDecimosDbuv = dados.nivelDecimosDbuv;
    larguraBandaRadioFmValida = dados.larguraBandaValida;
    larguraBandaRadioFmDecimosKHz = dados.larguraBandaDecimosKHz;
    pilotoEstereoRadioFm = dados.pilotoEstereo;
    pilotoEstereoRadioFmValido = dados.pilotoEstereoValido;
    detalhesTecnicosRadioFm = dados.detalhesTecnicos;
    sequenciaEventoEncoderFm = dados.sequenciaEventoEncoder;
    momentoEventoEncoderFmUs = dados.momentoEventoEncoderUs;
    telaAtual = TelaDisplay::RADIO_FM;
    exibindoVolumeNaBarraInferior = false;
    solicitarApresentacao();
}

void mostrarVolume(
    int volume
) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido()) {
        return;
    }

    if (!disponivel) {
        return;
    }

    volumeBarraInferior = volume;
    const bool mostrarSobreposicao =
        telaAtual == TelaDisplay::RADIO ||
        telaAtual == TelaDisplay::RADIO_FM;
    exibindoVolumeNaBarraInferior = mostrarSobreposicao;

    if (mostrarSobreposicao) {
        solicitarApresentacao();
    }
}

void mostrarConfiguracaoAlarmes(
    const DadosDisplayConfiguracaoAlarmes& dados
) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido() || !disponivel) {
        return;
    }

    dadosConfiguracaoAlarmesAtual = dados;
    telaAtual = TelaDisplay::CONFIGURACAO_ALARMES;
    exibindoVolumeNaBarraInferior = false;
    solicitarApresentacao();
}

void mostrarConfiguracoes(const DadosDisplayConfiguracoes& dados) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido() || !disponivel) {
        return;
    }

    dadosConfiguracoesAtual = dados;
    telaAtual = TelaDisplay::CONFIGURACOES;
    exibindoVolumeNaBarraInferior = false;
    solicitarApresentacao();
}

void configurarTemaInterfaceTft(TemaUiTft tema) {
#if RADIO_UI_TFT_POC
    BloqueioDisplay bloqueio;
    if (!bloqueio.foiAdquirido() || temaTftAtual == tema) {
        return;
    }
    temaTftAtual = tema;
    marcarModeloTftAlterado();
    if (tarefaDisplayHandle != nullptr) {
        xTaskNotifyGive(tarefaDisplayHandle);
    }
#else
    (void)tema;
#endif
}

void configurarTftLigado(bool ligado) {
#if RADIO_UI_TFT_POC
    BloqueioDisplay bloqueio;
    if (!bloqueio.foiAdquirido() || tftLigadoAtual == ligado) {
        return;
    }
    tftLigadoAtual = ligado;
    marcarModeloTftAlterado();
    if (tarefaDisplayHandle != nullptr) {
        xTaskNotifyGive(tarefaDisplayHandle);
    }
#else
    (void)ligado;
#endif
}

void mostrarTelaRelogio() {
    ProximoAlarme proximo;
    obterProximoAlarme(proximo);
    StatusAlarmes statusAlarmes = obterStatusAlarmes();
    // Usa o estado já conhecido pelo driver; não cria uma consulta I2C
    // adicional durante a transição da interface.
    bool rtcRespondendo = relogioRtcDisponivel();
    bool ntpValido = ultimaSincronizacaoNtpValida();

    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido()) {
        return;
    }

    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::RELOGIO;
    minutoRelogioExibido = -1;
    segundoRelogioExibido = -1;
    diaRelogioExibido = -1;
    momentoUltimaConsultaRelogioMs = 0;
    horarioRelogioDisponivel = false;
    proximoAlarmeRelogio = proximo;
    statusAlarmesRelogio = statusAlarmes;
    rtcRelogioRespondendo = rtcRespondendo;
    sincronizacaoNtpRelogioValida = ntpValido;
    exibindoVolumeNaBarraInferior = false;
    solicitarApresentacao();
}

void mostrarAlarme(const String& nome, int volume) {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido()) {
        return;
    }

    if (!disponivel) {
        return;
    }

    nomeAlarmeAtual = nome;
    volumeAlarmeAtual = volume;
    telaAtual = TelaDisplay::ALARME;
    exibindoVolumeNaBarraInferior = false;
    solicitarApresentacao();
}

void mostrarConfiguracaoWifi() {
    BloqueioDisplay bloqueio;

    if (!bloqueio.foiAdquirido()) {
        return;
    }

    if (!disponivel) {
        return;
    }

    telaAtual = TelaDisplay::CONFIGURACAO_WIFI;
    exibindoVolumeNaBarraInferior = false;
    solicitarApresentacao();
}

void processarDisplay() {
    if (
        tarefaDisplayHandle != nullptr &&
        xTaskGetCurrentTaskHandle() != tarefaDisplayHandle
    ) {
        return;
    }

#if RADIO_UI_TFT_POC
    bool processarTft = false;
    static EstadoUiTftPoc estadoTft;
#endif

    {
        BloqueioDisplay bloqueio;
        if (!bloqueio.foiAdquirido() || !disponivel) {
            return;
        }

        const bool forcarAtualizacao = atualizacaoPendente;
        atualizacaoPendente = false;

        if (telaAtual == TelaDisplay::RELOGIO) {
            atualizarTelaRelogio(forcarAtualizacao);
        } else if (telaAtual == TelaDisplay::RADIO) {
            if (atualizarHorarioTopoWeb()) {
                marcarModeloTftAlterado();
            }
            atualizarDadosRadioWeb(forcarAtualizacao);
        } else if (telaAtual == TelaDisplay::RADIO_FM) {
            if (atualizarHorarioTopoFm()) {
                marcarModeloTftAlterado();
            }
        }

#if RADIO_UI_TFT_POC
        if (
            tftPocDisponivel &&
            revisaoTftProcessada != revisaoModeloTft
        ) {
            preencherEstadoUiTftPoc(estadoTft);
            revisaoTftProcessada = revisaoModeloTft;
            processarTft = true;
        }
#endif
    }

#if RADIO_UI_TFT_POC
    if (processarTft) {
        // SPI e renderizacao permanecem fora do mutex do modelo.
        processarUiTftPoc(estadoTft);
    }
#endif
}
