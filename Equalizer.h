#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Equalizer - equalizador digital simples de 3 bandas
 *
 * Pensado para ESP32/Arduino, mas sem dependencia do framework Arduino.
 *
 * Bandas:
 *   - Graves:  low-shelf
 *   - Medios:  peaking EQ
 *   - Agudos:  high-shelf
 *
 * Ganhos em dB. Exemplo:
 *   eq.setTone(0.0f, 4.0f, 2.0f); // medios, graves, agudos
 *
 * O processamento atual trabalha com PCM signed 16-bit.
 */
class Equalizer {
public:
    struct Tone {
        float midsDb;
        float bassDb;
        float trebleDb;
    };

    explicit Equalizer(float sampleRate = 44100.0f);

    // Forma conveniente para, futuramente, implementar presets:
    // eq.setTone(preset.midsDb, preset.bassDb, preset.trebleDb);
    void setTone(float midsDb, float bassDb, float trebleDb);
    void setTone(const Tone& tone);

    // Controle simplificado de tonalidade:
    // -10 = mais graves
    //   0 = flat
    // +10 = mais agudos
    void setTilt(int value);

    Tone tone() const;

    void setSampleRate(float sampleRate);
    float sampleRate() const;

    // Frequencias centrais/de transicao. Podem ser ajustadas se necessario.
    void setBandFrequencies(float bassHz, float midsHz, float trebleHz);
    void setMidQ(float q);

    void setEnabled(bool enabled);
    bool enabled() const;

    // Limpa o estado interno dos filtros.
    void reset();

    // Processa PCM mono: samples[0], samples[1], ...
    void processMono(int16_t* samples, size_t sampleCount);

    // Processa PCM stereo intercalado:
    // L0, R0, L1, R1, ...
    // frameCount = quantidade de pares L/R.
    void processStereo(int16_t* samples, size_t frameCount);

    // Processa uma unica amostra float normalizada (-1.0 .. +1.0).
    // channel: 0 = esquerdo/mono, 1 = direito.
    float processSample(float sample, uint8_t channel = 0);

private:
    struct Biquad {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;

        float z1[2] = {0.0f, 0.0f};
        float z2[2] = {0.0f, 0.0f};

        float process(float x, uint8_t channel);
        void clear();
    };

    float sampleRate_;
    float bassHz_;
    float midsHz_;
    float trebleHz_;
    float midQ_;

    Tone tone_;
    bool enabled_;

    // Pre-gain automatico para reservar headroom quando houver boost.
    float preGain_;

    Biquad bass_;
    Biquad mids_;
    Biquad treble_;

    void updateFilters();
    void configureLowShelf(Biquad& filter, float frequency, float gainDb);
    void configurePeaking(Biquad& filter, float frequency, float q, float gainDb);
    void configureHighShelf(Biquad& filter, float frequency, float gainDb);

    static float clampDb(float value);
    static float clampFloat(float value, float minValue, float maxValue);
    static int16_t floatToInt16(float sample);
};