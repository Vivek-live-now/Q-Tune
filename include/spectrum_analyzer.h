#ifndef SPECTRUM_ANALYZER_H
#define SPECTRUM_ANALYZER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <Preferences.h>
#include "hw_config.h"
#include "display.h"

enum VisualizerPreset {
    PRESET_ANALOG_VU = 0,
    PRESET_HORIZ_BAR_VU = 1,
    PRESET_VU_SPECTROMETER = 2,
    PRESET_VINYL_VU = 3,
    PRESET_CASSETTE_PLAYER = 4,
    PRESET_CD_PLAYER = 5,
    PRESET_BAR_SPECTRUM = 6,
    PRESET_WAVEFORM = 7,
    PRESET_MILKDROP_PLASMA = 8,
    PRESET_STARFIELD = 9,
    PRESET_COUNT = 10
};

enum VisualizerSensitivity {
    VIS_SENS_LOW = 0,
    VIS_SENS_NORMAL = 1,
    VIS_SENS_HIGH = 2
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

    // Sensitivity adjustment & persistence
    VisualizerSensitivity getSensitivity() const { return sensitivity; }
    void setSensitivity(VisualizerSensitivity sens);
    void cycleSensitivity();
    const char* getSensitivityName() const;
    const char* getSensitivityShortName() const;
    float getSensitivityMultiplier() const;
    void loadSettings();
    void saveSettings();

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
    float getVoicePitch() const { return voicePitch; }
    float getVoiceConfidence() const { return voiceConfidence; }

    // Stereo channel levels & dB readouts
    float getLeftPeak() const { return leftPeakLevel; }
    float getRightPeak() const { return rightPeakLevel; }
    float getLeftRMS() const { return leftRmsLevel; }
    float getRightRMS() const { return rightRmsLevel; }
    float getLeftDb() const { return leftDb; }
    float getRightDb() const { return rightDb; }
    float getLeftPeakDb() const { return leftPeakDb; }
    float getRightPeakDb() const { return rightPeakDb; }
    bool isClipping() const;

    // Top status bar auto-hide management (5-second timeout for full-screen view)
    void wakeTopBar(unsigned long durationMs = 5000);
    bool isTopBarVisible() const;
    void resetTopBar();

private:
    static const size_t SAMPLE_SIZE = 128;
    static const size_t RING_BUFFER_SIZE = 1024;

    alignas(16) int32_t micBuffer[SAMPLE_SIZE]; // Working buffer for FFT / Waveform
    alignas(16) int16_t ringBuffer[RING_BUFFER_SIZE]; // Lock-free circular sample tap from AudioPlayer
    alignas(16) int16_t ringBufferL[RING_BUFFER_SIZE]; // Left channel circular tap
    alignas(16) int16_t ringBufferR[RING_BUFFER_SIZE]; // Right channel circular tap
    volatile size_t ringHead;
    unsigned long lastSampleFeedMs;
    unsigned long lastAnalysisMs;

    uint8_t bands[16];
    uint8_t peakHold[16];
    uint8_t peakDecayTimer[16];

    VisualizerPreset currentPreset;
    VisualizerSensitivity sensitivity;
    bool active;
    bool micHardwareInitialized;
    float peakLevel;
    float rmsLevel;
    float bassLevel;
    float bassFilterState1;
    float bassFilterState2;
    float voicePitch;
    float voiceConfidence;
    unsigned long topBarExpiryMs;
    unsigned long sensToastExpiryMs;

    // Stereo channel levels and dB
    float leftPeakLevel;
    float rightPeakLevel;
    float leftRmsLevel;
    float rightRmsLevel;
    float leftDb;
    float rightDb;
    float leftPeakDb;
    float rightPeakDb;
    unsigned long clipExpiryMs;

    // Analog VU ballistic simulation
    float needlePos;
    float needleVel;
    unsigned long lastBallisticMs;

    // Horizontal bar peak hold
    uint8_t leftBarPeakHold;
    uint8_t rightBarPeakHold;
    uint8_t leftBarDecayTimer;
    uint8_t rightBarDecayTimer;

    // Hybrid spectrometer 8-band peak hold
    uint8_t spec8PeakHold[8];
    uint8_t spec8DecayTimer[8];

    void processFFT();
    void detectVoicePitch();
    void drawAnalogVU();
    void drawHorizontalBarVU();
    void drawVUSpectrometer();
    void drawVinylVU();
    void drawCassettePlayer();
    void drawCdPlayer();
    void drawBars();
    void drawWaveform();
    void drawPlasma();
    void drawStarfield();
};

extern SpectrumAnalyzer spectrumAnalyzer;

#endif // SPECTRUM_ANALYZER_H
