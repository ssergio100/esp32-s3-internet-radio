#pragma once
#include <stdint.h>
// A UI recebe um snapshot coerente. As tasks dos radios NUNCA desenham na tela.
namespace radio_ui {
enum class Screen : uint8_t { CLOCK, ROOT, FM, WEB, ALARMS, CHIMES, SETTINGS };
struct FmStatus {
    uint32_t frequency_khz = 89100;
    int16_t signal_dbuv = 0;
    bool stereo = false;
    bool dx = false;
    char rds_ps[17] = "";
    char rds_text[65] = "";
};
struct WebStatus {
    char station[65] = "";
    char title[97] = "";
    char codec[13] = "";
    int bitrate_kbps = 0;
    int wifi_rssi_dbm = 0;
    int buffer_ms = 0;
    bool connected = false;
    bool connecting = false;
    bool browsing = false; // True: so muda o nome, detalhes sao tracos
};
struct UiState {
    Screen screen = Screen::CLOCK;
    bool tft_on = false;
    uint8_t nixie_brightness = 7; // 0..10
    uint8_t theme = 0;
    FmStatus fm;
    WebStatus web;
};
}
