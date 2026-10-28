#include <Arduino.h>
#include <AiEsp32RotaryEncoder.h>
#include <DFRobotDFPlayerMini.h>
#include <Wire.h>

// Mesma pinagem reservada no configuracao.h do firmware principal.
constexpr int PIN_ENCODER_DT = 2;
constexpr int PIN_ENCODER_CLK = 1;
constexpr int PIN_ENCODER_SW = 42;
constexpr uint8_t TRANSICOES_POR_DETENTE = 4;

constexpr int UART_DFPLAYER = 2;  // Controlador UART2; não é o GPIO2.
constexpr uint32_t BAUD_DFPLAYER = 9600;
constexpr int PIN_DFPLAYER_RX = 20;  // GPIO ligado ao RX do DFPlayer.
constexpr int PIN_DFPLAYER_TX = 19;  // GPIO ligado ao TX do DFPlayer.
constexpr int PIN_DFPLAYER_BUSY = 38;

constexpr int PIN_I2C_SDA = 16;
constexpr int PIN_I2C_SCL = 17;
constexpr uint32_t FREQUENCIA_I2C_HZ = 100000;
constexpr uint8_t ENDERECO_MCP23017 = 0x20;
constexpr uint8_t PIN_MCP_ENABLE_N_8002_DIREITO = 6;
constexpr uint8_t PIN_MCP_ENABLE_N_8002_ESQUERDO = 7;

constexpr uint8_t REG_MCP_IODIRA = 0x00;
constexpr uint8_t REG_MCP_IOCON_BANK1 = 0x05;
constexpr uint8_t REG_MCP_IOCON_BANK0 = 0x0A;
constexpr uint8_t REG_MCP_GPIOA = 0x12;
constexpr uint8_t REG_MCP_OLATA = 0x14;
constexpr uint8_t MASCARA_ENABLE_N_8002 =
    (1U << PIN_MCP_ENABLE_N_8002_DIREITO) |
    (1U << PIN_MCP_ENABLE_N_8002_ESQUERDO);

// A pasta chamada "sons" no projeto deve ser /01 no cartão do DFPlayer.
constexpr uint8_t PASTA_SONS = 1;
constexpr uint8_t VOLUME_TESTE = 30;
constexpr uint32_t TIMEOUT_DFPLAYER_MS = 800;
constexpr uint32_t DEBOUNCE_BOTAO_MS = 30;

HardwareSerial serialDfPlayer(UART_DFPLAYER);
DFRobotDFPlayerMini dfPlayer;
AiEsp32RotaryEncoder encoderVolume(
    PIN_ENCODER_DT,
    PIN_ENCODER_CLK,
    PIN_ENCODER_SW,
    -1,
    TRANSICOES_POR_DETENTE,
    false
);

uint8_t quantidadeSons = 0;
uint8_t somAtual = 0;
bool chaveamentoAudioDisponivel = false;
bool saidaDfPlayerAtiva = false;
uint8_t estadoPortaA = 0;

bool leituraBotaoAnterior = HIGH;
bool estadoBotaoConfirmado = HIGH;
bool pressaoBotaoDetectada = false;
uint32_t instanteUltimaBordaBotao = 0;

void IRAM_ATTR lerEncoderVolumeISR() {
    encoderVolume.readEncoder_ISR();
}

bool escreverMcp23017(uint8_t registro, uint8_t valor) {
    Wire.beginTransmission(ENDERECO_MCP23017);
    Wire.write(registro);
    Wire.write(valor);
    const uint8_t codigo = Wire.endTransmission(true);

    if (codigo != 0) {
        Serial.printf(
            "ERRO MCP23017: registro 0x%02X, codigo I2C %u.\n",
            static_cast<unsigned>(registro),
            static_cast<unsigned>(codigo)
        );
        return false;
    }

    return true;
}

bool lerMcp23017(uint8_t registro, uint8_t& valor) {
    Wire.beginTransmission(ENDERECO_MCP23017);
    Wire.write(registro);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    if (Wire.requestFrom(ENDERECO_MCP23017, static_cast<uint8_t>(1)) != 1) {
        return false;
    }

    valor = Wire.read();
    return true;
}

bool verificarEnable8002(bool mutar) {
    uint8_t direcao = 0;
    uint8_t latch = 0;
    uint8_t pinos = 0;
    if (
        !lerMcp23017(REG_MCP_IODIRA, direcao) ||
        !lerMcp23017(REG_MCP_OLATA, latch) ||
        !lerMcp23017(REG_MCP_GPIOA, pinos)
    ) {
        Serial.println("ERRO: nao foi possivel reler o MCP23017.");
        return false;
    }

    const uint8_t esperado = mutar ? MASCARA_ENABLE_N_8002 : 0;
    const bool correto =
        (direcao & MASCARA_ENABLE_N_8002) == 0 &&
        (latch & MASCARA_ENABLE_N_8002) == esperado &&
        (pinos & MASCARA_ENABLE_N_8002) == esperado;

    Serial.printf(
        "MCP23017 lido: IODIRA=0x%02X OLATA=0x%02X GPIOA=0x%02X; "
        "GPA7/GPA6=%s.\n",
        direcao,
        latch,
        pinos,
        correto ? (mutar ? "HIGH" : "LOW") : "INCORRETO"
    );
    return correto;
}

bool configurarEnable8002(bool mutar) {
    if (!chaveamentoAudioDisponivel) {
        return false;
    }

    uint8_t novoEstado = estadoPortaA;
    if (mutar) {
        novoEstado |= MASCARA_ENABLE_N_8002;
    } else {
        novoEstado &= static_cast<uint8_t>(~MASCARA_ENABLE_N_8002);
    }

    if (
        novoEstado != estadoPortaA &&
        !escreverMcp23017(REG_MCP_OLATA, novoEstado)
    ) {
        return false;
    }

    estadoPortaA = novoEstado;
    return verificarEnable8002(mutar);
}

void colocarAudioEmStandby() {
    configurarEnable8002(true);
}

bool iniciarEnable8002() {
    chaveamentoAudioDisponivel = false;

    // Garante BANK=0 mesmo se o MCP23017 permaneceu alimentado durante um
    // reset do ESP32. Em BANK=1, 0x14 não é OLATA e a escrita parecia ter
    // funcionado sem alterar GPA7/GPA6.
    if (
        !escreverMcp23017(REG_MCP_IOCON_BANK1, 0x00) ||
        !escreverMcp23017(REG_MCP_IOCON_BANK0, 0x00)
    ) {
        return false;
    }

    // Lê e preserva toda a porta A, principalmente GPA5 do relé. Somente os
    // bits dos enables dos NS8002 serão modificados por este teste.
    if (!lerMcp23017(REG_MCP_OLATA, estadoPortaA)) {
        return false;
    }

    estadoPortaA |= MASCARA_ENABLE_N_8002;
    if (!escreverMcp23017(REG_MCP_OLATA, estadoPortaA)) {
        return false;
    }

    uint8_t direcaoPortaA;
    if (!lerMcp23017(REG_MCP_IODIRA, direcaoPortaA)) {
        return false;
    }

    direcaoPortaA &= static_cast<uint8_t>(~MASCARA_ENABLE_N_8002);
    if (!escreverMcp23017(REG_MCP_IODIRA, direcaoPortaA)) {
        return false;
    }

    chaveamentoAudioDisponivel = true;
    if (!verificarEnable8002(true)) {
        chaveamentoAudioDisponivel = false;
        return false;
    }
    Serial.println(
        "MCP23017 pronto: GPA7/GPA6=HIGH; GPA5 preservado."
    );
    return true;
}

bool ativar8002ParaDfPlayer() {
    if (!chaveamentoAudioDisponivel) {
        Serial.println("ERRO: NS8002 nao habilitados; MCP23017 indisponivel.");
        return false;
    }

    if (saidaDfPlayerAtiva) {
        return true;
    }

    dfPlayer.enableDAC();

    // DAC_L/DAC_R chegam diretamente aos amplificadores. Só os dois enables
    // ativos em LOW são alterados; GPA5 permanece exatamente como estava.
    if (!configurarEnable8002(false)) {
        dfPlayer.disableDAC();
        return false;
    }

    saidaDfPlayerAtiva = true;

    Serial.println(
        "Saida DFPlayer ativa: GPA7/GPA6=LOW; GPA5 preservado."
    );
    return true;
}

void desativarSaidaDfPlayer() {
    colocarAudioEmStandby();
    dfPlayer.disableDAC();
    saidaDfPlayerAtiva = false;
}

void limparRecepcaoSerialDfPlayer() {
    while (serialDfPlayer.available() > 0) {
        serialDfPlayer.read();
    }
}

bool iniciarDfPlayer() {
    pinMode(PIN_DFPLAYER_BUSY, INPUT_PULLUP);
    serialDfPlayer.begin(
        BAUD_DFPLAYER,
        SERIAL_8N1,
        PIN_DFPLAYER_TX,  // RX do ESP recebe o TX do DFPlayer.
        PIN_DFPLAYER_RX   // TX do ESP envia ao RX do DFPlayer.
    );

    dfPlayer.setTimeOut(TIMEOUT_DFPLAYER_MS);

    // Sem ACK evita que a confirmação seja confundida com a resposta da
    // consulta de quantidade em algumas revisões e clones do módulo.
    dfPlayer.begin(serialDfPlayer, false, true);

    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    dfPlayer.volume(VOLUME_TESTE);
    delay(100);

    limparRecepcaoSerialDfPlayer();
    const int quantidadePasta =
        dfPlayer.readFileCountsInFolder(PASTA_SONS);
    limparRecepcaoSerialDfPlayer();
    const int quantidadeCartao = dfPlayer.readFileCounts();
    dfPlayer.disableDAC();

    int quantidade = quantidadePasta;
    if (quantidadeCartao > quantidade) {
        quantidade = quantidadeCartao;
    }

    Serial.printf(
        "Contagem DFPlayer: pasta /%02u=%d; cartao=%d.\n",
        static_cast<unsigned>(PASTA_SONS),
        quantidadePasta,
        quantidadeCartao
    );

    if (quantidade < 0) {
        Serial.println(
            "ERRO: sem resposta ao consultar pasta e cartao. "
            "Confira RX, TX, alimentacao e cartao."
        );
        return false;
    }

    if (quantidade == 0) {
        Serial.println(
            "ERRO: /01 nao existe ou nao contem 001.mp3, 002.mp3..."
        );
        return false;
    }

    quantidadeSons = static_cast<uint8_t>(
        quantidade > 255 ? 255 : quantidade
    );

    Serial.printf(
        "DFPlayer pronto: %u som(ns) encontrado(s) em /%02u.\n",
        static_cast<unsigned>(quantidadeSons),
        static_cast<unsigned>(PASTA_SONS)
    );
    return true;
}

void reproduzirSom(uint8_t numero) {
    if (
        quantidadeSons == 0 ||
        numero == 0 ||
        numero > quantidadeSons
    ) {
        return;
    }

    if (!ativar8002ParaDfPlayer()) {
        return;
    }

    somAtual = numero;
    dfPlayer.playFolder(PASTA_SONS, somAtual);
    Serial.printf(
        "Reproduzindo /%02u/%03u.mp3 (%u de %u).\n",
        static_cast<unsigned>(PASTA_SONS),
        static_cast<unsigned>(somAtual),
        static_cast<unsigned>(somAtual),
        static_cast<unsigned>(quantidadeSons)
    );
}

void deslocarSom(long deslocamento) {
    if (quantidadeSons == 0 || deslocamento == 0) {
        return;
    }

    const int32_t total = quantidadeSons;
    int32_t origem;

    if (somAtual == 0) {
        origem = deslocamento > 0 ? -1 : 0;
    } else {
        origem = somAtual - 1;
    }

    int32_t indice = (origem + deslocamento) % total;
    if (indice < 0) {
        indice += total;
    }

    reproduzirSom(static_cast<uint8_t>(indice + 1));
}

void processarBotaoEncoder() {
    const bool leitura = digitalRead(PIN_ENCODER_SW);
    const uint32_t agora = millis();

    if (leitura != leituraBotaoAnterior) {
        leituraBotaoAnterior = leitura;
        instanteUltimaBordaBotao = agora;
    }

    if (
        agora - instanteUltimaBordaBotao < DEBOUNCE_BOTAO_MS ||
        leitura == estadoBotaoConfirmado
    ) {
        return;
    }

    estadoBotaoConfirmado = leitura;
    if (estadoBotaoConfirmado == LOW) {
        pressaoBotaoDetectada = true;
        return;
    }

    if (pressaoBotaoDetectada) {
        pressaoBotaoDetectada = false;
        deslocarSom(1);
    }
}

void processarEventosDfPlayer() {
    while (dfPlayer.available()) {
        const uint8_t tipo = dfPlayer.readType();
        const uint16_t detalhe = dfPlayer.read();

        switch (tipo) {
            case DFPlayerPlayFinished:
                Serial.printf(
                    "Som %u concluido.\n",
                    static_cast<unsigned>(detalhe)
                );
                break;

            case DFPlayerCardRemoved:
                quantidadeSons = 0;
                desativarSaidaDfPlayer();
                Serial.println("ERRO: cartao removido.");
                break;

            case DFPlayerError:
                Serial.printf(
                    "Aviso DFPlayer: codigo %u; mantendo DAC e NS8002 ativos.\n",
                    static_cast<unsigned>(detalhe)
                );
                break;

            default:
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("Teste minimo: DFPlayer + encoder de volume");
    Serial.println(
        "Giro horario: proximo; anti-horario: anterior; "
        "clique curto: proximo."
    );

    encoderVolume.begin();
    encoderVolume.setup(lerEncoderVolumeISR);
    encoderVolume.setEncoderValue(0);
    encoderVolume.disableAcceleration();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, FREQUENCIA_I2C_HZ);
    iniciarEnable8002();

    if (!iniciarDfPlayer()) {
        desativarSaidaDfPlayer();
    }
}

void loop() {
    // A biblioteca entrega o deslocamento acumulado. Passá-lo inteiro mantém
    // um avanço por detente mesmo quando o loop recebe vários de uma vez.
    const long deslocamento = encoderVolume.encoderChanged();
    if (deslocamento != 0) {
        deslocarSom(deslocamento);
    }

    processarBotaoEncoder();
    processarEventosDfPlayer();
}
