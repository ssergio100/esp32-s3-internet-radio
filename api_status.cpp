#include "api_status.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "alarmes.h"
#include "audio_radio.h"

namespace {

    void adicionarStatusAudio(
        JsonDocument& documento
    ) {
        StatusAudio statusAudio = obterStatusAudio();

        documento["estado"] =
            obterTextoEstadoAudio(statusAudio.estado);
        documento["radio"] = statusAudio.radio;
        documento["titulo"] = statusAudio.titulo;
        documento["codec"] = statusAudio.codec;
        documento["bitrate"] = statusAudio.bitrate;
        documento["bufferBytes"] =
            statusAudio.bufferBytes;
        documento["bufferTotalBytes"] =
            statusAudio.bufferTotalBytes;
        documento["bufferMilissegundos"] =
            statusAudio.bufferMilissegundos;
        documento["eventosFluxoLento"] =
            statusAudio.eventosFluxoLento;
        documento["tentativasReconexao"] =
            statusAudio.tentativasReconexao;
        documento["stackMinimoBytes"] =
            statusAudio.stackMinimoBytes;
        documento["ultimoErro"] =
            statusAudio.ultimoErro;
        documento["alarmeAtivo"] =
            statusAudio.alarmeAtivo;
        documento["arquivoAlarmeSolicitado"] =
            statusAudio.arquivoAlarmeSolicitado;
        documento["arquivoAlarmeDisponivel"] =
            statusAudio.arquivoAlarmeDisponivel;
        documento["somPadraoAlarme"] =
            statusAudio.somPadraoAlarme;
        documento["radioAlarme"] =
            statusAudio.radioAlarme;
    }

    void adicionarStatusAlarmes(JsonDocument& documento) {
        StatusAlarmes status = obterStatusAlarmes();
        documento["quantidadeAlarmes"] = status.quantidade;
        documento["quantidadeAlarmesAtivos"] =
            status.quantidadeAtivos;
        documento["somPadraoAlarmeDisponivel"] =
            status.somPadraoDisponivel;
    }

    void adicionarStatusWifi(
        JsonDocument& documento
    ) {
        documento["wifiConectado"] =
            WiFi.status() == WL_CONNECTED;
        documento["rssi"] = WiFi.RSSI();
        documento["bssid"] = WiFi.BSSIDstr();
        documento["canalWifi"] = WiFi.channel();
    }

    void adicionarStatusMemoriaEUptime(
        JsonDocument& documento
    ) {
        documento["ramLivre"] = ESP.getFreeHeap();
        documento["maiorBlocoRam"] =
            ESP.getMaxAllocHeap();
        documento["psramLivre"] = ESP.getFreePsram();
        documento["maiorBlocoPsram"] =
            ESP.getMaxAllocPsram();
        documento["uptimeMs"] = millis();
    }

}

String criarStatusSistemaJson() {
    JsonDocument documento;

    adicionarStatusAudio(documento);
    adicionarStatusAlarmes(documento);
    adicionarStatusWifi(documento);
    adicionarStatusMemoriaEUptime(documento);

    String respostaJson;
    serializeJson(documento, respostaJson);

    return respostaJson;
}
