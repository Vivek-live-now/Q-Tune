#include "spectrum_analyzer.h"
#include "audio_player.h"
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

SpectrumAnalyzer::SpectrumAnalyzer() :
    currentPreset(PRESET_BAR_SPECTRUM),
    active(false),
    peakLevel(0.0f),
    rmsLevel(0.0f) {
    memset(bands, 0, sizeof(bands));
    memset(micBuffer, 0, sizeof(micBuffer));
}

bool SpectrumAnalyzer::begin() {
    active = false;
    peakLevel = 0.0f;
    rmsLevel = 0.0f;
    memset(bands, 0, sizeof(bands));
    memset(micBuffer, 0, sizeof(micBuffer));
    return true;
}

bool SpectrumAnalyzer::start() {
    if (active) return true;

    // Disconnect GPIO 44 from previous UART state and configure as digital input
    gpio_reset_pin((gpio_num_t)I2S_MIC_DIN);
    pinMode(I2S_MIC_DIN, INPUT);

    // Uninstall existing I2S driver on I2S_NUM to reconfigure for microphone RX
    i2s_driver_uninstall(I2S_NUM);

    // INMP441 standard Philips I2S configuration (32-bit slot width, master RX)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 128,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,        // GPIO 17
        .ws_io_num = I2S_LRCK,         // GPIO 18
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_MIC_DIN     // GPIO 44
    };

    if (i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL) != ESP_OK) {
        return false;
    }
    if (i2s_set_pin(I2S_NUM, &pin_config) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM);
        return false;
    }

    active = true;
    return true;
}

void SpectrumAnalyzer::stop() {
    if (!active) return;
    i2s_driver_uninstall(I2S_NUM);
    active = false;
    // Restore AudioPlayer I2S TX configuration
    audioPlayer.begin();
}

void SpectrumAnalyzer::sampleMicrophone() {
    if (!active) {
        if (!start()) return;
    }

    int32_t rawSamples[SAMPLE_SIZE * 2];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_NUM, rawSamples, sizeof(rawSamples), &bytesRead, pdMS_TO_TICKS(50));
    if (err != ESP_OK || bytesRead == 0) return;

    size_t pairCount = bytesRead / (sizeof(int32_t) * 2);
    if (pairCount == 0) return;

    int64_t sum = 0;
    for (size_t i = 0; i < pairCount && i < SAMPLE_SIZE; i++) {
        int32_t left = rawSamples[i * 2];
        int32_t right = rawSamples[i * 2 + 1];

        // INMP441 outputs 24-bit audio in upper bits of 32-bit slot
        // Auto-detect active channel (whether L/R pin is tied to GND or 3.3V)
        int32_t chosen = (labs(left) >= labs(right)) ? left : right;
        int16_t sample = (int16_t)(chosen >> 14);
        micBuffer[i] = sample;
        sum += sample;
    }

    for (size_t i = pairCount; i < SAMPLE_SIZE; i++) {
        micBuffer[i] = 0;
    }

    // High-pass filter / remove DC bias
    int16_t dc = (int16_t)(sum / (pairCount > 0 ? (int64_t)pairCount : 1));
    float sumSq = 0.0f;
    int16_t peak = 0;

    for (size_t i = 0; i < SAMPLE_SIZE; i++) {
        micBuffer[i] -= dc;
        int16_t a = abs(micBuffer[i]);
        if (a > peak) peak = a;
        sumSq += (float)a * a;
    }

    peakLevel = (float)peak;
    rmsLevel = sqrtf(sumSq / SAMPLE_SIZE);

    processFFT();
}

void SpectrumAnalyzer::processFFT() {
    float xr[SAMPLE_SIZE];
    float xi[SAMPLE_SIZE];

    // Hanning window to prevent spectral leakage
    for (size_t i = 0; i < SAMPLE_SIZE; i++) {
        float win = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (SAMPLE_SIZE - 1)));
        xr[i] = (float)micBuffer[i] * win;
        xi[i] = 0.0f;
    }

    // Bit reversal permutation
    for (size_t i = 1, j = 0; i < SAMPLE_SIZE; i++) {
        size_t bit = SAMPLE_SIZE >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float tr = xr[i]; xr[i] = xr[j]; xr[j] = tr;
            float ti = xi[i]; xi[i] = xi[j]; xi[j] = ti;
        }
    }

    // Cooley-Tukey Radix-2 butterflies
    for (size_t len = 2; len <= SAMPLE_SIZE; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float wlen_r = cosf(ang);
        float wlen_i = sinf(ang);
        for (size_t i = 0; i < SAMPLE_SIZE; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (size_t j = 0; j < len / 2; j++) {
                float u_r = xr[i + j];
                float u_i = xi[i + j];
                float v_r = xr[i + j + len / 2] * w_r - xi[i + j + len / 2] * w_i;
                float v_i = xr[i + j + len / 2] * w_i + xi[i + j + len / 2] * w_r;
                xr[i + j] = u_r + v_r;
                xi[i + j] = u_i + v_i;
                xr[i + j + len / 2] = u_r - v_r;
                xi[i + j + len / 2] = u_i - v_i;
                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }

    // Frequency bin magnitudes
    float mag[SAMPLE_SIZE / 2];
    for (size_t k = 0; k < SAMPLE_SIZE / 2; k++) {
        mag[k] = sqrtf(xr[k] * xr[k] + xi[k] * xi[k]);
    }

    // 16 Logarithmic octave frequency bands across 64 bins (125 Hz to 8000 Hz)
    static const uint8_t binStart[16] = {1, 2, 3, 4, 5, 7, 9, 12, 15, 19, 24, 30, 37, 44, 52, 60};
    static const uint8_t binEnd[16]   = {1, 2, 3, 4, 6, 8, 11, 14, 18, 23, 29, 36, 43, 51, 59, 63};

    for (int b = 0; b < 16; b++) {
        float bandMax = 0;
        for (uint8_t k = binStart[b]; k <= binEnd[b]; k++) {
            if (mag[k] > bandMax) bandMax = mag[k];
        }

        // High frequency equal loudness pre-emphasis
        float boost = 1.0f + ((float)b * 0.15f);
        float val = bandMax * boost;

        uint8_t targetHeight = 0;
        if (val > 40.0f) {
            float scaled = log10f(val / 40.0f) * 24.0f;
            if (scaled > 48.0f) scaled = 48.0f;
            if (scaled < 0.0f) scaled = 0.0f;
            targetHeight = (uint8_t)scaled;
        }

        // Fast attack, smooth decay
        if (targetHeight >= bands[b]) {
            bands[b] = targetHeight;
        } else {
            bands[b] = (uint8_t)(bands[b] * 0.75f);
        }
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
    u8g2.drawStr(0, 10, "Spectrum (INMP441)");

    if (peakLevel < 5.0f) {
        u8g2.drawStr(16, 32, "No audio / silence");
    }

    int barWidth = 6;
    for (int i = 0; i < 16; i++) {
        int h = bands[i];
        if (h > 48) h = 48;
        int x = i * 8;
        int y = 64 - h;
        if (h > 0) {
            u8g2.drawBox(x, y, barWidth, h);
        } else {
            u8g2.drawHLine(x, 63, barWidth);
        }
    }
    display.sendBuffer();
}

void SpectrumAnalyzer::drawWaveform() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "Oscilloscope (Mic)");

    float scale = 0.01f;
    if (peakLevel > 50.0f) {
        scale = 22.0f / peakLevel;
        if (scale > 0.1f) scale = 0.1f;
    }

    int prevY = 38;
    for (int x = 0; x < 128; x++) {
        int idx = (x * SAMPLE_SIZE) / 128;
        int val = (int)(micBuffer[idx] * scale);
        int y = 38 - val;
        if (y < 14) y = 14;
        if (y > 62) y = 62;

        if (x > 0) {
            u8g2.drawLine(x - 1, prevY, x, y);
        } else {
            u8g2.drawPixel(x, y);
        }
        prevY = y;
    }

    // Center baseline dots
    for (int x = 0; x < 128; x += 16) {
        u8g2.drawPixel(x, 38);
    }

    display.sendBuffer();
}

void SpectrumAnalyzer::drawPlasma() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "MilkDrop Plasma");

    float energy = (rmsLevel / 400.0f);
    if (energy > 2.5f) energy = 2.5f;
    float t = millis() * 0.002f * (1.0f + energy);

    for (int x = 0; x < 128; x += 4) {
        for (int y = 16; y < 64; y += 4) {
            float v = sinf(x * 0.1f + t) + cosf(y * 0.1f + t);
            if (v > (0.5f - energy * 0.1f)) {
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

    int warp = (int)(rmsLevel / 80.0f);
    if (warp > 12) warp = 12;

    for (int i = 0; i < 20; i++) {
        int x = (i * 17 + ((millis() / 10) * (1 + warp))) % 128;
        int y = 16 + (i * 7) % 48;
        if (warp > 2) {
            u8g2.drawHLine(x, y, min(warp, 5));
        } else {
            u8g2.drawPixel(x, y);
        }
    }
    display.sendBuffer();
}

SpectrumAnalyzer spectrumAnalyzer;
