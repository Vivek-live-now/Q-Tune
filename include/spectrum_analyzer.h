#ifndef SPECTRUM_ANALYZER_H
#define SPECTRUM_ANALYZER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "hw_config.h"
#include "display.h"

enum VisualizerPreset {
    PRESET_BAR_SPECTRUM,
    PRESET_WAVEFORM,
    PRESET_MILKDROP_PLASMA,
    PRESET_STARFIELD
};

class SpectrumAnalyzer {
public:
    SpectrumAnalyzer();
    bool begin();
    bool start();
    void stop();
    bool isRunning() const { return active; }

    // Live WAV decoding stream input (Thread-safe, non-blocking tap called by AudioPlayer)
    void feedSamples(const int16_t *samples, size_t count, uint8_t channels = 1);
    void sampleAudioStream();
    void clearSamples();

    // Hardware microphone sampling (Retained for Diagnostics hardware test)
    void sampleMicrophone();

    // Preset management & rendering
    void render();
    void render(VisualizerPreset preset);
    void nextPreset();
    void previousPreset();
    VisualizerPreset getPreset() const { return currentPreset; }
    void setPreset(VisualizerPreset preset) { currentPreset = preset; }
    const char* getPresetName() const;

    // Mini spectrum equalizer renderer for player screen HUD
    void renderMiniBars(U8G2 &u8g2, int x, int y, int width, int height);

    float getPeakLevel() const { return peakLevel; }
    float getRMSLevel() const { return rmsLevel; }
    float getBassLevel() const { return bassLevel; }
    const uint8_t* getBands() const { return bands; }

private:
    static const size_t SAMPLE_SIZE = 128;
    static const size_t RING_BUFFER_SIZE = 512;

    int32_t micBuffer[SAMPLE_SIZE]; // Working buffer for FFT / Waveform
    int16_t ringBuffer[RING_BUFFER_SIZE]; // Lock-free circular sample tap from AudioPlayer
    volatile size_t ringHead;
    unsigned long lastSampleFeedMs;
    unsigned long lastAnalysisMs;

    uint8_t bands[16];
    uint8_t peakHold[16];
    uint8_t peakDecayTimer[16];

    VisualizerPreset currentPreset;
    bool active;
    bool micHardwareInitialized;
    float peakLevel;
    float rmsLevel;
    float bassLevel;
    float bassFilterState1;
    float bassFilterState2;

    void processFFT();
    void drawBars();
    void drawWaveform();
    void drawPlasma();
    void drawStarfield();
};

extern SpectrumAnalyzer spectrumAnalyzer;

#endif // SPECTRUM_ANALYZER_H
