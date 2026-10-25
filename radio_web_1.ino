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

#include "configuracao.h"
#include "alarmes.h"
#include "radios.h"
#include "display_radio.h"
#include "wifi_radio.h"
#include "audio_radio.h"
#include "controles.h"
#include "indicador_led.h"
#include "relogio.h"
#include "servidor_web.h"
#include "telemetria.h"
#include "radio_fm.h"
#include "chaveamento_audio.h"
#include "dfplayer.h"
#include "chime.h"

enum class EstadoEquipamento {
    RADIO_WEB,
    RADIO_FM,
    RELOGIO
};

enum class ModoInterface {
    REPOUSO,
    SELECAO_RADIO,
    SELECAO_FONTE
};

enum class FaseAlarme {
    INATIVO,
    AGUARDANDO_PARADA_RADIO,
    AGUARDANDO_WIFI_RADIO,
    AGUARDANDO_POWER_DOWN_DAC_FM,
    TOCANDO
};

// Estado da navegação comandada pelo encoder de rádios.
ModoInterface modoInterface = ModoInterface::REPOUSO;
EstadoEquipamento estadoEquipamento =
    EstadoEquipamento::RELOGIO;
EstadoEquipamento fonteEmSelecao =
    EstadoEquipamento::RELOGIO;

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

    iniciarDisplay();
    iniciarControles();

    // Antes de GPA7/GPA6/GPA5 virarem saídas, o latch recebe o estado seguro:
    // relé em FM e enables ativos em LOW mantidos em HIGH (mute).
    iniciarChaveamentoAudio();

    // O DFPlayer prepara somente a UART e o catálogo numérico. Seu DAC e os
    // NS8002 permanecem em mute até um futuro alarme, chime ou prévia.
    if (!iniciarDfPlayer()) {
        Serial.println(
            "DFPlayer indisponivel; demais fontes continuam operacionais."
        );
    }

    // O RTC fornece hora imediatamente; o NTP o corrige quando a rede entrar.
    iniciarRelogio();

    // O TEF compartilha somente o barramento I2C. Ele começa em standby e uma
    // eventual ausência do módulo não impede as demais funções do aparelho.
    iniciarRadioFm(volumeAtual);

    prepararWifiEmStandby();

    if (!iniciarArmazenamentoEServidorWeb(false)) {
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

    if (!iniciarAudio(volumeAtual, true)) {
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

    if (alarmeEmExecucao) {
        if (leituraControles.deslocamentoEncoderVolume != 0) {
            processarAjusteVolume(
                leituraControles.deslocamentoEncoderVolume
            );
        }

        if (leituraControles.cliqueNavegacaoDetectado) {
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
        supervisionarWifi();
        processarServidorWeb();
        cancelarSelecaoRadioPorInatividade();
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

        if (!decoderDacEstaAtivo()) {
            bool usarSomPadrao =
                disparo.radioId == 0 ||
                obterRadioPorId(disparo.radioId) == nullptr;

            if (!retomarAudio(disparo.volume, usarSomPadrao)) {
                Serial.println(
                    "Alarme: falha ao retomar o servico de audio."
                );
            }
        }

        configurarSaidaAudioDac(
            disparo.volume <= VOLUME_MINIMO
        );
    }

    alarmeAtual = disparo;
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

    configurarSaidaAudioFm(
        alarmeAtual.volume <= VOLUME_MINIMO
    );
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

        configurarSaidaAudioStandby();

        if (
            statusFm.estado == EstadoRadioFm::ATIVO ||
            statusFm.estado == EstadoRadioFm::BUSCANDO
        ) {
            desativarRadioFm();
        }

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
        DURACAO_MAXIMA_ALARME_MINUTOS *
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

            if (wifiLigado()) {
                desligarWifi();
            }

            suspenderAudio();
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

    if (leitura.cliqueNavegacaoDetectado) {
        if (modoInterface == ModoInterface::REPOUSO) {
            entrarModoSelecaoRadio();
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

const char* obterNomeFonte(EstadoEquipamento fonte) {
    switch (fonte) {
        case EstadoEquipamento::RELOGIO:
            return "Relogio";
        case EstadoEquipamento::RADIO_FM:
            return "Radio FM";
        case EstadoEquipamento::RADIO_WEB:
            return "Radio Web";
    }

    return "Fonte";
}

void apresentarFonteEmSelecao() {
    mostrarMensagem(
        String("Fonte: ") + obterNomeFonte(fonteEmSelecao)
    );
}

void entrarModoSelecaoFonte(long deslocamentoInicial) {
    if (ativacaoRadioPendente) {
        // Interrompe uma ativação ainda não confirmada antes de apresentar
        // outra escolha. Assim nenhuma fonte liga por trás da tela de seleção.
        entrarEstadoRelogio();
    }

    modoInterface = ModoInterface::SELECAO_FONTE;
    fonteEmSelecao = estadoEquipamento;
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

    constexpr EstadoEquipamento ORDEM_FONTES[] = {
        EstadoEquipamento::RELOGIO,
        EstadoEquipamento::RADIO_FM,
        EstadoEquipamento::RADIO_WEB
    };
    constexpr long QUANTIDADE_FONTES =
        sizeof(ORDEM_FONTES) / sizeof(ORDEM_FONTES[0]);

    long indiceAtual = 0;
    for (long indice = 0; indice < QUANTIDADE_FONTES; ++indice) {
        if (ORDEM_FONTES[indice] == fonteEmSelecao) {
            indiceAtual = indice;
            break;
        }
    }

    long novoIndice =
        (indiceAtual + deslocamento) % QUANTIDADE_FONTES;
    if (novoIndice < 0) {
        novoIndice += QUANTIDADE_FONTES;
    }

    fonteEmSelecao = ORDEM_FONTES[novoIndice];
    momentoUltimaAtividadeSelecaoMs = millis();
    apresentarFonteEmSelecao();

    Serial.print("Fonte selecionada: ");
    Serial.println(obterNomeFonte(fonteEmSelecao));
}

void confirmarSelecaoFonte() {
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    if (
        fonteEmSelecao == estadoEquipamento &&
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

    switch (fonteEmSelecao) {
        case EstadoEquipamento::RELOGIO:
            entrarEstadoRelogio();
            break;
        case EstadoEquipamento::RADIO_FM:
            entrarEstadoRadioFm();
            break;
        case EstadoEquipamento::RADIO_WEB:
            entrarEstadoRadioWeb();
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
    fonteEmSelecao = estadoEquipamento;

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
    // clique tenha resposta imediata no OLED.
    mostrarTelaRelogio();
    apagarIndicadorLed();

    if (saindoDoRadioFm) {
        desativarRadioFm();
    }

    desativarServidorWeb();

    if (!suspenderAudio()) {
        Serial.println(
            "Comando para suspender o audio nao foi aceito."
        );
    }

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

    mostrarMensagem("Ativando Radio Web");
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

    processarAtivacaoRadioWeb();
}

void entrarEstadoRadioFm() {
    if (chimeEmExecucao) {
        finalizarChimeHorario();
    }
    finalizarPreviaChime();

    // Silencia os NS8002 durante a troca. Depois do power-down do DAC, GPA7
    // selecionará FM e GPA6/GPA5 apenas habilitarão os amplificadores.
    configurarSaidaAudioStandby();

    ativacaoRadioPendente = false;
    ativacaoRadioFmPendente = false;
    navegacaoFmDurantePowerDown = 0;
    momentoNavegacaoFmDurantePowerDownUs = 0;
    sequenciaNavegacaoFmDurantePowerDown = 0;
    estadoEquipamento = EstadoEquipamento::RADIO_FM;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    // Primeiro responde ao clique no OLED; em seguida encerra os recursos da
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
        desligarWifi();
        mostrarIndicadorRadioFmErro();
        mostrarMensagem("Falha ao desligar DAC");
        return;
    }

    // Invariante de prioridade: o TEF6686 só é ativado depois que o objeto
    // Audio foi destruído, o canal I2S removido e BCK/LRCK/DIN ficaram em LOW.
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

    if (radioAtivado) {
        configurarSaidaAudioFm(volumeAtual <= VOLUME_MINIMO);
    } else {
        configurarSaidaAudioStandby();
    }

    if (radioAtivado && navegacaoFmDurantePowerDown != 0) {
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

    Serial.println("Estado do equipamento: Radio FM.");
}

void processarAtivacaoRadioWeb() {
    if (!ativacaoRadioPendente || !wifiConectado()) {
        return;
    }

    // O DAC já foi preparado pelo AudioService. Agora GPA7 troca as entradas
    // dos NS8002 de FM para DAC e GPA6/GPA5 habilitam os dois canais.
    configurarSaidaAudioDac(volumeAtual <= VOLUME_MINIMO);

    reativarServidorWeb();

    estadoEquipamento = EstadoEquipamento::RADIO_WEB;
    ativacaoRadioPendente = false;
    modoInterface = ModoInterface::REPOUSO;
    barraVolumeVisivel = false;

    solicitarReproducaoRadio(indiceRadioAtual);

    Serial.println("Estado do equipamento: Radio Web.");
}

void entrarModoSelecaoRadio() {
    if (!sincronizarRadioAtualComCatalogo()) {
        mostrarMensagem("Sem radios");
        return;
    }

    modoInterface = ModoInterface::SELECAO_RADIO;
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

    mostrarVolume(volumeAjustado);

    barraVolumeVisivel = true;

    momentoUltimaAlteracaoVolumeMs =
        millis();

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
    } else if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        mostrarTelaRelogio();
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

    mostrarEstadoRadio(
        "Conectando...",
        indiceRadioAtual,
        obterQuantidadeRadios()
    );
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
            mostrarEstadoRadio(
                "Conectando...",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
            break;

        case EstadoAudio::BUFFERIZANDO:
            mostrarEstadoRadio(
                "Bufferizando...",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
            break;

        case EstadoAudio::TOCANDO:
        case EstadoAudio::DEGRADADO:
            mostrarRadioNoDisplay(indiceRadioAtual);
            break;

        case EstadoAudio::RECONECTANDO:
            mostrarEstadoRadio(
                "Reconectando...",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
            break;

        case EstadoAudio::ERRO:
            mostrarEstadoRadio(
                "Erro no audio",
                indiceRadioAtual,
                obterQuantidadeRadios()
            );
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
        modoInterface == ModoInterface::SELECAO_FONTE
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
    dados.sequenciaEventoEncoder = status.sequenciaEventoEncoder;
    dados.momentoEventoEncoderUs = status.momentoEventoEncoderUs;
    mostrarRadioFm(dados);
}
