#include "sono_profundo.h"

#include <Arduino.h>
#include <esp_err.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "configuracao.h"

void informarMotivoDespertar() {
    if (
        esp_sleep_get_wakeup_cause() ==
        ESP_SLEEP_WAKEUP_EXT0
    ) {
        Serial.println(
            "Inicializacao causada pelo botao do encoder."
        );
    }
}

bool configurarDespertarPeloBotaoEncoder() {
    gpio_num_t pinoBotao =
        static_cast<gpio_num_t>(PIN_ENCODER_SW);

    if (!rtc_gpio_is_valid_gpio(pinoBotao)) {
        Serial.println(
            "GPIO do botao nao aceita despertar RTC."
        );

        return false;
    }

    esp_err_t resultado =
        esp_sleep_enable_ext0_wakeup(
            pinoBotao,
            0
        );

    if (resultado != ESP_OK) {
        Serial.printf(
            "Falha ao configurar despertar: %s\n",
            esp_err_to_name(resultado)
        );

        return false;
    }

    // O botão conecta o pino ao GND. O pull-up do domínio RTC mantém
    // o nível inativo enquanto o ESP32-S3 estiver em deep sleep.
    resultado = rtc_gpio_pulldown_dis(pinoBotao);

    if (resultado == ESP_OK) {
        resultado = rtc_gpio_pullup_en(pinoBotao);
    }

    if (resultado != ESP_OK) {
        Serial.printf(
            "Falha ao preparar o botao RTC: %s\n",
            esp_err_to_name(resultado)
        );

        return false;
    }

    return true;
}

void entrarSonoProfundo() {
    Serial.println(
        "Entrando em sono profundo; pressione o encoder para acordar."
    );
    Serial.flush();

    esp_deep_sleep_start();
}
