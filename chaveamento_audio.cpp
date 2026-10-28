#include "chaveamento_audio.h"

#include <Arduino.h>
#include <Wire.h>

#include "configuracao.h"

namespace {

    // Registradores para BANK=0, estado padrão do MCP23017 após reset.
    constexpr uint8_t REG_IODIRA = 0x00;
    constexpr uint8_t REG_IOCON_BANK1 = 0x05;
    constexpr uint8_t REG_IOCON_BANK0 = 0x0A;
    constexpr uint8_t REG_GPIOA = 0x12;
    constexpr uint8_t REG_OLATA = 0x14;

    constexpr uint8_t MASCARA_RELE_SELECIONA_DAC =
        1U << PIN_MCP23017_RELE_8002_SELECIONA_DAC;
    constexpr uint8_t MASCARA_HABILITA_RADIO_FM =
        1U << PIN_MCP23017_HABILITA_RADIO_FM;
    constexpr uint8_t MASCARA_ENABLE_N_8002 =
        (1U << PIN_MCP23017_ENABLE_N_8002_DIREITO) |
        (1U << PIN_MCP23017_ENABLE_N_8002_ESQUERDO);
    constexpr uint8_t MASCARA_SAIDAS_CONTROLADAS =
        MASCARA_HABILITA_RADIO_FM |
        MASCARA_RELE_SELECIONA_DAC |
        MASCARA_ENABLE_N_8002;

    // Tabela elétrica centralizada; ordem física: GPA7,GPA6,GPA5,GPA4.
    constexpr uint8_t ESTADO_CAMINHO_RADIO_FM = 0x00;
    constexpr uint8_t ESTADO_RADIO_WEB_ATIVO = MASCARA_RELE_SELECIONA_DAC;
    // Os enables dos NS8002 são ativos em LOW.
    constexpr uint8_t ESTADO_STANDBY = MASCARA_ENABLE_N_8002;

    static_assert(
        MASCARA_RELE_SELECIONA_DAC == 0x20 &&
        MASCARA_HABILITA_RADIO_FM == 0x10 &&
        MASCARA_ENABLE_N_8002 == 0xC0 &&
        ESTADO_STANDBY == 0xC0 &&
        (ESTADO_CAMINHO_RADIO_FM | MASCARA_HABILITA_RADIO_FM) == 0x10 &&
        ESTADO_RADIO_WEB_ATIVO == 0x20,
        "Estados MCP23017 devem ser FM=0x10, Web=0x20, standby=0xC0."
    );

    bool disponivel = false;
    bool estadoAplicado = false;
    bool dfPlayerComPrioridade = false;
    uint8_t ultimoEstado = ESTADO_STANDBY;

    static_assert(
        PIN_MCP23017_HABILITA_RADIO_FM <= 7 &&
        PIN_MCP23017_RELE_8002_SELECIONA_DAC <= 7 &&
        PIN_MCP23017_ENABLE_N_8002_DIREITO <= 7 &&
        PIN_MCP23017_ENABLE_N_8002_ESQUERDO <= 7,
        "Os pinos da porta A do MCP23017 devem estar entre 0 e 7."
    );
    static_assert(
        PIN_MCP23017_HABILITA_RADIO_FM !=
            PIN_MCP23017_RELE_8002_SELECIONA_DAC &&
        PIN_MCP23017_HABILITA_RADIO_FM !=
            PIN_MCP23017_ENABLE_N_8002_DIREITO &&
        PIN_MCP23017_HABILITA_RADIO_FM !=
            PIN_MCP23017_ENABLE_N_8002_ESQUERDO &&
        PIN_MCP23017_RELE_8002_SELECIONA_DAC !=
            PIN_MCP23017_ENABLE_N_8002_DIREITO &&
        PIN_MCP23017_RELE_8002_SELECIONA_DAC !=
            PIN_MCP23017_ENABLE_N_8002_ESQUERDO &&
        PIN_MCP23017_ENABLE_N_8002_DIREITO !=
            PIN_MCP23017_ENABLE_N_8002_ESQUERDO,
        "Cada funcao de audio precisa de um pino distinto do MCP23017."
    );

    bool escreverRegistro(uint8_t registro, uint8_t valor) {
        Wire.beginTransmission(ENDERECO_I2C_MCP23017);
        Wire.write(registro);
        Wire.write(valor);
        const uint8_t codigo = Wire.endTransmission(true);

        if (codigo != 0) {
            Serial.printf(
                "MCP23017: falha I2C ao escrever 0x%02X (codigo %u).\n",
                registro,
                static_cast<unsigned>(codigo)
            );
            return false;
        }

        return true;
    }

    bool lerRegistro(uint8_t registro, uint8_t& valor) {
        Wire.beginTransmission(ENDERECO_I2C_MCP23017);
        Wire.write(registro);
        if (Wire.endTransmission(false) != 0) {
            return false;
        }
        if (
            Wire.requestFrom(
                ENDERECO_I2C_MCP23017,
                static_cast<uint8_t>(1)
            ) != 1
        ) {
            return false;
        }
        valor = Wire.read();
        return true;
    }

    bool verificarSaidas(uint8_t valor) {
        uint8_t direcao = 0;
        uint8_t latch = 0;
        uint8_t pinos = 0;
        if (
            !lerRegistro(REG_IODIRA, direcao) ||
            !lerRegistro(REG_OLATA, latch) ||
            !lerRegistro(REG_GPIOA, pinos)
        ) {
            Serial.println("MCP23017: falha ao reler as saidas de audio.");
            return false;
        }

        const bool correto =
            (direcao & MASCARA_SAIDAS_CONTROLADAS) == 0 &&
            (latch & MASCARA_SAIDAS_CONTROLADAS) == valor &&
            (pinos & MASCARA_SAIDAS_CONTROLADAS) == valor;
        if (!correto) {
            Serial.printf(
                "MCP23017: estado incorreto; IODIRA=0x%02X "
                "OLATA=0x%02X GPIOA=0x%02X esperado=0x%02X.\n",
                direcao,
                latch,
                pinos,
                valor
            );
        }
        return correto;
    }

    bool configurarSaidas(uint8_t valor, const char* descricao) {
        if (!disponivel) {
            Serial.printf(
                "MCP23017 indisponivel; saidas sem comando (%s).\n",
                descricao
            );
            return false;
        }

        if (estadoAplicado && ultimoEstado == valor) {
            if (verificarSaidas(valor)) {
                return true;
            }

            // O estado lógico em RAM pode continuar correto mesmo que o
            // MCP23017 tenha reiniciado ou perdido o latch. Nesse caso não
            // basta diagnosticar: reaplicamos o mesmo valor para recuperar o
            // relé e os enables sem exigir reset ou corte de alimentação.
            Serial.println(
                "MCP23017 audio: estado divergente; reaplicando saidas."
            );
        }

        const uint8_t direcaoPortaA =
            static_cast<uint8_t>(~MASCARA_SAIDAS_CONTROLADAS);
        if (!escreverRegistro(REG_IODIRA, direcaoPortaA)) {
            Serial.printf(
                "MCP23017: falha ao recuperar direcao das saidas (%s).\n",
                descricao
            );
            return false;
        }

        if (!escreverRegistro(REG_OLATA, valor)) {
            Serial.printf(
                "MCP23017: falha ao aplicar saidas de audio (%s).\n",
                descricao
            );
            return false;
        }

        if (!verificarSaidas(valor)) {
            return false;
        }

        ultimoEstado = valor;
        estadoAplicado = true;

        Serial.printf(
            "MCP23017: %s (GPA7=%u, GPA6=%u, GPA5=%u, GPA4=%u).\n",
            descricao,
            (valor >> 7U) & 1U,
            (valor >> 6U) & 1U,
            (valor >> 5U) & 1U,
            (valor >> 4U) & 1U
        );
        return true;
    }

}

bool iniciarChaveamentoAudio() {
    disponivel = false;
    estadoAplicado = false;
    dfPlayerComPrioridade = false;

    // Normaliza BANK=0 mesmo quando apenas o ESP32 foi reiniciado e o
    // MCP23017 conservou uma configuração anterior em BANK=1.
    if (
        !escreverRegistro(REG_IOCON_BANK1, 0x00) ||
        !escreverRegistro(REG_IOCON_BANK0, 0x00)
    ) {
        Serial.println("MCP23017 indisponivel; falha ao selecionar BANK=0.");
        return false;
    }

    // Prepara o latch no estado de mute antes de os pinos virarem saídas:
    // GPA4=LOW desliga o TEF6686, GPA5=LOW seleciona FM e GPA7/GPA6=HIGH
    // silenciam os NS8002.
    if (!escreverRegistro(REG_OLATA, ESTADO_STANDBY)) {
        Serial.println("MCP23017 indisponivel; saidas de audio sem comando.");
        return false;
    }

    const uint8_t direcaoPortaA =
        static_cast<uint8_t>(~MASCARA_SAIDAS_CONTROLADAS);
    if (!escreverRegistro(REG_IODIRA, direcaoPortaA)) {
        Serial.println("MCP23017: falha ao configurar GPA7/GPA6/GPA5/GPA4.");
        return false;
    }

    disponivel = true;
    if (!verificarSaidas(ESTADO_STANDBY)) {
        disponivel = false;
        return false;
    }
    ultimoEstado = ESTADO_STANDBY;
    estadoAplicado = true;
    Serial.println(
        "MCP23017 standby: GPA7/GPA6=HIGH, GPA5=LOW, GPA4=LOW."
    );
    return true;
}

bool configurarHabilitacaoRadioFm(bool habilitar) {
    uint8_t estado = ultimoEstado;
    if (habilitar) {
        estado |= MASCARA_HABILITA_RADIO_FM;
    } else {
        estado &= static_cast<uint8_t>(~MASCARA_HABILITA_RADIO_FM);
    }
    return configurarSaidas(
        estado,
        habilitar ? "TEF6686 habilitado" : "TEF6686 desligado"
    );
}

void configurarSaidaAudioStandby() {
    if (dfPlayerComPrioridade) {
        if (DEBUG_CHIME) {
            Serial.println(
                "CHIME DEBUG chaveamento: standby ignorado durante DFPlayer."
            );
        }
        return;
    }
    const uint8_t estado = ESTADO_STANDBY |
        (ultimoEstado & MASCARA_HABILITA_RADIO_FM);
    configurarSaidas(estado, "standby");
}

bool configurarSaidaAudioDac(bool mutar) {
    if (dfPlayerComPrioridade) {
        if (DEBUG_CHIME) {
            Serial.printf(
                "CHIME DEBUG chaveamento: DAC/mute=%u ignorado durante "
                "DFPlayer.\n",
                mutar ? 1U : 0U
            );
        }
        return false;
    }
    const uint8_t estado = ESTADO_RADIO_WEB_ATIVO |
        (mutar ? MASCARA_ENABLE_N_8002 : 0x00);
    return configurarSaidas(
        estado,
        mutar ? "DAC selecionado; mute" : "DAC selecionado; NS8002 ativos"
    );
}

bool configurarSaidaAudioDfPlayer(bool mutar) {
    if (!mutar) {
        dfPlayerComPrioridade = true;
    }

    uint8_t estado = ultimoEstado;

    if (mutar) {
        estado |= MASCARA_ENABLE_N_8002;
    } else {
        estado &= static_cast<uint8_t>(~MASCARA_ENABLE_N_8002);
    }

    const bool aplicado = configurarSaidas(
        estado,
        mutar
            ? "DFPlayer direto; NS8002 em mute; GPA5/GPA4 preservados"
            : "DFPlayer direto; NS8002 ativos; GPA5/GPA4 preservados"
    );

    if (!aplicado && !mutar) {
        dfPlayerComPrioridade = false;
    }

    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG chaveamento DFPlayer: pedidoMute=%u "
            "GPA7=%u GPA6=%u confirmado=%u.\n",
            mutar ? 1U : 0U,
            (estado >> PIN_MCP23017_ENABLE_N_8002_ESQUERDO) & 1U,
            (estado >> PIN_MCP23017_ENABLE_N_8002_DIREITO) & 1U,
            aplicado ? 1U : 0U
        );
    }

    return aplicado;
}

EstadoSaidaAudio obterEstadoSaidaAudio() {
    EstadoSaidaAudio estado;
    estado.valido = disponivel && estadoAplicado;
    estado.valorPortaA = ultimoEstado & MASCARA_SAIDAS_CONTROLADAS;
    return estado;
}

void restaurarEstadoSaidaAudio(const EstadoSaidaAudio& estado) {
    dfPlayerComPrioridade = false;

    if (DEBUG_CHIME) {
        Serial.printf(
            "CHIME DEBUG chaveamento: restaurando valido=%u portaA=0x%02X.\n",
            estado.valido ? 1U : 0U,
            estado.valorPortaA
        );
    }

    if (!estado.valido) {
        configurarSaidaAudioStandby();
        return;
    }

    configurarSaidas(
        estado.valorPortaA & MASCARA_SAIDAS_CONTROLADAS,
        "estado anterior ao DFPlayer restaurado"
    );
}

bool configurarSaidaAudioFm(bool mutar) {
    if (dfPlayerComPrioridade) {
        if (DEBUG_CHIME) {
            Serial.printf(
                "CHIME DEBUG chaveamento: FM/mute=%u ignorado durante "
                "DFPlayer.\n",
                mutar ? 1U : 0U
            );
        }
        return false;
    }
    const uint8_t estado = ESTADO_CAMINHO_RADIO_FM |
        (ultimoEstado & MASCARA_HABILITA_RADIO_FM) |
        (mutar ? MASCARA_ENABLE_N_8002 : 0x00);
    return configurarSaidas(
        estado,
        mutar ? "FM selecionado; mute" : "FM selecionado; NS8002 ativos"
    );
}
