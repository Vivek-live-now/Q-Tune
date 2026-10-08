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
    void sampleMicrophone();
    void render(VisualizerPreset preset);
    void nextPreset();
    float getPeakLevel() const { return peakLevel; }
    float getRMSLevel() const { return rmsLevel; }

private:
    static const size_t SAMPLE_SIZE = 128;
    int32_t micBuffer[SAMPLE_SIZE];
    uint8_t bands[16];
    VisualizerPreset currentPreset;
    bool active;
    float peakLevel;
    float rmsLevel;

    void processFFT();
    void drawBars();
    void drawWaveform();
    void drawPlasma();
    void drawStarfield();
};

extern SpectrumAnalyzer spectrumAnalyzer;

#endif // SPECTRUM_ANALYZER_H
