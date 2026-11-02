#include "sensor_clima.h"

#include <Wire.h>

namespace {

constexpr uint8_t ENDERECOS_SHT4X[] = {0x44, 0x45, 0x46};
constexpr uint8_t COMANDO_MEDICAO_ALTA_PRECISAO = 0xFD;
constexpr uint32_t TEMPO_MEDICAO_MS = 9;  // Máximo especificado: 8,3 ms.
constexpr uint32_t INTERVALO_MEDICOES_MS = 2000;
constexpr uint32_t INTERVALO_NOVA_DETECCAO_MS = 5000;

enum class FaseSensorClima : uint8_t {
    PROCURAR,
    AGUARDAR_MEDICAO,
    AGUARDAR_INTERVALO
};

portMUX_TYPE mutexLeitura = portMUX_INITIALIZER_UNLOCKED;
LeituraSensorClima leituraAtual;
FaseSensorClima fase = FaseSensorClima::PROCURAR;
uint8_t enderecoSensor = 0;
uint32_t momentoProximaAcaoMs = 0;

bool prazoAtingido(uint32_t agoraMs, uint32_t prazoMs) {
    return static_cast<int32_t>(agoraMs - prazoMs) >= 0;
}

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

bool enderecoResponde(uint8_t endereco) {
    Wire.beginTransmission(endereco);
    return Wire.endTransmission() == 0;
}

bool lerResposta(uint8_t* resposta) {
    const size_t recebidos = Wire.requestFrom(enderecoSensor, uint8_t{6});
    if (recebidos != 6 || Wire.available() != 6) {
        while (Wire.available() > 0) {
            Wire.read();
        }
        return false;
    }

    for (uint8_t i = 0; i < 6; ++i) {
        resposta[i] = static_cast<uint8_t>(Wire.read());
    }

    return
        calcularCrc(&resposta[0], 2) == resposta[2] &&
        calcularCrc(&resposta[3], 2) == resposta[5];
}

void publicarEstado(
    bool disponivel,
    bool valida,
    int16_t temperaturaDecimosC = 0,
    uint16_t umidadeDecimosRh = 0
) {
    portENTER_CRITICAL(&mutexLeitura);
    const bool mudou =
        leituraAtual.disponivel != disponivel ||
        leituraAtual.valida != valida ||
        (valida && (
            leituraAtual.temperaturaDecimosC != temperaturaDecimosC ||
            leituraAtual.umidadeDecimosRh != umidadeDecimosRh
        ));
    leituraAtual.disponivel = disponivel;
    leituraAtual.valida = valida;
    if (valida) {
        leituraAtual.temperaturaDecimosC = temperaturaDecimosC;
        leituraAtual.umidadeDecimosRh = umidadeDecimosRh;
    }
    if (mudou) {
        ++leituraAtual.revisao;
    }
    portEXIT_CRITICAL(&mutexLeitura);
}

void agendarNovaDeteccao(uint32_t agoraMs) {
    const bool estavaDisponivel = obterLeituraSensorClima().disponivel;
    enderecoSensor = 0;
    fase = FaseSensorClima::PROCURAR;
    momentoProximaAcaoMs = agoraMs + INTERVALO_NOVA_DETECCAO_MS;
    publicarEstado(false, false);
    if (estavaDisponivel) {
        Serial.println("SHT41 deixou de responder; nova deteccao agendada.");
    }
}

bool iniciarMedicao(uint32_t agoraMs) {
    if (!enviarComando(enderecoSensor, COMANDO_MEDICAO_ALTA_PRECISAO)) {
        agendarNovaDeteccao(agoraMs);
        return false;
    }
    fase = FaseSensorClima::AGUARDAR_MEDICAO;
    momentoProximaAcaoMs = agoraMs + TEMPO_MEDICAO_MS;
    return true;
}

}  // namespace

void iniciarSensorClima() {
    portENTER_CRITICAL(&mutexLeitura);
    leituraAtual = LeituraSensorClima{};
    portEXIT_CRITICAL(&mutexLeitura);
    enderecoSensor = 0;
    fase = FaseSensorClima::PROCURAR;
    momentoProximaAcaoMs = 0;
}

void processarSensorClima() {
    const uint32_t agoraMs = millis();
    if (!prazoAtingido(agoraMs, momentoProximaAcaoMs)) {
        return;
    }

    if (fase == FaseSensorClima::PROCURAR) {
        for (uint8_t endereco : ENDERECOS_SHT4X) {
            if (enderecoResponde(endereco)) {
                enderecoSensor = endereco;
                publicarEstado(true, false);
                Serial.printf("SHT41 detectado em 0x%02X.\n", enderecoSensor);
                iniciarMedicao(agoraMs);
                return;
            }
        }
        publicarEstado(false, false);
        momentoProximaAcaoMs = agoraMs + INTERVALO_NOVA_DETECCAO_MS;
        return;
    }

    if (fase == FaseSensorClima::AGUARDAR_INTERVALO) {
        iniciarMedicao(agoraMs);
        return;
    }

    uint8_t resposta[6] = {};
    if (!lerResposta(resposta)) {
        agendarNovaDeteccao(agoraMs);
        return;
    }

    const uint16_t temperaturaBruta =
        (static_cast<uint16_t>(resposta[0]) << 8) | resposta[1];
    const uint16_t umidadeBruta =
        (static_cast<uint16_t>(resposta[3]) << 8) | resposta[4];

    // Datasheet SHT4x: T [°C] = -45 + 175 * ticks / 65535.
    // A expressão abaixo preserva um decimal e arredonda apenas a divisão.
    const int32_t temperaturaDecimos =
        -450 +
        (1750L * static_cast<int32_t>(temperaturaBruta) + 32767L) / 65535L;
    int32_t umidadeDecimos =
        -60 +
        (1250L * static_cast<int32_t>(umidadeBruta) + 32767L) / 65535L;
    umidadeDecimos = constrain(umidadeDecimos, 0L, 1000L);

    publicarEstado(
        true,
        true,
        static_cast<int16_t>(temperaturaDecimos),
        static_cast<uint16_t>(umidadeDecimos)
    );
    fase = FaseSensorClima::AGUARDAR_INTERVALO;
    momentoProximaAcaoMs = agoraMs + INTERVALO_MEDICOES_MS;
}

LeituraSensorClima obterLeituraSensorClima() {
    LeituraSensorClima copia;
    portENTER_CRITICAL(&mutexLeitura);
    copia = leituraAtual;
    portEXIT_CRITICAL(&mutexLeitura);
    return copia;
}
