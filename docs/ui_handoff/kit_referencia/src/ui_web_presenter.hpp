#pragma once
#include "ui_state.hpp"
#include "ui_text_field.hpp"
#include <stdio.h>
namespace radio_ui {
// Exemplo com labels ja criados na tela (LVGL v9), chamado da task de UI.
// Os labels permanecem vivos e sao reutilizados; nao recriar widgets a cada update.
struct WebPresenter {
    TextField<65> station;
    TextField<97> title;
    TextField<20> codec;
    TextField<20> bitrate;
    TextField<20> wifi;
    TextField<20> buffer;
    WebPresenter(lv_obj_t* a, lv_obj_t* b, lv_obj_t* c,
                 lv_obj_t* d, lv_obj_t* e, lv_obj_t* f)
        : station(a), title(b), codec(c), bitrate(d), wifi(e), buffer(f) {}
    void apply(const WebStatus& s) {
        station.set(s.station);
        if (s.browsing || s.connecting || !s.connected) {
            title.set("-"); codec.set("-"); bitrate.set("-");
            wifi.set("-"); buffer.set("-");
            return;
        }
        title.set(s.title);
        codec.set(s.codec);
        char buf[24];
        snprintf(buf, sizeof(buf), "%d kbps", s.bitrate_kbps); bitrate.set(buf);
        snprintf(buf, sizeof(buf), "%d dBm", s.wifi_rssi_dbm); wifi.set(buf);
        snprintf(buf, sizeof(buf), "%.1f s", s.buffer_ms / 1000.0); buffer.set(buf);
    }
};
}
