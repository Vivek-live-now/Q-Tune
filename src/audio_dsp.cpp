#include "audio_dsp.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioDSP audioDSP;

BiquadFilter::BiquadFilter() {
    reset();
}

void BiquadFilter::reset() {
    b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
    a1 = 0.0f; a2 = 0.0f;
    x1_l = x2_l = y1_l = y2_l = 0.0f;
    x1_r = x2_r = y1_r = y2_r = 0.0f;
    active = false;
}

void BiquadFilter::initPeaking(float f0, float fs, float Q, float gainDb) {
    if (fabsf(gainDb) < 0.1f || fs <= 0.0f) {
        reset();
        return;
    }
    float A = powf(10.0f, gainDb / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / fs;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);

    float b0_raw = 1.0f + alpha * A;
    float b1_raw = -2.0f * cosw0;
    float b2_raw = 1.0f - alpha * A;
    float a0_raw = 1.0f + alpha / A;
    float a1_raw = -2.0f * cosw0;
    float a2_raw = 1.0f - alpha / A;

    b0 = b0_raw / a0_raw;
    b1 = b1_raw / a0_raw;
    b2 = b2_raw / a0_raw;
    a1 = a1_raw / a0_raw;
    a2 = a2_raw / a0_raw;
    active = true;
}

void BiquadFilter::initLowShelf(float f0, float fs, float Q, float gainDb) {
    if (fabsf(gainDb) < 0.1f || fs <= 0.0f) {
        reset();
        return;
    }
    float A = powf(10.0f, gainDb / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / fs;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);
    float two_sqrt_A_alpha = 2.0f * sqrtf(A) * alpha;

    float b0_raw = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + two_sqrt_A_alpha);
    float b1_raw = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
    float b2_raw = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - two_sqrt_A_alpha);
    float a0_raw = (A + 1.0f) + (A - 1.0f) * cosw0 + two_sqrt_A_alpha;
    float a1_raw = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
    float a2_raw = (A + 1.0f) + (A - 1.0f) * cosw0 - two_sqrt_A_alpha;

    b0 = b0_raw / a0_raw;
    b1 = b1_raw / a0_raw;
    b2 = b2_raw / a0_raw;
    a1 = a1_raw / a0_raw;
    a2 = a2_raw / a0_raw;
    active = true;
}

void BiquadFilter::initHighShelf(float f0, float fs, float Q, float gainDb) {
    if (fabsf(gainDb) < 0.1f || fs <= 0.0f) {
        reset();
        return;
    }
    float A = powf(10.0f, gainDb / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / fs;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);
    float two_sqrt_A_alpha = 2.0f * sqrtf(A) * alpha;

    float b0_raw = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + two_sqrt_A_alpha);
    float b1_raw = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
    float b2_raw = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - two_sqrt_A_alpha);
    float a0_raw = (A + 1.0f) - (A - 1.0f) * cosw0 + two_sqrt_A_alpha;
    float a1_raw = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
    float a2_raw = (A + 1.0f) - (A - 1.0f) * cosw0 - two_sqrt_A_alpha;

    b0 = b0_raw / a0_raw;
    b1 = b1_raw / a0_raw;
    b2 = b2_raw / a0_raw;
    a1 = a1_raw / a0_raw;
    a2 = a2_raw / a0_raw;
    active = true;
}

AudioDSP::AudioDSP()
    : currentSampleRate(44100),
      eqEnabled(true),
      currentPreset(EQ_PRESET_FLAT),
      crossfeedMode(CROSSFEED_OFF),
      crossfeed_lpf_l(0.0f),
      crossfeed_lpf_r(0.0f) {
    for (size_t i = 0; i < BAND_COUNT; i++) {
        bandGains[i] = 0;
    }
}

void AudioDSP::begin(uint32_t sampleRate) {
    currentSampleRate = (sampleRate > 0) ? sampleRate : 44100;
    updateFilterCoefficients();
    updateCrossfeedCoefficients();
}

void AudioDSP::setSampleRate(uint32_t sampleRate) {
    if (sampleRate == currentSampleRate || sampleRate == 0) return;
    currentSampleRate = sampleRate;
    updateFilterCoefficients();
    updateCrossfeedCoefficients();
}

const char* AudioDSP::getBandLabel(uint8_t bandIndex) {
    switch (bandIndex) {
        case 0: return "60Hz";
        case 1: return "250Hz";
        case 2: return "1kHz";
        case 3: return "4kHz";
        case 4: return "12kHz";
        default: return "";
    }
}

float AudioDSP::getBandCenterFreq(uint8_t bandIndex) {
    switch (bandIndex) {
        case 0: return 60.0f;
        case 1: return 250.0f;
        case 2: return 1000.0f;
        case 3: return 4000.0f;
        case 4: return 12000.0f;
        default: return 1000.0f;
    }
}

void AudioDSP::setBandGain(uint8_t bandIndex, int8_t gainDb) {
    if (bandIndex >= BAND_COUNT) return;
    if (gainDb < MIN_GAIN_DB) gainDb = MIN_GAIN_DB;
    if (gainDb > MAX_GAIN_DB) gainDb = MAX_GAIN_DB;

    bandGains[bandIndex] = gainDb;
    currentPreset = EQ_PRESET_CUSTOM;
    updateFilterCoefficients();
}

int8_t AudioDSP::getBandGain(uint8_t bandIndex) const {
    if (bandIndex >= BAND_COUNT) return 0;
    return bandGains[bandIndex];
}

void AudioDSP::setEqEnabled(bool enable) {
    eqEnabled = enable;
}

void AudioDSP::toggleEq() {
    eqEnabled = !eqEnabled;
}

void AudioDSP::setPreset(EqPreset preset) {
    currentPreset = preset;
    static const int8_t PRESET_GAINS[8][BAND_COUNT] = {
        {  0,  0,  0,  0,  0 }, // FLAT
        {  6,  3,  0,  0,  1 }, // BASS_BOOST
        { -2,  0,  4,  3,  1 }, // VOCAL
        {  5,  2, -1,  3,  5 }, // ROCK
        {  2,  3,  4,  2,  0 }, // POP
        {  3,  1, -1,  2,  3 }, // JAZZ
        {  4,  2,  0,  2,  4 }, // CLASSICAL
        {  6,  4,  0,  2,  5 }  // ELECTRONIC
    };

    if ((size_t)preset < 8) {
        for (size_t b = 0; b < BAND_COUNT; b++) {
            bandGains[b] = PRESET_GAINS[(size_t)preset][b];
        }
    }
    updateFilterCoefficients();
}

const char* AudioDSP::getPresetName() const {
    switch (currentPreset) {
        case EQ_PRESET_FLAT:        return "FLAT";
        case EQ_PRESET_BASS_BOOST:  return "BASS+";
        case EQ_PRESET_VOCAL:       return "VOCAL";
        case EQ_PRESET_ROCK:        return "ROCK";
        case EQ_PRESET_POP:         return "POP";
        case EQ_PRESET_JAZZ:        return "JAZZ";
        case EQ_PRESET_CLASSICAL:   return "CLASSIC";
        case EQ_PRESET_ELECTRONIC:  return "ELECTRO";
        case EQ_PRESET_CUSTOM:      return "CUSTOM";
        default:                    return "FLAT";
    }
}

void AudioDSP::cyclePreset() {
    uint8_t next = ((uint8_t)currentPreset + 1) % 9;
    setPreset((EqPreset)next);
}

void AudioDSP::setCrossfeedMode(CrossfeedMode mode) {
    crossfeedMode = mode;
    updateCrossfeedCoefficients();
}

const char* AudioDSP::getCrossfeedModeName() const {
    switch (crossfeedMode) {
        case CROSSFEED_OFF:     return "OFF";
        case CROSSFEED_SUBTLE:  return "SUBTLE";
        case CROSSFEED_NATURAL: return "NATURAL";
        default:                return "OFF";
    }
}

void AudioDSP::cycleCrossfeedMode() {
    crossfeedMode = (CrossfeedMode)(((uint8_t)crossfeedMode + 1) % 3);
    updateCrossfeedCoefficients();
}

void AudioDSP::updateFilterCoefficients() {
    float fs = (float)currentSampleRate;
    // Band 0: 60Hz Low Shelf
    filters[0].initLowShelf(60.0f, fs, 0.707f, (float)bandGains[0]);
    // Band 1: 250Hz Peaking
    filters[1].initPeaking(250.0f, fs, 1.0f, (float)bandGains[1]);
    // Band 2: 1000Hz Peaking
    filters[2].initPeaking(1000.0f, fs, 1.0f, (float)bandGains[2]);
    // Band 3: 4000Hz Peaking
    filters[3].initPeaking(4000.0f, fs, 1.0f, (float)bandGains[3]);
    // Band 4: 12000Hz High Shelf
    filters[4].initHighShelf(12000.0f, fs, 0.707f, (float)bandGains[4]);
}

void AudioDSP::updateCrossfeedCoefficients() {
    crossfeed_lpf_l = 0.0f;
    crossfeed_lpf_r = 0.0f;
}

int16_t AudioDSP::softClip(int32_t sample) {
    if (sample > 30000) {
        int32_t over = sample - 30000;
        int32_t comp = 30000 + (over * 2767) / (over + 2767);
        return (comp > 32767) ? 32767 : (int16_t)comp;
    } else if (sample < -30000) {
        int32_t over = -sample - 30000;
        int32_t comp = -30000 - (over * 2768) / (over + 2768);
        return (comp < -32768) ? -32768 : (int16_t)comp;
    }
    return (int16_t)sample;
}

void AudioDSP::process(int16_t* stereoSamples, size_t frameCount) {
    if (!stereoSamples || frameCount == 0) return;

    bool anyFilterActive = eqEnabled && (
        filters[0].active || filters[1].active ||
        filters[2].active || filters[3].active || filters[4].active
    );

    float cross_alpha = 0.0f;
    float cross_gain = 0.0f;
    float norm_factor = 1.0f;
    bool crossActive = (crossfeedMode != CROSSFEED_OFF);

    if (crossActive) {
        float fc = (crossfeedMode == CROSSFEED_NATURAL) ? 650.0f : 700.0f;
        cross_alpha = 2.0f * (float)M_PI * fc / (float)currentSampleRate;
        if (cross_alpha > 0.95f) cross_alpha = 0.95f;
        cross_gain = (crossfeedMode == CROSSFEED_NATURAL) ? 0.50f : 0.33f;
        norm_factor = 1.0f / (1.0f + cross_gain);
    }

    if (!anyFilterActive && !crossActive) {
        return; // Fast bypass
    }

    for (size_t i = 0; i < frameCount; i++) {
        size_t idx = i * 2;
        float l = (float)stereoSamples[idx];
        float r = (float)stereoSamples[idx + 1];

        // 1. Apply 5-Band Biquad EQ Filter Cascade
        if (anyFilterActive) {
            if (filters[0].active) { l = filters[0].processL(l); r = filters[0].processR(r); }
            if (filters[1].active) { l = filters[1].processL(l); r = filters[1].processR(r); }
            if (filters[2].active) { l = filters[2].processL(l); r = filters[2].processR(r); }
            if (filters[3].active) { l = filters[3].processL(l); r = filters[3].processR(r); }
            if (filters[4].active) { l = filters[4].processL(l); r = filters[4].processR(r); }
        }

        // 2. Apply Jan Meier / Chu Moy Headphone Crossfeed
        if (crossActive) {
            crossfeed_lpf_l += cross_alpha * (l - crossfeed_lpf_l);
            crossfeed_lpf_r += cross_alpha * (r - crossfeed_lpf_r);

            float out_l = (l + cross_gain * crossfeed_lpf_r) * norm_factor;
            float out_r = (r + cross_gain * crossfeed_lpf_l) * norm_factor;
            l = out_l;
            r = out_r;
        }

        // 3. Soft Limiting Saturation & Clamping
        stereoSamples[idx]     = softClip((int32_t)l);
        stereoSamples[idx + 1] = softClip((int32_t)r);
    }
}
