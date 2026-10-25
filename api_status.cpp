#include "api_status.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "alarmes.h"
#include "audio_radio.h"
#include "chime.h"
#include "dfplayer.h"

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
        documento["arquivoAlarmeSolicitado"] = false;
        documento["arquivoAlarmeDisponivel"] = false;
        documento["somPadraoAlarme"] =
            statusAudio.somPadraoAlarme;
        documento["radioAlarme"] =
            statusAudio.radioAlarme;
        documento["radioFmAlarme"] =
            statusAudio.radioFmAlarme;
    }

    void adicionarStatusAlarmes(JsonDocument& documento) {
        StatusAlarmes status = obterStatusAlarmes();
        documento["quantidadeAlarmes"] = status.quantidade;
        documento["quantidadeAlarmesAtivos"] =
            status.quantidadeAtivos;
        documento["somPadraoAlarmeDisponivel"] =
            status.somPadraoDisponivel;
    }

    void adicionarStatusDfPlayer(JsonDocument& documento) {
        EstadoDfPlayer estado = obterEstadoDfPlayer();
        CatalogoSonsDfPlayer catalogo = obterCatalogoSonsDfPlayer();

        documento["dfPlayerDisponivel"] = catalogo.disponivel;
        documento["dfPlayerQuantidadeSons"] = catalogo.quantidade;
        documento["dfPlayerFase"] = obterTextoFaseDfPlayer(estado.fase);
        documento["dfPlayerSomAtual"] = estado.somAtual;
        documento["dfPlayerVolumeEquipamento"] =
            estado.volumeEquipamento;
        documento["dfPlayerVolumeModulo"] = estado.volumeModulo;
        documento["dfPlayerAvisosComunicacao"] =
            estado.avisosComunicacao;
        documento["dfPlayerUso"] =
            static_cast<uint8_t>(estado.uso);
    }

    void adicionarStatusChime(JsonDocument& documento) {
        ConfiguracaoChime configuracao = obterConfiguracaoChime();
        EstadoDfPlayer estado = obterEstadoDfPlayer();

        documento["chimeAtivo"] = configuracao.ativo;
        documento["chimeSom"] = configuracao.som;
        documento["chimeVolume"] = configuracao.volume;
        documento["chimeIntervaloMinutos"] =
            configuracao.intervaloMinutos;
        documento["chimeExecutando"] =
            estado.uso == UsoDfPlayer::CHIME &&
            (
                estado.fase == FaseDfPlayer::INICIANDO ||
                estado.fase == FaseDfPlayer::TOCANDO
            );
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
    adicionarStatusDfPlayer(documento);
    adicionarStatusChime(documento);
    adicionarStatusWifi(documento);
    adicionarStatusMemoriaEUptime(documento);

    String respostaJson;
    serializeJson(documento, respostaJson);

    return respostaJson;
}
