#include "nixie.h"

#include <Arduino.h>
#include <esp32-hal-timer.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>

#include "configuracao.h"
#include "relogio.h"

namespace {

constexpr uint32_t FREQUENCIA_TIMER_NIXIE_HZ = 1000000;
constexpr uint8_t NIVEIS_BAIXOS_ADICIONAIS = 4;
constexpr uint8_t NIVEL_BRILHO_MAXIMO = 15;
constexpr uint8_t NIVEIS_FISICOS_BRILHO =
    NIVEL_BRILHO_MAXIMO - NIVEIS_BAIXOS_ADICIONAIS;
constexpr uint32_t INTERVALO_ATUALIZACAO_HORA_MS = 1000;
constexpr uint8_t DIGITO_APAGADO = 0xFF;

static_assert(
    NIXIE_TEMPO_ACESO_US % NIXIE_INTERVALO_FASE_US == 0,
    "NIXIE_TEMPO_ACESO_US deve ser multiplo do intervalo do temporizador."
);

constexpr uint32_t mascaraPino(int pino) {
    return uint32_t{1} << pino;
}

constexpr uint32_t MASCARA_BCD_A = mascaraPino(PIN_NIXIE_BCD_A);
constexpr uint32_t MASCARA_BCD_B = mascaraPino(PIN_NIXIE_BCD_B);
constexpr uint32_t MASCARA_BCD_C = mascaraPino(PIN_NIXIE_BCD_C);
constexpr uint32_t MASCARA_BCD_D = mascaraPino(PIN_NIXIE_BCD_D);
constexpr uint32_t MASCARA_BCD =
    MASCARA_BCD_A |
    MASCARA_BCD_B |
    MASCARA_BCD_C |
    MASCARA_BCD_D;

constexpr uint32_t MASCARA_SELECAO_1 =
    mascaraPino(PIN_NIXIE_CATODO_1);
constexpr uint32_t MASCARA_SELECAO_2 =
    mascaraPino(PIN_NIXIE_CATODO_2);
constexpr uint32_t MASCARA_SELECAO_3 =
    mascaraPino(PIN_NIXIE_CATODO_3);
constexpr uint32_t MASCARA_SELECAO_4 =
    mascaraPino(PIN_NIXIE_CATODO_4);
constexpr uint32_t MASCARA_SELECOES =
    MASCARA_SELECAO_1 |
    MASCARA_SELECAO_2 |
    MASCARA_SELECAO_3 |
    MASCARA_SELECAO_4;

static_assert(
    PIN_NIXIE_BCD_A >= 0 && PIN_NIXIE_BCD_A < 32 &&
    PIN_NIXIE_BCD_B >= 0 && PIN_NIXIE_BCD_B < 32 &&
    PIN_NIXIE_BCD_C >= 0 && PIN_NIXIE_BCD_C < 32 &&
    PIN_NIXIE_BCD_D >= 0 && PIN_NIXIE_BCD_D < 32 &&
    PIN_NIXIE_CATODO_1 >= 0 && PIN_NIXIE_CATODO_1 < 32 &&
    PIN_NIXIE_CATODO_2 >= 0 && PIN_NIXIE_CATODO_2 < 32 &&
    PIN_NIXIE_CATODO_3 >= 0 && PIN_NIXIE_CATODO_3 < 32 &&
    PIN_NIXIE_CATODO_4 >= 0 && PIN_NIXIE_CATODO_4 < 32,
    "O driver Nixie usa escrita agrupada nos GPIOs 0..31."
);

static_assert(
    (MASCARA_BCD & MASCARA_SELECOES) == 0,
    "Os GPIOs BCD e de chaveamento das Nixies devem ser distintos."
);

hw_timer_t* timerNixie = nullptr;
DRAM_ATTR volatile uint32_t quadroNixie = 0xFFFFFFFF;
DRAM_ATTR volatile uint8_t nivelBrilhoNixie = NIVEL_BRILHO_MAXIMO;
// Quando os tempos são alterados pelo menu, eles passam a representar as
// durações físicas do ciclo sem a redução de tempo usada pelo brilho.
DRAM_ATTR volatile bool temporizacaoDiretaNixie = false;
DRAM_ATTR volatile uint32_t tempoAcesoNixieUs =
    NIXIE_TEMPO_ACESO_US;
DRAM_ATTR volatile uint32_t tempoApagadoNixieUs =
    NIXIE_TEMPO_APAGADO_US;
DRAM_ATTR volatile uint32_t intervaloBcdNixieUs =
    NIXIE_INTERVALO_BCD_ESTAVEL_US;
bool nixiesIniciadas = false;
uint32_t momentoUltimaAtualizacaoMs = 0;

uint8_t normalizarDigito(int8_t digito) {
    return digito >= 0 && digito <= 9
        ? static_cast<uint8_t>(digito)
        : DIGITO_APAGADO;
}

uint32_t empacotarQuadro(
    int8_t digito1,
    int8_t digito2,
    int8_t digito3,
    int8_t digito4
) {
    return
        static_cast<uint32_t>(normalizarDigito(digito1)) |
        (static_cast<uint32_t>(normalizarDigito(digito2)) << 8) |
        (static_cast<uint32_t>(normalizarDigito(digito3)) << 16) |
        (static_cast<uint32_t>(normalizarDigito(digito4)) << 24);
}

uint32_t ajustarTempoEscalonado(
    uint32_t valor,
    long deslocamento,
    uint32_t minimo,
    uint32_t maximo,
    uint32_t passoFino
) {
    while (deslocamento != 0) {
        const bool aumentar = deslocamento > 0;
        uint32_t passo = passoFino;
        uint32_t limite = aumentar ? maximo : minimo;

        if (aumentar) {
            if (valor < NIXIE_TEMPO_LIMITE_AJUSTE_FINO_US) {
                limite = NIXIE_TEMPO_LIMITE_AJUSTE_FINO_US;
            } else if (valor < NIXIE_TEMPO_LIMITE_AJUSTE_MEDIO_US) {
                passo = NIXIE_TEMPO_PASSO_MEDIO_US;
                limite = NIXIE_TEMPO_LIMITE_AJUSTE_MEDIO_US;
            } else {
                passo = NIXIE_TEMPO_PASSO_GROSSO_US;
            }
            valor = passo >= limite - valor ? limite : valor + passo;
            deslocamento--;
        } else {
            if (valor > NIXIE_TEMPO_LIMITE_AJUSTE_MEDIO_US) {
                passo = NIXIE_TEMPO_PASSO_GROSSO_US;
                limite = NIXIE_TEMPO_LIMITE_AJUSTE_MEDIO_US;
            } else if (valor > NIXIE_TEMPO_LIMITE_AJUSTE_FINO_US) {
                passo = NIXIE_TEMPO_PASSO_MEDIO_US;
                limite = NIXIE_TEMPO_LIMITE_AJUSTE_FINO_US;
            }
            valor = passo >= valor - limite ? limite : valor - passo;
            deslocamento++;
        }
    }
    return constrain(valor, minimo, maximo);
}

uint32_t ajustarTempoAcesoEscalonado(
    uint32_t valor,
    long deslocamento
) {
    while (deslocamento != 0) {
        if (deslocamento > 0) {
            if (valor < NIXIE_INTERVALO_FASE_US) {
                valor = min(
                    valor + NIXIE_TEMPO_ACESO_PASSO_SUBFASE_US,
                    NIXIE_INTERVALO_FASE_US
                );
            } else {
                valor = ajustarTempoEscalonado(
                    valor,
                    1,
                    NIXIE_INTERVALO_FASE_US,
                    NIXIE_TEMPO_ACESO_MAXIMO_US,
                    NIXIE_TEMPO_ACESO_PASSO_US
                );
            }
            deslocamento--;
        } else {
            if (valor <= NIXIE_INTERVALO_FASE_US) {
                valor = valor > NIXIE_TEMPO_ACESO_MINIMO_US
                    ? valor - NIXIE_TEMPO_ACESO_PASSO_SUBFASE_US
                    : NIXIE_TEMPO_ACESO_MINIMO_US;
            } else {
                valor = ajustarTempoEscalonado(
                    valor,
                    -1,
                    NIXIE_INTERVALO_FASE_US,
                    NIXIE_TEMPO_ACESO_MAXIMO_US,
                    NIXIE_TEMPO_ACESO_PASSO_US
                );
            }
            deslocamento++;
        }
    }
    return valor;
}

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

void ARDUINO_ISR_ATTR atualizarMultiplexacaoNixie() {
    static uint8_t posicao = 0;
    static uint32_t fase = 0;
    static uint32_t fasesAcesasNoCiclo = 0;
    static uint32_t fasesTempoAcesoCiclo =
        NIXIE_TEMPO_ACESO_US / NIXIE_INTERVALO_FASE_US;
    static uint32_t momentoApagamentoUs = 0;
    static uint32_t tempoApagadoCicloUs = NIXIE_TEMPO_APAGADO_US;
    static uint32_t intervaloBcdCicloUs =
        NIXIE_INTERVALO_BCD_ESTAVEL_US;
    static uint32_t pulsoAcesoSubfaseCicloUs = 0;
    static bool temporizacaoDiretaCiclo = false;

    if (fase == 0) {
        // Desliga todos os optoacopladores e força o código BCD 15.
        // O instante real é registrado para garantir o tempo mínimo de
        // apagamento mesmo se uma interrupção do temporizador atrasar.
        REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_SELECOES);
        REG_WRITE(GPIO_OUT_W1TS_REG, MASCARA_BCD);
        // Captura os três ajustes juntos no início do ciclo do dígito. Uma
        // alteração pela interface passa a valer no próximo dígito.
        const uint32_t tempoAcesoCicloUs = tempoAcesoNixieUs;
        const uint32_t fasesTempoAceso =
            tempoAcesoCicloUs / NIXIE_INTERVALO_FASE_US;
        fasesTempoAcesoCiclo = fasesTempoAceso > 0
            ? fasesTempoAceso
            : 1;
        pulsoAcesoSubfaseCicloUs =
            tempoAcesoCicloUs < NIXIE_INTERVALO_FASE_US
                ? tempoAcesoCicloUs
                : 0;
        tempoApagadoCicloUs = tempoApagadoNixieUs;
        intervaloBcdCicloUs = intervaloBcdNixieUs;
        temporizacaoDiretaCiclo = temporizacaoDiretaNixie;
        momentoApagamentoUs = micros();
        fase = 1;
        return;
    } else if (fase == 1) {
        if (
            static_cast<uint32_t>(micros() - momentoApagamentoUs) <
            tempoApagadoCicloUs
        ) {
            return;
        }

        REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_BCD);

        const uint32_t quadro = quadroNixie;
        const uint8_t digito =
            static_cast<uint8_t>((quadro >> (posicao * 8)) & 0xFF);
        const uint8_t nivelBrilho = nivelBrilhoNixie;

        if (temporizacaoDiretaCiclo && nivelBrilho > 0) {
            fasesAcesasNoCiclo = fasesTempoAcesoCiclo;
        } else if (nivelBrilho > NIVEIS_BAIXOS_ADICIONAIS) {
            const uint8_t nivelFisico =
                nivelBrilho - NIVEIS_BAIXOS_ADICIONAIS;
            fasesAcesasNoCiclo =
                (
                    fasesTempoAcesoCiclo * nivelFisico +
                    NIVEIS_FISICOS_BRILHO - 1
                ) / NIVEIS_FISICOS_BRILHO;
        } else {
            fasesAcesasNoCiclo = 0;
        }

        if (digito <= 9 && nivelBrilho > 0) {
            escreverBcd(digito);
            if (intervaloBcdCicloUs > 0) {
                delayMicroseconds(intervaloBcdCicloUs);
            }
            REG_WRITE(GPIO_OUT_W1TS_REG, mascaraSelecao(posicao));

            if (
                temporizacaoDiretaCiclo &&
                pulsoAcesoSubfaseCicloUs > 0
            ) {
                delayMicroseconds(pulsoAcesoSubfaseCicloUs);
                REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_SELECOES);
            } else if (
                !temporizacaoDiretaCiclo &&
                nivelBrilho <= NIVEIS_BAIXOS_ADICIONAIS
            ) {
                // Subdivide somente a primeira fase estável de 227 us.
                // Os quatro níveis extras ficam abaixo do antigo nível 1
                // sem acelerar o temporizador geral da multiplexação.
                const uint32_t pulsoBaixoUs =
                    (NIXIE_INTERVALO_FASE_US * nivelBrilho) /
                    (NIVEIS_BAIXOS_ADICIONAIS + 1);
                delayMicroseconds(pulsoBaixoUs);
                REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_SELECOES);
            }
        }
    } else if (
        fasesAcesasNoCiclo > 0 &&
        fase > fasesAcesasNoCiclo
    ) {
        // Mantém o período total do dígito constante e reduz apenas o tempo
        // em que seu optoacoplador permanece habilitado.
        REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_SELECOES);
    }

    fase++;
    if (fase >= 1 + fasesTempoAcesoCiclo) {
        fase = 0;
        posicao = (posicao + 1) & 0x03;
    }
}

void configurarSaidaDesligada(int pino) {
    // Prepara o latch em LOW antes de transformar o GPIO em saída, evitando
    // um pulso de ativação no optoacoplador durante a inicialização.
    REG_WRITE(GPIO_OUT_W1TC_REG, mascaraPino(pino));
    pinMode(pino, OUTPUT);
}

}

bool iniciarNixies() {
    configurarSaidaDesligada(PIN_NIXIE_CATODO_1);
    configurarSaidaDesligada(PIN_NIXIE_CATODO_2);
    configurarSaidaDesligada(PIN_NIXIE_CATODO_3);
    configurarSaidaDesligada(PIN_NIXIE_CATODO_4);

    configurarSaidaDesligada(PIN_NIXIE_BCD_A);
    configurarSaidaDesligada(PIN_NIXIE_BCD_B);
    configurarSaidaDesligada(PIN_NIXIE_BCD_C);
    configurarSaidaDesligada(PIN_NIXIE_BCD_D);

    apagarNixies();

    timerNixie = timerBegin(FREQUENCIA_TIMER_NIXIE_HZ);
    if (timerNixie == nullptr) {
        Serial.println("Falha ao iniciar o temporizador das Nixies.");
        return false;
    }

    timerAttachInterrupt(timerNixie, &atualizarMultiplexacaoNixie);
    timerAlarm(
        timerNixie,
        NIXIE_INTERVALO_FASE_US,
        true,
        0
    );
    nixiesIniciadas = true;
    processarNixies();
    Serial.println(
        "Nixies prontas: BCD A/B/C/D=GPIO4/6/7/5; "
        "ordem UM/DM/UH/DH=GPIO14/13/12/11 HIGH."
    );
    return true;
}

void processarNixies() {
    if (!nixiesIniciadas) {
        return;
    }

    const uint32_t agoraMs = millis();
    if (
        momentoUltimaAtualizacaoMs != 0 &&
        agoraMs - momentoUltimaAtualizacaoMs <
            INTERVALO_ATUALIZACAO_HORA_MS
    ) {
        return;
    }
    momentoUltimaAtualizacaoMs = agoraMs;

    struct tm dataHora = {};
    if (!obterDataHoraLocal(dataHora)) {
        return;
    }

    mostrarDigitosNixie(
        dataHora.tm_min % 10,
        dataHora.tm_min / 10,
        dataHora.tm_hour % 10,
        dataHora.tm_hour >= 10 ? dataHora.tm_hour / 10 : -1
    );
}

void mostrarDigitosNixie(
    int8_t unidadeMinuto,
    int8_t dezenaMinuto,
    int8_t unidadeHora,
    int8_t dezenaHora
) {
    quadroNixie = empacotarQuadro(
        unidadeMinuto,
        dezenaMinuto,
        unidadeHora,
        dezenaHora
    );
}

// Ajuste público usado pelo encoder A nos modos relógio e repouso.
void ajustarBrilhoNixie(long deslocamento) {
    if (deslocamento == 0) {
        return;
    }

    const long novoNivel = constrain(
        static_cast<long>(nivelBrilhoNixie) + deslocamento,
        0L,
        static_cast<long>(NIVEL_BRILHO_MAXIMO)
    );
    nivelBrilhoNixie = static_cast<uint8_t>(novoNivel);
    // Um comando explícito de brilho encerra o teste de tempos diretos e
    // devolve ao controle normal por duty cycle.
    temporizacaoDiretaNixie = false;

    Serial.printf(
        "Brilho Nixie: %u/%u.\n",
        static_cast<unsigned>(nivelBrilhoNixie),
        static_cast<unsigned>(NIVEL_BRILHO_MAXIMO)
    );
}

uint8_t obterNivelBrilhoNixie() {
    return nivelBrilhoNixie;
}

void ajustarTempoAcesoNixie(long deslocamento) {
    if (deslocamento == 0) {
        return;
    }
    tempoAcesoNixieUs = ajustarTempoAcesoEscalonado(
        tempoAcesoNixieUs,
        deslocamento
    );
    nivelBrilhoNixie = NIVEL_BRILHO_MAXIMO;
    temporizacaoDiretaNixie = true;
}

uint32_t obterTempoAcesoNixieUs() {
    return tempoAcesoNixieUs;
}

void ajustarTempoApagadoNixie(long deslocamento) {
    if (deslocamento == 0) {
        return;
    }
    tempoApagadoNixieUs = ajustarTempoEscalonado(
        tempoApagadoNixieUs,
        deslocamento,
        NIXIE_TEMPO_APAGADO_MINIMO_US,
        NIXIE_TEMPO_APAGADO_MAXIMO_US,
        NIXIE_TEMPO_APAGADO_PASSO_US
    );
    nivelBrilhoNixie = NIVEL_BRILHO_MAXIMO;
    temporizacaoDiretaNixie = true;
}

uint32_t obterTempoApagadoNixieUs() {
    return tempoApagadoNixieUs;
}

void ajustarIntervaloBcdNixie(long deslocamento) {
    if (deslocamento == 0) {
        return;
    }
    int64_t novoValor =
        static_cast<int64_t>(intervaloBcdNixieUs) +
        static_cast<int64_t>(deslocamento) *
            NIXIE_INTERVALO_BCD_PASSO_US;
    novoValor = constrain(
        novoValor,
        static_cast<int64_t>(NIXIE_INTERVALO_BCD_MINIMO_US),
        static_cast<int64_t>(NIXIE_INTERVALO_BCD_MAXIMO_US)
    );
    intervaloBcdNixieUs = static_cast<uint32_t>(novoValor);
}

uint32_t obterIntervaloBcdNixieUs() {
    return intervaloBcdNixieUs;
}

void apagarNixies() {
    quadroNixie = 0xFFFFFFFF;
    REG_WRITE(GPIO_OUT_W1TC_REG, MASCARA_BCD | MASCARA_SELECOES);
}
