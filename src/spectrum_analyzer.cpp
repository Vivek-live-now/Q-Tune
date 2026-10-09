#include "spectrum_analyzer.h"
#include "audio_player.h"
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

SpectrumAnalyzer::SpectrumAnalyzer() :
    ringHead(0),
    lastSampleFeedMs(0),
    currentPreset(PRESET_BAR_SPECTRUM),
    active(false),
    micHardwareInitialized(false),
    peakLevel(0.0f),
    rmsLevel(0.0f) {
    memset(bands, 0, sizeof(bands));
    memset(peakHold, 0, sizeof(peakHold));
    memset(peakDecayTimer, 0, sizeof(peakDecayTimer));
    memset(micBuffer, 0, sizeof(micBuffer));
    memset(ringBuffer, 0, sizeof(ringBuffer));
}

bool SpectrumAnalyzer::begin() {
    active = false;
    micHardwareInitialized = false;
    peakLevel = 0.0f;
    rmsLevel = 0.0f;
    ringHead = 0;
    lastSampleFeedMs = 0;
    memset(bands, 0, sizeof(bands));
    memset(peakHold, 0, sizeof(peakHold));
    memset(peakDecayTimer, 0, sizeof(peakDecayTimer));
    memset(micBuffer, 0, sizeof(micBuffer));
    memset(ringBuffer, 0, sizeof(ringBuffer));
    return true;
}

bool SpectrumAnalyzer::start() {
    active = true;
    lastSampleFeedMs = millis();
    return true;
}

void SpectrumAnalyzer::stop() {
    if (!active && !micHardwareInitialized) return;
    if (micHardwareInitialized) {
        i2s_driver_uninstall(I2S_NUM);
        micHardwareInitialized = false;
        // Restore AudioPlayer I2S TX configuration
        audioPlayer.begin();
    }
    active = false;
    clearSamples();
}

void SpectrumAnalyzer::feedSamples(const int16_t *samples, size_t count, uint8_t channels) {
    if (!samples || count == 0) return;
    size_t head = ringHead;
    for (size_t i = 0; i < count; i++) {
        int16_t sample;
        if (channels == 1) {
            sample = samples[i];
        } else {
            // Downmix stereo (L + R) / 2 to mono
            sample = (int16_t)(((int32_t)samples[i * 2] + (int32_t)samples[i * 2 + 1]) / 2);
        }
        ringBuffer[head] = sample;
        head = (head + 1) % RING_BUFFER_SIZE;
    }
    ringHead = head;
    lastSampleFeedMs = millis();
}

void SpectrumAnalyzer::clearSamples() {
    memset(ringBuffer, 0, sizeof(ringBuffer));
    memset(micBuffer, 0, sizeof(micBuffer));
    memset(bands, 0, sizeof(bands));
    memset(peakHold, 0, sizeof(peakHold));
    memset(peakDecayTimer, 0, sizeof(peakDecayTimer));
    peakLevel = 0.0f;
    rmsLevel = 0.0f;
    ringHead = 0;
}

void SpectrumAnalyzer::sampleAudioStream() {
    unsigned long now = millis();
    bool isAudioActive = audioPlayer.isPlaying() && (now - lastSampleFeedMs < 350);

    if (!isAudioActive) {
        // Smoothly decay frequency bands and peak hold to zero when audio is stopped or paused
        for (int i = 0; i < 16; i++) {
            bands[i] = (uint8_t)(bands[i] * 0.70f);
            if (peakHold[i] > 0) peakHold[i]--;
        }
        peakLevel = peakLevel * 0.70f;
        rmsLevel = rmsLevel * 0.70f;
        for (size_t i = 0; i < SAMPLE_SIZE; i++) {
            micBuffer[i] = 0;
        }
        return;
    }

    // Capture latest SAMPLE_SIZE samples from lock-free circular ring buffer
    size_t head = ringHead;
    size_t start = (head + RING_BUFFER_SIZE - SAMPLE_SIZE) % RING_BUFFER_SIZE;

    int64_t sum = 0;
    for (size_t i = 0; i < SAMPLE_SIZE; i++) {
        size_t idx = (start + i) % RING_BUFFER_SIZE;
        // Scale 16-bit PCM down (>> 6) to match calibrated FFT dynamic range
        int16_t s = ringBuffer[idx] >> 6;
        micBuffer[i] = s;
        sum += s;
    }

    // DC bias removal
    int16_t dc = (int16_t)(sum / SAMPLE_SIZE);
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

void SpectrumAnalyzer::sampleMicrophone() {
    if (!micHardwareInitialized) {
        // Disconnect GPIO from previous state and configure as digital input
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
            .data_in_num = I2S_MIC_DIN     // GPIO 15 (Freed former I2C SDA)
        };

        if (i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL) != ESP_OK) {
            return;
        }
        if (i2s_set_pin(I2S_NUM, &pin_config) != ESP_OK) {
            i2s_driver_uninstall(I2S_NUM);
            return;
        }

        micHardwareInitialized = true;
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

    // 16 Logarithmic octave frequency bands across 64 bins (125 Hz to 8000+ Hz)
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
            if (scaled > 46.0f) scaled = 46.0f;
            if (scaled < 0.0f) scaled = 0.0f;
            targetHeight = (uint8_t)scaled;
        }

        // Fast attack, smooth decay
        if (targetHeight >= bands[b]) {
            bands[b] = targetHeight;
        } else {
            bands[b] = (uint8_t)(bands[b] * 0.78f);
        }

        // Dynamic peak hold cap calculation
        if (bands[b] >= peakHold[b]) {
            peakHold[b] = bands[b];
            peakDecayTimer[b] = 0;
        } else {
            peakDecayTimer[b]++;
            if (peakDecayTimer[b] >= 3 && peakHold[b] > 0) {
                peakHold[b]--;
                peakDecayTimer[b] = 0;
            }
        }
    }
}

void SpectrumAnalyzer::nextPreset() {
    currentPreset = (VisualizerPreset)((currentPreset + 1) % 4);
}

void SpectrumAnalyzer::previousPreset() {
    currentPreset = (VisualizerPreset)((currentPreset + 3) % 4);
}

const char* SpectrumAnalyzer::getPresetName() const {
    switch (currentPreset) {
        case PRESET_BAR_SPECTRUM:    return "BARS";
        case PRESET_WAVEFORM:        return "WAVE";
        case PRESET_MILKDROP_PLASMA: return "PLASMA";
        case PRESET_STARFIELD:       return "STAR";
        default:                     return "BARS";
    }
}

void SpectrumAnalyzer::render() {
    render(currentPreset);
}

void SpectrumAnalyzer::render(VisualizerPreset preset) {
    switch (preset) {
        case PRESET_BAR_SPECTRUM: drawBars(); break;
        case PRESET_WAVEFORM: drawWaveform(); break;
        case PRESET_MILKDROP_PLASMA: drawPlasma(); break;
        case PRESET_STARFIELD: drawStarfield(); break;
    }
}

void SpectrumAnalyzer::renderMiniBars(U8G2 &u8g2, int x, int y, int width, int height) {
    const int numMiniBands = 8;
    int spacing = 1;
    int barWidth = (width - ((numMiniBands - 1) * spacing)) / numMiniBands;
    if (barWidth < 2) barWidth = 2;

    for (int i = 0; i < numMiniBands; i++) {
        // Average adjacent 2 bands from 16 bands
        int val = (bands[i * 2] + bands[i * 2 + 1]) / 2;
        int barH = (val * height) / 46;
        if (barH > height) barH = height;
        if (barH < 0) barH = 0;

        int bx = x + i * (barWidth + spacing);
        int by = y + height - barH;

        if (barH > 0) {
            u8g2.drawBox(bx, by, barWidth, barH);
        } else {
            u8g2.drawHLine(bx, y + height - 1, barWidth);
        }
    }
}

void SpectrumAnalyzer::drawBars() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    String track = audioPlayer.getCurrentTrackName();
    if (track.length() == 0) {
        track = audioPlayer.isPlaying() ? "Live WAV" : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
    }
    if (track.length() > 13) {
        track = track.substring(0, 11) + "..";
    }
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "[BARS] %s", track.c_str());
    u8g2.drawStr(0, 10, headerBuf);
    u8g2.drawHLine(0, 12, 128);

    if (!audioPlayer.isPlaying() && peakLevel < 5.0f) {
        if (audioPlayer.isPaused()) {
            u8g2.drawStr(36, 36, "❚❚ PAUSED");
        } else {
            u8g2.drawStr(24, 36, "No Active Stream");
        }
    }

    int barWidth = 6;
    for (int i = 0; i < 16; i++) {
        int h = bands[i];
        if (h > 46) h = 46;
        int x = i * 8;
        int y = 64 - h;
        if (h > 0) {
            u8g2.drawBox(x, y, barWidth, h);
        } else {
            u8g2.drawHLine(x, 63, barWidth);
        }

        // Floating peak hold cap
        if (peakHold[i] > 0) {
            int capY = 63 - peakHold[i];
            if (capY < 14) capY = 14;
            u8g2.drawHLine(x, capY, barWidth);
        }
    }
    display.sendBuffer();
}

void SpectrumAnalyzer::drawWaveform() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    String track = audioPlayer.getCurrentTrackName();
    if (track.length() == 0) {
        track = audioPlayer.isPlaying() ? "Live WAV" : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
    }
    if (track.length() > 13) {
        track = track.substring(0, 11) + "..";
    }
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "[WAVE] %s", track.c_str());
    u8g2.drawStr(0, 10, headerBuf);
    u8g2.drawHLine(0, 12, 128);

    float scale = 0.01f;
    if (peakLevel > 20.0f) {
        scale = 22.0f / peakLevel;
        if (scale > 0.15f) scale = 0.15f;
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

    String track = audioPlayer.getCurrentTrackName();
    if (track.length() == 0) {
        track = audioPlayer.isPlaying() ? "Live WAV" : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
    }
    if (track.length() > 12) {
        track = track.substring(0, 10) + "..";
    }
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "[PLASMA] %s", track.c_str());
    u8g2.drawStr(0, 10, headerBuf);
    u8g2.drawHLine(0, 12, 128);

    float energy = (rmsLevel / 300.0f);
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

    String track = audioPlayer.getCurrentTrackName();
    if (track.length() == 0) {
        track = audioPlayer.isPlaying() ? "Live WAV" : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
    }
    if (track.length() > 13) {
        track = track.substring(0, 11) + "..";
    }
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "[STAR] %s", track.c_str());
    u8g2.drawStr(0, 10, headerBuf);
    u8g2.drawHLine(0, 12, 128);

    int warp = (int)(rmsLevel / 60.0f);
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
