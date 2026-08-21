/*
 * Arquivo principal e mapa de execução do firmware.
 *
 * O setup() inicializa, nesta ordem: interface física, relógio, Wi-Fi,
 * armazenamento/servidor, lista de estações e serviço de áudio.
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
#include "player.h"
#include "relogio.h"
#include "servidor_web.h"
#include "telemetria.h"

enum class EstadoEquipamento {
    RADIO_WEB,
    PLAYER,
    RELOGIO
};

enum class ModoInterface {
    VOLUME,
    SELECAO_RADIO,
    SELECAO_ARQUIVO
};

enum class FaseAlarme {
    INATIVO,
    AGUARDANDO_PARADA_RADIO,
    TOCANDO
};

constexpr EstadoEquipamento ORDEM_SELETOR_ESTADOS[] = {
    EstadoEquipamento::RELOGIO,
    EstadoEquipamento::PLAYER,
    EstadoEquipamento::RADIO_WEB
};

// Estado da interação com o encoder e o display.
ModoInterface modoInterface = ModoInterface::VOLUME;
EstadoEquipamento estadoEquipamento =
    EstadoEquipamento::RADIO_WEB;

int volumeAtual = VOLUME_PADRAO;

int indiceRadioAtual = 0;
int indiceRadioEmSelecao = 0;
int indiceArquivoAtual = 0;
int indiceArquivoEmSelecao = 0;
int indiceEstadoSelecionado = 0;

unsigned long momentoUltimaAlteracaoVolumeMs = 0;
unsigned long momentoUltimaAtividadeSelecaoMs = 0;

bool barraVolumeVisivel = false;
bool reproducaoPlayerSolicitada = false;
bool playerObservouAudioAtivo = false;

bool alarmeEmExecucao = false;
bool alarmeObservouAudioAtivo = false;
bool wifiDesligadoPeloAlarme = false;
FaseAlarme faseAlarme = FaseAlarme::INATIVO;
EstadoEquipamento estadoAntesDoAlarme =
    EstadoEquipamento::RADIO_WEB;
DisparoAlarme alarmeAtual;
unsigned long inicioAlarmeAtualMs = 0;
unsigned long proximaTentativaAudioAlarmeMs = 0;

// =====================================================
// Protótipos
// =====================================================

void mostrarRadioNoDisplay(int indiceRadio);
void solicitarReproducaoRadio(int indiceRadio);

void entrarModoSelecaoRadio();
void confirmarSelecaoRadio();

void entrarEstadoRelogio();
void entrarEstadoRadioWeb();
void entrarEstadoPlayer();

void processarNavegacaoEstados(long deslocamentoEncoder);
void confirmarEstadoSelecionado();

void processarLeituraControles(
    const LeituraControles& leitura
);

void processarAjusteVolume(
    long deslocamentoEncoder
);

void processarNavegacaoRadios(
    long deslocamentoEncoder
);

void entrarModoSelecaoArquivo();
void processarNavegacaoArquivos(long deslocamentoEncoder);
void confirmarSelecaoArquivo();
bool iniciarReproducaoArquivoPlayer(int indice, bool automatica);
void processarReproducaoSequencialPlayer();
void mostrarArquivoAtualPlayer(bool emSelecao = false);
void cancelarSelecaoArquivoPorInatividade();

void restaurarBarraAposTempoVolume();
void cancelarSelecaoRadioPorInatividade();

void atualizarDisplayEstadoAudio(
    bool forcarAtualizacao = false
);

void processarAgendamentoAlarmes();
void iniciarExecucaoAlarme(const DisparoAlarme& disparo);
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

    // O RTC fornece hora imediatamente; o NTP o corrige quando a rede entrar.
    iniciarRelogio();

    conectarWifi();

    if (!iniciarArmazenamentoEServidorWeb()) {
        mostrarMensagem(
            "Erro no armazenamento"
        );

        return;
    }

    // O catálogo é criado antes do áudio começar. Assim, a página de alarmes
    // consulta somente a RAM e nunca varre o cartão durante a rádio web.
    mostrarMensagem("Lendo cartao");
    prepararPlayer();
    iniciarAlarmes();

    carregarRadios();

    if (!iniciarAudio(volumeAtual)) {
        mostrarMensagem(
            "Erro no audio"
        );

        return;
    }

    indiceRadioAtual = 0;
    indiceRadioEmSelecao = indiceRadioAtual;

    solicitarReproducaoRadio(indiceRadioAtual);
}

// =====================================================
// Loop
// =====================================================

void loop() {
    processarRelogio();
    processarAgendamentoAlarmes();
    processarDisplay();

    LeituraControles leituraControles =
        lerControles();

    if (alarmeEmExecucao) {
        if (leituraControles.cliqueDetectado) {
            finalizarExecucaoAlarme(true);
        } else {
            processarExecucaoAlarme();
        }

        atualizarIndicadorEstadoAudio();
        registrarTelemetriaPeriodica();
        return;
    }

    processarLeituraControles(leituraControles);

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RADIO_WEB) {
        supervisionarWifi();
        processarServidorWeb();
        cancelarSelecaoRadioPorInatividade();
    } else {
        cancelarSelecaoArquivoPorInatividade();
        processarReproducaoSequencialPlayer();
    }

    restaurarBarraAposTempoVolume();

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

void iniciarExecucaoAlarme(const DisparoAlarme& disparo) {
    bool iniciandoCadeia = !alarmeEmExecucao;

    if (iniciandoCadeia) {
        estadoAntesDoAlarme = estadoEquipamento;
    } else {
        Serial.printf(
            "Alarme %lu substituiu o alarme %lu.\n",
            static_cast<unsigned long>(disparo.id),
            static_cast<unsigned long>(alarmeAtual.id)
        );
    }

    alarmeAtual = disparo;
    alarmeEmExecucao = true;
    alarmeObservouAudioAtivo = false;
    inicioAlarmeAtualMs = millis();
    proximaTentativaAudioAlarmeMs = 0;

    if (iniciandoCadeia) {
        wifiDesligadoPeloAlarme = false;

        if (estadoAntesDoAlarme == EstadoEquipamento::RADIO_WEB) {
            // A rádio deve terminar antes de desligarmos sua rede. O MP3 só
            // começa depois que o serviço de áudio confirmar PARADO.
            desativarServidorWeb();
            faseAlarme = FaseAlarme::AGUARDANDO_PARADA_RADIO;

            if (!pararAudio()) {
                proximaTentativaAudioAlarmeMs = millis() + 200;
                Serial.println(
                    "Alarme: comando para parar a radio rejeitado."
                );
            }
        } else {
            faseAlarme = FaseAlarme::TOCANDO;

            if (estadoAntesDoAlarme == EstadoEquipamento::RELOGIO) {
                if (!retomarAudio(disparo.volume, true)) {
                    Serial.println(
                        "Alarme: falha ao retomar o servico de audio."
                    );
                }
            }
        }
    }

    modoInterface = ModoInterface::VOLUME;
    barraVolumeVisivel = false;
    reproducaoPlayerSolicitada = false;
    playerObservouAudioAtivo = false;

    mostrarAlarme(alarmeAtual.nome);

    if (faseAlarme == FaseAlarme::TOCANDO) {
        iniciarCicloAudioAlarme();
    }

    Serial.printf(
        "Alarme iniciado: %s (ID %lu).\n",
        alarmeAtual.nome.c_str(),
        static_cast<unsigned long>(alarmeAtual.id)
    );
}

void iniciarCicloAudioAlarme() {
    alarmeObservouAudioAtivo = false;

    if (
        !tocarArquivoAlarme(
            alarmeAtual.arquivo,
            alarmeAtual.volume
        )
    ) {
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

    if (faseAlarme == FaseAlarme::AGUARDANDO_PARADA_RADIO) {
        if (statusAudio.estado == EstadoAudio::PARADO) {
            desligarWifi();
            wifiDesligadoPeloAlarme = true;
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

    Serial.printf(
        "Alarme finalizado: %s (%s).\n",
        alarmeAtual.nome.c_str(),
        interrompidoPeloUsuario
            ? "clique no encoder"
            : "limite de tempo"
    );

    alarmeEmExecucao = false;
    alarmeObservouAudioAtivo = false;
    faseAlarme = FaseAlarme::INATIVO;
    alarmeAtual = DisparoAlarme{};

    pararAudio();
    alterarVolumeAudio(volumeAtual);

    estadoEquipamento = estadoAntesDoAlarme;
    modoInterface = ModoInterface::VOLUME;
    barraVolumeVisivel = false;

    switch (estadoAntesDoAlarme) {
        case EstadoEquipamento::RADIO_WEB:
            if (wifiDesligadoPeloAlarme) {
                conectarWifi();
                wifiDesligadoPeloAlarme = false;
            }

            reativarServidorWeb();
            solicitarReproducaoRadio(indiceRadioAtual);
            break;

        case EstadoEquipamento::PLAYER:
            mostrarArquivoAtualPlayer();
            break;

        case EstadoEquipamento::RELOGIO:
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
    if (leitura.cliqueLongoDetectado) {
        if (estadoEquipamento != EstadoEquipamento::RELOGIO) {
            entrarEstadoRelogio();
        }

        return;
    }

    if (estadoEquipamento == EstadoEquipamento::RELOGIO) {
        if (leitura.cliqueDetectado) {
            confirmarEstadoSelecionado();
        } else if (leitura.deslocamentoEncoder != 0) {
            processarNavegacaoEstados(leitura.deslocamentoEncoder);
        }

        return;
    }

    if (leitura.cliqueDetectado) {
        if (modoInterface == ModoInterface::VOLUME) {
            if (estadoEquipamento == EstadoEquipamento::RADIO_WEB) {
                entrarModoSelecaoRadio();
            } else {
                entrarModoSelecaoArquivo();
            }
        } else if (modoInterface == ModoInterface::SELECAO_RADIO) {
            confirmarSelecaoRadio();
        } else {
            confirmarSelecaoArquivo();
        }

        return;
    }

    if (leitura.deslocamentoEncoder == 0) {
        return;
    }

    if (modoInterface == ModoInterface::VOLUME) {
        processarAjusteVolume(
            leitura.deslocamentoEncoder
        );
    } else if (modoInterface == ModoInterface::SELECAO_RADIO) {
        processarNavegacaoRadios(
            leitura.deslocamentoEncoder
        );
    } else {
        processarNavegacaoArquivos(
            leitura.deslocamentoEncoder
        );
    }
}

// =====================================================
// Estados do equipamento
// =====================================================

void entrarEstadoRelogio() {
    estadoEquipamento = EstadoEquipamento::RELOGIO;
    indiceEstadoSelecionado = 0;
    modoInterface = ModoInterface::VOLUME;
    barraVolumeVisivel = false;
    reproducaoPlayerSolicitada = false;
    playerObservouAudioAtivo = false;

    // A troca visual acontece antes do desligamento dos serviços para que o
    // clique tenha resposta imediata no OLED.
    mostrarTelaRelogio();
    apagarIndicadorLed();
    desativarServidorWeb();

    if (!suspenderAudio()) {
        Serial.println(
            "Comando para suspender o audio nao foi aceito."
        );
    }

    desligarWifi();

    Serial.println("Estado do equipamento: Relogio.");
}

void processarNavegacaoEstados(long deslocamentoEncoder) {
    constexpr int QUANTIDADE_ESTADOS =
        sizeof(ORDEM_SELETOR_ESTADOS) /
        sizeof(ORDEM_SELETOR_ESTADOS[0]);

    long novoIndice =
        indiceEstadoSelecionado + deslocamentoEncoder;
    novoIndice %= QUANTIDADE_ESTADOS;

    if (novoIndice < 0) {
        novoIndice += QUANTIDADE_ESTADOS;
    }

    indiceEstadoSelecionado = static_cast<int>(novoIndice);

    switch (ORDEM_SELETOR_ESTADOS[indiceEstadoSelecionado]) {
        case EstadoEquipamento::RELOGIO:
            mostrarTelaRelogio();
            Serial.println("Seletor: Relogio.");
            break;

        case EstadoEquipamento::PLAYER:
            mostrarOpcaoEstado("PLAYER");
            Serial.println("Seletor: Player.");
            break;

        case EstadoEquipamento::RADIO_WEB:
            mostrarOpcaoEstado("RADIO WEB");
            Serial.println("Seletor: Radio Web.");
            break;
    }
}

void confirmarEstadoSelecionado() {
    switch (ORDEM_SELETOR_ESTADOS[indiceEstadoSelecionado]) {
        case EstadoEquipamento::RELOGIO:
            mostrarTelaRelogio();
            break;

        case EstadoEquipamento::PLAYER:
            entrarEstadoPlayer();
            break;

        case EstadoEquipamento::RADIO_WEB:
            entrarEstadoRadioWeb();
            break;
    }
}

void entrarEstadoRadioWeb() {
    mostrarMensagem("Ativando Radio");

    conectarWifi();
    reativarServidorWeb();

    if (!retomarAudio(volumeAtual)) {
        Serial.println("Falha ao retomar o servico de audio.");
        desativarServidorWeb();
        desligarWifi();
        mostrarTelaRelogio();
        return;
    }

    estadoEquipamento = EstadoEquipamento::RADIO_WEB;
    modoInterface = ModoInterface::VOLUME;
    barraVolumeVisivel = false;

    solicitarReproducaoRadio(indiceRadioAtual);

    Serial.println("Estado do equipamento: Radio Web.");
}

void entrarEstadoPlayer() {
    mostrarMensagem("Lendo cartao");

    bool catalogoDisponivel = prepararPlayer();

    if (!retomarAudio(volumeAtual, true)) {
        Serial.println("Player: falha ao retomar o servico de audio.");
        mostrarTelaRelogio();
        return;
    }

    estadoEquipamento = EstadoEquipamento::PLAYER;
    modoInterface = ModoInterface::VOLUME;
    barraVolumeVisivel = false;

    int quantidadeArquivos = obterQuantidadeArquivosPlayer();

    if (!catalogoDisponivel) {
        mostrarMensagem("Player indisponivel");
    } else if (quantidadeArquivos == 0) {
        mostrarMensagem("Sem MP3 em /sons");
    } else {
        indiceArquivoAtual = constrain(
            indiceArquivoAtual,
            0,
            quantidadeArquivos - 1
        );
        indiceArquivoEmSelecao = indiceArquivoAtual;
        mostrarArquivoAtualPlayer();
    }

    Serial.println("Estado do equipamento: Player.");
}

void entrarModoSelecaoRadio() {
    modoInterface = ModoInterface::SELECAO_RADIO;
    barraVolumeVisivel = false;

    indiceRadioEmSelecao = indiceRadioAtual;
    momentoUltimaAtividadeSelecaoMs = millis();

    const Radio* radio =
        obterRadio(indiceRadioEmSelecao);

    if (radio != nullptr) {
        mostrarSelecaoRadio(
            radio->nome,
            indiceRadioEmSelecao,
            obterQuantidadeRadios()
        );
    }

    Serial.println("Modo: seleção de rádio");
}

void confirmarSelecaoRadio() {
    modoInterface = ModoInterface::VOLUME;

    if (indiceRadioEmSelecao == indiceRadioAtual) {
        atualizarDisplayEstadoAudio(true);
    } else {
        solicitarReproducaoRadio(indiceRadioEmSelecao);
    }

    Serial.println("Rádio confirmada; modo: volume");
}

void entrarModoSelecaoArquivo() {
    int quantidadeArquivos = obterQuantidadeArquivosPlayer();

    if (quantidadeArquivos <= 0) {
        mostrarMensagem("Sem MP3 em /sons");
        return;
    }

    modoInterface = ModoInterface::SELECAO_ARQUIVO;
    barraVolumeVisivel = false;
    indiceArquivoEmSelecao = indiceArquivoAtual;
    momentoUltimaAtividadeSelecaoMs = millis();

    mostrarArquivoAtualPlayer(true);
    Serial.println("Player: selecao de arquivo.");
}

void processarNavegacaoArquivos(long deslocamentoEncoder) {
    int quantidadeArquivos = obterQuantidadeArquivosPlayer();

    if (quantidadeArquivos <= 0) {
        return;
    }

    long novoIndice =
        indiceArquivoEmSelecao + deslocamentoEncoder;
    novoIndice %= quantidadeArquivos;

    if (novoIndice < 0) {
        novoIndice += quantidadeArquivos;
    }

    indiceArquivoEmSelecao = static_cast<int>(novoIndice);
    momentoUltimaAtividadeSelecaoMs = millis();
    mostrarArquivoAtualPlayer(true);

    const ArquivoPlayer* arquivo =
        obterArquivoPlayer(indiceArquivoEmSelecao);

    if (arquivo != nullptr) {
        Serial.print("Player selecionado: ");
        Serial.println(arquivo->caminho);
    }
}

void confirmarSelecaoArquivo() {
    iniciarReproducaoArquivoPlayer(
        indiceArquivoEmSelecao,
        false
    );
}

bool iniciarReproducaoArquivoPlayer(
    int indice,
    bool automatica
) {
    const ArquivoPlayer* arquivo = obterArquivoPlayer(indice);

    if (arquivo == nullptr) {
        mostrarMensagem("Arquivo invalido");
        return false;
    }

    if (!tocarArquivoPlayer(arquivo->caminho)) {
        mostrarMensagem("Comando rejeitado");
        return false;
    }

    indiceArquivoAtual = indice;
    indiceArquivoEmSelecao = indice;
    modoInterface = ModoInterface::VOLUME;
    reproducaoPlayerSolicitada = true;
    playerObservouAudioAtivo = false;
    mostrarArquivoAtualPlayer();

    Serial.print(
        automatica
            ? "Player sequencial: "
            : "Player reproduzindo: "
    );
    Serial.println(arquivo->caminho);

    return true;
}

void processarReproducaoSequencialPlayer() {
    if (!reproducaoPlayerSolicitada) {
        return;
    }

    EstadoAudio estadoAudio = obterStatusAudio().estado;

    if (
        estadoAudio == EstadoAudio::BUFFERIZANDO ||
        estadoAudio == EstadoAudio::TOCANDO ||
        estadoAudio == EstadoAudio::DEGRADADO
    ) {
        playerObservouAudioAtivo = true;
        return;
    }

    if (estadoAudio == EstadoAudio::ERRO) {
        reproducaoPlayerSolicitada = false;
        playerObservouAudioAtivo = false;
        return;
    }

    if (
        !playerObservouAudioAtivo ||
        estadoAudio != EstadoAudio::PARADO ||
        modoInterface == ModoInterface::SELECAO_ARQUIVO
    ) {
        return;
    }

    reproducaoPlayerSolicitada = false;
    playerObservouAudioAtivo = false;

    if (!REPRODUCAO_SEQUENCIAL_PLAYER) {
        Serial.println("Player: reproducao finalizada.");
        return;
    }

    int quantidadeArquivos = obterQuantidadeArquivosPlayer();

    if (quantidadeArquivos <= 0) {
        return;
    }

    int proximoIndice =
        (indiceArquivoAtual + 1) % quantidadeArquivos;

    iniciarReproducaoArquivoPlayer(
        proximoIndice,
        true
    );
}

void mostrarArquivoAtualPlayer(bool emSelecao) {
    int indice = emSelecao
        ? indiceArquivoEmSelecao
        : indiceArquivoAtual;
    const ArquivoPlayer* arquivo = obterArquivoPlayer(indice);

    if (arquivo == nullptr) {
        mostrarMensagem("Sem MP3 em /sons");
        return;
    }

    mostrarArquivoPlayer(
        arquivo->nome,
        indice,
        obterQuantidadeArquivosPlayer(),
        emSelecao
    );
}

// =====================================================
// Modo volume
// =====================================================

void processarAjusteVolume(
    long deslocamentoEncoder
) {
    long novoVolume =
        volumeAtual + deslocamentoEncoder;

    novoVolume = constrain(
        novoVolume,
        static_cast<long>(VOLUME_MINIMO),
        static_cast<long>(VOLUME_MAXIMO)
    );

    if (novoVolume == volumeAtual) {
        return;
    }

    volumeAtual = static_cast<int>(novoVolume);

    alterarVolumeAudio(volumeAtual);
    mostrarVolume(volumeAtual);

    barraVolumeVisivel = true;

    momentoUltimaAlteracaoVolumeMs =
        millis();

    Serial.printf(
        "Volume: %d\n",
        volumeAtual
    );
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
// Retorno automático do controle para volume
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

    modoInterface = ModoInterface::VOLUME;
    indiceRadioEmSelecao = indiceRadioAtual;
    barraVolumeVisivel = false;

    Serial.println(
        "Seleção cancelada por inatividade; modo: volume"
    );

    atualizarDisplayEstadoAudio(true);
}

void cancelarSelecaoArquivoPorInatividade() {
    if (modoInterface != ModoInterface::SELECAO_ARQUIVO) {
        return;
    }

    if (
        millis() - momentoUltimaAtividadeSelecaoMs <
        TEMPO_INATIVIDADE_SELECAO_MS
    ) {
        return;
    }

    modoInterface = ModoInterface::VOLUME;
    indiceArquivoEmSelecao = indiceArquivoAtual;
    barraVolumeVisivel = false;
    mostrarArquivoAtualPlayer();

    Serial.println("Player: selecao cancelada por inatividade.");
}

// =====================================================
// Retorno automático da barra inferior
// =====================================================

void restaurarBarraAposTempoVolume() {
    if (modoInterface != ModoInterface::VOLUME) {
        barraVolumeVisivel = false;
        return;
    }

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
        atualizarDisplayEstadoAudio(true);
    } else if (estadoEquipamento == EstadoEquipamento::PLAYER) {
        mostrarArquivoAtualPlayer();
    }
}

// =====================================================
// Rádio
// =====================================================

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

    StatusAudio statusAudio =
        obterStatusAudio();

    if (estadoEquipamento != EstadoEquipamento::RADIO_WEB) {
        return;
    }

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
        modoInterface !=
            ModoInterface::VOLUME ||
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
