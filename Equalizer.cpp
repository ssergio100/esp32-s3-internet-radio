#include "Equalizer.h"

#include <math.h>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMaxToneDb = 12.0f;
constexpr float kMinToneDb = -12.0f;
constexpr float kHeadroomDb = 1.0f;
}

Equalizer::Equalizer(float sampleRate)
    : sampleRate_(sampleRate > 1000.0f ? sampleRate : 44100.0f),
      bassHz_(120.0f),
      midsHz_(1000.0f),
      trebleHz_(8000.0f),
      midQ_(0.9f),
      tone_{0.0f, 0.0f, 0.0f},
      enabled_(true),
      preGain_(1.0f) {
    updateFilters();
}

void Equalizer::setTone(float midsDb, float bassDb, float trebleDb) {
    tone_.midsDb = clampDb(midsDb);
    tone_.bassDb = clampDb(bassDb);
    tone_.trebleDb = clampDb(trebleDb);
    updateFilters();
}

void Equalizer::setTone(const Tone& tone) {
    setTone(tone.midsDb, tone.bassDb, tone.trebleDb);
}

void Equalizer::setTilt(int value) {

    // int tonalidade = 0;
    // eq.setTilt(tonalidade);

    // eq.setTilt(-10);  // graves +6 dB / agudos -6 dB
    // eq.setTilt(-5);   // graves +3 dB / agudos -3 dB
    // eq.setTilt(0);    // flat
    // eq.setTilt(5);    // graves -3 dB / agudos +3 dB
    // eq.setTilt(10);   // graves -6 dB / agudos +6 dB


    if (value < -10) {
        value = -10;
    }

    if (value > 10) {
        value = 10;
    }

    // Cada passo corresponde a 0,6 dB.
    // Faixa total: -6 dB a +6 dB.
    const float amount = static_cast<float>(value) * 0.6f;

    setTone(
        0.0f,       // médios permanecem neutros
        -amount,    // graves
        amount      // agudos
    );
}

Equalizer::Tone Equalizer::tone() const {
    return tone_;
}

void Equalizer::setSampleRate(float sampleRate) {
    if (sampleRate < 1000.0f) {
        return;
    }

    sampleRate_ = sampleRate;
    updateFilters();
    reset();
}

float Equalizer::sampleRate() const {
    return sampleRate_;
}

void Equalizer::setBandFrequencies(float bassHz, float midsHz, float trebleHz) {
    const float maxFrequency = sampleRate_ * 0.45f;

    bassHz_ = clampFloat(bassHz, 20.0f, maxFrequency);
    midsHz_ = clampFloat(midsHz, 20.0f, maxFrequency);
    trebleHz_ = clampFloat(trebleHz, 20.0f, maxFrequency);

    updateFilters();
    reset();
}

void Equalizer::setMidQ(float q) {
    midQ_ = clampFloat(q, 0.1f, 10.0f);
    updateFilters();
    reset();
}

void Equalizer::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool Equalizer::enabled() const {
    return enabled_;
}

void Equalizer::reset() {
    bass_.clear();
    mids_.clear();
    treble_.clear();
}

void Equalizer::processMono(int16_t* samples, size_t sampleCount) {
    if (samples == nullptr || !enabled_) {
        return;
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        const float input = static_cast<float>(samples[i]) / 32768.0f;
        samples[i] = floatToInt16(processSample(input, 0));
    }
}

void Equalizer::processStereo(int16_t* samples, size_t frameCount) {
    if (samples == nullptr || !enabled_) {
        return;
    }

    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t leftIndex = frame * 2;
        const size_t rightIndex = leftIndex + 1;

        const float left = static_cast<float>(samples[leftIndex]) / 32768.0f;
        const float right = static_cast<float>(samples[rightIndex]) / 32768.0f;

        samples[leftIndex] = floatToInt16(processSample(left, 0));
        samples[rightIndex] = floatToInt16(processSample(right, 1));
    }
}

float Equalizer::processSample(float sample, uint8_t channel) {
    if (!enabled_) {
        return sample;
    }

    if (channel > 1) {
        channel = 0;
    }

    float value = sample * preGain_;
    value = bass_.process(value, channel);
    value = mids_.process(value, channel);
    value = treble_.process(value, channel);

    // Protecao final. O pre-gain reduz bastante a chance de clipping,
    // mas boosts de bandas sobrepostas ainda podem somar.
    return clampFloat(value, -1.0f, 0.999969f);
}

float Equalizer::Biquad::process(float x, uint8_t channel) {
    const float y = b0 * x + z1[channel];
    z1[channel] = b1 * x - a1 * y + z2[channel];
    z2[channel] = b2 * x - a2 * y;
    return y;
}

void Equalizer::Biquad::clear() {
    z1[0] = z1[1] = 0.0f;
    z2[0] = z2[1] = 0.0f;
}

void Equalizer::updateFilters() {
    configureLowShelf(bass_, bassHz_, tone_.bassDb);
    configurePeaking(mids_, midsHz_, midQ_, tone_.midsDb);
    configureHighShelf(treble_, trebleHz_, tone_.trebleDb);

    // Reserva headroom automaticamente quando alguma banda recebe boost.
    float maxBoost = tone_.bassDb;
    if (tone_.midsDb > maxBoost) {
        maxBoost = tone_.midsDb;
    }
    if (tone_.trebleDb > maxBoost) {
        maxBoost = tone_.trebleDb;
    }

    if (maxBoost > 0.0f) {
        preGain_ = powf(10.0f, -(maxBoost + kHeadroomDb) / 20.0f);
    } else {
        preGain_ = 1.0f;
    }
}

void Equalizer::configureLowShelf(Biquad& filter, float frequency, float gainDb) {
    frequency = clampFloat(frequency, 20.0f, sampleRate_ * 0.45f);

    const float A = powf(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate_;
    const float cosW0 = cosf(w0);
    const float sinW0 = sinf(w0);
    const float sqrtA = sqrtf(A);

    // Shelf slope S = 1. Para S=1, o termo interno simplifica para 2.
    const float alpha = (sinW0 / 2.0f) * sqrtf(2.0f);

    const float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha);
    const float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW0);
    const float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha);
    const float a0 = (A + 1.0f) + (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
    const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW0);
    const float a2 = (A + 1.0f) + (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha;

    filter.b0 = b0 / a0;
    filter.b1 = b1 / a0;
    filter.b2 = b2 / a0;
    filter.a1 = a1 / a0;
    filter.a2 = a2 / a0;
}

void Equalizer::configurePeaking(Biquad& filter, float frequency, float q, float gainDb) {
    frequency = clampFloat(frequency, 20.0f, sampleRate_ * 0.45f);
    q = clampFloat(q, 0.1f, 10.0f);

    const float A = powf(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate_;
    const float cosW0 = cosf(w0);
    const float sinW0 = sinf(w0);
    const float alpha = sinW0 / (2.0f * q);

    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cosW0;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cosW0;
    const float a2 = 1.0f - alpha / A;

    filter.b0 = b0 / a0;
    filter.b1 = b1 / a0;
    filter.b2 = b2 / a0;
    filter.a1 = a1 / a0;
    filter.a2 = a2 / a0;
}

void Equalizer::configureHighShelf(Biquad& filter, float frequency, float gainDb) {
    frequency = clampFloat(frequency, 20.0f, sampleRate_ * 0.45f);

    const float A = powf(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate_;
    const float cosW0 = cosf(w0);
    const float sinW0 = sinf(w0);
    const float sqrtA = sqrtf(A);

    // Shelf slope S = 1. Para S=1, o termo interno simplifica para 2.
    const float alpha = (sinW0 / 2.0f) * sqrtf(2.0f);

    const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha);
    const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0);
    const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha);
    const float a0 = (A + 1.0f) - (A - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
    const float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0);
    const float a2 = (A + 1.0f) - (A - 1.0f) * cosW0 - 2.0f * sqrtA * alpha;

    filter.b0 = b0 / a0;
    filter.b1 = b1 / a0;
    filter.b2 = b2 / a0;
    filter.a1 = a1 / a0;
    filter.a2 = a2 / a0;
}

float Equalizer::clampDb(float value) {
    return clampFloat(value, kMinToneDb, kMaxToneDb);
}

float Equalizer::clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

int16_t Equalizer::floatToInt16(float sample) {
    sample = clampFloat(sample, -1.0f, 0.999969f);

    if (sample <= -1.0f) {
        return -32768;
    }

    return static_cast<int16_t>(sample * 32767.0f);
}