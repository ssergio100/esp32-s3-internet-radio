#include "wifi_radio.h"
#include "configuracao.h"
#include "display_radio.h"
#include "indicador_led.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

namespace {

constexpr unsigned long INTERVALO_RECONEXAO_WIFI_MS =
    10000;

String ssidSalvo;
String senhaSalva;
bool selecaoPontoPermitidoNecessaria = false;
bool varreduraWifiEmAndamento = false;
unsigned long momentoUltimaVarreduraMs = 0;

bool bssidEstaBloqueado(const String& bssid) {
    for (const char* bssidBloqueado : BSSIDS_WIFI_BLOQUEADOS) {
        if (bssid.equalsIgnoreCase(bssidBloqueado)) {
            return true;
        }
    }

    return false;
}

void guardarCredenciaisAtuais() {
    String ssidAtual = WiFi.SSID();

    if (ssidAtual.isEmpty()) {
        return;
    }

    ssidSalvo = ssidAtual;
    senhaSalva = WiFi.psk();
}

bool rejeitarAssociacaoBloqueada() {
    if (
        WiFi.status() != WL_CONNECTED ||
        !bssidEstaBloqueado(WiFi.BSSIDstr())
    ) {
        return false;
    }

    guardarCredenciaisAtuais();

    Serial.println();
    Serial.print("BSSID bloqueado rejeitado: ");
    Serial.println(WiFi.BSSIDstr());

    WiFi.disconnect(false, false);
    selecaoPontoPermitidoNecessaria = true;
    momentoUltimaVarreduraMs =
        millis() - INTERVALO_RECONEXAO_WIFI_MS;

    return true;
}

void conectarAoMelhorPontoPermitido(int quantidadeRedes) {
    int melhorIndice = -1;
    int32_t melhorRssi = INT32_MIN;

    for (int indice = 0; indice < quantidadeRedes; indice++) {
        if (
            WiFi.SSID(indice) != ssidSalvo ||
            bssidEstaBloqueado(WiFi.BSSIDstr(indice)) ||
            WiFi.RSSI(indice) <= melhorRssi
        ) {
            continue;
        }

        melhorIndice = indice;
        melhorRssi = WiFi.RSSI(indice);
    }

    if (melhorIndice < 0) {
        Serial.println(
            "Nenhum ponto de acesso permitido encontrado."
        );
        WiFi.scanDelete();
        return;
    }

    uint8_t bssidEscolhido[6];
    WiFi.BSSID(melhorIndice, bssidEscolhido);
    int32_t canalEscolhido = WiFi.channel(melhorIndice);
    String textoBssidEscolhido = WiFi.BSSIDstr(melhorIndice);

    WiFi.scanDelete();

    Serial.print("Conectando ao BSSID permitido: ");
    Serial.println(textoBssidEscolhido);

    // O BSSID escolhido vale somente para esta tentativa. As credenciais
    // persistidas pelo WiFiManager continuam livres para uso em outro local.
    WiFi.persistent(false);
    WiFi.begin(
        ssidSalvo.c_str(),
        senhaSalva.c_str(),
        canalEscolhido,
        bssidEscolhido
    );
}

void processarSelecaoPontoPermitido() {
    if (
        !selecaoPontoPermitidoNecessaria ||
        ssidSalvo.isEmpty() ||
        WiFi.status() == WL_CONNECTED
    ) {
        return;
    }

    if (varreduraWifiEmAndamento) {
        int resultado = WiFi.scanComplete();

        if (resultado == WIFI_SCAN_RUNNING) {
            return;
        }

        varreduraWifiEmAndamento = false;
        momentoUltimaVarreduraMs = millis();

        if (resultado >= 0) {
            conectarAoMelhorPontoPermitido(resultado);
        } else {
            Serial.println("Falha ao procurar pontos de acesso.");
            WiFi.scanDelete();
        }

        return;
    }

    unsigned long agoraMs = millis();

    if (
        agoraMs - momentoUltimaVarreduraMs <
        INTERVALO_RECONEXAO_WIFI_MS
    ) {
        return;
    }

    momentoUltimaVarreduraMs = agoraMs;
    Serial.println("Procurando ponto de acesso permitido...");

    int resultado = WiFi.scanNetworks(
        true,
        false,
        false,
        300,
        0,
        ssidSalvo.c_str()
    );

    if (resultado == WIFI_SCAN_RUNNING) {
        varreduraWifiEmAndamento = true;
    } else if (resultado >= 0) {
        conectarAoMelhorPontoPermitido(resultado);
    } else {
        Serial.println("Não foi possível iniciar a varredura Wi-Fi.");
    }
}

}

void supervisionarWifi() {
    static wl_status_t estadoAnterior =
        WL_CONNECTED;

    wl_status_t estadoAtual = WiFi.status();

    if (rejeitarAssociacaoBloqueada()) {
        estadoAtual = WiFi.status();
    }

    if (estadoAtual == WL_CONNECTED) {
        guardarCredenciaisAtuais();
        selecaoPontoPermitidoNecessaria = false;

        if (estadoAnterior != WL_CONNECTED) {
            Serial.println();
            Serial.println("Wi-Fi reconectado.");
            Serial.print("BSSID: ");
            Serial.println(WiFi.BSSIDstr());
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
        }

        estadoAnterior = estadoAtual;
        return;
    }

    if (estadoAnterior == WL_CONNECTED) {
        Serial.println();
        Serial.println("Wi-Fi desconectado.");
        selecaoPontoPermitidoNecessaria = true;
        momentoUltimaVarreduraMs =
            millis() - INTERVALO_RECONEXAO_WIFI_MS;
    }

    estadoAnterior = estadoAtual;
    processarSelecaoPontoPermitido();
}

void conectarWifi() {
    WiFi.mode(WIFI_STA);

    mostrarMensagem("Conectando Wi-Fi");

    Serial.println();
    Serial.println("Conectando ao Wi-Fi...");

    WiFiManager wifiManager;

    wifiManager.setConnectTimeout(20);

    wifiManager.setAPCallback([](WiFiManager *gerenciador) {
        Serial.println();
        Serial.println("Portal de configuração iniciado.");

        Serial.print("Rede: ");
        Serial.println(
            gerenciador->getConfigPortalSSID()
        );

        Serial.println(
            "Endereço: http://192.168.4.1"
        );

       mostrarConfiguracaoWifi();
    });

    wifiManager.setCustomHeadElement(
        "<script>"
        "document.addEventListener('DOMContentLoaded',function(){"
            "document.querySelectorAll('.msg').forEach(function(el){"
                "if(el.textContent.trim()==='No AP set'){"
                    "el.remove();"
                "}"
            "});"
        "});"
        "</script>"
    );

    // O portal permanece disponível até receber
    // credenciais válidas. Isso permite levar o rádio
    // para um local com outra rede sem regravar o firmware.
    wifiManager.setConfigPortalBlocking(false);

    bool conectado = wifiManager.autoConnect(
        "RADIO-WEB"
    );

    WiFi.setAutoReconnect(false);

    if (conectado && rejeitarAssociacaoBloqueada()) {
        conectado = false;

        // O WiFiManager considerou as credenciais válidas antes de o filtro
        // rejeitar o ponto. Reabre o portal para não prender o rádio nessa rede.
        wifiManager.startConfigPortal("RADIO-WEB");
    }

    while (!conectado) {
        wifiManager.process();
        atualizarIndicadorConexaoWifi();

        if (WiFi.status() == WL_CONNECTED) {
            conectado = !rejeitarAssociacaoBloqueada();

            if (conectado) {
                guardarCredenciaisAtuais();
                selecaoPontoPermitidoNecessaria = false;
            }
        }

        processarSelecaoPontoPermitido();

        delay(10);
    }

    guardarCredenciaisAtuais();

    apagarIndicadorLed();

    WiFi.setSleep(true);

    Serial.println();
    Serial.println("Wi-Fi conectado.");

    Serial.print("Rede: ");
    Serial.println(WiFi.SSID());

    Serial.print("Sinal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("BSSID: ");
    Serial.println(WiFi.BSSIDstr());

    Serial.print("Canal: ");
    Serial.println(WiFi.channel());

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    mostrarMensagem("Wi-Fi conectado");

    delay(500);
}
