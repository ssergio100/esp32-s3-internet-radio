#include <Arduino.h>
#include <esp32-hal-timer.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>

// ---------------------------------------------------------------------------
// TESTE ISOLADO DO MESMO MULTIPLEXADOR USADO PELO PROGRAMA PRINCIPAL
// ---------------------------------------------------------------------------
//
// Exibe continuamente 23:16. A ISR, a escrita agrupada dos GPIOs, o período
// do temporizador e a máquina de fases são iguais aos de nixie.cpp.
//
// Se este exemplo também apresentar vazamento, o problema está na sequência
// elétrica, nos tempos ou no hardware. Se funcionar aqui e falhar no firmware
// principal, a diferença está na execução concorrente dos demais serviços.

// Decoder BCD K155ID1. Ordem confirmada no teste estático de 0 a 9.
constexpr int PIN_BCD_A = 4;  // peso 1
constexpr int PIN_BCD_B = 6;  // peso 2
constexpr int PIN_BCD_C = 7;  // peso 4
constexpr int PIN_BCD_D = 5;  // peso 8

// Optoacopladores das válvulas, da esquerda para a direita. Ativos em HIGH.
constexpr int PIN_CHAVE_NIXIE_1 = 11;
constexpr int PIN_CHAVE_NIXIE_2 = 12;
constexpr int PIN_CHAVE_NIXIE_3 = 13;
constexpr int PIN_CHAVE_NIXIE_4 = 14;

// ---------------------------------------------------------------------------
// AJUSTES IDÊNTICOS AOS DO PROGRAMA PRINCIPAL
// ---------------------------------------------------------------------------
constexpr uint32_t FREQUENCIA_TIMER_NIXIE_HZ = 1000000;
constexpr uint32_t INTERVALO_FASE_NIXIE_US = 500;
constexpr uint32_t TEMPO_ACESO_US = 2500;
constexpr uint32_t TEMPO_APAGADO_US = 1000;
constexpr uint32_t INTERVALO_BCD_APAGADO_US = 15;

constexpr uint8_t FASES_POR_DIGITO =
    1 + (TEMPO_ACESO_US / INTERVALO_FASE_NIXIE_US);

static_assert(
    TEMPO_ACESO_US % INTERVALO_FASE_NIXIE_US == 0,
    "TEMPO_ACESO_US deve ser multiplo do intervalo do temporizador."
);

constexpr uint32_t mascaraPino(int pino) {
    return uint32_t{1} << pino;
}

constexpr uint32_t MASCARA_BCD_A = mascaraPino(PIN_BCD_A);
constexpr uint32_t MASCARA_BCD_B = mascaraPino(PIN_BCD_B);
constexpr uint32_t MASCARA_BCD_C = mascaraPino(PIN_BCD_C);
constexpr uint32_t MASCARA_BCD_D = mascaraPino(PIN_BCD_D);
constexpr uint32_t MASCARA_BCD =
    MASCARA_BCD_A |
    MASCARA_BCD_B |
    MASCARA_BCD_C |
    MASCARA_BCD_D;

constexpr uint32_t MASCARA_SELECAO_1 =
    mascaraPino(PIN_CHAVE_NIXIE_1);
constexpr uint32_t MASCARA_SELECAO_2 =
    mascaraPino(PIN_CHAVE_NIXIE_2);
constexpr uint32_t MASCARA_SELECAO_3 =
    mascaraPino(PIN_CHAVE_NIXIE_3);
constexpr uint32_t MASCARA_SELECAO_4 =
    mascaraPino(PIN_CHAVE_NIXIE_4);
constexpr uint32_t MASCARA_SELECOES =
    MASCARA_SELECAO_1 |
    MASCARA_SELECAO_2 |
    MASCARA_SELECAO_3 |
    MASCARA_SELECAO_4;

static_assert(
    (MASCARA_BCD & MASCARA_SELECOES) == 0,
    "Os GPIOs BCD e de chaveamento devem ser distintos."
);

hw_timer_t* timerNixie = nullptr;

// Um byte por válvula, da esquerda para a direita: 2, 3, 1 e 6.
DRAM_ATTR volatile uint32_t quadroNixie = 0x06010302;

void ARDUINO_ISR_ATTR escreverBcd(uint8_t digito) {
    uint32_t niveisAltos = 0;
    if ((digito & 0x01) != 0) {
        niveisAltos |= MASCARA_BCD_A;
    }
    if ((digito & 0x02) != 0) {
        niveisAltos |= MASCARA_BCD_B;
    }
    if ((digito & 0x04) != 0) {
        niveisAltos |= MASCARA_BCD_C;
    }
    if ((digito & 0x08) != 0) {
        niveisAltos |= MASCARA_BCD_D;
    }
    if (niveisAltos != 0) {
        REG_WRITE(GPIO_OUT_W1TS_REG, niveisAltos);
    }
}

uint32_t ARDUINO_ISR_ATTR mascaraSelecao(uint8_t posicao) {
    switch (posicao) {
        case 0:
            return MASCARA_SELECAO_1;
        case 1:
            return MASCARA_SELECAO_2;
        case 2:
            return MASCARA_SELECAO_3;
        default:
            return MASCARA_SELECAO_4;
    }
}

// Cópia intencional da ISR do programa principal. Mantenha as duas iguais
// enquanto este diagnóstico estiver sendo realizado.
void ARDUINO_ISR_ATTR atualizarMultiplexacaoNixie() {
    static uint8_t posicao = 0;
    static uint8_t fase = 0;
    static uint32_t momentoApagamentoUs = 0;

    if (fase == 0) {
        REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_SELECOES);
        REG_WRITE(GPIO_OUT_W1TS_REG, MASCARA_BCD);
        momentoApagamentoUs = micros();
        fase = 1;
        return;
    } else if (fase == 1) {
        if (
            static_cast<uint32_t>(micros() - momentoApagamentoUs) <
            TEMPO_APAGADO_US
        ) {
            return;
        }

        REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_BCD);

        const uint32_t quadro = quadroNixie;
        const uint8_t digito =
            static_cast<uint8_t>((quadro >> (posicao * 8)) & 0xFF);

        if (digito <= 9) {
            escreverBcd(digito);
            delayMicroseconds(INTERVALO_BCD_APAGADO_US);
            REG_WRITE(GPIO_OUT_W1TS_REG, mascaraSelecao(posicao));
        }
    }

    fase++;
    if (fase >= FASES_POR_DIGITO) {
        fase = 0;
        posicao = (posicao + 1) & 0x03;
    }
}

void configurarSaidaDesligada(int pino) {
    REG_WRITE(GPIO_OUT_W1TC_REG, mascaraPino(pino));
    pinMode(pino, OUTPUT);
}

void setup() {
    Serial.begin(115200);

    configurarSaidaDesligada(PIN_CHAVE_NIXIE_1);
    configurarSaidaDesligada(PIN_CHAVE_NIXIE_2);
    configurarSaidaDesligada(PIN_CHAVE_NIXIE_3);
    configurarSaidaDesligada(PIN_CHAVE_NIXIE_4);

    configurarSaidaDesligada(PIN_BCD_A);
    configurarSaidaDesligada(PIN_BCD_B);
    configurarSaidaDesligada(PIN_BCD_C);
    configurarSaidaDesligada(PIN_BCD_D);

    REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_BCD | MASCARA_SELECOES);

    timerNixie = timerBegin(FREQUENCIA_TIMER_NIXIE_HZ);
    if (timerNixie == nullptr) {
        Serial.println("Falha ao iniciar o temporizador das Nixies.");
        return;
    }

    timerAttachInterrupt(timerNixie, &atualizarMultiplexacaoNixie);
    timerAlarm(timerNixie, INTERVALO_FASE_NIXIE_US, true, 0);

    Serial.println();
    Serial.println("Teste Nixie com a ISR do programa principal: 23:16.");
    Serial.println("BCD GPIO4/6/7/5; valvulas GPIO11/12/13/14, HIGH.");
    Serial.printf(
        "Tempos: fase=%lu; aceso=%lu; apagado=%lu; BCD=%lu us.\n",
        static_cast<unsigned long>(INTERVALO_FASE_NIXIE_US),
        static_cast<unsigned long>(TEMPO_ACESO_US),
        static_cast<unsigned long>(TEMPO_APAGADO_US),
        static_cast<unsigned long>(INTERVALO_BCD_APAGADO_US)
    );
}

void loop() {
    // A multiplexação acontece integralmente na interrupção do temporizador.
    delay(1000);
}
