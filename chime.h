#ifndef CHIME_H
#define CHIME_H

#include <Arduino.h>

struct ConfiguracaoChime {
    bool ativo = false;
    uint8_t som = 1;
    uint8_t volume = 1;
    uint8_t intervaloMinutos = 60;
    bool silencioNoturno = false;
    uint8_t inicioSilencioHora = 22;
    uint8_t fimSilencioHora = 7;
};

// Carrega a configuração global mantida no mesmo documento dos alarmes.
void carregarConfiguracaoChime();
ConfiguracaoChime obterConfiguracaoChime();
bool salvarConfiguracaoChime(const ConfiguracaoChime& configuracao);

// Entrega no máximo um disparo em cada janela configurada do relógio local.
bool verificarDisparoChime(ConfiguracaoChime& configuracao);

#endif
