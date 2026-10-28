/*
 * Arquivo principal e mapa de execução do firmware.
 *
 * O setup() inicializa, nesta ordem: interface física, relógio, receptor FM,
 * Wi-Fi, armazenamento/servidor, lista de estações e serviço de áudio web.
 *
 * O loop() apenas coordena os módulos. A reprodução de áudio acontece em
 * uma tarefa dedicada implementada em audio_radio.cpp.
 * As regras de estados, volume, estações e arquivos permanecem neste arquivo para
 * que o fluxo principal do produto possa ser lido em um só lugar.
 */

#include <Arduino.h>
#include <Wire.h>

#include "configuracao.h"
#include "alarmes.h"
#include "radios.h"
#include "display_radio.h"
#include "wifi_radio.h"
#include "audio_radio.h"
#include "controles.h"
#include "indicador_led.h"
#include "relogio.h"
#include "relogio_rtc.h"
#include "servidor_web.h"
#include "telemetria.h"
#include "radio_fm.h"
#include "chaveamento_audio.h"
#include "dfplayer.h"
#include "chime.h"
#include "configuracoes_usuario.h"

enum class EstadoEquipamento {
    RADIO_WEB,
    RADIO_FM,
    RELOGIO
};

enum class ModoInterface {
    REPOUSO,
    SELECAO_RADIO,
    SELECAO_FONTE,
    LISTA_ALARMES,
    EDITOR_ALARME,
    DIAS_ALARME,
    NOME_ALARME,
    CONFIRMAR_EXCLUSAO_ALARME,
    LISTA_CHIME,
    CONFIGURAR_CHIME,
    LISTA_CONFIGURACOES,
    CONFIGURACOES_RELOGIO,
    AJUSTE_HORA,
    CONFIGURACOES_TELA,
    CONFIGURAR_REPOUSO_TFT,
    CONFIGURAR_TEMA_TFT,
    DETALHES_RADIO_FM,
    DETALHES_RADIO_WEB
};

enum class ItemMenuPrincipal : uint8_t {
    RELOGIO,
    RADIO_FM,
    RADIO_WEB,
    ALARMES,
    CHIMES,
    CONFIGURACOES
};

enum class CampoConfiguracaoChime : uint8_t {
    ATIVO,
    PERIODICIDADE,
    ARQUIVO_DFPLAYER,
    VOLUME,
    SILENCIO_NOTURNO,
    INICIO_SILENCIO,
    FIM_SILENCIO
};

enum class CampoEditorAlarme : uint8_t {
    HORA,
    MINUTO,
    REPETICAO,
    FONTE,
    ARQUIVO_DFPLAYER,
    VOLUME,
    DURACAO,
    ATIVO,
    NOME,
    EXCLUIR
};

enum class FaseAlarme {
    INATIVO,
    AGUARDANDO_PARADA_RADIO,
    AGUARDANDO_WIFI_RADIO,
    AGUARDANDO_POWER_DOWN_DAC_FM,
    AGUARDANDO_POWER_DOWN_DAC_DFPLAYER,
    TOCANDO
};

// Estado da navegação comandada pelo encoder de rádios.
ModoInterface modoInterface = ModoInterface::REPOUSO;
EstadoEquipamento estadoEquipamento =
    EstadoEquipamento::RELOGIO;
ItemMenuPrincipal itemMenuPrincipalEmSelecao =
    ItemMenuPrincipal::RELOGIO;

ConfiguracaoAlarme configuracoesAlarmes[QUANTIDADE_MAXIMA_ALARMES];
size_t quantidadeConfiguracoesAlarmes = 0;
size_t indiceAlarmeSelecionado = 0;
ConfiguracaoAlarme alarmeEmEdicao;
uint8_t indiceCampoAlarme = 0;
uint8_t diaAlarmeSelecionado = 1;
bool confirmarExclusaoAlarme = false;
bool erroPersistenciaAlarme = false;
bool alarmeNovoEmEdicao = false;
bool alarmeAlterado = false;
constexpr uint8_t TAMANHO_NOME_ALARME_UI = 12;
char nomeAlarmeUi[TAMANHO_NOME_ALARME_UI + 1] = {};
uint8_t indiceCaractereNomeAlarme = 0;
uint32_t sequenciaEventoInterfaceAlarmes = 0;
uint32_t momentoEventoInterfaceAlarmesUs = 0;

ConfiguracoesUsuario configuracoesUsuario;
uint8_t indiceConfiguracaoSelecionada = 0;
uint8_t horaEmAjuste = 0;
uint8_t minutoEmAjuste = 0;
uint8_t campoHoraEmAjuste = 0;
uint16_t repousoTftEmAjuste = 0;
TemaUiTft temaTftEmAjuste = TemaUiTft::LARANJA;
TemaUiTft temaTftAntesDaPrevia = TemaUiTft::LARANJA;
unsigned long momentoUltimaAtividadeTftMs = 0;
bool tftEmRepouso = false;

int volumeAtual = VOLUME_PADRAO;

int indiceRadioAtual = 0;
int indiceRadioEmSelecao = 0;
uint32_t idRadioAtual = 0;

unsigned long momentoUltimaAlteracaoVolumeMs = 0;
unsigned long momentoUltimaAtividadeSelecaoMs = 0;

bool barraVolumeVisivel = false;
bool ativacaoRadioPendente = false;
bool ativacaoRadioFmPendente = false;
unsigned long inicioPowerDownDacMs = 0;
long navegacaoFmDurantePowerDown = 0;
uint32_t momentoNavegacaoFmDurantePowerDownUs = 0;
uint32_t sequenciaNavegacaoFmDurantePowerDown = 0;

bool alarmeEmExecucao = false;
bool alarmeObservouAudioAtivo = false;
FaseAlarme faseAlarme = FaseAlarme::INATIVO;
EstadoEquipamento estadoAntesDoAlarme =
    EstadoEquipamento::RADIO_WEB;
DisparoAlarme alarmeAtual;
unsigned long inicioAlarmeAtualMs = 0;
unsigned long inicioPreparacaoFonteAlarmeMs = 0;
unsigned long proximaTentativaAudioAlarmeMs = 0;
unsigned long inicioPowerDownAlarmeFmMs = 0;

bool chimeEmExecucao = false;
ConfiguracaoChime configuracaoChimeEmEdicao;
ConfiguracaoChime configuracaoChimeAntesEdicao;
uint8_t indiceChimeSelecionado = 0;
CampoConfiguracaoChime campoChimeEmEdicao =
    CampoConfiguracaoChime::ATIVO;
bool erroPersistenciaChime = false;

// =====================================================
// Protótipos
// =====================================================

void mostrarRadioNoDisplay(int indiceRadio);
void solicitarReproducaoRadio(int indiceRadio);
bool sincronizarRadioAtualComCatalogo();

void entrarModoSelecaoRadio();
void confirmarSelecaoRadio();
void entrarModoSelecaoFonte(long deslocamentoInicial = 0);
void processarNavegacaoFontes(long deslocamento);
void confirmarSelecaoFonte();
void cancelarSelecaoFontePorInatividade();
void entrarListaAlarmes();
void apresentarListaAlarmes();
void apresentarEditorAlarme();
void processarControlesAlarmes(const LeituraControles& leitura);
void encerrarInterfaceAlarmes();
void entrarConfiguracaoChime();
void apresentarConfiguracaoChime();
void processarControlesChime(const LeituraControles& leitura);
void entrarListaConfiguracoes();
void apresentarConfiguracoes();
void processarControlesConfiguracoes(const LeituraControles& leitura);
void processarRepousoTft();
void entrarEstadoRelogio();
void entrarEstadoRadioWeb();
void entrarEstadoRadioFm();
void processarAtivacaoRadioWeb();
void processarAtivacaoRadioFm();

void processarLeituraControles(
    const LeituraControles& leitura
);

void processarAjusteVolume(
    long deslocamentoVolume
);

void processarNavegacaoRadios(
    long deslocamentoEncoder
);

void restaurarBarraAposTempoVolume();
void cancelarSelecaoRadioPorInatividade();

void atualizarDisplayEstadoAudio(
    bool forcarAtualizacao = false
);
void atualizarApresentacaoRadioFm(
    bool forcarAtualizacao = false
);

void processarAgendamentoAlarmes();
void processarChimeHorario();
void finalizarChimeHorario();
void processarPreviaChime();
void finalizarPreviaChime();
void iniciarExecucaoAlarme(const DisparoAlarme& disparo);
void prepararFonteAlarme();
bool alarmeAtualUsaRadioWeb();
bool alarmeAtualUsaRadioFm();
bool alarmeAtualUsaDfPlayer();
void ativarFonteRadioFmAlarme();
void usarSomPadraoAposFalhaRadioFm();
void iniciarCicloAudioAlarme();
void processarExecucaoAlarme();
void finalizarExecucaoAlarme(bool interrompidoPeloUsuario);

// =====================================================
// Setup
// =====================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Barramento dos periféricos I2C, independente do TFT (que usa SPI).
    Wire.begin(
        PIN_BARRAMENTO_I2C_SDA,
        PIN_BARRAMENTO_I2C_SCL,
        FREQUENCIA_BARRAMENTO_I2C_HZ
    );

    configuracoesUsuario = carregarConfiguracoesUsuario();
    iniciarDisplay();
    configurarTemaInterfaceTft(configuracoesUsuario.temaTft);
    iniciarControles();
    momentoUltimaAtividadeTftMs = millis();

    // Antes de GPA7/GPA6/GPA5/GPA4 virarem saídas, o latch recebe o estado
    // seguro: GPA4=LOW desliga o TEF6686, GPA5 seleciona FM e GPA7/GPA6
    // ficam HIGH (mute dos NS8002).
    iniciarChaveamentoAudio();

    // O DFPlayer prepara somente a UART e o catálogo numérico. Seu DAC e os
    // NS8002 permanecem em mute até um futuro alarme, chime ou prévia.
    const bool dfPlayerPronto = iniciarDfPlayer();
    if (!dfPlayerPronto) {
        Serial.println(
            "DFPlayer indisponivel; demais fontes continuam operacionais."
        );
    }

    // O RTC fornece hora imediatamente; o NTP o corrige quando a rede entrar.
    iniciarRelogio();
    configurarSincronizacaoAutomaticaRelogio(
        configuracoesUsuario.sincronizacaoAutomatica
    );

    // O TEF6686 permanece sem alimentação no modo relógio. Ao selecionar FM,
    // GPA4 sobe antes de o driver ser inicializado; fora do FM volta a LOW.

    prepararWifiEmStandby();

    const bool armazenamentoPronto =
        iniciarArmazenamentoEServidorWeb(false);
    if (!armazenamentoPronto) {
        mostrarMensagem(
            "Erro no armazenamento"
        );
        return;
    }

    // Os alarmes e as estações são carregados antes do serviço de áudio.
    mostrarMensagem("Carregando dados");
    iniciarAlarmes();
    carregarConfiguracaoChime();

    carregarRadios();

    const bool servicoAudioPronto = iniciarAudio(volumeAtual, true);
    if (!servicoAudioPronto) {
        mostrarMensagem(
            "Erro no audio"
        );
        return;
    }

    indiceRadioAtual = 0;
    indiceRadioEmSelecao = indiceRadioAtual;
    entrarEstadoRelogio();
}

// =====================================================
// Loop
// =====================================================

void loop() {
    LeituraControles leituraControles =
        lerControles();

    const bool houveAtividadeInterface =
        leituraControles.deslocamentoEncoderVolume != 0 ||
        leituraControles.deslocamentoEncoderNavegacao != 0 ||
        leituraControles.cliqueVolumeDetectado ||
        leituraControles.cliqueNavegacaoDetectado;
    if (houveAtividadeInterface) {
        momentoUltimaAtividadeTftMs = millis();
        if (tftEmRepouso) {
            tftEmRepouso = false;
            configurarTftLigado(true);
            // O primeiro evento apenas acorda a tela, sem alterar a opção
            // que permaneceu selecionada durante o repouso.
            leituraControles = LeituraControles{};
        }
    }

    // Precisa ocorrer antes de qualquer retorno antecipado do coordenador.
    processarDfPlayer();

    // A interface física tem precedência sobre todos os serviços
    // cooperativos. Áudio e display continuam em suas tarefas dedicadas.
    processarRelogio();
    processarAgendamentoAlarmes();
    processarChimeHorario();
    processarPreviaChime();

    // Contingência: retorna imediatamente quando DisplayService está ativo.
    processarDisplay();
    processarRepousoTft();

    if (alarmeEmExecucao) {
        const bool usuarioInterrompeu =
            leituraControles.deslocamentoEncoderVolume != 0 ||
            leituraControles.deslocamentoEncoderNavegacao != 0 ||
            leituraControles.cliqueVolumeDetectado ||
            leituraControles.cliqueNavegacaoDetectado;
        if (usuarioInterrompeu) {
            finalizarExecucaoAlarme(true);
        } else {
            processarExecucaoAlarme();
        }

        if (alarmeAtualUsaRadioWeb()) {
            supervisionarWifi();
        } else if (alarmeAtualUsaRadioFm()) {
            processarRadioFm();
        }

        atualizarIndicadorEstadoAudio();
        registrarTelemetriaPeriodica();
        return;
    }

    if (chimeEmExecucao && houveAtividadeInterface) {
        // Durante o toque, qualquer ação física serve primeiro para
        // silenciar o chime. O mesmo evento não navega pela interface.
        finalizarChimeHorario();
        return;
    }

    processarLeituraControles(leituraControles);
    cancelarSelecaoFontePorInatividade();

    processarAtivacaoRadioFm();
    processarRadioFm();

    restaurarBarraAposTempoVolume();

    if (ativacaoRadioPendente) {
        supervisionarWifi();
        processarAtivacaoRadioWeb();
        atualizarIndicadorEstadoAudio();
        registrarTelemetriaPeriodica();
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        registrarTelemetriaPeriodica();
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RADIO_FM) {
        if (!ativacaoRadioFmPendente) {
            atualizarApresentacaoRadioFm();
        }
        registrarTelemetriaPeriodica();
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RADIO_WEB) {
        cancelarSelecaoRadioPorInatividade();
        supervisionarWifi();
        processarServidorWeb();
    }

    atualizarIndicadorEstadoAudio();
    atualizarDisplayEstadoAudio();

    registrarTelemetriaPeriodica();
}

// =====================================================
// Alarmes
// =====================================================

void processarAgendamentoAlarmes() {
    DisparoAlarme disparo;

    if (!verificarDisparoAlarme(disparo)) {
        return;
    }

    iniciarExecucaoAlarme(disparo);
}

void processarChimeHorario() {
    if (chimeEmExecucao) {
        EstadoDfPlayer estado = obterEstadoDfPlayer();

        if (
            estado.fase == FaseDfPlayer::CONCLUIDO ||
            estado.fase == FaseDfPlayer::ERRO
        ) {
            finalizarChimeHorario();
        }

        return;
    }

    ConfiguracaoChime configuracao;
    if (!verificarDisparoChime(configuracao)) {
        return;
    }

    if (DEBUG_CHIME) {
        EstadoDfPlayer estado = obterEstadoDfPlayer();
        Serial.printf(
            "CHIME DEBUG coordenador: janela recebida; equipamento=%u "
            "alarme=%u dfUso=%u dfFase=%u.\n",
            static_cast<unsigned>(estadoEquipamento),
            alarmeEmExecucao ? 1U : 0U,
            static_cast<unsigned>(estado.uso),
            static_cast<unsigned>(estado.fase)
        );
    }

    // O horário é consumido mesmo com um alarme em curso: alarmes têm
    // prioridade e o chime não deve começar atrasado no mesmo minuto.
    if (alarmeEmExecucao) {
        Serial.println(
            "CHIME DEBUG coordenador: disparo ignorado; alarme em execucao."
        );
        return;
    }

    finalizarPreviaChime();

    if (!reproduzirSomDfPlayer(
        configuracao.som,
        configuracao.volume,
        UsoDfPlayer::CHIME
    )) {
        EstadoDfPlayer estado = obterEstadoDfPlayer();
        Serial.printf(
            "CHIME DEBUG coordenador: falha ao iniciar DFPlayer; "
            "pronto=%u fase=%u erro=%u.\n",
            estado.pronto ? 1U : 0U,
            static_cast<unsigned>(estado.fase),
            static_cast<unsigned>(estado.erro)
        );
        return;
    }

    chimeEmExecucao = true;
    Serial.printf(
        "CHIME DEBUG coordenador: chime iniciado; som=%03u volume=%u/%u "
        "intervalo=%u min.\n",
        static_cast<unsigned>(configuracao.som),
        static_cast<unsigned>(configuracao.volume),
        static_cast<unsigned>(VOLUME_MAXIMO),
        static_cast<unsigned>(configuracao.intervaloMinutos)
    );
}

void finalizarChimeHorario() {
    if (!chimeEmExecucao) {
        return;
    }

    if (DEBUG_CHIME) {
        EstadoDfPlayer estado = obterEstadoDfPlayer();
        Serial.printf(
            "CHIME DEBUG coordenador: finalizando; fase=%u uso=%u "
            "reproduzindo=%u.\n",
            static_cast<unsigned>(estado.fase),
            static_cast<unsigned>(estado.uso),
            estado.reproduzindo ? 1U : 0U
        );
    }
    pararDfPlayer();
    chimeEmExecucao = false;
    Serial.println("CHIME DEBUG coordenador: chime finalizado.");
}

void processarPreviaChime() {
    EstadoDfPlayer estado = obterEstadoDfPlayer();
    if (
        estado.uso == UsoDfPlayer::PREVIA &&
        (
            estado.fase == FaseDfPlayer::CONCLUIDO ||
            estado.fase == FaseDfPlayer::ERRO
        )
    ) {
        finalizarPreviaChime();
    }
}

void finalizarPreviaChime() {
    EstadoDfPlayer estado = obterEstadoDfPlayer();
    if (estado.uso != UsoDfPlayer::PREVIA) {
        return;
    }

    pararDfPlayer();
    Serial.println("Previa do chime finalizada.");
}

void iniciarExecucaoAlarme(const DisparoAlarme& disparo) {
    if (chimeEmExecucao) {
        finalizarChimeHorario();
    }
    finalizarPreviaChime();
    if (obterEstadoDfPlayer().uso == UsoDfPlayer::PREVIA) {
        pararDfPlayer();
    }

    bool iniciandoCadeia = !alarmeEmExecucao;

    if (iniciandoCadeia) {
        estadoAntesDoAlarme = estadoEquipamento;

        if (estadoAntesDoAlarme == EstadoEquipamento::RADIO_WEB) {
            desativarServidorWeb();
        }
    } else {
        Serial.printf(
            "Alarme %lu substituiu o alarme %lu.\n",
            static_cast<unsigned long>(disparo.id),
            static_cast<unsigned long>(alarmeAtual.id)
        );
    }

    // Toda troca de fonte começa em mute. Rádio FM mantém internamente a
    // última frequência sintonizada mesmo quando o TEF6686 entra em repouso.
    definirAlarmeFmAtivo(false);
    configurarSaidaAudioStandby();

    if (!disparo.radioFm) {
        StatusRadioFm statusFm = obterStatusRadioFm();

        if (
            statusFm.estado == EstadoRadioFm::ATIVO ||
            statusFm.estado == EstadoRadioFm::BUSCANDO
        ) {
            desativarRadioFm();
        }

        if (
            disparo.somDfPlayer == 0 &&
            !decoderDacEstaAtivo()
        ) {
            bool usarSomPadrao =
                !disparo.radioWebUltimaEstacao &&
                (
                    disparo.radioId == 0 ||
                    obterRadioPorId(disparo.radioId) == nullptr
                );

            if (!retomarAudio(disparo.volume, usarSomPadrao)) {
                Serial.println(
                    "Alarme: falha ao retomar o servico de audio."
                );
            }
        }

        if (disparo.somDfPlayer == 0) {
            configurarSaidaAudioDac(
                disparo.volume <= VOLUME_MINIMO
            );
        }
    }

    alarmeAtual = disparo;
    if (alarmeAtual.radioWebUltimaEstacao) {
        alarmeAtual.radioId = idRadioAtual;
    }
    alarmeEmExecucao = true;
    alarmeObservouAudioAtivo = false;
    inicioAlarmeAtualMs = millis();
    inicioPreparacaoFonteAlarmeMs = 0;
    proximaTentativaAudioAlarmeMs = 0;
    inicioPowerDownAlarmeFmMs = 0;

    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    prepararFonteAlarme();
    mostrarAlarme(
        alarmeAtual.nome,
        alarmeAtual.volume
    );

    Serial.printf(
        "Alarme iniciado: %s (ID %lu).\n",
        alarmeAtual.nome.c_str(),
        static_cast<unsigned long>(alarmeAtual.id)
    );
}

bool alarmeAtualUsaRadioWeb() {
    return
        !alarmeAtual.radioFm &&
        alarmeAtual.radioId != 0 &&
        obterRadioPorId(alarmeAtual.radioId) != nullptr;
}

bool alarmeAtualUsaRadioFm() {
    return alarmeAtual.radioFm;
}

bool alarmeAtualUsaDfPlayer() {
    return alarmeAtual.somDfPlayer != 0;
}

void usarSomPadraoAposFalhaRadioFm() {
    Serial.println(
        "Alarme: Radio FM indisponivel; usando som padrao."
    );

    definirAlarmeFmAtivo(false);
    alarmeAtual.radioFm = false;
    desativarRadioFm();

    if (
        !decoderDacEstaAtivo() &&
        !retomarAudio(alarmeAtual.volume, true)
    ) {
        Serial.println(
            "Alarme: falha ao preparar o DAC para o som padrao."
        );
    }

    configurarSaidaAudioDac(
        alarmeAtual.volume <= VOLUME_MINIMO
    );
    inicioPreparacaoFonteAlarmeMs = 0;
    inicioPowerDownAlarmeFmMs = 0;
    prepararFonteAlarme();
}

void ativarFonteRadioFmAlarme() {
    inicioPowerDownAlarmeFmMs = 0;

    if (!ativarRadioFm(alarmeAtual.volume)) {
        usarSomPadraoAposFalhaRadioFm();
        return;
    }

    if (!configurarSaidaAudioFm(
        alarmeAtual.volume <= VOLUME_MINIMO
    )) {
        Serial.println(
            "Alarme: falha ao habilitar os NS8002 para o Radio FM."
        );
        usarSomPadraoAposFalhaRadioFm();
        return;
    }
    definirAlarmeFmAtivo(true);
    faseAlarme = FaseAlarme::TOCANDO;

    StatusRadioFm statusFm = obterStatusRadioFm();
    Serial.printf(
        "Alarme: Radio FM ativado na ultima sintonia, %u.%02u MHz.\n",
        statusFm.frequencia10kHz / 100,
        statusFm.frequencia10kHz % 100
    );
}

void prepararFonteAlarme() {
    if (alarmeAtualUsaRadioFm()) {
        bool decoderEstavaAtivo = decoderDacEstaAtivo();
        StatusRadioFm statusFm = obterStatusRadioFm();

        if (
            statusFm.estado == EstadoRadioFm::ATIVO ||
            statusFm.estado == EstadoRadioFm::BUSCANDO
        ) {
            configurarSaidaAudioStandby();
            desativarRadioFm();
        }

        // A alimentação do TEF6686 vem antes de qualquer acesso I2C. Durante
        // a preparação, os NS8002 continuam em mute e só serão habilitados
        // depois que o receptor estiver inicializado.
        if (!configurarHabilitacaoRadioFm(true)) {
            Serial.println(
                "Alarme: falha ao colocar GPA4 em HIGH para o Radio FM."
            );
            usarSomPadraoAposFalhaRadioFm();
            return;
        }
        configurarSaidaAudioStandby();

        if (!suspenderAudio()) {
            Serial.println(
                "Alarme: falha ao desligar o DAC antes do Radio FM."
            );
            usarSomPadraoAposFalhaRadioFm();
            return;
        }

        if (wifiLigado()) {
            desligarWifi();
        }

        if (decoderEstavaAtivo) {
            faseAlarme = FaseAlarme::AGUARDANDO_POWER_DOWN_DAC_FM;
            inicioPowerDownAlarmeFmMs = millis();
            return;
        }

        ativarFonteRadioFmAlarme();
        return;
    }

    if (alarmeAtualUsaDfPlayer()) {
        bool decoderEstavaAtivo = decoderDacEstaAtivo();
        configurarSaidaAudioStandby();

        if (decoderEstavaAtivo) {
            if (!suspenderAudio()) {
                Serial.println(
                    "Alarme: falha ao desligar o DAC antes do DFPlayer."
                );
                alarmeAtual.somDfPlayer = 0;
                prepararFonteAlarme();
                return;
            }
            if (wifiLigado()) {
                desligarWifi();
            }
            faseAlarme =
                FaseAlarme::AGUARDANDO_POWER_DOWN_DAC_DFPLAYER;
            inicioPowerDownAlarmeFmMs = millis();
            return;
        }

        if (wifiLigado()) {
            desligarWifi();
        }
        if (!reproduzirSomDfPlayer(
            alarmeAtual.somDfPlayer,
            alarmeAtual.volume,
            UsoDfPlayer::ALARME
        )) {
            Serial.println(
                "Alarme: DFPlayer indisponivel; usando som padrao."
            );
            alarmeAtual.somDfPlayer = 0;
            if (
                !decoderDacEstaAtivo() &&
                !retomarAudio(alarmeAtual.volume, true)
            ) {
                Serial.println(
                    "Alarme: falha ao preparar DAC para fallback."
                );
            }
            configurarSaidaAudioDac(
                alarmeAtual.volume <= VOLUME_MINIMO
            );
            prepararFonteAlarme();
            return;
        }

        faseAlarme = FaseAlarme::TOCANDO;
        return;
    }

    if (alarmeAtualUsaRadioWeb()) {
        if (wifiConectado()) {
            faseAlarme = FaseAlarme::TOCANDO;
            iniciarCicloAudioAlarme();
            return;
        }

        if (!iniciarReconexaoWifiSalvo()) {
            Serial.println(
                "Alarme: rede salva indisponivel; usando som padrao."
            );
            alarmeAtual.radioId = 0;
            prepararFonteAlarme();
            return;
        }

        pararAudio();
        faseAlarme = FaseAlarme::AGUARDANDO_WIFI_RADIO;
        inicioPreparacaoFonteAlarmeMs = millis();
        return;
    }

    if (alarmeAtual.radioId != 0) {
        Serial.printf(
            "Alarme: radio ID %lu inexistente; usando som padrao.\n",
            static_cast<unsigned long>(alarmeAtual.radioId)
        );
    }

    // Arquivos locais só começam depois que qualquer stream de rede terminou
    // e a pilha Wi-Fi foi completamente desligada.
    if (wifiLigado()) {
        faseAlarme = FaseAlarme::AGUARDANDO_PARADA_RADIO;

        if (!pararAudio()) {
            proximaTentativaAudioAlarmeMs = millis() + 200;
            Serial.println(
                "Alarme: comando para parar a fonte de rede rejeitado."
            );
        }

        return;
    }

    faseAlarme = FaseAlarme::TOCANDO;
    iniciarCicloAudioAlarme();
}

void iniciarCicloAudioAlarme() {
    alarmeObservouAudioAtivo = false;

    const Radio* radio =
        obterRadioPorId(alarmeAtual.radioId);

    if (radio != nullptr) {
        if (inicioPreparacaoFonteAlarmeMs == 0) {
            inicioPreparacaoFonteAlarmeMs = millis();
        }

        if (!tocarRadioAlarme(
            radio->nome,
            radio->url,
            alarmeAtual.volume
        )) {
            proximaTentativaAudioAlarmeMs = millis() + 2000;
            Serial.println("Alarme: comando da radio rejeitado.");
        }

        return;
    }

    if (!tocarArquivoAlarme(
        "",
        alarmeAtual.volume
    )) {
        proximaTentativaAudioAlarmeMs = millis() + 2000;
        Serial.println("Alarme: comando de audio rejeitado.");
    }
}

void processarExecucaoAlarme() {
    unsigned long agoraMs = millis();
    constexpr uint32_t MILISSEGUNDOS_POR_MINUTO = 60000;
    uint32_t duracaoMaximaMs =
        (
            alarmeAtual.duracaoMinutos > 0
                ? alarmeAtual.duracaoMinutos
                : DURACAO_MAXIMA_ALARME_MINUTOS
        ) *
        MILISSEGUNDOS_POR_MINUTO;

    if (agoraMs - inicioAlarmeAtualMs >= duracaoMaximaMs) {
        finalizarExecucaoAlarme(false);
        return;
    }

    StatusAudio statusAudio = obterStatusAudio();

    if (faseAlarme == FaseAlarme::AGUARDANDO_POWER_DOWN_DAC_FM) {
        if (
            millis() - inicioPowerDownAlarmeFmMs >=
                TEMPO_POWER_DOWN_PCM5102A_MS
        ) {
            ativarFonteRadioFmAlarme();
        }

        return;
    }

    if (
        faseAlarme ==
        FaseAlarme::AGUARDANDO_POWER_DOWN_DAC_DFPLAYER
    ) {
        if (
            agoraMs - inicioPowerDownAlarmeFmMs >=
                TEMPO_POWER_DOWN_PCM5102A_MS
        ) {
            inicioPowerDownAlarmeFmMs = 0;
            prepararFonteAlarme();
        }
        return;
    }

    if (alarmeAtualUsaRadioFm()) {
        StatusRadioFm statusFm = obterStatusRadioFm();

        if (
            faseAlarme == FaseAlarme::TOCANDO &&
            (
                statusFm.estado == EstadoRadioFm::ERRO ||
                statusFm.estado == EstadoRadioFm::INDISPONIVEL
            )
        ) {
            usarSomPadraoAposFalhaRadioFm();
        }

        return;
    }

    if (alarmeAtualUsaDfPlayer()) {
        EstadoDfPlayer estadoDfPlayer = obterEstadoDfPlayer();
        if (estadoDfPlayer.fase == FaseDfPlayer::CONCLUIDO) {
            reproduzirSomDfPlayer(
                alarmeAtual.somDfPlayer,
                alarmeAtual.volume,
                UsoDfPlayer::ALARME
            );
        } else if (estadoDfPlayer.fase == FaseDfPlayer::ERRO) {
            pararDfPlayer();
            alarmeAtual.somDfPlayer = 0;
            if (
                !decoderDacEstaAtivo() &&
                !retomarAudio(alarmeAtual.volume, true)
            ) {
                Serial.println(
                    "Alarme: fallback do DFPlayer sem DAC."
                );
            }
            configurarSaidaAudioDac(
                alarmeAtual.volume <= VOLUME_MINIMO
            );
            prepararFonteAlarme();
        }
        return;
    }

    if (faseAlarme == FaseAlarme::AGUARDANDO_WIFI_RADIO) {
        if (wifiConectado()) {
            faseAlarme = FaseAlarme::TOCANDO;
            inicioPreparacaoFonteAlarmeMs = 0;
            iniciarCicloAudioAlarme();
            Serial.println(
                "Alarme: Wi-Fi conectado; iniciando radio web."
            );
        } else if (
            agoraMs - inicioPreparacaoFonteAlarmeMs >=
            TEMPO_LIMITE_CONEXAO_RADIO_ALARME_MS
        ) {
            Serial.println(
                "Alarme: limite da conexao atingido; usando som padrao."
            );
            alarmeAtual.radioId = 0;
            prepararFonteAlarme();
        }

        return;
    }

    if (faseAlarme == FaseAlarme::AGUARDANDO_PARADA_RADIO) {
        if (statusAudio.estado == EstadoAudio::PARADO) {
            if (wifiLigado()) {
                desligarWifi();
            }

            faseAlarme = FaseAlarme::TOCANDO;
            iniciarCicloAudioAlarme();
            Serial.println(
                "Alarme: radio parada e Wi-Fi desligado; iniciando audio."
            );
        } else if (
            proximaTentativaAudioAlarmeMs != 0 &&
            static_cast<int32_t>(
                agoraMs - proximaTentativaAudioAlarmeMs
            ) >= 0
        ) {
            if (pararAudio()) {
                proximaTentativaAudioAlarmeMs = 0;
            } else {
                proximaTentativaAudioAlarmeMs = agoraMs + 200;
            }
        }

        return;
    }

    if (alarmeAtualUsaRadioWeb()) {
        if (
            statusAudio.estado == EstadoAudio::TOCANDO ||
            statusAudio.estado == EstadoAudio::DEGRADADO
        ) {
            inicioPreparacaoFonteAlarmeMs = 0;
            return;
        }

        if (inicioPreparacaoFonteAlarmeMs == 0) {
            inicioPreparacaoFonteAlarmeMs = agoraMs;
        }

        if (
            proximaTentativaAudioAlarmeMs != 0 &&
            (
                statusAudio.estado == EstadoAudio::ERRO ||
                statusAudio.estado == EstadoAudio::PARADO
            ) &&
            static_cast<int32_t>(
                agoraMs - proximaTentativaAudioAlarmeMs
            ) >= 0
        ) {
            proximaTentativaAudioAlarmeMs = agoraMs + 2000;
            iniciarCicloAudioAlarme();
        }

        if (
            agoraMs - inicioPreparacaoFonteAlarmeMs >=
            TEMPO_LIMITE_CONEXAO_RADIO_ALARME_MS
        ) {
            Serial.println(
                "Alarme: radio web indisponivel; usando som padrao."
            );
            alarmeAtual.radioId = 0;
            prepararFonteAlarme();
        }

        return;
    }

    if (
        statusAudio.estado == EstadoAudio::BUFFERIZANDO ||
        statusAudio.estado == EstadoAudio::TOCANDO ||
        statusAudio.estado == EstadoAudio::DEGRADADO
    ) {
        alarmeObservouAudioAtivo = true;
        return;
    }

    if (
        statusAudio.estado == EstadoAudio::PARADO &&
        alarmeObservouAudioAtivo
    ) {
        iniciarCicloAudioAlarme();
        return;
    }

    if (
        statusAudio.estado == EstadoAudio::ERRO &&
        static_cast<int32_t>(
            agoraMs - proximaTentativaAudioAlarmeMs
        ) >= 0
    ) {
        proximaTentativaAudioAlarmeMs = agoraMs + 2000;
        iniciarCicloAudioAlarme();
    }
}

void finalizarExecucaoAlarme(bool interrompidoPeloUsuario) {
    if (!alarmeEmExecucao) {
        return;
    }

    bool fonteAlarmeEraFm = alarmeAtualUsaRadioFm();
    bool fonteAlarmeEraDfPlayer = alarmeAtualUsaDfPlayer();
    definirAlarmeFmAtivo(false);

    Serial.printf(
        "Alarme finalizado: %s (%s).\n",
        alarmeAtual.nome.c_str(),
        interrompidoPeloUsuario
            ? "clique no encoder de radios"
            : "limite de tempo"
    );

    alarmeEmExecucao = false;
    alarmeObservouAudioAtivo = false;
    faseAlarme = FaseAlarme::INATIVO;
    alarmeAtual = DisparoAlarme{};
    inicioPowerDownAlarmeFmMs = 0;

    if (fonteAlarmeEraFm) {
        configurarSaidaAudioStandby();
        desativarRadioFm();
    } else if (fonteAlarmeEraDfPlayer) {
        pararDfPlayer();
    } else {
        pararAudio();
        alterarVolumeAudio(volumeAtual);
    }

    estadoEquipamento = estadoAntesDoAlarme;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    switch (estadoAntesDoAlarme) {
        case EstadoEquipamento::RADIO_WEB:
            // Reutiliza a mesma transição assíncrona da interface. Assim a
            // restauração depois de um alarme nunca prende os encoders em uma
            // reconexão Wi-Fi bloqueante.
            entrarEstadoRadioWeb();
            break;


        case EstadoEquipamento::RADIO_FM:
            entrarEstadoRadioFm();
            break;

        case EstadoEquipamento::RELOGIO:
            configurarSaidaAudioStandby();
            suspenderAudio();
            desligarWifi();
            apagarIndicadorLed();
            mostrarTelaRelogio();
            break;
    }
}

// =====================================================
// Eventos dos controles
// =====================================================

void processarLeituraControles(
    const LeituraControles& leitura
) {
    if (
        modoInterface == ModoInterface::LISTA_CHIME ||
        modoInterface == ModoInterface::CONFIGURAR_CHIME
    ) {
        processarControlesChime(leitura);
        return;
    }

    if (
        modoInterface == ModoInterface::LISTA_CONFIGURACOES ||
        modoInterface == ModoInterface::CONFIGURACOES_RELOGIO ||
        modoInterface == ModoInterface::AJUSTE_HORA ||
        modoInterface == ModoInterface::CONFIGURACOES_TELA ||
        modoInterface == ModoInterface::CONFIGURAR_REPOUSO_TFT ||
        modoInterface == ModoInterface::CONFIGURAR_TEMA_TFT
    ) {
        processarControlesConfiguracoes(leitura);
        return;
    }

    if (
        modoInterface == ModoInterface::LISTA_ALARMES ||
        modoInterface == ModoInterface::EDITOR_ALARME ||
        modoInterface == ModoInterface::DIAS_ALARME ||
        modoInterface == ModoInterface::NOME_ALARME ||
        modoInterface == ModoInterface::CONFIRMAR_EXCLUSAO_ALARME
    ) {
        processarControlesAlarmes(leitura);
        return;
    }

    if (leitura.deslocamentoEncoderVolume != 0) {
        if (modoInterface == ModoInterface::SELECAO_FONTE) {
            processarNavegacaoFontes(
                leitura.deslocamentoEncoderVolume
            );
        } else if (
            estadoEquipamento == EstadoEquipamento::RELOGIO &&
            !ativacaoRadioPendente
        ) {
            entrarModoSelecaoFonte(
                leitura.deslocamentoEncoderVolume
            );
        } else {
            processarAjusteVolume(
                leitura.deslocamentoEncoderVolume
            );
        }
    }

    if (leitura.cliqueVolumeDetectado) {
        if (modoInterface == ModoInterface::DETALHES_RADIO_WEB) {
            configurarDetalhesRadioWeb(false);
        }

        if (modoInterface == ModoInterface::SELECAO_FONTE) {
            confirmarSelecaoFonte();
        } else {
            entrarModoSelecaoFonte();
        }

        return;
    }

    if (modoInterface == ModoInterface::SELECAO_FONTE) {
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RADIO_FM) {
        if (leitura.cliqueNavegacaoDetectado) {
            modoInterface =
                modoInterface == ModoInterface::DETALHES_RADIO_FM
                    ? ModoInterface::REPOUSO
                    : ModoInterface::DETALHES_RADIO_FM;
            atualizarApresentacaoRadioFm(true);
            return;
        }

        if (leitura.deslocamentoEncoderNavegacao != 0) {
            if (ativacaoRadioFmPendente) {
                navegacaoFmDurantePowerDown +=
                    leitura.deslocamentoEncoderNavegacao;
                momentoNavegacaoFmDurantePowerDownUs =
                    leitura.momentoEncoderNavegacaoUs;
                sequenciaNavegacaoFmDurantePowerDown =
                    leitura.sequenciaEncoderNavegacao;
            } else {
                navegarRadioFm(
                    leitura.deslocamentoEncoderNavegacao,
                    leitura.momentoEncoderNavegacaoUs,
                    leitura.sequenciaEncoderNavegacao
                );
                atualizarApresentacaoRadioFm(true);
            }
        }

        return;
    }

    if (modoInterface == ModoInterface::DETALHES_RADIO_WEB) {
        if (leitura.cliqueNavegacaoDetectado) {
            modoInterface = ModoInterface::REPOUSO;
            configurarDetalhesRadioWeb(false);
            atualizarDisplayEstadoAudio(true);
        }
        return;
    }

    if (leitura.cliqueNavegacaoDetectado) {
        if (modoInterface == ModoInterface::REPOUSO) {
            modoInterface = ModoInterface::DETALHES_RADIO_WEB;
            configurarDetalhesRadioWeb(true);
        } else {
            confirmarSelecaoRadio();
        }

        return;
    }

    if (leitura.deslocamentoEncoderNavegacao == 0) {
        return;
    }

    if (modoInterface == ModoInterface::SELECAO_RADIO) {
        processarNavegacaoRadios(
            leitura.deslocamentoEncoderNavegacao
        );
    } else {
        entrarModoSelecaoRadio();
        if (modoInterface == ModoInterface::SELECAO_RADIO) {
            processarNavegacaoRadios(
                leitura.deslocamentoEncoderNavegacao
            );
        }
    }
}

// =====================================================
// Seleção da fonte pelo encoder de volume
// =====================================================

const char* obterNomeItemMenuPrincipal(ItemMenuPrincipal item) {
    switch (item) {
        case ItemMenuPrincipal::RELOGIO:
            return "Relógio";
        case ItemMenuPrincipal::RADIO_FM:
            return "Radio FM";
        case ItemMenuPrincipal::RADIO_WEB:
            return "Radio Web";
        case ItemMenuPrincipal::ALARMES:
            return "Alarmes";
        case ItemMenuPrincipal::CHIMES:
            return "Chime";
        case ItemMenuPrincipal::CONFIGURACOES:
            return "Configurações";
    }

    return "Funcao";
}

ItemMenuPrincipal obterItemMenuDoEstadoAtual() {
    switch (estadoEquipamento) {
        case EstadoEquipamento::RELOGIO:
            return ItemMenuPrincipal::RELOGIO;
        case EstadoEquipamento::RADIO_FM:
            return ItemMenuPrincipal::RADIO_FM;
        case EstadoEquipamento::RADIO_WEB:
            return ItemMenuPrincipal::RADIO_WEB;
    }
    return ItemMenuPrincipal::RELOGIO;
}

void apresentarFonteEmSelecao() {
    mostrarMensagem(
            obterNomeItemMenuPrincipal(itemMenuPrincipalEmSelecao)
    );
}

void entrarModoSelecaoFonte(long deslocamentoInicial) {
    if (ativacaoRadioPendente) {
        // Interrompe uma ativação ainda não confirmada antes de apresentar
        // outra escolha. Assim nenhuma fonte liga por trás da tela de seleção.
        entrarEstadoRelogio();
    }

    modoInterface = ModoInterface::SELECAO_FONTE;
    itemMenuPrincipalEmSelecao = obterItemMenuDoEstadoAtual();
    barraVolumeVisivel = false;
    momentoUltimaAtividadeSelecaoMs = millis();

    if (deslocamentoInicial != 0) {
        processarNavegacaoFontes(deslocamentoInicial);
    } else {
        apresentarFonteEmSelecao();
    }

    Serial.println("Modo: selecao de fonte.");
}

void processarNavegacaoFontes(long deslocamento) {
    if (deslocamento == 0) {
        return;
    }

    constexpr ItemMenuPrincipal ORDEM_ITENS[] = {
        ItemMenuPrincipal::RELOGIO,
        ItemMenuPrincipal::RADIO_FM,
        ItemMenuPrincipal::RADIO_WEB,
        ItemMenuPrincipal::ALARMES,
        ItemMenuPrincipal::CHIMES,
        ItemMenuPrincipal::CONFIGURACOES
    };
    constexpr long QUANTIDADE_ITENS =
        sizeof(ORDEM_ITENS) / sizeof(ORDEM_ITENS[0]);

    long indiceAtual = 0;
    for (long indice = 0; indice < QUANTIDADE_ITENS; ++indice) {
        if (ORDEM_ITENS[indice] == itemMenuPrincipalEmSelecao) {
            indiceAtual = indice;
            break;
        }
    }

    long novoIndice =
        (indiceAtual + deslocamento) % QUANTIDADE_ITENS;
    if (novoIndice < 0) {
        novoIndice += QUANTIDADE_ITENS;
    }

    itemMenuPrincipalEmSelecao = ORDEM_ITENS[novoIndice];
    momentoUltimaAtividadeSelecaoMs = millis();
    apresentarFonteEmSelecao();

    Serial.print("Fonte selecionada: ");
    Serial.println(
        obterNomeItemMenuPrincipal(itemMenuPrincipalEmSelecao)
    );
}

void confirmarSelecaoFonte() {
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    if (itemMenuPrincipalEmSelecao == ItemMenuPrincipal::ALARMES) {
        entrarListaAlarmes();
        return;
    }
    if (itemMenuPrincipalEmSelecao == ItemMenuPrincipal::CHIMES) {
        entrarConfiguracaoChime();
        return;
    }
    if (
        itemMenuPrincipalEmSelecao ==
        ItemMenuPrincipal::CONFIGURACOES
    ) {
        entrarListaConfiguracoes();
        return;
    }

    ItemMenuPrincipal itemEstadoAtual = obterItemMenuDoEstadoAtual();
    if (
        itemMenuPrincipalEmSelecao == itemEstadoAtual &&
        !ativacaoRadioPendente &&
        !ativacaoRadioFmPendente
    ) {
        switch (estadoEquipamento) {
            case EstadoEquipamento::RELOGIO:
                mostrarTelaRelogio();
                break;
            case EstadoEquipamento::RADIO_FM:
                atualizarApresentacaoRadioFm(true);
                break;
            case EstadoEquipamento::RADIO_WEB:
                atualizarDisplayEstadoAudio(true);
                break;
        }
        return;
    }

    switch (itemMenuPrincipalEmSelecao) {
        case ItemMenuPrincipal::RELOGIO:
            entrarEstadoRelogio();
            break;
        case ItemMenuPrincipal::RADIO_FM:
            entrarEstadoRadioFm();
            break;
        case ItemMenuPrincipal::RADIO_WEB:
            entrarEstadoRadioWeb();
            break;
        case ItemMenuPrincipal::ALARMES:
        case ItemMenuPrincipal::CHIMES:
        case ItemMenuPrincipal::CONFIGURACOES:
            break;
    }
}

void cancelarSelecaoFontePorInatividade() {
    if (
        modoInterface != ModoInterface::SELECAO_FONTE ||
        millis() - momentoUltimaAtividadeSelecaoMs <
            TEMPO_INATIVIDADE_SELECAO_MS
    ) {
        return;
    }

    modoInterface = ModoInterface::REPOUSO;
    itemMenuPrincipalEmSelecao = obterItemMenuDoEstadoAtual();

    switch (estadoEquipamento) {
        case EstadoEquipamento::RELOGIO:
            mostrarTelaRelogio();
            break;
        case EstadoEquipamento::RADIO_FM:
            atualizarApresentacaoRadioFm(true);
            break;
        case EstadoEquipamento::RADIO_WEB:
            atualizarDisplayEstadoAudio(true);
            break;
    }

    Serial.println("Selecao de fonte cancelada por inatividade.");
}

// =====================================================
// Configurações locais — relógio e TFT
// =====================================================

namespace {

constexpr uint16_t OPCOES_REPOUSO_TFT[] = {
    1, 2, 5, 10, 15, 30, 0
};
constexpr long QUANTIDADE_TEMAS_TFT =
    static_cast<long>(TemaUiTft::APENAS_LARANJA) + 1;

const char* nomeTemaTft(TemaUiTft tema) {
    switch (tema) {
        case TemaUiTft::LARANJA:
            return "LARANJA NIXIE";
        case TemaUiTft::VERDE:
            return "VERDE FÓSFORO";
        case TemaUiTft::AZUL:
            return "AZUL GELO";
        case TemaUiTft::BRANCO:
            return "BRANCO CLÁSSICO";
        case TemaUiTft::VERMELHO:
            return "VERMELHO";
        case TemaUiTft::APENAS_LARANJA:
            return "LARANJA";
    }
    return "LARANJA NIXIE";
}

const char* nomeTemaTftCurto(TemaUiTft tema) {
    switch (tema) {
        case TemaUiTft::LARANJA:
            return "LARANJA";
        case TemaUiTft::VERDE:
            return "VERDE";
        case TemaUiTft::AZUL:
            return "AZUL";
        case TemaUiTft::BRANCO:
            return "BRANCO";
        case TemaUiTft::VERMELHO:
            return "VERMELHO";
        case TemaUiTft::APENAS_LARANJA:
            return "SÓ LARANJA";
    }
    return "LARANJA";
}

void formatarRepousoTft(
    uint16_t minutos,
    char* destino,
    size_t tamanho
) {
    if (minutos == 0) {
        strlcpy(destino, "DESLIGADO", tamanho);
    } else {
        snprintf(destino, tamanho, "%u MIN", minutos);
    }
}

uint8_t indiceOpcaoRepouso(uint16_t minutos) {
    for (
        uint8_t indice = 0;
        indice < sizeof(OPCOES_REPOUSO_TFT) /
            sizeof(OPCOES_REPOUSO_TFT[0]);
        indice++
    ) {
        if (OPCOES_REPOUSO_TFT[indice] == minutos) {
            return indice;
        }
    }
    return 6;
}

void preencherItemConfiguracao(
    ItemListaAlarmesDisplay& item,
    const char* titulo,
    const char* subtitulo,
    const char* valor
) {
    strlcpy(item.titulo, titulo, sizeof(item.titulo));
    strlcpy(item.subtitulo, subtitulo, sizeof(item.subtitulo));
    strlcpy(item.valor, valor, sizeof(item.valor));
}

void voltarAoMenuPrincipalPelasConfiguracoes() {
    modoInterface = ModoInterface::SELECAO_FONTE;
    itemMenuPrincipalEmSelecao = ItemMenuPrincipal::CONFIGURACOES;
    apresentarFonteEmSelecao();
}

void salvarPreferenciasInterface() {
    if (!salvarConfiguracoesUsuario(configuracoesUsuario)) {
        Serial.println("Falha ao salvar as configurações da interface.");
    }
}

}

void entrarListaConfiguracoes() {
    modoInterface = ModoInterface::LISTA_CONFIGURACOES;
    indiceConfiguracaoSelecionada = 0;
    apresentarConfiguracoes();
}

void apresentarConfiguracoes() {
    DadosDisplayConfiguracoes dados;
    dados.vista = VistaConfiguracoes::LISTA;
    const bool ajusteDiretoSelecionado =
        modoInterface == ModoInterface::CONFIGURACOES_RELOGIO &&
        indiceConfiguracaoSelecionada == 1;
    strlcpy(
        dados.indicadorTopoDireito,
        ajusteDiretoSelecionado
            ? "B: AJUSTA · CLIQUE VOLTA"
            : "A: ENTRA · B: VOLTA",
        sizeof(dados.indicadorTopoDireito)
    );

    if (modoInterface == ModoInterface::LISTA_CONFIGURACOES) {
        strlcpy(dados.titulo, "CONFIGURAÇÕES", sizeof(dados.titulo));
        dados.quantidadeItens = 2;
        preencherItemConfiguracao(
            dados.itens[0],
            "RELÓGIO",
            "Hora manual e sincronização",
            "ENTRAR"
        );
        preencherItemConfiguracao(
            dados.itens[1],
            "TELA",
            "Repouso e cor da interface",
            "ENTRAR"
        );
    } else if (modoInterface == ModoInterface::CONFIGURACOES_RELOGIO) {
        strlcpy(dados.titulo, "RELÓGIO", sizeof(dados.titulo));
        dados.quantidadeItens = 2;
        struct tm dataHora = {};
        char horario[12] = "--:--";
        if (obterDataHoraLocal(dataHora)) {
            snprintf(
                horario,
                sizeof(horario),
                "%02d:%02d",
                dataHora.tm_hour,
                dataHora.tm_min
            );
        }
        preencherItemConfiguracao(
            dados.itens[0],
            "AJUSTAR HORA",
            "Preserva a data atual",
            horario
        );
        preencherItemConfiguracao(
            dados.itens[1],
            "SINCRONIZAÇÃO AUTOMÁTICA",
            "B gira para alterar",
            configuracoesUsuario.sincronizacaoAutomatica
                ? "LIGADA"
                : "DESLIG."
        );
    } else if (modoInterface == ModoInterface::CONFIGURACOES_TELA) {
        strlcpy(dados.titulo, "TELA", sizeof(dados.titulo));
        dados.quantidadeItens = 2;
        char repouso[12];
        formatarRepousoTft(
            configuracoesUsuario.repousoTftMinutos,
            repouso,
            sizeof(repouso)
        );
        preencherItemConfiguracao(
            dados.itens[0],
            "REPOUSO DO TFT",
            "Som e alarmes continuam ativos",
            repouso
        );
        preencherItemConfiguracao(
            dados.itens[1],
            "COR DA INTERFACE",
            "Paleta aplicada a todas as telas",
            nomeTemaTftCurto(configuracoesUsuario.temaTft)
        );
    } else {
        dados.vista = VistaConfiguracoes::VALOR;
        if (modoInterface == ModoInterface::AJUSTE_HORA) {
            strlcpy(dados.titulo, "AJUSTAR HORA", sizeof(dados.titulo));
            strlcpy(
                dados.rotulo,
                campoHoraEmAjuste == 0 ? "HORAS" : "MINUTOS",
                sizeof(dados.rotulo)
            );
            snprintf(
                dados.valor,
                sizeof(dados.valor),
                "%02u:%02u",
                horaEmAjuste,
                minutoEmAjuste
            );
            strlcpy(
                dados.ajuda,
                "A gira: campo · B gira: valor",
                sizeof(dados.ajuda)
            );
        } else if (
            modoInterface == ModoInterface::CONFIGURAR_REPOUSO_TFT
        ) {
            strlcpy(dados.titulo, "REPOUSO DO TFT", sizeof(dados.titulo));
            strlcpy(dados.rotulo, "TEMPO SEM USO", sizeof(dados.rotulo));
            formatarRepousoTft(
                repousoTftEmAjuste,
                dados.valor,
                sizeof(dados.valor)
            );
            strlcpy(
                dados.ajuda,
                "B gira: altera · A confirma",
                sizeof(dados.ajuda)
            );
        } else {
            strlcpy(dados.titulo, "COR DA INTERFACE", sizeof(dados.titulo));
            strlcpy(dados.rotulo, "PRÉVIA", sizeof(dados.rotulo));
            strlcpy(
                dados.valor,
                nomeTemaTft(temaTftEmAjuste),
                sizeof(dados.valor)
            );
            strlcpy(
                dados.ajuda,
                "B gira: prévia · A confirma",
                sizeof(dados.ajuda)
            );
        }
    }

    dados.itemSelecionado = indiceConfiguracaoSelecionada;
    mostrarConfiguracoes(dados);
}

void processarControlesConfiguracoes(
    const LeituraControles& leitura
) {
    const bool emLista =
        modoInterface == ModoInterface::LISTA_CONFIGURACOES ||
        modoInterface == ModoInterface::CONFIGURACOES_RELOGIO ||
        modoInterface == ModoInterface::CONFIGURACOES_TELA;

    if (leitura.cliqueNavegacaoDetectado) {
        if (modoInterface == ModoInterface::LISTA_CONFIGURACOES) {
            voltarAoMenuPrincipalPelasConfiguracoes();
        } else if (modoInterface == ModoInterface::CONFIGURACOES_RELOGIO ||
                   modoInterface == ModoInterface::CONFIGURACOES_TELA) {
            modoInterface = ModoInterface::LISTA_CONFIGURACOES;
            indiceConfiguracaoSelecionada = 0;
            apresentarConfiguracoes();
        } else {
            if (modoInterface == ModoInterface::CONFIGURAR_TEMA_TFT) {
                configurarTemaInterfaceTft(temaTftAntesDaPrevia);
            }
            modoInterface =
                modoInterface == ModoInterface::AJUSTE_HORA
                    ? ModoInterface::CONFIGURACOES_RELOGIO
                    : ModoInterface::CONFIGURACOES_TELA;
            apresentarConfiguracoes();
        }
        return;
    }

    if (emLista && leitura.deslocamentoEncoderVolume != 0) {
        const long quantidade =
            modoInterface == ModoInterface::LISTA_CONFIGURACOES
                ? 2L
                : 2L;
        long novo =
            (static_cast<long>(indiceConfiguracaoSelecionada) +
                leitura.deslocamentoEncoderVolume) % quantidade;
        if (novo < 0) {
            novo += quantidade;
        }
        indiceConfiguracaoSelecionada = static_cast<uint8_t>(novo);
        apresentarConfiguracoes();
    } else if (
        modoInterface == ModoInterface::AJUSTE_HORA &&
        leitura.deslocamentoEncoderVolume != 0
    ) {
        campoHoraEmAjuste = campoHoraEmAjuste == 0 ? 1 : 0;
        apresentarConfiguracoes();
    }

    if (
        modoInterface == ModoInterface::CONFIGURACOES_RELOGIO &&
        indiceConfiguracaoSelecionada == 1 &&
        leitura.deslocamentoEncoderNavegacao != 0
    ) {
        const bool novaSincronizacao =
            !configuracoesUsuario.sincronizacaoAutomatica;
        if (
            novaSincronizacao !=
            configuracoesUsuario.sincronizacaoAutomatica
        ) {
            configuracoesUsuario.sincronizacaoAutomatica =
                novaSincronizacao;
            configurarSincronizacaoAutomaticaRelogio(
                novaSincronizacao
            );
            salvarPreferenciasInterface();
        }
        apresentarConfiguracoes();
    }

    if (!emLista && leitura.deslocamentoEncoderNavegacao != 0) {
        long deslocamento = leitura.deslocamentoEncoderNavegacao;
        if (modoInterface == ModoInterface::AJUSTE_HORA) {
            if (campoHoraEmAjuste == 0) {
                long valor = (horaEmAjuste + deslocamento) % 24;
                if (valor < 0) valor += 24;
                horaEmAjuste = static_cast<uint8_t>(valor);
            } else {
                long valor = (minutoEmAjuste + deslocamento) % 60;
                if (valor < 0) valor += 60;
                minutoEmAjuste = static_cast<uint8_t>(valor);
            }
        } else if (
            modoInterface == ModoInterface::CONFIGURAR_REPOUSO_TFT
        ) {
            constexpr long quantidade =
                sizeof(OPCOES_REPOUSO_TFT) /
                sizeof(OPCOES_REPOUSO_TFT[0]);
            long indice =
                (indiceOpcaoRepouso(repousoTftEmAjuste) +
                    deslocamento) % quantidade;
            if (indice < 0) indice += quantidade;
            repousoTftEmAjuste = OPCOES_REPOUSO_TFT[indice];
        } else if (modoInterface == ModoInterface::CONFIGURAR_TEMA_TFT) {
            long tema =
                (static_cast<long>(temaTftEmAjuste) + deslocamento) %
                QUANTIDADE_TEMAS_TFT;
            if (tema < 0) tema += QUANTIDADE_TEMAS_TFT;
            temaTftEmAjuste = static_cast<TemaUiTft>(tema);
            configurarTemaInterfaceTft(temaTftEmAjuste);
        }
        apresentarConfiguracoes();
    }

    if (!leitura.cliqueVolumeDetectado) {
        return;
    }

    if (modoInterface == ModoInterface::LISTA_CONFIGURACOES) {
        modoInterface =
            indiceConfiguracaoSelecionada == 0
                ? ModoInterface::CONFIGURACOES_RELOGIO
                : ModoInterface::CONFIGURACOES_TELA;
        indiceConfiguracaoSelecionada = 0;
    } else if (modoInterface == ModoInterface::CONFIGURACOES_RELOGIO) {
        if (indiceConfiguracaoSelecionada == 0) {
            struct tm dataHora = {};
            if (obterDataHoraLocal(dataHora)) {
                horaEmAjuste = dataHora.tm_hour;
                minutoEmAjuste = dataHora.tm_min;
            }
            campoHoraEmAjuste = 0;
            modoInterface = ModoInterface::AJUSTE_HORA;
        }
    } else if (modoInterface == ModoInterface::CONFIGURACOES_TELA) {
        if (indiceConfiguracaoSelecionada == 0) {
            repousoTftEmAjuste =
                configuracoesUsuario.repousoTftMinutos;
            modoInterface = ModoInterface::CONFIGURAR_REPOUSO_TFT;
        } else {
            temaTftEmAjuste = configuracoesUsuario.temaTft;
            temaTftAntesDaPrevia = configuracoesUsuario.temaTft;
            modoInterface = ModoInterface::CONFIGURAR_TEMA_TFT;
        }
    } else if (modoInterface == ModoInterface::AJUSTE_HORA) {
        if (ajustarHoraLocalRelogio(horaEmAjuste, minutoEmAjuste)) {
            modoInterface = ModoInterface::CONFIGURACOES_RELOGIO;
            indiceConfiguracaoSelecionada = 0;
        }
    } else if (
        modoInterface == ModoInterface::CONFIGURAR_REPOUSO_TFT
    ) {
        configuracoesUsuario.repousoTftMinutos = repousoTftEmAjuste;
        momentoUltimaAtividadeTftMs = millis();
        salvarPreferenciasInterface();
        modoInterface = ModoInterface::CONFIGURACOES_TELA;
        indiceConfiguracaoSelecionada = 0;
    } else if (modoInterface == ModoInterface::CONFIGURAR_TEMA_TFT) {
        configuracoesUsuario.temaTft = temaTftEmAjuste;
        salvarPreferenciasInterface();
        modoInterface = ModoInterface::CONFIGURACOES_TELA;
        indiceConfiguracaoSelecionada = 1;
    }
    apresentarConfiguracoes();
}

// =====================================================
// Configuração local do chime
// =====================================================

namespace {

constexpr CampoConfiguracaoChime CAMPOS_CHIME[] = {
    CampoConfiguracaoChime::ATIVO,
    CampoConfiguracaoChime::PERIODICIDADE,
    CampoConfiguracaoChime::ARQUIVO_DFPLAYER,
    CampoConfiguracaoChime::VOLUME,
    CampoConfiguracaoChime::SILENCIO_NOTURNO,
    CampoConfiguracaoChime::INICIO_SILENCIO,
    CampoConfiguracaoChime::FIM_SILENCIO
};

constexpr uint8_t INTERVALOS_CHIME[] = {
    1, 5, 15, 30, 60
};

constexpr uint8_t QUANTIDADE_CAMPOS_CHIME =
    sizeof(CAMPOS_CHIME) / sizeof(CAMPOS_CHIME[0]);

void encerrarPreviaConfiguracaoChime() {
    if (obterEstadoDfPlayer().uso == UsoDfPlayer::PREVIA) {
        pararDfPlayer();
    }
}

void iniciarPreviaConfiguracaoChime() {
    CatalogoSonsDfPlayer catalogo = obterCatalogoSonsDfPlayer();
    if (
        !catalogo.disponivel ||
        catalogo.quantidade == 0 ||
        configuracaoChimeEmEdicao.som == 0 ||
        configuracaoChimeEmEdicao.som > catalogo.quantidade
    ) {
        return;
    }

    EstadoDfPlayer estado = obterEstadoDfPlayer();
    if (
        estado.uso != UsoDfPlayer::NENHUM &&
        estado.uso != UsoDfPlayer::PREVIA
    ) {
        return;
    }
    if (
        estado.uso == UsoDfPlayer::PREVIA &&
        estado.somAtual == configuracaoChimeEmEdicao.som &&
        (
            estado.fase == FaseDfPlayer::INICIANDO ||
            estado.fase == FaseDfPlayer::TOCANDO
        )
    ) {
        definirVolumeDfPlayer(configuracaoChimeEmEdicao.volume);
        return;
    }
    if (estado.uso == UsoDfPlayer::PREVIA) {
        pararDfPlayer();
    }
    reproduzirSomDfPlayer(
        configuracaoChimeEmEdicao.som,
        configuracaoChimeEmEdicao.volume,
        UsoDfPlayer::PREVIA
    );
}

void formatarArquivoChime(char* destino, size_t tamanho) {
    SomDfPlayer som;
    if (obterSomDfPlayer(configuracaoChimeEmEdicao.som, som)) {
        snprintf(
            destino,
            tamanho,
            "%03u.MP3",
            static_cast<unsigned>(som.numero)
        );
    } else {
        strlcpy(destino, "INDISPONÍVEL", tamanho);
    }
}

uint8_t indiceIntervaloChime(uint8_t intervalo) {
    for (
        uint8_t indice = 0;
        indice < sizeof(INTERVALOS_CHIME) /
            sizeof(INTERVALOS_CHIME[0]);
        indice++
    ) {
        if (INTERVALOS_CHIME[indice] == intervalo) {
            return indice;
        }
    }
    return 0;
}

void voltarAoMenuPrincipalPeloChime() {
    encerrarPreviaConfiguracaoChime();
    modoInterface = ModoInterface::SELECAO_FONTE;
    itemMenuPrincipalEmSelecao = ItemMenuPrincipal::CHIMES;
    erroPersistenciaChime = false;
    apresentarFonteEmSelecao();
}

bool chimePodeSerAtivado() {
    if (!configuracaoChimeEmEdicao.ativo) {
        return true;
    }
    CatalogoSonsDfPlayer catalogo = obterCatalogoSonsDfPlayer();
    return
        catalogo.disponivel &&
        configuracaoChimeEmEdicao.som >= 1 &&
        configuracaoChimeEmEdicao.som <= catalogo.quantidade;
}

void preencherItemChime(
    ItemListaAlarmesDisplay& item,
    CampoConfiguracaoChime campo
) {
    char valor[24] = {};
    switch (campo) {
        case CampoConfiguracaoChime::ATIVO:
            preencherItemConfiguracao(
                item,
                "CHIME ATIVO",
                "Toques automáticos",
                configuracaoChimeEmEdicao.ativo ? "SIM" : "NÃO"
            );
            return;
        case CampoConfiguracaoChime::PERIODICIDADE:
            snprintf(
                valor,
                sizeof(valor),
                "%u MIN",
                configuracaoChimeEmEdicao.intervaloMinutos
            );
            preencherItemConfiguracao(
                item,
                "PERIODICIDADE",
                "Intervalo entre os toques",
                valor
            );
            return;
        case CampoConfiguracaoChime::ARQUIVO_DFPLAYER:
            formatarArquivoChime(valor, sizeof(valor));
            preencherItemConfiguracao(
                item,
                "ARQUIVO DFPLAYER",
                "Som reproduzido pelo chime",
                valor
            );
            return;
        case CampoConfiguracaoChime::VOLUME:
            snprintf(
                valor,
                sizeof(valor),
                "%u%%",
                static_cast<unsigned>(
                    (
                        configuracaoChimeEmEdicao.volume * 100U +
                        VOLUME_MAXIMO / 2
                    ) / VOLUME_MAXIMO
                )
            );
            preencherItemConfiguracao(
                item,
                "VOLUME DO CHIME",
                "Independente do áudio principal",
                valor
            );
            return;
        case CampoConfiguracaoChime::SILENCIO_NOTURNO:
            preencherItemConfiguracao(
                item,
                "SILÊNCIO NOTURNO",
                "Bloqueia toques no intervalo",
                configuracaoChimeEmEdicao.silencioNoturno
                    ? "SIM"
                    : "NÃO"
            );
            return;
        case CampoConfiguracaoChime::INICIO_SILENCIO:
            snprintf(
                valor,
                sizeof(valor),
                "%02u:00",
                configuracaoChimeEmEdicao.inicioSilencioHora
            );
            preencherItemConfiguracao(
                item,
                "INÍCIO DO SILÊNCIO",
                "Hora local",
                valor
            );
            return;
        case CampoConfiguracaoChime::FIM_SILENCIO:
            snprintf(
                valor,
                sizeof(valor),
                "%02u:00",
                configuracaoChimeEmEdicao.fimSilencioHora
            );
            preencherItemConfiguracao(
                item,
                "FIM DO SILÊNCIO",
                "Hora local",
                valor
            );
            return;
    }
}

}

void entrarConfiguracaoChime() {
    encerrarPreviaConfiguracaoChime();
    configuracaoChimeEmEdicao = obterConfiguracaoChime();
    indiceChimeSelecionado = 0;
    erroPersistenciaChime = false;
    modoInterface = ModoInterface::LISTA_CHIME;
    apresentarConfiguracaoChime();
}

void apresentarConfiguracaoChime() {
    DadosDisplayConfiguracoes dados;
    strlcpy(dados.titulo, "CHIME", sizeof(dados.titulo));
    strlcpy(
        dados.indicadorTopoDireito,
        "A: ENTRA · B: VOLTA",
        sizeof(dados.indicadorTopoDireito)
    );

    if (modoInterface == ModoInterface::LISTA_CHIME) {
        dados.vista = VistaConfiguracoes::LISTA;
        uint8_t inicio = 0;
        if (indiceChimeSelecionado >= MAXIMO_ITENS_ALARMES_VISIVEIS) {
            inicio = static_cast<uint8_t>(
                indiceChimeSelecionado -
                MAXIMO_ITENS_ALARMES_VISIVEIS + 1
            );
        }
        if (
            inicio + MAXIMO_ITENS_ALARMES_VISIVEIS >
            QUANTIDADE_CAMPOS_CHIME
        ) {
            inicio = static_cast<uint8_t>(
                QUANTIDADE_CAMPOS_CHIME -
                MAXIMO_ITENS_ALARMES_VISIVEIS
            );
        }

        for (
            uint8_t indice = inicio;
            indice < QUANTIDADE_CAMPOS_CHIME &&
                dados.quantidadeItens <
                    MAXIMO_ITENS_ALARMES_VISIVEIS;
            indice++
        ) {
            preencherItemChime(
                dados.itens[dados.quantidadeItens++],
                CAMPOS_CHIME[indice]
            );
        }
        dados.itemSelecionado = static_cast<uint8_t>(
            indiceChimeSelecionado - inicio
        );
    } else {
        dados.vista = VistaConfiguracoes::VALOR;
        char arquivo[24];
        switch (campoChimeEmEdicao) {
            case CampoConfiguracaoChime::ATIVO:
                strlcpy(dados.rotulo, "CHIME ATIVO", sizeof(dados.rotulo));
                strlcpy(
                    dados.valor,
                    configuracaoChimeEmEdicao.ativo ? "SIM" : "NÃO",
                    sizeof(dados.valor)
                );
                strlcpy(dados.ajuda, "B gira: altera", sizeof(dados.ajuda));
                break;
            case CampoConfiguracaoChime::PERIODICIDADE:
                strlcpy(dados.rotulo, "PERIODICIDADE", sizeof(dados.rotulo));
                snprintf(
                    dados.valor,
                    sizeof(dados.valor),
                    "%u MIN",
                    configuracaoChimeEmEdicao.intervaloMinutos
                );
                strlcpy(
                    dados.ajuda,
                    "Opções: 1, 5, 15, 30 e 60 min",
                    sizeof(dados.ajuda)
                );
                break;
            case CampoConfiguracaoChime::ARQUIVO_DFPLAYER:
                strlcpy(dados.rotulo, "ARQUIVO DFPLAYER", sizeof(dados.rotulo));
                formatarArquivoChime(arquivo, sizeof(arquivo));
                strlcpy(dados.valor, arquivo, sizeof(dados.valor));
                strlcpy(
                    dados.ajuda,
                    "B gira: troca e reproduz prévia",
                    sizeof(dados.ajuda)
                );
                break;
            case CampoConfiguracaoChime::VOLUME:
                strlcpy(dados.rotulo, "VOLUME DO CHIME", sizeof(dados.rotulo));
                snprintf(
                    dados.valor,
                    sizeof(dados.valor),
                    "%u%%",
                    static_cast<unsigned>(
                        (
                            configuracaoChimeEmEdicao.volume * 100U +
                            VOLUME_MAXIMO / 2
                        ) / VOLUME_MAXIMO
                    )
                );
                strlcpy(
                    dados.ajuda,
                    "B gira: ajusta durante a prévia",
                    sizeof(dados.ajuda)
                );
                break;
            case CampoConfiguracaoChime::SILENCIO_NOTURNO:
                strlcpy(dados.rotulo, "SILÊNCIO NOTURNO", sizeof(dados.rotulo));
                strlcpy(
                    dados.valor,
                    configuracaoChimeEmEdicao.silencioNoturno
                        ? "SIM"
                        : "NÃO",
                    sizeof(dados.valor)
                );
                strlcpy(dados.ajuda, "B gira: altera", sizeof(dados.ajuda));
                break;
            case CampoConfiguracaoChime::INICIO_SILENCIO:
                strlcpy(dados.rotulo, "INÍCIO DO SILÊNCIO", sizeof(dados.rotulo));
                snprintf(
                    dados.valor,
                    sizeof(dados.valor),
                    "%02u:00",
                    configuracaoChimeEmEdicao.inicioSilencioHora
                );
                strlcpy(dados.ajuda, "B gira: ajusta a hora", sizeof(dados.ajuda));
                break;
            case CampoConfiguracaoChime::FIM_SILENCIO:
                strlcpy(dados.rotulo, "FIM DO SILÊNCIO", sizeof(dados.rotulo));
                snprintf(
                    dados.valor,
                    sizeof(dados.valor),
                    "%02u:00",
                    configuracaoChimeEmEdicao.fimSilencioHora
                );
                strlcpy(dados.ajuda, "B gira: ajusta a hora", sizeof(dados.ajuda));
                break;
        }
        if (erroPersistenciaChime) {
            strlcpy(
                dados.ajuda,
                "Falha ao salvar ou som indisponível",
                sizeof(dados.ajuda)
            );
        }
    }

    mostrarConfiguracoes(dados);
}

void processarControlesChime(const LeituraControles& leitura) {
    if (leitura.cliqueNavegacaoDetectado) {
        if (modoInterface == ModoInterface::LISTA_CHIME) {
            voltarAoMenuPrincipalPeloChime();
        } else {
            configuracaoChimeEmEdicao = configuracaoChimeAntesEdicao;
            encerrarPreviaConfiguracaoChime();
            erroPersistenciaChime = false;
            modoInterface = ModoInterface::LISTA_CHIME;
            apresentarConfiguracaoChime();
        }
        return;
    }

    if (modoInterface == ModoInterface::LISTA_CHIME) {
        if (leitura.deslocamentoEncoderVolume != 0) {
            uint8_t indiceAnterior = indiceChimeSelecionado;
            long novo =
                (static_cast<long>(indiceChimeSelecionado) +
                    leitura.deslocamentoEncoderVolume) %
                QUANTIDADE_CAMPOS_CHIME;
            if (novo < 0) {
                novo += QUANTIDADE_CAMPOS_CHIME;
            }
            indiceChimeSelecionado = static_cast<uint8_t>(novo);

            const bool transicaoArquivoParaVolume =
                CAMPOS_CHIME[indiceAnterior] ==
                    CampoConfiguracaoChime::ARQUIVO_DFPLAYER &&
                CAMPOS_CHIME[indiceChimeSelecionado] ==
                    CampoConfiguracaoChime::VOLUME;
            if (!transicaoArquivoParaVolume) {
                encerrarPreviaConfiguracaoChime();
            }
            apresentarConfiguracaoChime();
        }

        if (leitura.cliqueVolumeDetectado) {
            campoChimeEmEdicao = CAMPOS_CHIME[indiceChimeSelecionado];
            configuracaoChimeAntesEdicao = configuracaoChimeEmEdicao;
            erroPersistenciaChime = false;
            modoInterface = ModoInterface::CONFIGURAR_CHIME;

            if (
                campoChimeEmEdicao ==
                    CampoConfiguracaoChime::ARQUIVO_DFPLAYER
            ) {
                CatalogoSonsDfPlayer catalogo =
                    obterCatalogoSonsDfPlayer();
                if (
                    catalogo.disponivel &&
                    catalogo.quantidade > 0 &&
                    (
                        configuracaoChimeEmEdicao.som == 0 ||
                        configuracaoChimeEmEdicao.som >
                            catalogo.quantidade
                    )
                ) {
                    configuracaoChimeEmEdicao.som = 1;
                }
                iniciarPreviaConfiguracaoChime();
            } else if (
                campoChimeEmEdicao ==
                    CampoConfiguracaoChime::VOLUME
            ) {
                iniciarPreviaConfiguracaoChime();
            } else {
                encerrarPreviaConfiguracaoChime();
            }
            apresentarConfiguracaoChime();
        }
        return;
    }

    if (leitura.deslocamentoEncoderNavegacao != 0) {
        const long passo = leitura.deslocamentoEncoderNavegacao;
        switch (campoChimeEmEdicao) {
            case CampoConfiguracaoChime::ATIVO:
                configuracaoChimeEmEdicao.ativo = passo > 0;
                break;
            case CampoConfiguracaoChime::PERIODICIDADE:
            {
                constexpr long quantidade =
                    sizeof(INTERVALOS_CHIME) /
                    sizeof(INTERVALOS_CHIME[0]);
                long indice =
                    (indiceIntervaloChime(
                        configuracaoChimeEmEdicao.intervaloMinutos
                    ) + passo) % quantidade;
                if (indice < 0) {
                    indice += quantidade;
                }
                configuracaoChimeEmEdicao.intervaloMinutos =
                    INTERVALOS_CHIME[indice];
                break;
            }
            case CampoConfiguracaoChime::ARQUIVO_DFPLAYER:
            {
                CatalogoSonsDfPlayer catalogo =
                    obterCatalogoSonsDfPlayer();
                if (!catalogo.disponivel || catalogo.quantidade == 0) {
                    break;
                }
                long indice =
                    (static_cast<long>(
                        configuracaoChimeEmEdicao.som
                    ) - 1 + passo) % catalogo.quantidade;
                if (indice < 0) {
                    indice += catalogo.quantidade;
                }
                configuracaoChimeEmEdicao.som =
                    static_cast<uint8_t>(indice + 1);
                iniciarPreviaConfiguracaoChime();
                break;
            }
            case CampoConfiguracaoChime::VOLUME:
                configuracaoChimeEmEdicao.volume =
                    static_cast<uint8_t>(constrain(
                        static_cast<long>(
                            configuracaoChimeEmEdicao.volume
                        ) + passo,
                        1L,
                        static_cast<long>(VOLUME_MAXIMO)
                    ));
                if (obterEstadoDfPlayer().uso == UsoDfPlayer::PREVIA) {
                    definirVolumeDfPlayer(
                        configuracaoChimeEmEdicao.volume
                    );
                }
                break;
            case CampoConfiguracaoChime::SILENCIO_NOTURNO:
                configuracaoChimeEmEdicao.silencioNoturno = passo > 0;
                break;
            case CampoConfiguracaoChime::INICIO_SILENCIO:
            {
                long hora =
                    (configuracaoChimeEmEdicao.inicioSilencioHora +
                        passo) % 24;
                if (hora < 0) {
                    hora += 24;
                }
                configuracaoChimeEmEdicao.inicioSilencioHora =
                    static_cast<uint8_t>(hora);
                break;
            }
            case CampoConfiguracaoChime::FIM_SILENCIO:
            {
                long hora =
                    (configuracaoChimeEmEdicao.fimSilencioHora +
                        passo) % 24;
                if (hora < 0) {
                    hora += 24;
                }
                configuracaoChimeEmEdicao.fimSilencioHora =
                    static_cast<uint8_t>(hora);
                break;
            }
        }
        erroPersistenciaChime = false;
        apresentarConfiguracaoChime();
    }

    if (!leitura.cliqueVolumeDetectado) {
        return;
    }

    if (!chimePodeSerAtivado()) {
        erroPersistenciaChime = true;
        apresentarConfiguracaoChime();
        return;
    }

    if (!salvarConfiguracaoChime(configuracaoChimeEmEdicao)) {
        erroPersistenciaChime = true;
        apresentarConfiguracaoChime();
        return;
    }

    if (
        campoChimeEmEdicao !=
            CampoConfiguracaoChime::ARQUIVO_DFPLAYER
    ) {
        encerrarPreviaConfiguracaoChime();
    }
    erroPersistenciaChime = false;
    modoInterface = ModoInterface::LISTA_CHIME;
    apresentarConfiguracaoChime();
}

void processarRepousoTft() {
    if (
        tftEmRepouso ||
        configuracoesUsuario.repousoTftMinutos == 0
    ) {
        return;
    }

    uint32_t limiteMs =
        static_cast<uint32_t>(
            configuracoesUsuario.repousoTftMinutos
        ) * 60UL * 1000UL;
    if (millis() - momentoUltimaAtividadeTftMs < limiteMs) {
        return;
    }

    tftEmRepouso = true;
    configurarTftLigado(false);
}

// =====================================================
// Interface local de alarmes — editor por foco
// =====================================================

namespace {

const char* nomeFonteAlarme(FonteAlarmeConfiguravel fonte) {
    switch (fonte) {
        case FonteAlarmeConfiguravel::RADIO_FM:
            return "RÁDIO FM";
        case FonteAlarmeConfiguravel::RADIO_WEB:
            return "RÁDIO WEB";
        case FonteAlarmeConfiguravel::DFPLAYER:
            return "DFPLAYER";
        case FonteAlarmeConfiguravel::SOM_PADRAO:
            return "SOM PADRÃO";
    }
    return "-";
}

void resumirDiasAlarme(
    const ConfiguracaoAlarme& alarme,
    char* destino,
    size_t tamanho
) {
    if (alarme.execucaoUnica) {
        snprintf(
            destino,
            tamanho,
            "%02u/%02u/%04u",
            alarme.dia,
            alarme.mes,
            alarme.ano
        );
        return;
    }

    const uint8_t mascara = alarme.diasSemana;
    if (mascara == 0b00111110) {
        strlcpy(destino, "SEG-SEX", tamanho);
        return;
    }
    if (mascara == 0b01000001) {
        strlcpy(destino, "FIM DE SEMANA", tamanho);
        return;
    }
    if (mascara == 0b01111111) {
        strlcpy(destino, "TODOS OS DIAS", tamanho);
        return;
    }

    static constexpr const char* NOMES[] = {
        "DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"
    };
    destino[0] = '\0';
    for (uint8_t indice = 0; indice < 7; indice++) {
        if ((mascara & (1U << indice)) == 0) {
            continue;
        }
        if (destino[0] != '\0') {
            // Formato compacto para que todos os dias selecionados caibam
            // no cartão do editor ao voltar da seleção personalizada.
            strlcat(destino, " ", tamanho);
        }
        strlcat(destino, NOMES[indice], tamanho);
    }
    if (destino[0] == '\0') {
        strlcpy(destino, "NENHUM DIA", tamanho);
    }
}

void prepararEdicaoNomeAlarme() {
    static constexpr const char* CARACTERES =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    memset(nomeAlarmeUi, ' ', TAMANHO_NOME_ALARME_UI);
    nomeAlarmeUi[TAMANHO_NOME_ALARME_UI] = '\0';

    size_t quantidade = min(
        strlen(alarmeEmEdicao.nome),
        static_cast<size_t>(TAMANHO_NOME_ALARME_UI)
    );
    for (size_t indice = 0; indice < quantidade; indice++) {
        char caractere = alarmeEmEdicao.nome[indice];
        if (caractere >= 'a' && caractere <= 'z') {
            caractere = static_cast<char>(caractere - 'a' + 'A');
        }
        nomeAlarmeUi[indice] =
            strchr(CARACTERES, caractere) != nullptr
                ? caractere
                : ' ';
    }
    indiceCaractereNomeAlarme = 0;
}

void finalizarEdicaoNomeAlarme() {
    size_t tamanho = TAMANHO_NOME_ALARME_UI;
    while (tamanho > 0 && nomeAlarmeUi[tamanho - 1] == ' ') {
        tamanho--;
    }

    char nomeFinal[TAMANHO_NOME_ALARME_UI + 1] = {};
    if (tamanho == 0) {
        strlcpy(nomeFinal, "Alarme", sizeof(nomeFinal));
    } else {
        memcpy(nomeFinal, nomeAlarmeUi, tamanho);
        nomeFinal[tamanho] = '\0';
    }

    if (strcmp(alarmeEmEdicao.nome, nomeFinal) != 0) {
        strlcpy(
            alarmeEmEdicao.nome,
            nomeFinal,
            sizeof(alarmeEmEdicao.nome)
        );
        alarmeAlterado = true;
    }
}

void montarNomeAlarmeComCursor(char* destino, size_t tamanho) {
    size_t posicao = 0;
    for (
        uint8_t indice = 0;
        indice < TAMANHO_NOME_ALARME_UI && posicao + 1 < tamanho;
        indice++
    ) {
        if (
            indice == indiceCaractereNomeAlarme &&
            posicao + 3 < tamanho
        ) {
            destino[posicao++] = '[';
            destino[posicao++] =
                nomeAlarmeUi[indice] == ' '
                    ? '_'
                    : nomeAlarmeUi[indice];
            destino[posicao++] = ']';
        } else {
            destino[posicao++] =
                nomeAlarmeUi[indice] == ' '
                    ? '_'
                    : nomeAlarmeUi[indice];
        }
    }
    destino[posicao] = '\0';
}

void alterarCaractereNomeAlarme(long passo) {
    static constexpr const char* CARACTERES =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    constexpr int QUANTIDADE_CARACTERES = 38;
    const char* encontrado = strchr(
        CARACTERES,
        nomeAlarmeUi[indiceCaractereNomeAlarme]
    );
    int atual = encontrado == nullptr
        ? 0
        : static_cast<int>(encontrado - CARACTERES);
    int novo = (atual + passo) % QUANTIDADE_CARACTERES;
    if (novo < 0) {
        novo += QUANTIDADE_CARACTERES;
    }
    nomeAlarmeUi[indiceCaractereNomeAlarme] = CARACTERES[novo];
}

uint8_t montarCamposEditorAlarme(
    CampoEditorAlarme* campos,
    size_t capacidade
) {
    uint8_t quantidade = 0;
    auto adicionar = [&](CampoEditorAlarme campo) {
        if (quantidade < capacidade) {
            campos[quantidade++] = campo;
        }
    };

    adicionar(CampoEditorAlarme::HORA);
    adicionar(CampoEditorAlarme::MINUTO);
    adicionar(CampoEditorAlarme::REPETICAO);
    adicionar(CampoEditorAlarme::FONTE);
    if (
        alarmeEmEdicao.fonte ==
        FonteAlarmeConfiguravel::DFPLAYER
    ) {
        adicionar(CampoEditorAlarme::ARQUIVO_DFPLAYER);
    }
    adicionar(CampoEditorAlarme::VOLUME);
    adicionar(CampoEditorAlarme::DURACAO);
    adicionar(CampoEditorAlarme::ATIVO);
    adicionar(CampoEditorAlarme::NOME);
    adicionar(CampoEditorAlarme::EXCLUIR);
    return quantidade;
}

CampoEditorAlarme campoEditorAlarmeAtual() {
    CampoEditorAlarme campos[10];
    uint8_t quantidade = montarCamposEditorAlarme(
        campos,
        sizeof(campos) / sizeof(campos[0])
    );
    if (quantidade == 0) {
        return CampoEditorAlarme::HORA;
    }
    indiceCampoAlarme = min(
        indiceCampoAlarme,
        static_cast<uint8_t>(quantidade - 1)
    );
    return campos[indiceCampoAlarme];
}

void registrarEventoInterfaceAlarmes(uint32_t momentoUs) {
    sequenciaEventoInterfaceAlarmes++;
    momentoEventoInterfaceAlarmesUs =
        momentoUs != 0 ? momentoUs : micros();
}

void preencherIndicadorTopoAlarmes(
    DadosDisplayConfiguracaoAlarmes& dados
) {
    const char* fonte = "SEM ÁUDIO";
    if (estadoEquipamento == EstadoEquipamento::RADIO_FM) {
        fonte = "FM";
    } else if (estadoEquipamento == EstadoEquipamento::RADIO_WEB) {
        fonte = "WEB";
    }

    struct tm dataHora = {};
    if (obterDataHoraLocal(dataHora)) {
        snprintf(
            dados.indicadorTopoDireito,
            sizeof(dados.indicadorTopoDireito),
            "%s · %02d:%02d",
            fonte,
            dataHora.tm_hour,
            dataHora.tm_min
        );
    } else {
        snprintf(
            dados.indicadorTopoDireito,
            sizeof(dados.indicadorTopoDireito),
            "%s · --:--",
            fonte
        );
    }
}

void preencherResumoEditor(
    DadosDisplayConfiguracaoAlarmes& dados
) {
    snprintf(
        dados.horarioResumo,
        sizeof(dados.horarioResumo),
        "%02u:%02u",
        alarmeEmEdicao.hora,
        alarmeEmEdicao.minuto
    );
    char dias[40];
    resumirDiasAlarme(alarmeEmEdicao, dias, sizeof(dias));
    snprintf(
        dados.resumo,
        sizeof(dados.resumo),
        "%s · %s",
        dias,
        nomeFonteAlarme(alarmeEmEdicao.fonte)
    );
    strlcpy(
        dados.estadoResumo,
        alarmeEmEdicao.ativo ? "ON" : "OFF",
        sizeof(dados.estadoResumo)
    );
}

void encerrarPreviaAlarme() {
    EstadoDfPlayer estado = obterEstadoDfPlayer();
    if (estado.uso == UsoDfPlayer::PREVIA) {
        pararDfPlayer();
    }
}

void reproduzirPreviaArquivoAlarme() {
    CatalogoSonsDfPlayer catalogo = obterCatalogoSonsDfPlayer();
    if (
        !catalogo.disponivel ||
        catalogo.quantidade == 0 ||
        alarmeEmEdicao.somDfPlayer == 0 ||
        alarmeEmEdicao.somDfPlayer > catalogo.quantidade
    ) {
        return;
    }

    EstadoDfPlayer estado = obterEstadoDfPlayer();
    if (
        estado.uso != UsoDfPlayer::NENHUM &&
        estado.uso != UsoDfPlayer::PREVIA
    ) {
        return;
    }
    if (estado.uso == UsoDfPlayer::PREVIA) {
        pararDfPlayer();
    }
    reproduzirSomDfPlayer(
        alarmeEmEdicao.somDfPlayer,
        alarmeEmEdicao.volume,
        UsoDfPlayer::PREVIA
    );
}

bool salvarEdicaoAlarme() {
    if (!alarmeAlterado && !alarmeNovoEmEdicao) {
        return true;
    }
    erroPersistenciaAlarme =
        !salvarConfiguracaoAlarme(alarmeEmEdicao);
    if (!erroPersistenciaAlarme) {
        alarmeAlterado = false;
        alarmeNovoEmEdicao = false;
    }
    return !erroPersistenciaAlarme;
}

void abrirAlarmeSelecionado() {
    erroPersistenciaAlarme = false;
    alarmeAlterado = false;
    alarmeNovoEmEdicao =
        indiceAlarmeSelecionado == quantidadeConfiguracoesAlarmes;

    if (alarmeNovoEmEdicao) {
        if (!criarConfiguracaoAlarme(alarmeEmEdicao)) {
            erroPersistenciaAlarme = true;
            apresentarListaAlarmes();
            return;
        }
    } else {
        alarmeEmEdicao =
            configuracoesAlarmes[indiceAlarmeSelecionado];
    }

    indiceCampoAlarme = 0;
    diaAlarmeSelecionado = 1;
    modoInterface = ModoInterface::EDITOR_ALARME;
    apresentarEditorAlarme();
}

}

void entrarListaAlarmes() {
    encerrarPreviaAlarme();
    quantidadeConfiguracoesAlarmes = listarConfiguracoesAlarmes(
        configuracoesAlarmes,
        QUANTIDADE_MAXIMA_ALARMES
    );
    size_t quantidadeItens = quantidadeConfiguracoesAlarmes + 1;
    if (indiceAlarmeSelecionado >= quantidadeItens) {
        indiceAlarmeSelecionado = quantidadeItens - 1;
    }
    modoInterface = ModoInterface::LISTA_ALARMES;
    erroPersistenciaAlarme = false;
    momentoUltimaAtividadeSelecaoMs = millis();
    apresentarListaAlarmes();
}

void apresentarListaAlarmes() {
    DadosDisplayConfiguracaoAlarmes dados;
    dados.vista = VistaConfiguracaoAlarmes::LISTA;
    preencherIndicadorTopoAlarmes(dados);
    dados.erroPersistencia = erroPersistenciaAlarme;
    dados.sequenciaEventoEncoder = sequenciaEventoInterfaceAlarmes;
    dados.momentoEventoEncoderUs = momentoEventoInterfaceAlarmesUs;

    const size_t total = quantidadeConfiguracoesAlarmes + 1;
    size_t inicio = 0;
    if (indiceAlarmeSelecionado >= MAXIMO_ITENS_ALARMES_VISIVEIS) {
        inicio = indiceAlarmeSelecionado -
            MAXIMO_ITENS_ALARMES_VISIVEIS + 1;
    }
    if (
        total > MAXIMO_ITENS_ALARMES_VISIVEIS &&
        inicio + MAXIMO_ITENS_ALARMES_VISIVEIS > total
    ) {
        inicio = total - MAXIMO_ITENS_ALARMES_VISIVEIS;
    }

    for (
        size_t indice = inicio;
        indice < total &&
            dados.quantidadeItens < MAXIMO_ITENS_ALARMES_VISIVEIS;
        indice++
    ) {
        ItemListaAlarmesDisplay& item =
            dados.itens[dados.quantidadeItens++];
        if (indice == quantidadeConfiguracoesAlarmes) {
            strlcpy(
                item.titulo,
                "+ CRIAR NOVO ALARME",
                sizeof(item.titulo)
            );
            strlcpy(
                item.subtitulo,
                "Adicionar horário independente",
                sizeof(item.subtitulo)
            );
            strlcpy(item.valor, "CRIAR", sizeof(item.valor));
            continue;
        }

        const ConfiguracaoAlarme& alarme =
            configuracoesAlarmes[indice];
        char dias[40];
        resumirDiasAlarme(alarme, dias, sizeof(dias));
        snprintf(
            item.titulo,
            sizeof(item.titulo),
            "%02u:%02u  %s",
            alarme.hora,
            alarme.minuto,
            alarme.nome
        );
        snprintf(
            item.subtitulo,
            sizeof(item.subtitulo),
            "%s · %s",
            dias,
            nomeFonteAlarme(alarme.fonte)
        );
        strlcpy(
            item.valor,
            alarme.ativo ? "ATIVO" : "OFF",
            sizeof(item.valor)
        );
    }

    dados.itemSelecionado = static_cast<uint8_t>(
        indiceAlarmeSelecionado - inicio
    );
    mostrarConfiguracaoAlarmes(dados);
}

void apresentarEditorAlarme() {
    CampoEditorAlarme campos[10];
    uint8_t quantidadeCampos = montarCamposEditorAlarme(
        campos,
        sizeof(campos) / sizeof(campos[0])
    );
    indiceCampoAlarme = min(
        indiceCampoAlarme,
        static_cast<uint8_t>(quantidadeCampos - 1)
    );
    CampoEditorAlarme campo = campos[indiceCampoAlarme];

    DadosDisplayConfiguracaoAlarmes dados;
    dados.vista =
        modoInterface == ModoInterface::DIAS_ALARME
            ? VistaConfiguracaoAlarmes::DIAS_PERSONALIZADOS
            : modoInterface == ModoInterface::NOME_ALARME
                ? VistaConfiguracaoAlarmes::EDITAR_NOME
            : modoInterface ==
                    ModoInterface::CONFIRMAR_EXCLUSAO_ALARME
                ? VistaConfiguracaoAlarmes::CONFIRMAR_EXCLUSAO
                : VistaConfiguracaoAlarmes::EDITOR;
    preencherIndicadorTopoAlarmes(dados);
    preencherResumoEditor(dados);
    dados.campoAtual = indiceCampoAlarme;
    dados.quantidadeCampos = quantidadeCampos;
    dados.mascaraDias = alarmeEmEdicao.diasSemana;
    dados.diaSelecionado = diaAlarmeSelecionado;
    dados.confirmarExclusao = confirmarExclusaoAlarme;
    dados.erroPersistencia = erroPersistenciaAlarme;
    dados.sequenciaEventoEncoder = sequenciaEventoInterfaceAlarmes;
    dados.momentoEventoEncoderUs = momentoEventoInterfaceAlarmesUs;

    switch (campo) {
        case CampoEditorAlarme::HORA:
            strlcpy(dados.rotuloCampo, "HORAS", sizeof(dados.rotuloCampo));
            snprintf(dados.valorCampo, sizeof(dados.valorCampo), "%02u", alarmeEmEdicao.hora);
            strlcpy(dados.ajudaCampo, "B gira para ajustar", sizeof(dados.ajudaCampo));
            break;
        case CampoEditorAlarme::MINUTO:
            strlcpy(dados.rotuloCampo, "MINUTOS", sizeof(dados.rotuloCampo));
            snprintf(dados.valorCampo, sizeof(dados.valorCampo), "%02u", alarmeEmEdicao.minuto);
            strlcpy(dados.ajudaCampo, "B gira para ajustar", sizeof(dados.ajudaCampo));
            break;
        case CampoEditorAlarme::REPETICAO:
            strlcpy(dados.rotuloCampo, "REPETIÇÃO", sizeof(dados.rotuloCampo));
            resumirDiasAlarme(alarmeEmEdicao, dados.valorCampo, sizeof(dados.valorCampo));
            strlcpy(dados.ajudaCampo, "B gira: predefinições · A: personalizar", sizeof(dados.ajudaCampo));
            break;
        case CampoEditorAlarme::FONTE:
            strlcpy(dados.rotuloCampo, "FONTE", sizeof(dados.rotuloCampo));
            strlcpy(dados.valorCampo, nomeFonteAlarme(alarmeEmEdicao.fonte), sizeof(dados.valorCampo));
            strlcpy(
                dados.ajudaCampo,
                alarmeEmEdicao.fonte == FonteAlarmeConfiguravel::RADIO_FM
                    ? "Usará a última frequência FM"
                    : alarmeEmEdicao.fonte == FonteAlarmeConfiguravel::RADIO_WEB
                        ? "Usará a última estação Web"
                        : "Arquivo local do DFPlayer",
                sizeof(dados.ajudaCampo)
            );
            break;
        case CampoEditorAlarme::ARQUIVO_DFPLAYER:
        {
            strlcpy(dados.rotuloCampo, "ARQUIVO DFPLAYER", sizeof(dados.rotuloCampo));
            SomDfPlayer som;
            if (obterSomDfPlayer(alarmeEmEdicao.somDfPlayer, som)) {
                snprintf(
                    dados.valorCampo,
                    sizeof(dados.valorCampo),
                    "%03u.MP3",
                    som.numero
                );
            } else {
                strlcpy(dados.valorCampo, "INDISPONÍVEL", sizeof(dados.valorCampo));
            }
            strlcpy(dados.ajudaCampo, "B gira: troca e reproduz a prévia", sizeof(dados.ajudaCampo));
            break;
        }
        case CampoEditorAlarme::VOLUME:
        {
            strlcpy(dados.rotuloCampo, "VOLUME DO ALARME", sizeof(dados.rotuloCampo));
            int percentual =
                (alarmeEmEdicao.volume * 100 + VOLUME_MAXIMO / 2) /
                VOLUME_MAXIMO;
            snprintf(dados.valorCampo, sizeof(dados.valorCampo), "%d%%", percentual);
            strlcpy(dados.ajudaCampo, "Escala real preservada", sizeof(dados.ajudaCampo));
            break;
        }
        case CampoEditorAlarme::DURACAO:
            strlcpy(dados.rotuloCampo, "TEMPO MÁXIMO", sizeof(dados.rotuloCampo));
            snprintf(dados.valorCampo, sizeof(dados.valorCampo), "%u MIN", alarmeEmEdicao.duracaoMinutos);
            strlcpy(dados.ajudaCampo, "Encerramento automático do alarme", sizeof(dados.ajudaCampo));
            break;
        case CampoEditorAlarme::ATIVO:
            strlcpy(dados.rotuloCampo, "ESTADO", sizeof(dados.rotuloCampo));
            strlcpy(dados.valorCampo, alarmeEmEdicao.ativo ? "LIGADO" : "DESLIGADO", sizeof(dados.valorCampo));
            strlcpy(dados.ajudaCampo, "B gira ou A pressiona", sizeof(dados.ajudaCampo));
            break;
        case CampoEditorAlarme::NOME:
            strlcpy(dados.rotuloCampo, "NOME", sizeof(dados.rotuloCampo));
            if (modoInterface == ModoInterface::NOME_ALARME) {
                montarNomeAlarmeComCursor(
                    dados.valorCampo,
                    sizeof(dados.valorCampo)
                );
                strlcpy(
                    dados.ajudaCampo,
                    "A: posição/avança · B gira: letra · B botão: volta",
                    sizeof(dados.ajudaCampo)
                );
            } else {
                strlcpy(
                    dados.valorCampo,
                    alarmeEmEdicao.nome,
                    sizeof(dados.valorCampo)
                );
                strlcpy(
                    dados.ajudaCampo,
                    "A pressiona para editar o nome",
                    sizeof(dados.ajudaCampo)
                );
            }
            break;
        case CampoEditorAlarme::EXCLUIR:
            strlcpy(dados.rotuloCampo, "AÇÃO", sizeof(dados.rotuloCampo));
            strlcpy(dados.valorCampo, "EXCLUIR ALARME", sizeof(dados.valorCampo));
            strlcpy(dados.ajudaCampo, "A pressiona · exige confirmação", sizeof(dados.ajudaCampo));
            break;
    }

    mostrarConfiguracaoAlarmes(dados);
}

void encerrarInterfaceAlarmes() {
    encerrarPreviaAlarme();
    modoInterface = ModoInterface::SELECAO_FONTE;
    itemMenuPrincipalEmSelecao = ItemMenuPrincipal::ALARMES;
    erroPersistenciaAlarme = false;
    apresentarFonteEmSelecao();
}

void processarControlesAlarmes(const LeituraControles& leitura) {
    bool evento =
        leitura.deslocamentoEncoderNavegacao != 0 ||
        leitura.deslocamentoEncoderVolume != 0 ||
        leitura.cliqueNavegacaoDetectado ||
        leitura.cliqueVolumeDetectado;
    if (evento) {
        registrarEventoInterfaceAlarmes(
            leitura.momentoEncoderNavegacaoUs
        );
        momentoUltimaAtividadeSelecaoMs = millis();
    }

    if (modoInterface == ModoInterface::LISTA_ALARMES) {
        // No equipamento, o encoder A e o encoder de volume/funcoes.
        // Seu giro navega e seu clique entra no item. O encoder B e o
        // encoder de radios; dentro dos alarmes, seu botao sempre volta.
        if (leitura.cliqueNavegacaoDetectado) {
            encerrarInterfaceAlarmes();
            return;
        }
        if (leitura.deslocamentoEncoderVolume != 0) {
            long total = static_cast<long>(
                quantidadeConfiguracoesAlarmes + 1
            );
            long novo =
                (static_cast<long>(indiceAlarmeSelecionado) +
                    leitura.deslocamentoEncoderVolume) % total;
            if (novo < 0) {
                novo += total;
            }
            indiceAlarmeSelecionado = static_cast<size_t>(novo);
            apresentarListaAlarmes();
        }
        if (leitura.cliqueVolumeDetectado) {
            abrirAlarmeSelecionado();
        }
        return;
    }

    if (modoInterface == ModoInterface::DIAS_ALARME) {
        if (leitura.cliqueNavegacaoDetectado) {
            modoInterface = ModoInterface::EDITOR_ALARME;
            apresentarEditorAlarme();
            return;
        }
        if (leitura.deslocamentoEncoderVolume != 0) {
            long novo =
                (static_cast<long>(diaAlarmeSelecionado) +
                    leitura.deslocamentoEncoderVolume) % 7;
            if (novo < 0) {
                novo += 7;
            }
            diaAlarmeSelecionado = static_cast<uint8_t>(novo);
        }
        if (leitura.cliqueVolumeDetectado) {
            uint8_t bit = 1U << diaAlarmeSelecionado;
            uint8_t novaMascara = alarmeEmEdicao.diasSemana ^ bit;
            if (novaMascara != 0) {
                alarmeEmEdicao.diasSemana = novaMascara;
                alarmeEmEdicao.execucaoUnica = false;
                alarmeAlterado = true;
            }
        }
        apresentarEditorAlarme();
        return;
    }

    if (modoInterface == ModoInterface::NOME_ALARME) {
        if (leitura.cliqueNavegacaoDetectado) {
            finalizarEdicaoNomeAlarme();
            modoInterface = ModoInterface::EDITOR_ALARME;
            apresentarEditorAlarme();
            return;
        }

        bool alterouInterface = false;
        if (leitura.deslocamentoEncoderVolume != 0) {
            long novo =
                (static_cast<long>(indiceCaractereNomeAlarme) +
                    leitura.deslocamentoEncoderVolume) %
                TAMANHO_NOME_ALARME_UI;
            if (novo < 0) {
                novo += TAMANHO_NOME_ALARME_UI;
            }
            indiceCaractereNomeAlarme = static_cast<uint8_t>(novo);
            alterouInterface = true;
        }
        if (leitura.deslocamentoEncoderNavegacao != 0) {
            alterarCaractereNomeAlarme(
                leitura.deslocamentoEncoderNavegacao
            );
            alterouInterface = true;
        }
        if (leitura.cliqueVolumeDetectado) {
            indiceCaractereNomeAlarme = static_cast<uint8_t>(
                (indiceCaractereNomeAlarme + 1) %
                TAMANHO_NOME_ALARME_UI
            );
            alterouInterface = true;
        }
        if (alterouInterface) {
            apresentarEditorAlarme();
        }
        return;
    }

    if (
        modoInterface ==
        ModoInterface::CONFIRMAR_EXCLUSAO_ALARME
    ) {
        if (leitura.cliqueNavegacaoDetectado) {
            modoInterface = ModoInterface::EDITOR_ALARME;
            apresentarEditorAlarme();
            return;
        }
        if (leitura.deslocamentoEncoderVolume != 0) {
            confirmarExclusaoAlarme = !confirmarExclusaoAlarme;
            apresentarEditorAlarme();
        }
        if (leitura.cliqueVolumeDetectado) {
            if (!confirmarExclusaoAlarme) {
                modoInterface = ModoInterface::EDITOR_ALARME;
                apresentarEditorAlarme();
                return;
            }
            encerrarPreviaAlarme();
            if (
                !alarmeNovoEmEdicao &&
                !excluirConfiguracaoAlarme(alarmeEmEdicao.id)
            ) {
                erroPersistenciaAlarme = true;
                apresentarEditorAlarme();
                return;
            }
            alarmeNovoEmEdicao = false;
            alarmeAlterado = false;
            indiceAlarmeSelecionado = 0;
            entrarListaAlarmes();
        }
        return;
    }

    if (modoInterface != ModoInterface::EDITOR_ALARME) {
        return;
    }

    if (leitura.cliqueNavegacaoDetectado) {
        encerrarPreviaAlarme();
        if (!salvarEdicaoAlarme()) {
            apresentarEditorAlarme();
            return;
        }
        quantidadeConfiguracoesAlarmes = listarConfiguracoesAlarmes(
            configuracoesAlarmes,
            QUANTIDADE_MAXIMA_ALARMES
        );
        if (quantidadeConfiguracoesAlarmes > 0) {
            size_t indiceEncontrado = 0;
            for (
                size_t indice = 0;
                indice < quantidadeConfiguracoesAlarmes;
                indice++
            ) {
                if (
                    configuracoesAlarmes[indice].id ==
                    alarmeEmEdicao.id
                ) {
                    indiceEncontrado = indice;
                    break;
                }
            }
            indiceAlarmeSelecionado = indiceEncontrado;
        }
        modoInterface = ModoInterface::LISTA_ALARMES;
        apresentarListaAlarmes();
        return;
    }

    CampoEditorAlarme campos[10];
    uint8_t quantidadeCampos = montarCamposEditorAlarme(
        campos,
        sizeof(campos) / sizeof(campos[0])
    );

    if (leitura.deslocamentoEncoderVolume != 0) {
        CampoEditorAlarme campoAnterior = campoEditorAlarmeAtual();
        long novo =
            (static_cast<long>(indiceCampoAlarme) +
                leitura.deslocamentoEncoderVolume) %
            quantidadeCampos;
        if (novo < 0) {
            novo += quantidadeCampos;
        }
        indiceCampoAlarme = static_cast<uint8_t>(novo);
        CampoEditorAlarme campoNovo = campoEditorAlarmeAtual();
        if (
            campoAnterior == CampoEditorAlarme::VOLUME &&
            campoNovo != CampoEditorAlarme::VOLUME
        ) {
            encerrarPreviaAlarme();
        }
        apresentarEditorAlarme();
        return;
    }

    CampoEditorAlarme campo = campoEditorAlarmeAtual();
    if (leitura.cliqueVolumeDetectado) {
        if (campo == CampoEditorAlarme::REPETICAO) {
            if (alarmeEmEdicao.execucaoUnica) {
                alarmeEmEdicao.execucaoUnica = false;
                alarmeEmEdicao.diasSemana = 0b00111110;
                alarmeAlterado = true;
            }
            modoInterface = ModoInterface::DIAS_ALARME;
            apresentarEditorAlarme();
        } else if (campo == CampoEditorAlarme::ATIVO) {
            alarmeEmEdicao.ativo = !alarmeEmEdicao.ativo;
            alarmeAlterado = true;
            apresentarEditorAlarme();
        } else if (campo == CampoEditorAlarme::NOME) {
            prepararEdicaoNomeAlarme();
            modoInterface = ModoInterface::NOME_ALARME;
            apresentarEditorAlarme();
        } else if (campo == CampoEditorAlarme::EXCLUIR) {
            confirmarExclusaoAlarme = false;
            modoInterface =
                ModoInterface::CONFIRMAR_EXCLUSAO_ALARME;
            apresentarEditorAlarme();
        }
        return;
    }

    long passo = leitura.deslocamentoEncoderNavegacao;
    if (passo == 0) {
        return;
    }

    switch (campo) {
        case CampoEditorAlarme::HORA:
            alarmeEmEdicao.hora = static_cast<uint8_t>(
                (alarmeEmEdicao.hora + passo % 24 + 24) % 24
            );
            break;
        case CampoEditorAlarme::MINUTO:
            alarmeEmEdicao.minuto = static_cast<uint8_t>(
                (alarmeEmEdicao.minuto + passo % 60 + 60) % 60
            );
            break;
        case CampoEditorAlarme::REPETICAO:
        {
            static constexpr uint8_t PREDEFINICOES[] = {
                0b00111110,
                0b01000001,
                0b01111111
            };
            int atual = -1;
            if (!alarmeEmEdicao.execucaoUnica) {
                for (uint8_t indice = 0; indice < 3; indice++) {
                    if (
                        alarmeEmEdicao.diasSemana ==
                        PREDEFINICOES[indice]
                    ) {
                        atual = indice;
                        break;
                    }
                }
            }
            int novo = (atual + passo) % 4;
            if (novo < 0) {
                novo += 4;
            }
            alarmeEmEdicao.execucaoUnica = false;
            if (novo == 3) {
                if (alarmeEmEdicao.diasSemana == 0) {
                    alarmeEmEdicao.diasSemana = 0b00111110;
                }
                modoInterface = ModoInterface::DIAS_ALARME;
            } else {
                alarmeEmEdicao.diasSemana = PREDEFINICOES[novo];
            }
            break;
        }
        case CampoEditorAlarme::FONTE:
        {
            int atual = 0;
            if (
                alarmeEmEdicao.fonte ==
                FonteAlarmeConfiguravel::RADIO_WEB
            ) {
                atual = 1;
            } else if (
                alarmeEmEdicao.fonte ==
                FonteAlarmeConfiguravel::DFPLAYER
            ) {
                atual = 2;
            }
            int novo = (atual + passo) % 3;
            if (novo < 0) {
                novo += 3;
            }
            if (novo != 2) {
                encerrarPreviaAlarme();
            }
            alarmeEmEdicao.fonte =
                novo == 0
                    ? FonteAlarmeConfiguravel::RADIO_FM
                    : novo == 1
                        ? FonteAlarmeConfiguravel::RADIO_WEB
                        : FonteAlarmeConfiguravel::DFPLAYER;
            alarmeEmEdicao.radioId = 0;
            alarmeEmEdicao.usarUltimaEstacaoWeb = novo == 1;
            if (novo == 2) {
                CatalogoSonsDfPlayer catalogo =
                    obterCatalogoSonsDfPlayer();
                if (catalogo.disponivel && catalogo.quantidade > 0) {
                    // Ao escolher DFPlayer como nova fonte, a navegação dos
                    // sons sempre começa de forma previsível em 001.mp3 e a
                    // primeira prévia é disparada imediatamente.
                    alarmeEmEdicao.somDfPlayer = 1;
                }
                reproduzirPreviaArquivoAlarme();
            }
            break;
        }
        case CampoEditorAlarme::ARQUIVO_DFPLAYER:
        {
            CatalogoSonsDfPlayer catalogo =
                obterCatalogoSonsDfPlayer();
            if (!catalogo.disponivel || catalogo.quantidade == 0) {
                return;
            }
            long novo =
                (static_cast<long>(alarmeEmEdicao.somDfPlayer) - 1 +
                    passo) % catalogo.quantidade;
            if (novo < 0) {
                novo += catalogo.quantidade;
            }
            alarmeEmEdicao.somDfPlayer =
                static_cast<uint8_t>(novo + 1);
            reproduzirPreviaArquivoAlarme();
            break;
        }
        case CampoEditorAlarme::VOLUME:
            alarmeEmEdicao.volume = static_cast<uint8_t>(constrain(
                static_cast<long>(alarmeEmEdicao.volume) + passo,
                1L,
                static_cast<long>(VOLUME_MAXIMO)
            ));
            if (obterEstadoDfPlayer().uso == UsoDfPlayer::PREVIA) {
                definirVolumeDfPlayer(alarmeEmEdicao.volume);
            }
            break;
        case CampoEditorAlarme::DURACAO:
            alarmeEmEdicao.duracaoMinutos =
                static_cast<uint8_t>(constrain(
                    static_cast<long>(
                        alarmeEmEdicao.duracaoMinutos
                    ) + passo,
                    1L,
                    static_cast<long>(
                        DURACAO_MAXIMA_CONFIGURAVEL_ALARME_MINUTOS
                    )
                ));
            break;
        case CampoEditorAlarme::ATIVO:
            alarmeEmEdicao.ativo = passo > 0;
            break;
        case CampoEditorAlarme::NOME:
        case CampoEditorAlarme::EXCLUIR:
            return;
    }

    alarmeAlterado = true;
    apresentarEditorAlarme();
}

// =====================================================
// Estados do equipamento
// =====================================================

void entrarEstadoRelogio() {
    if (chimeEmExecucao) {
        finalizarChimeHorario();
    }
    finalizarPreviaChime();

    bool saindoDoRadioFm =
        estadoEquipamento == EstadoEquipamento::RADIO_FM;

    ativacaoRadioPendente = false;
    ativacaoRadioFmPendente = false;
    navegacaoFmDurantePowerDown = 0;
    momentoNavegacaoFmDurantePowerDownUs = 0;
    sequenciaNavegacaoFmDurantePowerDown = 0;
    estadoEquipamento = EstadoEquipamento::RELOGIO;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    configurarSaidaAudioStandby();

    // A troca visual acontece antes do desligamento dos serviços para que o
    // clique tenha resposta imediata no TFT.
    mostrarTelaRelogio();
    apagarIndicadorLed();

    if (saindoDoRadioFm) {
        desativarRadioFm();
    }

    if (!suspenderAudio()) {
        Serial.println(
            "Comando para suspender o audio nao foi aceito."
        );
    }

    desativarServidorWeb();
    desligarWifi();

    Serial.println("Estado do equipamento: Relogio.");
}

void entrarEstadoRadioWeb() {
    if (chimeEmExecucao) {
        finalizarChimeHorario();
    }
    finalizarPreviaChime();

    if (!sincronizarRadioAtualComCatalogo()) {
        mostrarMensagem("Sem radios");
        return;
    }

    bool saindoDoRadioFm =
        estadoEquipamento == EstadoEquipamento::RADIO_FM;

    mostrarMensagem("INICIANDO RÁDIO WEB...");
    ativacaoRadioPendente = true;
    ativacaoRadioFmPendente = false;
    navegacaoFmDurantePowerDown = 0;
    momentoNavegacaoFmDurantePowerDownUs = 0;
    sequenciaNavegacaoFmDurantePowerDown = 0;
    estadoEquipamento = EstadoEquipamento::RELOGIO;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    if (saindoDoRadioFm) {
        desativarRadioFm();
    }

    // Enquanto Wi-Fi e decoder são preparados, não deixa nenhuma fonte
    // conectada aos amplificadores.
    configurarSaidaAudioStandby();

    if (!wifiConectado()) {
        if (!iniciarReconexaoWifiSalvo()) {
            // Primeiro uso: o portal continua sendo o caminho de provisão. Em
            // boots seguintes as credenciais são carregadas em standby e a
            // conexão direta assíncrona evita a varredura inicial.
            conectarWifi();
        }
    }

    mostrarMensagem(
        "PREPARANDO ÁUDIO E CONECTANDO WI-FI..."
    );

    // A associação Wi-Fi continua em segundo plano enquanto decoder e I2S são
    // recriados. O tempo percebido passa a ser o maior dos dois, não a soma.
    if (!retomarAudio(volumeAtual)) {
        Serial.println("Falha ao preparar decoder para Radio Web.");
        ativacaoRadioPendente = false;
        estadoEquipamento = EstadoEquipamento::RELOGIO;
        configurarSaidaAudioStandby();
        desligarWifi();
        mostrarMensagem("Falha no audio");
        return;
    }

    if (!wifiConectado()) {
        mostrarMensagem(
            "ÁUDIO PRONTO · CONECTANDO WI-FI..."
        );
    }

    processarAtivacaoRadioWeb();
}

void entrarEstadoRadioFm() {
    if (chimeEmExecucao) {
        finalizarChimeHorario();
    }
    finalizarPreviaChime();

    // Primeira ação da transição: alimenta o TEF6686 por GPA4, antes de
    // qualquer acesso I2C. Em seguida, mantém os NS8002 em mute enquanto o
    // DAC é desligado e o receptor é inicializado.
    if (!configurarHabilitacaoRadioFm(true)) {
        Serial.println(
            "Radio FM bloqueado: GPA4=HIGH nao foi confirmado."
        );
        mostrarMensagem("Falha ao alimentar Radio FM");
        return;
    }
    configurarSaidaAudioStandby();

    ativacaoRadioPendente = false;
    ativacaoRadioFmPendente = false;
    navegacaoFmDurantePowerDown = 0;
    momentoNavegacaoFmDurantePowerDownUs = 0;
    sequenciaNavegacaoFmDurantePowerDown = 0;
    estadoEquipamento = EstadoEquipamento::RADIO_FM;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    // Primeiro responde ao clique no TFT; em seguida encerra os recursos da
    // fonte web e acorda o receptor dedicado.
    StatusRadioFm statusAtual = obterStatusRadioFm();
    DadosDisplayRadioFm dadosAtivacao;
    dadosAtivacao.frequencia10kHz = statusAtual.frequencia10kHz;
    dadosAtivacao.estado = "Ativando...";
    mostrarRadioFm(dadosAtivacao);

    desativarServidorWeb();

    const bool audioDesligado =
        suspenderAudio() && !decoderDacEstaAtivo();

    if (!audioDesligado) {
        Serial.println(
            "Radio FM bloqueado: decoder/DAC nao confirmou desligamento."
        );
        estadoEquipamento = EstadoEquipamento::RELOGIO;
        desativarRadioFm();
        desligarWifi();
        mostrarIndicadorRadioFmErro();
        mostrarMensagem("Falha ao desligar DAC");
        return;
    }

    // O módulo já está alimentado. Os comandos I2C de inicialização só serão
    // enviados depois que Audio/I2S forem destruídos e BCK/LRCK/DIN estiverem
    // em LOW.
    desligarWifi();
    inicioPowerDownDacMs = millis();
    ativacaoRadioFmPendente = true;
}

void processarAtivacaoRadioFm() {
    if (
        !ativacaoRadioFmPendente ||
        estadoEquipamento != EstadoEquipamento::RADIO_FM ||
        millis() - inicioPowerDownDacMs <
            TEMPO_POWER_DOWN_PCM5102A_MS
    ) {
        return;
    }

    ativacaoRadioFmPendente = false;
    const bool radioAtivado = ativarRadioFm(volumeAtual);
    bool saidaFmConfirmada = false;

    if (radioAtivado) {
        saidaFmConfirmada = configurarSaidaAudioFm(
            volumeAtual <= VOLUME_MINIMO
        );
        if (!saidaFmConfirmada) {
            Serial.println(
                "Radio FM bloqueado: NS8002 nao foram habilitados."
            );
            desativarRadioFm();
            configurarSaidaAudioStandby();
        }
    } else {
        configurarSaidaAudioStandby();
    }

    if (
        radioAtivado &&
        saidaFmConfirmada &&
        navegacaoFmDurantePowerDown != 0
    ) {
        navegarRadioFm(
            navegacaoFmDurantePowerDown,
            momentoNavegacaoFmDurantePowerDownUs,
            sequenciaNavegacaoFmDurantePowerDown
        );
    }

    navegacaoFmDurantePowerDown = 0;
    momentoNavegacaoFmDurantePowerDownUs = 0;
    sequenciaNavegacaoFmDurantePowerDown = 0;
    atualizarApresentacaoRadioFm(true);

    Serial.println(
        radioAtivado && saidaFmConfirmada
            ? "Estado do equipamento: Radio FM."
            : "Falha ao ativar Radio FM."
    );
}

void processarAtivacaoRadioWeb() {
    if (!ativacaoRadioPendente || !wifiConectado()) {
        return;
    }

    // O DAC já foi preparado pelo AudioService. Agora GPA5 troca as entradas
    // dos NS8002 de FM para DAC e GPA7/GPA6 habilitam os dois canais.
    const bool mutar = volumeAtual <= VOLUME_MINIMO;
    bool chaveamentoConfirmado = configurarSaidaAudioDac(mutar);

    if (!chaveamentoConfirmado) {
        Serial.println(
            "Radio Web: recuperando MCP23017 antes de selecionar o DAC."
        );
        chaveamentoConfirmado =
            iniciarChaveamentoAudio() &&
            configurarSaidaAudioDac(mutar);
    }

    if (!chaveamentoConfirmado) {
        Serial.println(
            "Radio Web bloqueado: GPA5=HIGH nao foi confirmado."
        );
        ativacaoRadioPendente = false;
        estadoEquipamento = EstadoEquipamento::RELOGIO;
        suspenderAudio();
        desligarWifi();
        mostrarMensagem("FALHA NO CHAVEAMENTO DE ÁUDIO");
        return;
    }

    reativarServidorWeb();

    estadoEquipamento = EstadoEquipamento::RADIO_WEB;
    ativacaoRadioPendente = false;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;
    configurarDetalhesRadioWeb(false);

    solicitarReproducaoRadio(indiceRadioAtual);

    Serial.println("Estado do equipamento: Radio Web.");
}

void entrarModoSelecaoRadio() {
    if (!sincronizarRadioAtualComCatalogo()) {
        mostrarMensagem("Sem radios");
        return;
    }

    modoInterface = ModoInterface::SELECAO_RADIO;
    configurarDetalhesRadioWeb(false);
    barraVolumeVisivel = false;
    momentoUltimaAtividadeSelecaoMs = millis();

    const Radio* radio = obterRadio(indiceRadioEmSelecao);

    if (radio != nullptr) {
        mostrarSelecaoRadio(
            radio->nome,
            indiceRadioEmSelecao,
            obterQuantidadeRadios()
        );
    }

    Serial.println("Modo: selecao de radio.");
}

void confirmarSelecaoRadio() {
    modoInterface = ModoInterface::REPOUSO;
    configurarDetalhesRadioWeb(false);
    barraVolumeVisivel = false;

    if (indiceRadioEmSelecao == indiceRadioAtual) {
        atualizarDisplayEstadoAudio(true);
    } else {
        solicitarReproducaoRadio(indiceRadioEmSelecao);
    }

    Serial.println("Radio confirmada; modo: repouso.");
}


// =====================================================
// Volume dedicado
// =====================================================

void processarAjusteVolume(
    long deslocamentoVolume
) {
    int volumeAnterior = alarmeEmExecucao
        ? static_cast<int>(alarmeAtual.volume)
        : volumeAtual;

    long novoVolume =
        volumeAnterior + deslocamentoVolume;

    novoVolume = constrain(
        novoVolume,
        static_cast<long>(VOLUME_MINIMO),
        static_cast<long>(VOLUME_MAXIMO)
    );

    if (novoVolume == volumeAnterior) {
        const bool manterJanelaVolume =
            !alarmeEmExecucao &&
            (
                estadoEquipamento == EstadoEquipamento::RADIO_WEB ||
                estadoEquipamento == EstadoEquipamento::RADIO_FM
            );
        if (manterJanelaVolume) {
            mostrarVolume(volumeAnterior);
            barraVolumeVisivel = true;
            momentoUltimaAlteracaoVolumeMs = millis();
        }
        return;
    }

    int volumeAjustado = static_cast<int>(novoVolume);

    if (alarmeEmExecucao) {
        // O cadastro persistido permanece intacto. A partir do primeiro
        // ajuste, o volume ouvido no alarme também se torna o volume geral
        // que será conservado depois da restauração.
        alarmeAtual.volume = static_cast<uint8_t>(volumeAjustado);
    }

    volumeAtual = volumeAjustado;

    if (alarmeEmExecucao && alarmeAtualUsaRadioFm()) {
        ajustarVolumeRadioFm(volumeAjustado);
    } else if (alarmeEmExecucao && alarmeAtualUsaDfPlayer()) {
        definirVolumeDfPlayer(volumeAjustado);
    } else if (
        alarmeEmExecucao ||
        estadoEquipamento == EstadoEquipamento::RADIO_WEB
    ) {
        alterarVolumeAudio(volumeAjustado);
    } else if (
        estadoEquipamento == EstadoEquipamento::RADIO_FM
    ) {
        ajustarVolumeRadioFm(volumeAjustado);
    }

    const bool muteMudou =
        (volumeAnterior <= VOLUME_MINIMO) !=
        (volumeAjustado <= VOLUME_MINIMO);
    if (muteMudou) {
        const bool mutar = volumeAjustado <= VOLUME_MINIMO;

        if (alarmeEmExecucao && alarmeAtualUsaRadioFm()) {
            if (faseAlarme == FaseAlarme::TOCANDO) {
                configurarSaidaAudioFm(mutar);
            } else {
                configurarSaidaAudioStandby();
            }
        } else if (
            alarmeEmExecucao && alarmeAtualUsaDfPlayer()
        ) {
            definirVolumeDfPlayer(volumeAjustado);
        } else if (
            alarmeEmExecucao ||
            estadoEquipamento == EstadoEquipamento::RADIO_WEB
        ) {
            configurarSaidaAudioDac(mutar);
        } else if (estadoEquipamento == EstadoEquipamento::RADIO_FM) {
            configurarSaidaAudioFm(mutar);
        }
    }

    if (alarmeEmExecucao) {
        mostrarAlarme(
            alarmeAtual.nome,
            volumeAjustado
        );

        Serial.printf(
            "Volume do alarme e geral: %d\n",
            volumeAjustado
        );
        return;
    }

    const bool exibirJanelaVolume =
        estadoEquipamento == EstadoEquipamento::RADIO_WEB ||
        estadoEquipamento == EstadoEquipamento::RADIO_FM;

    // Mantém o modelo do display sincronizado mesmo no relógio, mas a janela
    // em primeiro plano existe somente nas duas telas de rádio.
    mostrarVolume(volumeAjustado);

    if (exibirJanelaVolume) {
        barraVolumeVisivel = true;
        momentoUltimaAlteracaoVolumeMs = millis();
    }

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        Serial.printf(
            "Volume para a proxima fonte: %d\n",
            volumeAjustado
        );
    } else {
        Serial.printf("Volume: %d\n", volumeAjustado);
    }
}

// =====================================================
// Modo seleção de rádio
// =====================================================

void processarNavegacaoRadios(
    long deslocamentoEncoder
) {
    int quantidadeRadios =
        obterQuantidadeRadios();

    if (quantidadeRadios <= 0) {
        return;
    }

    long novoIndice =
        indiceRadioEmSelecao + deslocamentoEncoder;

    // O resto preserva todos os passos e mantém a navegação circular.
    novoIndice %= quantidadeRadios;

    if (novoIndice < 0) {
        novoIndice += quantidadeRadios;
    }

    indiceRadioEmSelecao =
        static_cast<int>(novoIndice);

    momentoUltimaAtividadeSelecaoMs = millis();
    barraVolumeVisivel = false;

    Serial.print(
        "Selecionada: "
    );

    const Radio* radio =
        obterRadio(
            indiceRadioEmSelecao
        );

    if (radio != nullptr) {
        mostrarSelecaoRadio(
            radio->nome,
            indiceRadioEmSelecao,
            quantidadeRadios
        );

        Serial.println(
            radio->nome
        );
    }
}

// =====================================================
// Retorno automático da navegação para o repouso
// =====================================================

void cancelarSelecaoRadioPorInatividade() {
    if (
        modoInterface !=
            ModoInterface::SELECAO_RADIO
    ) {
        return;
    }

    if (
        millis() - momentoUltimaAtividadeSelecaoMs <
            TEMPO_INATIVIDADE_SELECAO_MS
    ) {
        return;
    }

    modoInterface = ModoInterface::REPOUSO;
    indiceRadioEmSelecao = indiceRadioAtual;
    barraVolumeVisivel = false;

    Serial.println(
        "Seleção cancelada por inatividade; modo: repouso"
    );

    atualizarDisplayEstadoAudio(true);
}


void restaurarBarraAposTempoVolume() {
    if (!barraVolumeVisivel) {
        return;
    }

    if (
        millis() - momentoUltimaAlteracaoVolumeMs <
        TEMPO_BARRA_VOLUME_MS
    ) {
        return;
    }

    barraVolumeVisivel = false;

    if (estadoEquipamento == EstadoEquipamento::RADIO_WEB) {
        if (modoInterface == ModoInterface::SELECAO_RADIO) {
            const Radio* radio =
                obterRadio(indiceRadioEmSelecao);

            if (radio != nullptr) {
                mostrarSelecaoRadio(
                    radio->nome,
                    indiceRadioEmSelecao,
                    obterQuantidadeRadios()
                );
            }
        } else {
            atualizarDisplayEstadoAudio(true);
        }
    } else if (estadoEquipamento == EstadoEquipamento::RADIO_FM) {
        atualizarApresentacaoRadioFm(true);
    }
}

// =====================================================
// Rádio
// =====================================================

bool sincronizarRadioAtualComCatalogo() {
    int quantidadeRadios = obterQuantidadeRadios();

    if (quantidadeRadios <= 0) {
        indiceRadioAtual = 0;
        indiceRadioEmSelecao = 0;
        idRadioAtual = 0;
        return false;
    }

    int indicePeloId = obterIndiceRadioPorId(idRadioAtual);

    if (indicePeloId >= 0) {
        indiceRadioAtual = indicePeloId;
    } else {
        indiceRadioAtual = 0;
        idRadioAtual = obterRadio(0)->id;

        Serial.println(
            "Radio atual removida; selecionando a primeira do catalogo."
        );
    }

    indiceRadioEmSelecao = indiceRadioAtual;
    return true;
}

void solicitarReproducaoRadio(int indiceRadio) {
    const Radio* radio =
        obterRadio(indiceRadio);

    if (radio == nullptr) {
        mostrarMensagem(
            "Radio invalida"
        );

        return;
    }

    bool comandoAceito =
        tocarRadio(
            radio->nome,
            radio->url
        );

    if (!comandoAceito) {
        mostrarMensagem(
            "Comando rejeitado"
        );

        mostrarRadioNoDisplay(
            indiceRadioAtual
        );

        return;
    }

    indiceRadioAtual = indiceRadio;
    indiceRadioEmSelecao = indiceRadio;
    idRadioAtual = radio->id;

    mostrarMensagem("WI-FI PRONTO · ABRINDO ESTAÇÃO...");
}

void mostrarRadioNoDisplay(int indiceRadio) {
    const Radio* radio =
        obterRadio(indiceRadio);

    if (radio == nullptr) {
        return;
    }

    mostrarNomeRadio(
        radio->nome,
        indiceRadio,
        obterQuantidadeRadios()
    );
}

// =====================================================
// Apresentação do estado do áudio no display
// =====================================================

void atualizarDisplayEstadoAudio(
    bool forcarAtualizacao
) {
    static EstadoAudio ultimoEstadoApresentado =
        EstadoAudio::DESLIGADO;

    if (estadoEquipamento != EstadoEquipamento::RADIO_WEB) {
        return;
    }

    StatusAudio statusAudio =
        obterStatusAudio();

    if (
        !forcarAtualizacao &&
        statusAudio.estado == ultimoEstadoApresentado
    ) {
        return;
    }

    ultimoEstadoApresentado = statusAudio.estado;

    Serial.print("Estado do audio: ");
    Serial.println(
        obterTextoEstadoAudio(
            statusAudio.estado
        )
    );

    if (
        modoInterface != ModoInterface::REPOUSO ||
        barraVolumeVisivel
    ) {
        return;
    }

    switch (statusAudio.estado) {
        case EstadoAudio::CONECTANDO:
            mostrarMensagem("CONECTANDO À ESTAÇÃO...");
            break;

        case EstadoAudio::BUFFERIZANDO:
            mostrarMensagem("BUFFERIZANDO ÁUDIO...");
            break;

        case EstadoAudio::TOCANDO:
        case EstadoAudio::DEGRADADO:
            mostrarRadioNoDisplay(indiceRadioAtual);
            break;

        case EstadoAudio::RECONECTANDO:
            mostrarMensagem("RECONECTANDO À ESTAÇÃO...");
            break;

        case EstadoAudio::ERRO:
            mostrarMensagem("FALHA AO ABRIR A ESTAÇÃO");
            break;

        default:
            break;
    }
}

void atualizarApresentacaoRadioFm(
    bool forcarAtualizacao
) {
    static uint32_t ultimaRevisaoApresentada = 0;
    static EstadoRadioFm ultimoEstadoIndicador =
        EstadoRadioFm::INDISPONIVEL;

    if (
        estadoEquipamento != EstadoEquipamento::RADIO_FM ||
        (
            modoInterface != ModoInterface::REPOUSO &&
            modoInterface != ModoInterface::DETALHES_RADIO_FM
        )
    ) {
        return;
    }

    StatusRadioFm status = obterStatusRadioFm();
    bool estadoIndicadorMudou =
        status.estado != ultimoEstadoIndicador;

    if (forcarAtualizacao || estadoIndicadorMudou) {
        ultimoEstadoIndicador = status.estado;

        switch (status.estado) {
            case EstadoRadioFm::ATIVO:
                configurarSaidaAudioFm(
                    volumeAtual <= VOLUME_MINIMO
                );
                mostrarIndicadorRadioFmAtivo();
                break;

            case EstadoRadioFm::BUSCANDO:
                configurarSaidaAudioFm(true);
                mostrarIndicadorRadioFmBuscando();
                break;

            case EstadoRadioFm::INDISPONIVEL:
            case EstadoRadioFm::ERRO:
                configurarSaidaAudioFm(true);
                mostrarIndicadorRadioFmErro();
                break;

            case EstadoRadioFm::REPOUSO:
                configurarSaidaAudioStandby();
                apagarIndicadorLed();
                break;
        }
    }

    if (
        !forcarAtualizacao &&
        status.revisao == ultimaRevisaoApresentada
    ) {
        return;
    }

    ultimaRevisaoApresentada = status.revisao;

    if (barraVolumeVisivel && !forcarAtualizacao) {
        return;
    }

    String textoEstado;

    switch (status.estado) {
        case EstadoRadioFm::ATIVO:
            break;

        case EstadoRadioFm::BUSCANDO:
            textoEstado = "Buscando...";
            break;

        case EstadoRadioFm::INDISPONIVEL:
            textoEstado = "TEF indisponivel";
            break;

        case EstadoRadioFm::ERRO:
            textoEstado = status.ultimoErro[0] != '\0'
                ? status.ultimoErro
                : "Erro no radio";
            break;

        case EstadoRadioFm::REPOUSO:
            textoEstado = "Repouso";
            break;
    }

    DadosDisplayRadioFm dados;
    dados.frequencia10kHz = status.frequencia10kHz;
    dados.nomeEstacao = status.nomeEstacao;
    dados.radioTexto = status.radioTexto;
    dados.estado = textoEstado.c_str();
    dados.nomeEstacaoValido = status.nomeEstacaoValido;
    dados.radioTextoValido = status.radioTextoValido;
    dados.buscando = status.estado == EstadoRadioFm::BUSCANDO;
    dados.buscaConcluida =
        status.estado == EstadoRadioFm::ATIVO &&
        status.sequenciaEventoEncoder != 0;
    dados.qualidadeValida = status.qualidadeValida;
    dados.nivelDecimosDbuv = status.nivelDecimosDbuv;
    dados.larguraBandaValida = status.larguraBandaValida;
    dados.larguraBandaDecimosKHz = status.larguraBandaDecimosKHz;
    dados.pilotoEstereo = status.pilotoEstereo;
    dados.pilotoEstereoValido = status.pilotoEstereoValido;
    dados.detalhesTecnicos =
        modoInterface == ModoInterface::DETALHES_RADIO_FM;
    dados.sequenciaEventoEncoder = status.sequenciaEventoEncoder;
    dados.momentoEventoEncoderUs = status.momentoEventoEncoderUs;
    mostrarRadioFm(dados);
}
