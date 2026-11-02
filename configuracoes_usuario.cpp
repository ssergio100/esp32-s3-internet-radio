#include "configuracoes_usuario.h"

#include <Preferences.h>

namespace {

constexpr const char* ESPACO_NVS = "radio_ui";
constexpr uint16_t REPOUSOS_VALIDOS[] = {0, 1, 2, 5, 10, 15, 30};
constexpr uint8_t ATENUACAO_MAXIMA_DB = 30;
constexpr uint16_t LARGURAS_FM_VALIDAS[] = {
    0, 56, 64, 72, 84, 97, 114, 133, 151,
    168, 184, 200, 217, 236, 254, 287, 311
};

uint8_t limitarAtenuacao(uint8_t atenuacaoDb) {
    return min(atenuacaoDb, ATENUACAO_MAXIMA_DB);
}

bool repousoValido(uint16_t minutos) {
    for (uint16_t valor : REPOUSOS_VALIDOS) {
        if (valor == minutos) {
            return true;
        }
    }
    return false;
}

bool larguraFmValida(uint16_t larguraKHz) {
    for (uint16_t valor : LARGURAS_FM_VALIDAS) {
        if (valor == larguraKHz) {
            return true;
        }
    }
    return false;
}

}

ConfiguracoesUsuario carregarConfiguracoesUsuario() {
    ConfiguracoesUsuario configuracoes;
    Preferences preferencias;
    if (!preferencias.begin(ESPACO_NVS, true)) {
        return configuracoes;
    }

    configuracoes.sincronizacaoAutomatica =
        preferencias.getBool("ntp", true);
    uint16_t repouso = preferencias.getUShort("repouso", 0);
    configuracoes.repousoTftMinutos =
        repousoValido(repouso) ? repouso : 0;

    uint8_t tema = preferencias.getUChar(
        "tema",
        static_cast<uint8_t>(TemaUiTft::LARANJA)
    );
    if (tema <= static_cast<uint8_t>(TemaUiTft::APENAS_LARANJA)) {
        configuracoes.temaTft = static_cast<TemaUiTft>(tema);
    }

    configuracoes.atenuacaoFmDb = limitarAtenuacao(
        preferencias.getUChar("att_fm", 0)
    );
    configuracoes.atenuacaoRadioWebDb = limitarAtenuacao(
        preferencias.getUChar("att_web", 0)
    );
    configuracoes.atenuacaoDfPlayerDb = limitarAtenuacao(
        preferencias.getUChar("att_df", 0)
    );

    configuracoes.radioFm.sintoniaPorScanner =
        preferencias.getBool("fm_scan", true);
    uint16_t larguraFm = preferencias.getUShort("fm_bw", 0);
    configuracoes.radioFm.larguraBandaKHz =
        larguraFmValida(larguraFm) ? larguraFm : 0;
    configuracoes.radioFm.stereoAutomatico =
        preferencias.getBool("fm_st", true);
    configuracoes.radioFm.multipath =
        preferencias.getBool("fm_mph", true);
    configuracoes.radioFm.noiseBlanker =
        preferencias.getBool("fm_nb", true);
    configuracoes.radioFm.softmute =
        preferencias.getBool("fm_sm", true);
    configuracoes.radioFm.highcut =
        preferencias.getBool("fm_hc", true);
    uint8_t passo = preferencias.getUChar("fm_step", 10);
    configuracoes.radioFm.passoSintonia10kHz =
        passo == 5 || passo == 10 || passo == 20 ? passo : 10;
    configuracoes.radioFm.rds = preferencias.getBool("fm_rds", true);
    configuracoes.radioFm.channelEqualizer =
        preferencias.getBool("fm_ceq", true);
    uint16_t deemphasis = preferencias.getUShort("fm_de", 50);
    configuracoes.radioFm.deemphasisUs = deemphasis == 75 ? 75 : 50;
    configuracoes.radioFm.rfAgcDecimosDbuv = constrain(
        preferencias.getUShort("fm_agc", 920),
        static_cast<uint16_t>(840),
        static_cast<uint16_t>(920)
    );
    configuracoes.radioFm.stereoBlend =
        preferencias.getBool("fm_sblend", true);
    configuracoes.radioFm.stereoHighBlend =
        preferencias.getBool("fm_hblend", true);
    configuracoes.radioFm.levelOffsetDecimosDb = constrain(
        preferencias.getShort("fm_lo", 0),
        static_cast<int16_t>(-480),
        static_cast<int16_t>(150)
    );
    uint8_t opcoesLargura = preferencias.getUChar("fm_bwopt", 1);
    configuracoes.radioFm.bandwidthOptions =
        opcoesLargura >= 1 && opcoesLargura <= 3
            ? opcoesLargura
            : 1;

    preferencias.end();
    return configuracoes;
}

bool salvarConfiguracoesUsuario(
    const ConfiguracoesUsuario& configuracoes
) {
    Preferences preferencias;
    if (!preferencias.begin(ESPACO_NVS, false)) {
        return false;
    }

    bool ok = true;
    ok = preferencias.putBool(
        "ntp",
        configuracoes.sincronizacaoAutomatica
    ) == sizeof(uint8_t) && ok;
    ok = preferencias.putUShort(
        "repouso",
        configuracoes.repousoTftMinutos
    ) == sizeof(uint16_t) && ok;
    ok = preferencias.putUChar(
        "tema",
        static_cast<uint8_t>(configuracoes.temaTft)
    ) == sizeof(uint8_t) && ok;
    ok = preferencias.putUChar(
        "att_fm",
        limitarAtenuacao(configuracoes.atenuacaoFmDb)
    ) == sizeof(uint8_t) && ok;
    ok = preferencias.putUChar(
        "att_web",
        limitarAtenuacao(configuracoes.atenuacaoRadioWebDb)
    ) == sizeof(uint8_t) && ok;
    ok = preferencias.putUChar(
        "att_df",
        limitarAtenuacao(configuracoes.atenuacaoDfPlayerDb)
    ) == sizeof(uint8_t) && ok;
    const AjustesRecepcaoRadioFm& fm = configuracoes.radioFm;
    ok = preferencias.putBool("fm_scan", fm.sintoniaPorScanner) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putUShort("fm_bw", fm.larguraBandaKHz) ==
        sizeof(uint16_t) && ok;
    ok = preferencias.putBool("fm_st", fm.stereoAutomatico) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putBool("fm_mph", fm.multipath) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putBool("fm_nb", fm.noiseBlanker) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putBool("fm_sm", fm.softmute) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putBool("fm_hc", fm.highcut) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putUChar("fm_step", fm.passoSintonia10kHz) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putBool("fm_rds", fm.rds) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putBool("fm_ceq", fm.channelEqualizer) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putUShort("fm_de", fm.deemphasisUs) ==
        sizeof(uint16_t) && ok;
    ok = preferencias.putUShort("fm_agc", fm.rfAgcDecimosDbuv) ==
        sizeof(uint16_t) && ok;
    ok = preferencias.putBool("fm_sblend", fm.stereoBlend) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putBool("fm_hblend", fm.stereoHighBlend) ==
        sizeof(uint8_t) && ok;
    ok = preferencias.putShort("fm_lo", fm.levelOffsetDecimosDb) ==
        sizeof(int16_t) && ok;
    ok = preferencias.putUChar("fm_bwopt", fm.bandwidthOptions) ==
        sizeof(uint8_t) && ok;
    preferencias.end();
    return ok;
}
