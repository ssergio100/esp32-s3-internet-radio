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
void mostrarArquivoAtualPlayer(bool emSelecao = false);
void cancelarSelecaoArquivoPorInatividade();

void restaurarBarraAposTempoVolume();
void cancelarSelecaoRadioPorInatividade();

void atualizarDisplayEstadoAudio(
    bool forcarAtualizacao = false
);

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
    processarDisplay();

    LeituraControles leituraControles =
        lerControles();

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
    }

    restaurarBarraAposTempoVolume();

    atualizarIndicadorEstadoAudio();
    atualizarDisplayEstadoAudio();

    registrarTelemetriaPeriodica();
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

    if (!retomarAudio(volumeAtual)) {
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
    const ArquivoPlayer* arquivo =
        obterArquivoPlayer(indiceArquivoEmSelecao);

    if (arquivo == nullptr) {
        mostrarMensagem("Arquivo invalido");
        return;
    }

    if (!tocarArquivoPlayer(arquivo->caminho)) {
        mostrarMensagem("Comando rejeitado");
        return;
    }

    indiceArquivoAtual = indiceArquivoEmSelecao;
    modoInterface = ModoInterface::VOLUME;
    mostrarArquivoAtualPlayer();

    Serial.print("Player reproduzindo: ");
    Serial.println(arquivo->caminho);
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
