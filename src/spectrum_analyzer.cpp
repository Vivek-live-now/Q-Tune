#include "spectrum_analyzer.h"

SpectrumAnalyzer::SpectrumAnalyzer() : currentPreset(PRESET_BAR_SPECTRUM) {
    memset(bands, 0, sizeof(bands));
}

bool SpectrumAnalyzer::begin() {
    return true;
}

void SpectrumAnalyzer::sampleMicrophone() {
    for (int i = 0; i < 16; i++) {
        bands[i] = random(5, 55);
    }
}

void SpectrumAnalyzer::nextPreset() {
    currentPreset = (VisualizerPreset)((currentPreset + 1) % 4);
}

void SpectrumAnalyzer::render(VisualizerPreset preset) {
    switch (preset) {
        case PRESET_BAR_SPECTRUM: drawBars(); break;
        case PRESET_WAVEFORM: drawWaveform(); break;
        case PRESET_MILKDROP_PLASMA: drawPlasma(); break;
        case PRESET_STARFIELD: drawStarfield(); break;
    }
}

void SpectrumAnalyzer::drawBars() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Spectrum Analyzer");

    int barWidth = 6;
    for (int i = 0; i < 16; i++) {
        int h = bands[i];
        int x = i * 8;
        int y = 64 - h;
        u8g2.drawBox(x, y, barWidth, h);
    }
    display.sendBuffer();
}

void SpectrumAnalyzer::drawWaveform() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "INMP441 Waveform");

    for (int x = 0; x < 127; x++) {
        int y = 36 + (int)(sin(x * 0.1 + millis() * 0.01) * 20.0);
        u8g2.drawPixel(x, y);
    }
    display.sendBuffer();
}

void SpectrumAnalyzer::drawPlasma() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "MilkDrop Plasma");

    float t = millis() * 0.002f;
    for (int x = 0; x < 128; x += 4) {
        for (int y = 16; y < 64; y += 4) {
            float v = sin(x * 0.1f + t) + cos(y * 0.1f + t);
            if (v > 0.5f) {
                u8g2.drawBox(x, y, 3, 3);
            }
        }
    }
    display.sendBuffer();
}

void SpectrumAnalyzer::drawStarfield() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Starfield Audio");

    for (int i = 0; i < 20; i++) {
        int x = (i * 17 + (millis() / 10)) % 128;
        int y = 16 + (i * 7) % 48;
        u8g2.drawPixel(x, y);
    }
    display.sendBuffer();
}

SpectrumAnalyzer spectrumAnalyzer;
