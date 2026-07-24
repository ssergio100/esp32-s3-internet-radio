#include "radios.h"

namespace {

const Radio radios[] = {
    {
        "Groove Salad",
        "https://ice5.somafm.com/groovesalad-128-mp3"
    },
    {
        "Beat Blender",
        "https://ice5.somafm.com/beatblender-128-mp3"
    },
    {
        "Drone Zone",
        "https://ice5.somafm.com/dronezone-128-mp3"
    },
    {
        "Antena 1",
        "https://antenaone.crossradio.com.br/stream/1"
    },
    {
        "Saudade FM",
        "https://playerservices.streamtheworld.com/api/livestream-redirect/SAUDADE_FMAAC.aac"
    }
};

constexpr int QUANTIDADE_RADIOS =
    sizeof(radios) / sizeof(radios[0]);

}

int obterQuantidadeRadios() {
    return QUANTIDADE_RADIOS;
}

const Radio* obterRadio(int indice) {
    if (
        indice < 0 ||
        indice >= QUANTIDADE_RADIOS
    ) {
        return nullptr;
    }

    return &radios[indice];
}