#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>
#include <cstring>
#include "audio_dsp.h"

int main() {
    std::cout << "Starting Host C++ AudioDSP & Audiophile unit verification..." << std::endl;

    audioDSP.begin(44100);
    assert(audioDSP.getSampleRate() == 44100);

    // 1. Flat preset bypass verification
    audioDSP.setPreset(EQ_PRESET_FLAT);
    audioDSP.setCrossfeedMode(CROSSFEED_OFF);
    assert(audioDSP.getPreset() == EQ_PRESET_FLAT);
    assert(strcmp(audioDSP.getPresetName(), "FLAT") == 0);

    for (size_t b = 0; b < AudioDSP::BAND_COUNT; b++) {
        assert(audioDSP.getBandGain(b) == 0);
    }

    int16_t test_frame[4] = { 1000, -1000, 2000, -2000 };
    int16_t orig_frame[4] = { 1000, -1000, 2000, -2000 };
    audioDSP.process(test_frame, 2);
    assert(memcmp(test_frame, orig_frame, sizeof(test_frame)) == 0);
    std::cout << "  [PASS] 1. Flat preset exact unity gain bypass verified." << std::endl;

    // 2. Preset gains & Custom Band Controls
    audioDSP.setPreset(EQ_PRESET_BASS_BOOST);
    assert(audioDSP.getPreset() == EQ_PRESET_BASS_BOOST);
    assert(audioDSP.getBandGain(0) == 6);
    assert(audioDSP.getBandGain(1) == 3);
    assert(strcmp(audioDSP.getPresetName(), "BASS+") == 0);

    audioDSP.setBandGain(0, 10);
    assert(audioDSP.getBandGain(0) == 10);
    assert(audioDSP.getPreset() == EQ_PRESET_CUSTOM);
    assert(strcmp(audioDSP.getPresetName(), "CUSTOM") == 0);

    audioDSP.cyclePreset();
    assert(audioDSP.getPreset() == EQ_PRESET_FLAT);
    std::cout << "  [PASS] 2. Presets and manual band adjustments (-12dB to +12dB) verified." << std::endl;

    // 3. Frequency Filtering Response: Bass Boost vs Attenuation
    audioDSP.setPreset(EQ_PRESET_FLAT);
    audioDSP.setBandGain(0, 12); // +12dB at 60Hz
    const size_t NUM_SAMPLES = 441; // 10ms of 60Hz sine wave
    std::vector<int16_t> bass_boost_pcm(NUM_SAMPLES * 2);
    std::vector<int16_t> bass_cut_pcm(NUM_SAMPLES * 2);

    for (size_t i = 0; i < NUM_SAMPLES; i++) {
        float t = (float)i / 44100.0f;
        int16_t s = (int16_t)(sinf(2.0f * M_PI * 60.0f * t) * 5000.0f);
        bass_boost_pcm[i * 2] = s;
        bass_boost_pcm[i * 2 + 1] = s;
        bass_cut_pcm[i * 2] = s;
        bass_cut_pcm[i * 2 + 1] = s;
    }

    audioDSP.process(bass_boost_pcm.data(), NUM_SAMPLES);

    audioDSP.setBandGain(0, -12); // -12dB at 60Hz
    audioDSP.process(bass_cut_pcm.data(), NUM_SAMPLES);

    // Peak amplitude of boosted signal must exceed peak amplitude of cut signal
    int16_t peak_boost = 0, peak_cut = 0;
    for (size_t i = NUM_SAMPLES / 2; i < NUM_SAMPLES; i++) {
        if (abs(bass_boost_pcm[i * 2]) > peak_boost) peak_boost = abs(bass_boost_pcm[i * 2]);
        if (abs(bass_cut_pcm[i * 2]) > peak_cut) peak_cut = abs(bass_cut_pcm[i * 2]);
    }
    assert(peak_boost > peak_cut * 2);
    std::cout << "  [PASS] 3. Biquad IIR filter frequency response verified (peak boost=" << peak_boost << ", cut=" << peak_cut << ")." << std::endl;

    // 4. Headphone Crossfeed Channel Blending (Jan Meier Natural mode)
    audioDSP.setPreset(EQ_PRESET_FLAT);
    audioDSP.setCrossfeedMode(CROSSFEED_OFF);

    // Hard-panned signal (Left channel = 10000, Right channel = 0)
    std::vector<int16_t> hard_panned(200 * 2);
    for (size_t i = 0; i < 200; i++) {
        hard_panned[i * 2] = 10000;
        hard_panned[i * 2 + 1] = 0;
    }

    audioDSP.process(hard_panned.data(), 200);
    // In OFF mode, right channel must stay 0
    assert(hard_panned[100 * 2 + 1] == 0);

    // Now turn on CROSSFEED_NATURAL
    audioDSP.setCrossfeedMode(CROSSFEED_NATURAL);
    assert(strcmp(audioDSP.getCrossfeedModeName(), "NATURAL") == 0);

    for (size_t i = 0; i < 200; i++) {
        hard_panned[i * 2] = 10000;
        hard_panned[i * 2 + 1] = 0;
    }
    audioDSP.process(hard_panned.data(), 200);

    // In NATURAL mode, right channel must receive blended crossfeed energy (> 0)
    assert(hard_panned[100 * 2 + 1] > 1000);
    assert(hard_panned[100 * 2] < 10000); // Normalized to prevent acoustic clipping
    std::cout << "  [PASS] 4. Headphone Crossfeed channel blending verified (crossfeed right=" << hard_panned[100 * 2 + 1] << ")." << std::endl;

    // 5. Soft-Knee Saturation Limiter (Zero Digital Wrapping)
    assert(AudioDSP::softClip(20000) == 20000);
    assert(AudioDSP::softClip(-20000) == -20000);

    int16_t clipped_pos = AudioDSP::softClip(60000);
    int16_t clipped_neg = AudioDSP::softClip(-60000);
    assert(clipped_pos <= 32767 && clipped_pos > 30000);
    assert(clipped_neg >= -32768 && clipped_neg < -30000);
    std::cout << "  [PASS] 5. Soft-Knee Saturation Limiter verified (clipped pos=" << clipped_pos << ", neg=" << clipped_neg << ")." << std::endl;

    std::cout << "[PASS] Host C++ AudioDSP & Audiophile unit verification passed." << std::endl;
    return 0;
}
