#include "configuracoes_usuario.h"

#include <Preferences.h>

namespace {

constexpr const char* ESPACO_NVS = "radio_ui";
constexpr uint16_t REPOUSOS_VALIDOS[] = {0, 1, 2, 5, 10, 15, 30};

bool repousoValido(uint16_t minutos) {
    for (uint16_t valor : REPOUSOS_VALIDOS) {
        if (valor == minutos) {
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
    preferencias.end();
    return ok;
}
