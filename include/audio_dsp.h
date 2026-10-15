#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum EqPreset {
    EQ_PRESET_FLAT = 0,
    EQ_PRESET_BASS_BOOST = 1,
    EQ_PRESET_VOCAL = 2,
    EQ_PRESET_ROCK = 3,
    EQ_PRESET_POP = 4,
    EQ_PRESET_JAZZ = 5,
    EQ_PRESET_CLASSICAL = 6,
    EQ_PRESET_ELECTRONIC = 7,
    EQ_PRESET_CUSTOM = 8
};

enum CrossfeedMode {
    CROSSFEED_OFF = 0,
    CROSSFEED_SUBTLE = 1,  // Chu Moy curve (700Hz, -9.5dB crossfeed)
    CROSSFEED_NATURAL = 2  // Jan Meier curve (650Hz, -6.0dB crossfeed)
};

// Second-Order IIR Biquad Filter (Direct Form I)
struct BiquadFilter {
    float b0, b1, b2;
    float a1, a2;
    float x1_l, x2_l, y1_l, y2_l;
    float x1_r, x2_r, y1_r, y2_r;
    bool active;

    BiquadFilter();
    void reset();
    void initPeaking(float f0, float fs, float Q, float gainDb);
    void initLowShelf(float f0, float fs, float Q, float gainDb);
    void initHighShelf(float f0, float fs, float Q, float gainDb);

    inline float processL(float in) {
        if (!active) return in;
        float out = b0 * in + b1 * x1_l + b2 * x2_l - a1 * y1_l - a2 * y2_l;
        x2_l = x1_l; x1_l = in;
        y2_l = y1_l; y1_l = out;
        return out;
    }

    inline float processR(float in) {
        if (!active) return in;
        float out = b0 * in + b1 * x1_r + b2 * x2_r - a1 * y1_r - a2 * y2_r;
        x2_r = x1_r; x1_r = in;
        y2_r = y1_r; y1_r = out;
        return out;
    }
};

class AudioDSP {
public:
    static const size_t BAND_COUNT = 5;
    static const int8_t MIN_GAIN_DB = -12;
    static const int8_t MAX_GAIN_DB = 12;

    AudioDSP();

    void begin(uint32_t sampleRate = 44100);
    void setSampleRate(uint32_t sampleRate);
    uint32_t getSampleRate() const { return currentSampleRate; }

    // 5-Band Equalizer controls
    void setEqEnabled(bool enable);
    bool isEqEnabled() const { return eqEnabled; }
    void toggleEq();

    void setBandGain(uint8_t bandIndex, int8_t gainDb);
    int8_t getBandGain(uint8_t bandIndex) const;
    static const char* getBandLabel(uint8_t bandIndex);
    static float getBandCenterFreq(uint8_t bandIndex);

    void setPreset(EqPreset preset);
    EqPreset getPreset() const { return currentPreset; }
    const char* getPresetName() const;
    void cyclePreset();

    // Headphone Crossfeed controls
    void setCrossfeedMode(CrossfeedMode mode);
    CrossfeedMode getCrossfeedMode() const { return crossfeedMode; }
    const char* getCrossfeedModeName() const;
    void cycleCrossfeedMode();

    // Main Audio Stream Processing Pipeline
    void process(int16_t* stereoSamples, size_t frameCount);

    // Fast Soft-Knee Saturation Limiter (Prevents Digital Wrapping)
    static int16_t softClip(int32_t sample);

private:
    uint32_t currentSampleRate;
    bool eqEnabled;
    EqPreset currentPreset;
    int8_t bandGains[BAND_COUNT];
    BiquadFilter filters[BAND_COUNT];

    CrossfeedMode crossfeedMode;
    float crossfeed_lpf_l;
    float crossfeed_lpf_r;

    void updateFilterCoefficients();
    void updateCrossfeedCoefficients();
};

extern AudioDSP audioDSP;

#endif // AUDIO_DSP_H
