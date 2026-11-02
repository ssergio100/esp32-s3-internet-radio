#include <Arduino.h>
#include <Wire.h>

// Mesmo barramento I2C utilizado pelo projeto principal radio_web_1.
constexpr int PIN_I2C_SDA = 16;
constexpr int PIN_I2C_SCL = 17;
constexpr uint32_t FREQUENCIA_I2C_HZ = 100000;

constexpr uint8_t ENDERECOS_SHT4X[] = {0x44, 0x45, 0x46};
constexpr uint8_t COMANDO_MEDICAO_ALTA_PRECISAO = 0xFD;
constexpr uint8_t COMANDO_LER_NUMERO_SERIE = 0x89;
constexpr uint8_t COMANDO_SOFT_RESET = 0x94;

constexpr uint32_t INTERVALO_LEITURA_MS = 2000;
constexpr uint32_t TEMPO_MEDICAO_MS = 9;  // Máximo oficial: 8,3 ms.

uint8_t enderecoSht41 = 0;
uint32_t momentoUltimaTentativaMs = 0;

uint8_t calcularCrc(const uint8_t* dados, size_t quantidade) {
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < quantidade; ++i) {
        crc ^= dados[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) != 0
                ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                : static_cast<uint8_t>(crc << 1);
        }
    }

    return crc;
}

bool enviarComando(uint8_t endereco, uint8_t comando) {
    Wire.beginTransmission(endereco);
    Wire.write(comando);
    return Wire.endTransmission() == 0;
}

bool lerSeisBytes(uint8_t endereco, uint8_t* resposta) {
    const size_t recebidos = Wire.requestFrom(endereco, uint8_t{6});
    if (recebidos != 6 || Wire.available() != 6) {
        while (Wire.available() > 0) {
            Wire.read();
        }
        return false;
    }

    for (uint8_t i = 0; i < 6; ++i) {
        resposta[i] = static_cast<uint8_t>(Wire.read());
    }

    return true;
}

bool respostaComCrcValido(const uint8_t* resposta) {
    return
        calcularCrc(&resposta[0], 2) == resposta[2] &&
        calcularCrc(&resposta[3], 2) == resposta[5];
}

bool lerNumeroSerie(uint8_t endereco, uint32_t& numeroSerie) {
    if (!enviarComando(endereco, COMANDO_LER_NUMERO_SERIE)) {
        return false;
    }

    delay(2);

    uint8_t resposta[6] = {};
    if (!lerSeisBytes(endereco, resposta) || !respostaComCrcValido(resposta)) {
        return false;
    }

    numeroSerie =
        (static_cast<uint32_t>(resposta[0]) << 24) |
        (static_cast<uint32_t>(resposta[1]) << 16) |
        (static_cast<uint32_t>(resposta[3]) << 8) |
        static_cast<uint32_t>(resposta[4]);
    return true;
}

void listarDispositivosI2C() {
    Serial.println("Varredura I2C:");
    uint8_t encontrados = 0;

    for (uint8_t endereco = 1; endereco < 0x7F; ++endereco) {
        Wire.beginTransmission(endereco);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  dispositivo em 0x%02X\n", endereco);
            ++encontrados;
        }
    }

    if (encontrados == 0) {
        Serial.println("  nenhum dispositivo respondeu");
    }
}

bool detectarSht41() {
    for (uint8_t endereco : ENDERECOS_SHT4X) {
        uint32_t numeroSerie = 0;
        if (lerNumeroSerie(endereco, numeroSerie)) {
            enderecoSht41 = endereco;
            Serial.printf(
                "SHT4x identificado em 0x%02X; serie 0x%08lX.\n",
                enderecoSht41,
                static_cast<unsigned long>(numeroSerie)
            );
            return true;
        }
    }

    enderecoSht41 = 0;
    Serial.println("SHT41 nao identificado nos enderecos 0x44, 0x45 ou 0x46.");
    return false;
}

bool lerTemperaturaUmidade(float& temperaturaC, float& umidadeRh) {
    if (!enviarComando(enderecoSht41, COMANDO_MEDICAO_ALTA_PRECISAO)) {
        Serial.println("ERRO: SHT41 nao confirmou o comando de medicao.");
        return false;
    }

    delay(TEMPO_MEDICAO_MS);

    uint8_t resposta[6] = {};
    if (!lerSeisBytes(enderecoSht41, resposta)) {
        Serial.println("ERRO: resposta incompleta do SHT41.");
        return false;
    }

    if (!respostaComCrcValido(resposta)) {
        Serial.println("ERRO: CRC invalido na resposta do SHT41.");
        return false;
    }

    const uint16_t temperaturaBruta =
        (static_cast<uint16_t>(resposta[0]) << 8) | resposta[1];
    const uint16_t umidadeBruta =
        (static_cast<uint16_t>(resposta[3]) << 8) | resposta[4];

    temperaturaC =
        -45.0f + 175.0f * static_cast<float>(temperaturaBruta) / 65535.0f;
    umidadeRh =
        -6.0f + 125.0f * static_cast<float>(umidadeBruta) / 65535.0f;
    umidadeRh = constrain(umidadeRh, 0.0f, 100.0f);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("Teste isolado do sensor SHT41");
    Serial.printf(
        "I2C: SDA=GPIO%d, SCL=GPIO%d, frequencia=%lu Hz.\n",
        PIN_I2C_SDA,
        PIN_I2C_SCL,
        static_cast<unsigned long>(FREQUENCIA_I2C_HZ)
    );

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(FREQUENCIA_I2C_HZ);

    listarDispositivosI2C();

    // O reset afeta apenas o endereço que responder como SHT4x.
    if (detectarSht41()) {
        enviarComando(enderecoSht41, COMANDO_SOFT_RESET);
        delay(2);
        detectarSht41();
    }
}

void loop() {
    const uint32_t agoraMs = millis();
    if (agoraMs - momentoUltimaTentativaMs < INTERVALO_LEITURA_MS) {
        return;
    }
    momentoUltimaTentativaMs = agoraMs;

    if (enderecoSht41 == 0 && !detectarSht41()) {
        return;
    }

    float temperaturaC = 0.0f;
    float umidadeRh = 0.0f;
    if (!lerTemperaturaUmidade(temperaturaC, umidadeRh)) {
        enderecoSht41 = 0;
        return;
    }

    Serial.printf(
        "Temperatura: %.2f °C | Umidade: %.2f %%RH\n",
        temperaturaC,
        umidadeRh
    );
}
