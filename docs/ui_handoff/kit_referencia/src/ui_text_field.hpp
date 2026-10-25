#pragma once
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
// Exemplo de componente leve: so invalida o widget quando o conteudo mudou.
// Deve ser chamado APENAS da task de UI, nunca de ISR ou task de rede.
namespace radio_ui {
template <size_t N> class TextField {
    lv_obj_t* label_;
    char previous_[N];
public:
    explicit TextField(lv_obj_t* label) : label_(label), previous_{} {}
    void set(const char* value) {
        if (!value) value = "";
        char next[N];
        snprintf(next, sizeof(next), "%s", value);
        if (strcmp(previous_, next) == 0) return;
        snprintf(previous_, sizeof(previous_), "%s", next);
        // O LVGL copia o texto; so chamamos quando mudou.
        lv_label_set_text(label_, previous_);
    }
};
}
