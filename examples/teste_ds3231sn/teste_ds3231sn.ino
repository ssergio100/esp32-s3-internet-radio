/*
 * Exemplo independente para testar um RTC DS3231SN no ESP32-S3.
 *
 * Ligacoes:
 *   DS3231SN VCC -> 3V3
 *   DS3231SN GND -> GND
 *   DS3231SN SDA -> GPIO9
 *   DS3231SN SCL -> GPIO10
 *
 * O RTC compartilha o barramento I2C com o OLED. O sketch usa somente a
 * porta serial e nao altera o display.
 */

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

// A Arduino IDE pode associar automaticamente este caminho a "Arduino Nano
// ESP32", apesar do sketch.yaml. Interrompa a compilacao antes que uma placa
// incorreta aplique outra pinagem ou outras opcoes de memoria.
#if !defined(ARDUINO_ESP32S3_DEV)
#error "Placa incorreta: selecione esp32 > ESP32S3 Dev Module na Arduino IDE."
#endif

constexpr int PIN_I2C_SDA = 9;
constexpr int PIN_I2C_SCL = 10;
constexpr uint32_t FREQUENCIA_I2C_HZ = 100000;
constexpr uint8_t ENDERECO_DS3231 = 0x68;
constexpr unsigned long INTERVALO_LEITURA_MS = 1000;

RTC_DS3231 rtc;

bool rtcDisponivel = false;
unsigned long momentoUltimaLeituraMs = 0;

bool dispositivoResponde(uint8_t endereco) {
    Wire.beginTransmission(endereco);
    return Wire.endTransmission() == 0;
}

void listarDispositivosI2C() {
    Serial.println("Dispositivos encontrados no barramento I2C:");
    int quantidade = 0;

    for (uint8_t endereco = 1; endereco < 127; endereco++) {
        if (!dispositivoResponde(endereco)) {
            continue;
        }

        Serial.printf("  - 0x%02X", endereco);

        if (endereco == ENDERECO_DS3231) {
            Serial.print(" (DS3231SN esperado)");
        } else if (endereco == 0x3C) {
            Serial.print(" (OLED esperado)");
        }

        Serial.println();
        quantidade++;
    }

    if (quantidade == 0) {
        Serial.println("  Nenhum dispositivo respondeu.");
    }
}

void mostrarAjuda() {
    Serial.println();
    Serial.println("Comandos:");
    Serial.println("  l - ler agora");
    Serial.println("  a - acertar o RTC com a data/hora de compilacao");
    Serial.println("  i - repetir a varredura do barramento I2C");
    Serial.println("  h - mostrar esta ajuda");
    Serial.println();
}

void mostrarLeitura() {
    if (!rtcDisponivel) {
        Serial.println("ERRO: o RTC nao esta disponivel.");
        return;
    }

    DateTime dataHora = rtc.now();

    if (!dataHora.isValid()) {
        Serial.println("ERRO: o RTC devolveu uma data/hora invalida.");
        return;
    }

    constexpr const char* NOMES_DIAS[] = {
        "domingo", "segunda", "terca", "quarta",
        "quinta", "sexta", "sabado"
    };

    Serial.printf(
        "%04u-%02u-%02u %02u:%02u:%02u | %-7s | %6.2f C | OSF=%u\n",
        static_cast<unsigned int>(dataHora.year()),
        static_cast<unsigned int>(dataHora.month()),
        static_cast<unsigned int>(dataHora.day()),
        static_cast<unsigned int>(dataHora.hour()),
        static_cast<unsigned int>(dataHora.minute()),
        static_cast<unsigned int>(dataHora.second()),
        NOMES_DIAS[dataHora.dayOfTheWeek()],
        rtc.getTemperature(),
        rtc.lostPower() ? 1 : 0
    );
}

void acertarComDataHoraCompilacao() {
    if (!rtcDisponivel) {
        Serial.println("ERRO: o RTC nao esta disponivel.");
        return;
    }

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    Serial.println("RTC acertado com a data/hora de compilacao.");
    Serial.println(
        "Observacao: o valor reflete o momento da compilacao, nao o envio do comando."
    );
    mostrarLeitura();
}

void tratarComando(char comando) {
    switch (comando) {
        case 'l':
        case 'L':
            mostrarLeitura();
            break;

        case 'a':
        case 'A':
            acertarComDataHoraCompilacao();
            break;

        case 'i':
        case 'I':
            listarDispositivosI2C();
            break;

        case 'h':
        case 'H':
        case '?':
            mostrarAjuda();
            break;

        case '\r':
        case '\n':
        case ' ':
        case '\t':
            break;

        default:
            Serial.printf("Comando desconhecido: '%c'\n", comando);
            mostrarAjuda();
            break;
    }
}

void setup() {
    Serial.begin(115200);

    unsigned long inicioEsperaSerialMs = millis();
    while (!Serial && millis() - inicioEsperaSerialMs < 2000) {
        delay(10);
    }

    Serial.println();
    Serial.println("=== Teste independente do DS3231SN ===");
    Serial.printf(
        "SDA=GPIO%d, SCL=GPIO%d, I2C=%lu Hz\n",
        PIN_I2C_SDA,
        PIN_I2C_SCL,
        static_cast<unsigned long>(FREQUENCIA_I2C_HZ)
    );

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(FREQUENCIA_I2C_HZ);

    listarDispositivosI2C();
    rtcDisponivel = rtc.begin(&Wire);

    if (!rtcDisponivel) {
        Serial.println();
        Serial.println("ERRO: o DS3231SN nao respondeu no endereco 0x68.");
        Serial.println("Confira VCC, GND, SDA e SCL.");
        mostrarAjuda();
        return;
    }

    if (rtc.lostPower()) {
        Serial.println();
        Serial.println("AVISO: OSF=1; a data/hora ainda nao e confiavel.");
        Serial.println("Envie 'a' para acertar usando o momento da compilacao.");
    }

    mostrarAjuda();
    mostrarLeitura();
    momentoUltimaLeituraMs = millis();
}

void loop() {
    while (Serial.available()) {
        tratarComando(Serial.read());
    }

    if (!rtcDisponivel) {
        delay(10);
        return;
    }

    unsigned long agoraMs = millis();

    if (agoraMs - momentoUltimaLeituraMs >= INTERVALO_LEITURA_MS) {
        momentoUltimaLeituraMs += INTERVALO_LEITURA_MS;
        mostrarLeitura();
    }
}
