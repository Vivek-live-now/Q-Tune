#include "spectrum_analyzer.h"
#include "audio_player.h"
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

SpectrumAnalyzer::SpectrumAnalyzer() :
    ringHead(0),
    lastSampleFeedMs(0),
    lastAnalysisMs(0),
    currentPreset(PRESET_ANALOG_VU),
    sensitivity(VIS_SENS_NORMAL),
    active(false),
    micHardwareInitialized(false),
    peakLevel(0.0f),
    rmsLevel(0.0f),
    bassLevel(0.0f),
    bassFilterState1(0.0f),
    bassFilterState2(0.0f),
    voicePitch(0.0f),
    voiceConfidence(0.0f),
    topBarExpiryMs(0),
    sensToastExpiryMs(0),
    leftPeakLevel(0.0f),
    rightPeakLevel(0.0f),
    leftRmsLevel(0.0f),
    rightRmsLevel(0.0f),
    leftDb(-20.0f),
    rightDb(-20.0f),
    needlePos(0.0f),
    needleVel(0.0f),
    lastBallisticMs(0),
    leftBarPeakHold(0),
    rightBarPeakHold(0),
    leftBarDecayTimer(0),
    rightBarDecayTimer(0) {
    memset(bands, 0, sizeof(bands));
    memset(peakHold, 0, sizeof(peakHold));
    memset(peakDecayTimer, 0, sizeof(peakDecayTimer));
    memset(micBuffer, 0, sizeof(micBuffer));
    memset(ringBuffer, 0, sizeof(ringBuffer));
    memset(ringBufferL, 0, sizeof(ringBufferL));
    memset(ringBufferR, 0, sizeof(ringBufferR));
    memset(spec8PeakHold, 0, sizeof(spec8PeakHold));
    memset(spec8DecayTimer, 0, sizeof(spec8DecayTimer));
}

void SpectrumAnalyzer::loadSettings() {
    Preferences prefs;
    if (prefs.begin("qtune_vis", true)) {
        uint8_t s = prefs.getUChar("sens", (uint8_t)VIS_SENS_NORMAL);
        if (s > 2) s = (uint8_t)VIS_SENS_NORMAL;
        sensitivity = (VisualizerSensitivity)s;
        prefs.end();
    }
}

void SpectrumAnalyzer::saveSettings() {
    Preferences prefs;
    if (prefs.begin("qtune_vis", false)) {
        prefs.putUChar("sens", (uint8_t)sensitivity);
        prefs.end();
    }
}

void SpectrumAnalyzer::setSensitivity(VisualizerSensitivity sens) {
    sensitivity = sens;
    saveSettings();
    wakeTopBar(5000);
}

void SpectrumAnalyzer::cycleSensitivity() {
    if (sensitivity == VIS_SENS_LOW) {
        sensitivity = VIS_SENS_NORMAL;
    } else if (sensitivity == VIS_SENS_NORMAL) {
        sensitivity = VIS_SENS_HIGH;
    } else {
        sensitivity = VIS_SENS_LOW;
    }
    saveSettings();
    wakeTopBar(5000);
    sensToastExpiryMs = millis() + 2000;
}

const char* SpectrumAnalyzer::getSensitivityName() const {
    switch (sensitivity) {
        case VIS_SENS_LOW:    return "LOW";
        case VIS_SENS_NORMAL: return "NORMAL";
        case VIS_SENS_HIGH:   return "HIGH";
        default:              return "NORMAL";
    }
}

const char* SpectrumAnalyzer::getSensitivityShortName() const {
    switch (sensitivity) {
        case VIS_SENS_LOW:    return "LOW";
        case VIS_SENS_NORMAL: return "NRM";
        case VIS_SENS_HIGH:   return "HGH";
        default:              return "NRM";
    }
}

float SpectrumAnalyzer::getSensitivityMultiplier() const {
    switch (sensitivity) {
        case VIS_SENS_LOW:    return 0.60f;
        case VIS_SENS_NORMAL: return 1.00f;
        case VIS_SENS_HIGH:   return 1.75f;
        default:              return 1.00f;
    }
}

void SpectrumAnalyzer::wakeTopBar(unsigned long durationMs) {
    topBarExpiryMs = millis() + durationMs;
}

bool SpectrumAnalyzer::isTopBarVisible() const {
    return millis() < topBarExpiryMs;
}

void SpectrumAnalyzer::resetTopBar() {
    topBarExpiryMs = 0;
}

bool SpectrumAnalyzer::begin() {
    loadSettings();
    active = false;
    micHardwareInitialized = false;
    peakLevel = 0.0f;
    rmsLevel = 0.0f;
    bassLevel = 0.0f;
    bassFilterState1 = 0.0f;
    bassFilterState2 = 0.0f;
    topBarExpiryMs = 0;
    sensToastExpiryMs = 0;
    ringHead = 0;
    lastSampleFeedMs = 0;
    lastAnalysisMs = 0;
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
    wakeTopBar(5000);
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
        int16_t sampleL, sampleR;
        if (channels == 1) {
            sampleL = samples[i];
            sampleR = samples[i];
        } else {
            sampleL = samples[i * 2];
            sampleR = samples[i * 2 + 1];
        }
        ringBufferL[head] = sampleL;
        ringBufferR[head] = sampleR;
        // Downmix stereo (L + R) / 2 to mono for FFT
        ringBuffer[head] = (int16_t)(((int32_t)sampleL + (int32_t)sampleR) / 2);
        head = (head + 1) % RING_BUFFER_SIZE;
    }
    ringHead = head;
    lastSampleFeedMs = millis();
}

void SpectrumAnalyzer::clearSamples() {
    memset(ringBuffer, 0, sizeof(ringBuffer));
    memset(ringBufferL, 0, sizeof(ringBufferL));
    memset(ringBufferR, 0, sizeof(ringBufferR));
    memset(micBuffer, 0, sizeof(micBuffer));
    memset(bands, 0, sizeof(bands));
    memset(peakHold, 0, sizeof(peakHold));
    memset(peakDecayTimer, 0, sizeof(peakDecayTimer));
    memset(spec8PeakHold, 0, sizeof(spec8PeakHold));
    memset(spec8DecayTimer, 0, sizeof(spec8DecayTimer));
    peakLevel = 0.0f;
    rmsLevel = 0.0f;
    bassLevel = 0.0f;
    bassFilterState1 = 0.0f;
    bassFilterState2 = 0.0f;
    voicePitch = 0.0f;
    voiceConfidence = 0.0f;
    leftPeakLevel = 0.0f;
    rightPeakLevel = 0.0f;
    leftRmsLevel = 0.0f;
    rightRmsLevel = 0.0f;
    leftDb = -20.0f;
    rightDb = -20.0f;
    needlePos = 0.0f;
    needleVel = 0.0f;
    leftBarPeakHold = 0;
    rightBarPeakHold = 0;
    leftBarDecayTimer = 0;
    rightBarDecayTimer = 0;
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
        bassLevel = bassLevel * 0.70f;
        leftPeakLevel = leftPeakLevel * 0.70f;
        rightPeakLevel = rightPeakLevel * 0.70f;
        leftRmsLevel = leftRmsLevel * 0.70f;
        rightRmsLevel = rightRmsLevel * 0.70f;
        leftDb = leftDb * 0.85f - 3.0f;
        if (leftDb < -20.0f) leftDb = -20.0f;
        rightDb = rightDb * 0.85f - 3.0f;
        if (rightDb < -20.0f) rightDb = -20.0f;
        needlePos = needlePos * 0.75f;
        needleVel = 0.0f;
        if (leftBarPeakHold > 0) leftBarPeakHold--;
        if (rightBarPeakHold > 0) rightBarPeakHold--;
        for (int i = 0; i < 8; i++) {
            if (spec8PeakHold[i] > 0) spec8PeakHold[i]--;
        }
        bassFilterState1 = 0.0f;
        bassFilterState2 = 0.0f;
        voicePitch = 0.0f;
        voiceConfidence = voiceConfidence * 0.70f;
        for (size_t i = 0; i < SAMPLE_SIZE; i++) {
            micBuffer[i] = 0;
        }
        return;
    }

    // Rate-limit analysis to ~66 FPS (at least 15 ms between passes)
    if (now - lastAnalysisMs < 15) {
        return;
    }
    lastAnalysisMs = now;

    // Capture latest SAMPLE_SIZE samples from lock-free circular ring buffer
    size_t head = ringHead;
    size_t start = (head + RING_BUFFER_SIZE - SAMPLE_SIZE) % RING_BUFFER_SIZE;

    int64_t sum = 0;
    float leftSumSq = 0.0f;
    float rightSumSq = 0.0f;
    int16_t lPeak = 0;
    int16_t rPeak = 0;

    for (size_t i = 0; i < SAMPLE_SIZE; i++) {
        size_t idx = (start + i) % RING_BUFFER_SIZE;
        // Scale 16-bit PCM down (>> 6) to match calibrated FFT dynamic range
        int16_t s = ringBuffer[idx] >> 6;
        int16_t sL = ringBufferL[idx] >> 6;
        int16_t sR = ringBufferR[idx] >> 6;
        micBuffer[i] = s;
        sum += s;

        int16_t aL = abs(sL);
        int16_t aR = abs(sR);
        if (aL > lPeak) lPeak = aL;
        if (aR > rPeak) rPeak = aR;
        leftSumSq += (float)aL * aL;
        rightSumSq += (float)aR * aR;
    }

    // DC bias removal
    int16_t dc = (int16_t)(sum / SAMPLE_SIZE);
    float sumSq = 0.0f;
    int16_t peak = 0;

    // 2-pole IIR low-pass filter at ~160 Hz for dedicated true bass/kick transient detection
    // alpha = 2 * pi * 160 / 44100 ~= 0.0228f
    const float alpha = 0.0228f;
    float bassSumSq = 0.0f;

    for (size_t i = 0; i < SAMPLE_SIZE; i++) {
        micBuffer[i] -= dc;
        int16_t a = abs(micBuffer[i]);
        if (a > peak) peak = a;
        sumSq += (float)a * a;

        // Cascade two single-pole low pass filters for >40x rejection of mids/highs
        float raw = (float)micBuffer[i];
        bassFilterState1 += alpha * (raw - bassFilterState1);
        bassFilterState2 += alpha * (bassFilterState1 - bassFilterState2);
        bassSumSq += bassFilterState2 * bassFilterState2;
    }

    float sensMult = getSensitivityMultiplier();
    peakLevel = (float)peak * sensMult;
    rmsLevel = sqrtf(sumSq / SAMPLE_SIZE) * sensMult;
    bassLevel = sqrtf(bassSumSq / SAMPLE_SIZE) * sensMult;

    leftPeakLevel = (float)lPeak * sensMult;
    rightPeakLevel = (float)rPeak * sensMult;
    leftRmsLevel = sqrtf(leftSumSq / SAMPLE_SIZE) * sensMult;
    rightRmsLevel = sqrtf(rightSumSq / SAMPLE_SIZE) * sensMult;

    // Standard VU dB calculations (-18 dBFS = 0 VU reference -> ~64.0 amplitude)
    float vL = leftRmsLevel / 64.0f;
    float vR = rightRmsLevel / 64.0f;
    leftDb = (vL > 0.05f) ? (20.0f * log10f(vL)) : -20.0f;
    if (leftDb > 3.5f) leftDb = 3.5f;
    if (leftDb < -20.0f) leftDb = -20.0f;

    rightDb = (vR > 0.05f) ? (20.0f * log10f(vR)) : -20.0f;
    if (rightDb > 3.5f) rightDb = 3.5f;
    if (rightDb < -20.0f) rightDb = -20.0f;

    // Ballistic simulation (300ms rise time to 99%, ANSI C16.5 mechanical response)
    float dt = (lastBallisticMs > 0 && now > lastBallisticMs) ? ((float)(now - lastBallisticMs) / 1000.0f) : 0.020f;
    if (dt > 0.1f) dt = 0.020f;
    lastBallisticMs = now;

    float vMax = (vL >= vR) ? vL : vR;
    float targetDeflection = (vMax > 0.08f) ? ((vMax - 0.08f) / 1.33f) : 0.0f;
    if (targetDeflection > 1.08f) targetDeflection = 1.08f;
    if (targetDeflection < 0.0f) targetDeflection = 0.0f;

    float err = targetDeflection - needlePos;
    needleVel += (err * 120.0f - needleVel * 15.0f) * dt;
    needlePos += needleVel * dt;
    if (needlePos < 0.0f) { needlePos = 0.0f; needleVel = 0.0f; }
    if (needlePos > 1.08f) { needlePos = 1.08f; needleVel = 0.0f; }

    // Horizontal bar peak hold update
    uint8_t barWL = (uint8_t)constrain((int)(vL * 58.0f), 0, 84);
    uint8_t barWR = (uint8_t)constrain((int)(vR * 58.0f), 0, 84);
    if (barWL >= leftBarPeakHold) {
        leftBarPeakHold = barWL;
        leftBarDecayTimer = 0;
    } else {
        leftBarDecayTimer++;
        if (leftBarDecayTimer > 20 && leftBarPeakHold > 0) {
            leftBarPeakHold--;
        }
    }
    if (barWR >= rightBarPeakHold) {
        rightBarPeakHold = barWR;
        rightBarDecayTimer = 0;
    } else {
        rightBarDecayTimer++;
        if (rightBarDecayTimer > 20 && rightBarPeakHold > 0) {
            rightBarPeakHold--;
        }
    }

    processFFT();

    // 8-band spectrometer peak hold update for hybrid mode
    for (int b = 0; b < 8; b++) {
        uint8_t h8 = (bands[b * 2] + bands[b * 2 + 1]) / 2;
        if (h8 >= spec8PeakHold[b]) {
            spec8PeakHold[b] = h8;
            spec8DecayTimer[b] = 0;
        } else {
            spec8DecayTimer[b]++;
            if (spec8DecayTimer[b] > 3 && spec8PeakHold[b] > 0) {
                spec8PeakHold[b]--;
                spec8DecayTimer[b] = 0;
            }
        }
    }

    detectVoicePitch();
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
    bassLevel = rmsLevel * 0.7f;

    processFFT();
    detectVoicePitch();
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

        // High frequency equal loudness pre-emphasis & sensitivity scaling
        float boost = (1.0f + ((float)b * 0.15f)) * getSensitivityMultiplier();
        float val = bandMax * boost;

        uint8_t targetHeight = 0;
        if (val > 40.0f) {
            float scaled = log10f(val / 40.0f) * 28.0f;
            if (scaled > 62.0f) scaled = 62.0f;
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

void SpectrumAnalyzer::detectVoicePitch() {
    // Human voice fundamental pitch range: ~80 Hz to ~800 Hz
    // Downsample circular ring buffer by 4: effective fs = 44100 / 4 = 11025 Hz
    // At fs = 11025 Hz:
    // Min lag = 11025 / 800 ~= 14 samples (~787 Hz)
    // Max lag = 11025 / 80  ~= 138 samples (~80 Hz)
    const size_t DOWNSAMPLE = 4;
    const size_t N_SAMPLES = 256;
    int16_t down[N_SAMPLES];

    size_t head = ringHead;
    size_t start = (head + RING_BUFFER_SIZE - (N_SAMPLES * DOWNSAMPLE)) % RING_BUFFER_SIZE;

    int32_t dcSum = 0;
    for (size_t i = 0; i < N_SAMPLES; i++) {
        size_t idx = (start + i * DOWNSAMPLE) % RING_BUFFER_SIZE;
        down[i] = ringBuffer[idx];
        dcSum += down[i];
    }
    int16_t dc = (int16_t)(dcSum / (int32_t)N_SAMPLES);
    for (size_t i = 0; i < N_SAMPLES; i++) {
        down[i] -= dc;
    }

    // High-pass filter at ~80 Hz on the downsampled buffer to reject sub-bass rumble and kick drum fundamental
    // 1-pole high-pass: y[n] = x[n] - x[n-1] + 0.95 * y[n-1]
    float hpPrevX = (float)down[0];
    float hpPrevY = 0.0f;
    for (size_t i = 0; i < N_SAMPLES; i++) {
        float x = (float)down[i];
        float y = x - hpPrevX + 0.95f * hpPrevY;
        hpPrevX = x;
        hpPrevY = y;
        down[i] = (int16_t)constrain((int)y, -32767, 32767);
    }

    // Correlation window length N = 100 samples (~9.1 ms, covers full fundamental cycles down to 110 Hz)
    const size_t CORR_LEN = 100;
    const size_t MIN_LAG = 14;  // ~787 Hz
    const size_t MAX_LAG = 138; // ~80 Hz

    float energy0 = 0.0f;
    for (size_t n = 0; n < CORR_LEN; n++) {
        float s = (float)down[n];
        energy0 += s * s;
    }

    if (energy0 < 1000.0f) {
        // Below acoustic noise floor
        voiceConfidence = voiceConfidence * 0.70f;
        if (voiceConfidence < 0.05f) voicePitch = 0.0f;
        return;
    }

    float bestCorr = -1.0f;
    size_t bestLag = 0;
    float sumCorr = 0.0f;
    size_t lagCount = 0;

    for (size_t lag = MIN_LAG; lag <= MAX_LAG; lag++) {
        float num = 0.0f;
        float energyLag = 0.0f;
        for (size_t n = 0; n < CORR_LEN; n++) {
            float s0 = (float)down[n];
            float sLag = (float)down[n + lag];
            num += s0 * sLag;
            energyLag += sLag * sLag;
        }

        float denom = sqrtf(energy0 * energyLag) + 1.0f;
        float normCorr = num / denom;
        sumCorr += fabsf(normCorr);
        lagCount++;

        if (normCorr > bestCorr) {
            bestCorr = normCorr;
            bestLag = lag;
        }
    }

    float avgCorr = (lagCount > 0) ? (sumCorr / (float)lagCount) : 0.1f;
    float peakProminence = bestCorr / (avgCorr + 0.02f);

    float refinedLag = (float)bestLag;
    if (bestLag > MIN_LAG && bestLag < MAX_LAG && bestCorr > 0.22f) {
        // Parabolic interpolation for fine sub-sample pitch resolution
        auto getCorr = [&](size_t l) -> float {
            float num = 0.0f, el = 0.0f;
            for (size_t n = 0; n < CORR_LEN; n++) {
                float s0 = (float)down[n];
                float sl = (float)down[n + l];
                num += s0 * sl;
                el += sl * sl;
            }
            return num / (sqrtf(energy0 * el) + 1.0f);
        };
        float y1 = getCorr(bestLag - 1);
        float y2 = bestCorr;
        float y3 = getCorr(bestLag + 1);
        float denom = 2.0f * (2.0f * y2 - y1 - y3);
        if (fabsf(denom) > 1e-4f) {
            float delta = (y3 - y1) / denom;
            refinedLag += constrain(delta, -0.5f, 0.5f);
        }
    }

    // Require strong correlation & distinct peak prominence to reject instrumental solos
    float reqCorr = (voiceConfidence > 0.30f) ? 0.22f : 0.28f;
    if (refinedLag >= (float)MIN_LAG && bestCorr >= reqCorr && peakProminence >= 1.25f) {
        float rawPitch = 11025.0f / refinedLag;
        // Smooth pitch tracking across vocal octaves
        if (voiceConfidence > 0.20f && voicePitch > 60.0f) {
            voicePitch += (rawPitch - voicePitch) * 0.35f;
        } else {
            voicePitch = rawPitch;
        }
        float confTarget = constrain(bestCorr * (peakProminence / 2.0f), 0.0f, 1.0f);
        voiceConfidence += (confTarget - voiceConfidence) * 0.35f;
    } else {
        voiceConfidence = voiceConfidence * 0.78f;
        if (voiceConfidence < 0.05f) {
            voicePitch = 0.0f;
        }
    }
}

void SpectrumAnalyzer::nextPreset() {
    currentPreset = (VisualizerPreset)((currentPreset + 1) % PRESET_COUNT);
    wakeTopBar(5000);
}

void SpectrumAnalyzer::previousPreset() {
    currentPreset = (VisualizerPreset)((currentPreset + PRESET_COUNT - 1) % PRESET_COUNT);
    wakeTopBar(5000);
}

const char* SpectrumAnalyzer::getPresetName() const {
    switch (currentPreset) {
        case PRESET_ANALOG_VU:       return "ANALOG VU";
        case PRESET_HORIZ_BAR_VU:    return "HORIZ VU";
        case PRESET_VU_SPECTROMETER: return "VU+SPEC";
        case PRESET_BAR_SPECTRUM:    return "BARS";
        case PRESET_WAVEFORM:        return "WAVE";
        case PRESET_MILKDROP_PLASMA: return "PLASMA";
        case PRESET_STARFIELD:       return "STAR";
        default:                     return "ANALOG VU";
    }
}

void SpectrumAnalyzer::render() {
    render(currentPreset);
}

void SpectrumAnalyzer::render(VisualizerPreset preset) {
    switch (preset) {
        case PRESET_ANALOG_VU: drawAnalogVU(); break;
        case PRESET_HORIZ_BAR_VU: drawHorizontalBarVU(); break;
        case PRESET_VU_SPECTROMETER: drawVUSpectrometer(); break;
        case PRESET_BAR_SPECTRUM: drawBars(); break;
        case PRESET_WAVEFORM: drawWaveform(); break;
        case PRESET_MILKDROP_PLASMA: drawPlasma(); break;
        case PRESET_STARFIELD: drawStarfield(); break;
        default: drawAnalogVU(); break;
    }
}

void SpectrumAnalyzer::drawAnalogVU() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    bool showBar = isTopBarVisible();
    if (showBar) {
        String track = audioPlayer.getCurrentTrackName();
        if (track.length() == 0) {
            track = audioPlayer.isPlaying() ? (audioPlayer.isFLAC() ? "FLAC Audio" : "Live Audio") : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
        }
        if (track.length() > 13) {
            track = track.substring(0, 11) + "..";
        }
        char headerBuf[32];
        if (millis() < sensToastExpiryMs) {
            snprintf(headerBuf, sizeof(headerBuf), "SENS: %s (%.1fx)", getSensitivityName(), getSensitivityMultiplier());
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "[VU:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    int pivotX = 64;
    int pivotY = showBar ? 63 : 62;
    int arcR = showBar ? 42 : 46;
    int needleR = showBar ? 44 : 48;

    // Curved meter scale arc from 150 deg down to 30 deg
    for (int deg = 150; deg >= 30; deg -= 2) {
        float rad = (float)deg * (float)M_PI / 180.0f;
        int x = pivotX + (int)(cosf(rad) * arcR);
        int y = pivotY - (int)(sinf(rad) * arcR);
        u8g2.drawPixel(x, y);

        // Danger sector from 68 down to 30 deg (0 dB to +3 dB): reinforced double arc
        if (deg <= 68) {
            int xIn = pivotX + (int)(cosf(rad) * (arcR - 1));
            int yIn = pivotY - (int)(sinf(rad) * (arcR - 1));
            u8g2.drawPixel(xIn, yIn);
        }
    }

    // Tick marks across the scale
    struct VUTick {
        float deg;
        bool major;
    };
    static const VUTick ticks[] = {
        {150.0f, true},  // -20
        {128.5f, true},  // -10
        {117.0f, false}, // -7
        {106.5f, true},  // -5
        {93.5f,  false}, // -3
        {77.0f,  false}, // -1
        {67.5f,  true},  //  0
        {56.0f,  false}, // +1
        {43.5f,  false}, // +2
        {30.0f,  true}   // +3
    };

    for (size_t i = 0; i < sizeof(ticks) / sizeof(ticks[0]); i++) {
        float rad = ticks[i].deg * (float)M_PI / 180.0f;
        int rIn = arcR - (ticks[i].major ? 4 : 2);
        int rOut = arcR + (ticks[i].major ? 4 : 2);
        int x0 = pivotX + (int)(cosf(rad) * rIn);
        int y0 = pivotY - (int)(sinf(rad) * rIn);
        int x1 = pivotX + (int)(cosf(rad) * rOut);
        int y1 = pivotY - (int)(sinf(rad) * rOut);
        u8g2.drawLine(x0, y0, x1, y1);
    }

    // Scale numeric labels
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(12, showBar ? 42 : 39, "-20");
    u8g2.drawStr(27, showBar ? 28 : 24, "-10");
    u8g2.drawStr(46, showBar ? 20 : 16, "-5");
    u8g2.drawStr(77, showBar ? 21 : 17, "0");
    u8g2.drawStr(104, showBar ? 42 : 39, "+3");

    // Center "VU" label
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(59, pivotY - 14, "VU");

    // Moving Needle with ballistics
    float clampedPos = needlePos;
    if (clampedPos < 0.0f) clampedPos = 0.0f;
    if (clampedPos > 1.05f) clampedPos = 1.05f;
    float needleDeg = 150.0f - clampedPos * 120.0f;
    float needleRad = needleDeg * (float)M_PI / 180.0f;
    int tipX = pivotX + (int)(cosf(needleRad) * needleR);
    int tipY = pivotY - (int)(sinf(needleRad) * needleR);

    // Needle stroke with solid visual presence
    u8g2.drawLine(pivotX, pivotY, tipX, tipY);
    int baseOffX = (needleDeg < 90.0f) ? -1 : 1;
    u8g2.drawLine(pivotX + baseOffX, pivotY, tipX, tipY);

    // Center pivot hub disk
    u8g2.drawDisc(pivotX, pivotY, 3);
    u8g2.setDrawColor(0);
    u8g2.drawPixel(pivotX, pivotY);
    u8g2.setDrawColor(1);

    // Lower corners: digital peak/dB readouts
    u8g2.setFont(u8g2_font_4x6_tr);
    char lBuf[16], rBuf[16];
    if (leftDb <= -19.5f) {
        snprintf(lBuf, sizeof(lBuf), "L:--.-");
    } else {
        snprintf(lBuf, sizeof(lBuf), "L:%+.1f", leftDb);
    }
    if (rightDb <= -19.5f) {
        snprintf(rBuf, sizeof(rBuf), "R:--.-");
    } else {
        snprintf(rBuf, sizeof(rBuf), "R:%+.1f", rightDb);
    }
    u8g2.drawStr(2, 62, lBuf);
    u8g2.drawStr(98, 62, rBuf);

    // Peak clipping / overload indicator
    if (leftDb >= 0.0f || rightDb >= 0.0f) {
        u8g2.drawFrame(51, showBar ? 25 : 22, 26, 8);
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(53, showBar ? 31 : 28, "CLIP");
    }

    display.sendBuffer();
}

void SpectrumAnalyzer::drawHorizontalBarVU() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    bool showBar = isTopBarVisible();
    if (showBar) {
        String track = audioPlayer.getCurrentTrackName();
        if (track.length() == 0) {
            track = audioPlayer.isPlaying() ? (audioPlayer.isFLAC() ? "FLAC Audio" : "Live Audio") : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
        }
        if (track.length() > 13) {
            track = track.substring(0, 11) + "..";
        }
        char headerBuf[32];
        if (millis() < sensToastExpiryMs) {
            snprintf(headerBuf, sizeof(headerBuf), "SENS: %s (%.1fx)", getSensitivityName(), getSensitivityMultiplier());
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "[HRZ:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    int startY = showBar ? 15 : 6;
    int barW = 86;
    int barH = 7;
    int barX = 14;
    int lY = startY;
    int rY = startY + 20;
    int zeroMarkX = barX + (int)(barW * 0.69f);

    // Channel labels
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, lY + 6, "L");
    u8g2.drawStr(2, rY + 6, "R");

    // Outer bar frames
    u8g2.drawFrame(barX, lY, barW, barH);
    u8g2.drawFrame(barX, rY, barW, barH);

    auto dbToWidth = [&](float db) -> int {
        if (db <= -20.0f) return 0;
        float norm = (db + 20.0f) / 23.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        return (int)(norm * (barW - 2));
    };

    int fillWL = dbToWidth(leftDb);
    int fillWR = dbToWidth(rightDb);
    int zeroPixelW = zeroMarkX - barX - 1;

    // Fill Left Bar
    if (fillWL > 0) {
        int safeW = (fillWL <= zeroPixelW) ? fillWL : zeroPixelW;
        u8g2.drawBox(barX + 1, lY + 1, safeW, barH - 2);
        if (fillWL > zeroPixelW) {
            int overW = fillWL - zeroPixelW;
            for (int dx = 0; dx < overW; dx++) {
                int px = barX + 1 + zeroPixelW + dx;
                for (int dy = 0; dy < barH - 2; dy++) {
                    if ((px + dy) % 2 == 0) u8g2.drawPixel(px, lY + 1 + dy);
                }
            }
        }
        u8g2.setDrawColor(0);
        for (int sx = barX + 4; sx < barX + fillWL; sx += 4) {
            u8g2.drawVLine(sx, lY + 1, barH - 2);
        }
        u8g2.setDrawColor(1);
    }

    // Fill Right Bar
    if (fillWR > 0) {
        int safeW = (fillWR <= zeroPixelW) ? fillWR : zeroPixelW;
        u8g2.drawBox(barX + 1, rY + 1, safeW, barH - 2);
        if (fillWR > zeroPixelW) {
            int overW = fillWR - zeroPixelW;
            for (int dx = 0; dx < overW; dx++) {
                int px = barX + 1 + zeroPixelW + dx;
                for (int dy = 0; dy < barH - 2; dy++) {
                    if ((px + dy) % 2 == 0) u8g2.drawPixel(px, rY + 1 + dy);
                }
            }
        }
        u8g2.setDrawColor(0);
        for (int sx = barX + 4; sx < barX + fillWR; sx += 4) {
            u8g2.drawVLine(sx, rY + 1, barH - 2);
        }
        u8g2.setDrawColor(1);
    }

    // Floating Peak Hold indicators
    int peakPixL = leftBarPeakHold;
    if (peakPixL > barW - 3) peakPixL = barW - 3;
    if (peakPixL > 0) {
        u8g2.drawVLine(barX + 1 + peakPixL, lY - 1, barH + 2);
    }

    int peakPixR = rightBarPeakHold;
    if (peakPixR > barW - 3) peakPixR = barW - 3;
    if (peakPixR > 0) {
        u8g2.drawVLine(barX + 1 + peakPixR, rY - 1, barH + 2);
    }

    // Central Scale markings between L and R bars
    int scaleY = startY + 11;
    u8g2.drawHLine(barX, scaleY + 3, barW);

    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawVLine(barX, scaleY + 1, 5);
    u8g2.drawStr(barX - 2, scaleY + 2, "-20");

    int x10 = barX + (int)(barW * 0.25f);
    u8g2.drawVLine(x10, scaleY + 1, 5);
    u8g2.drawStr(x10 - 4, scaleY + 2, "-10");

    int x5 = barX + (int)(barW * 0.48f);
    u8g2.drawVLine(x5, scaleY + 1, 5);
    u8g2.drawStr(x5 - 2, scaleY + 2, "-5");

    u8g2.drawVLine(zeroMarkX, scaleY - 1, 9);
    u8g2.drawStr(zeroMarkX - 2, scaleY + 2, "0");

    int x3 = barX + barW - 1;
    u8g2.drawVLine(x3, scaleY + 1, 5);
    u8g2.drawStr(x3 - 6, scaleY + 2, "+3");

    // Right-side numeric readouts
    u8g2.setFont(u8g2_font_4x6_tr);
    char bufL[12], bufR[12];
    snprintf(bufL, sizeof(bufL), (leftDb <= -19.5f) ? "--.-" : "%+.1f", leftDb);
    snprintf(bufR, sizeof(bufR), (rightDb <= -19.5f) ? "--.-" : "%+.1f", rightDb);
    u8g2.drawStr(103, lY + 6, bufL);
    u8g2.drawStr(103, rY + 6, bufR);

    // Bottom telemetry strip
    int bottomY = rY + barH + 9;
    if (bottomY <= 63) {
        u8g2.drawHLine(0, bottomY - 3, 128);
        char botBuf[36];
        snprintf(botBuf, sizeof(botBuf), "RMS:%-4.0f PEAK:%-4.0f CAL:-18VU", rmsLevel, peakLevel);
        u8g2.drawStr(4, bottomY + 5, botBuf);
    }

    display.sendBuffer();
}

void SpectrumAnalyzer::drawVUSpectrometer() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    bool showBar = isTopBarVisible();
    if (showBar) {
        String track = audioPlayer.getCurrentTrackName();
        if (track.length() == 0) {
            track = audioPlayer.isPlaying() ? (audioPlayer.isFLAC() ? "FLAC Audio" : "Live Audio") : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
        }
        if (track.length() > 13) {
            track = track.substring(0, 11) + "..";
        }
        char headerBuf[32];
        if (millis() < sensToastExpiryMs) {
            snprintf(headerBuf, sizeof(headerBuf), "SENS: %s (%.1fx)", getSensitivityName(), getSensitivityMultiplier());
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "[V+S:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    // TOP PANE: Compact Dual Stereo VU Bars
    int vuY = showBar ? 14 : 3;
    int barW = 82;
    int barH = 5;
    int barX = 14;

    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(2, vuY + 5, "L");
    u8g2.drawStr(2, vuY + 12, "R");

    u8g2.drawFrame(barX, vuY, barW, barH);
    u8g2.drawFrame(barX, vuY + 7, barW, barH);

    int zeroX = barX + (int)(barW * 0.69f);
    u8g2.drawVLine(zeroX, vuY - 1, 14);

    auto dbToCompactW = [&](float db) -> int {
        if (db <= -20.0f) return 0;
        float norm = (db + 20.0f) / 23.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        return (int)(norm * (barW - 2));
    };
    int wL = dbToCompactW(leftDb);
    int wR = dbToCompactW(rightDb);

    if (wL > 0) u8g2.drawBox(barX + 1, vuY + 1, wL, barH - 2);
    if (wR > 0) u8g2.drawBox(barX + 1, vuY + 8, wR, barH - 2);

    int pkL = (leftBarPeakHold * barW) / 88;
    int pkR = (rightBarPeakHold * barW) / 88;
    if (pkL > barW - 3) pkL = barW - 3;
    if (pkR > barW - 3) pkR = barW - 3;
    if (pkL > 0) u8g2.drawVLine(barX + 1 + pkL, vuY - 1, barH + 2);
    if (pkR > 0) u8g2.drawVLine(barX + 1 + pkR, vuY + 6, barH + 2);

    char vDbBuf[16];
    float maxDb = (leftDb >= rightDb) ? leftDb : rightDb;
    snprintf(vDbBuf, sizeof(vDbBuf), (maxDb <= -19.5f) ? "--.-" : "%+.1f", maxDb);
    u8g2.drawStr(100, vuY + 8, vDbBuf);

    // DIVIDER LINE
    int divY = vuY + 15;
    u8g2.drawHLine(0, divY, 128);

    // BOTTOM PANE: 8-Band Real-Time Audio FFT Frequency Spectrometer
    static const char* bandLabels[8] = {"63", "125", "250", "500", "1k", "2k", "4k", "8k"};
    int maxSpecH = 63 - (divY + 8);
    int specBottomY = 63 - 7;
    int numBands = 8;
    int bWidth = 11;
    int bSpacing = 4;
    int startX = 6;

    for (int b = 0; b < numBands; b++) {
        int val = (bands[b * 2] + bands[b * 2 + 1]) / 2;
        int h = (val * maxSpecH) / 48;
        if (h > maxSpecH) h = maxSpecH;
        if (h < 0) h = 0;

        int bx = startX + b * (bWidth + bSpacing);
        int by = specBottomY - h;

        if (h > 0) {
            u8g2.drawBox(bx, by, bWidth, h);
            u8g2.setDrawColor(0);
            for (int sy = specBottomY - 3; sy > by; sy -= 3) {
                u8g2.drawHLine(bx, sy, bWidth);
            }
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawHLine(bx, specBottomY, bWidth);
        }

        int capH = (spec8PeakHold[b] * maxSpecH) / 48;
        if (capH > maxSpecH) capH = maxSpecH;
        if (capH > 0) {
            int capY = specBottomY - capH;
            u8g2.drawHLine(bx, capY, bWidth);
        }

        u8g2.setFont(u8g2_font_4x6_tr);
        if (b % 2 == 0) {
            u8g2.drawStr(bx, 63, bandLabels[b]);
        }
    }

    display.sendBuffer();
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

    bool showBar = isTopBarVisible();
    if (showBar) {
        String track = audioPlayer.getCurrentTrackName();
        if (track.length() == 0) {
            track = audioPlayer.isPlaying() ? (audioPlayer.isFLAC() ? "FLAC Audio" : "Live Audio") : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
        }
        if (track.length() > 13) {
            track = track.substring(0, 11) + "..";
        }
        char headerBuf[32];
        if (millis() < sensToastExpiryMs) {
            snprintf(headerBuf, sizeof(headerBuf), "SENS: %s (%.1fx)", getSensitivityName(), getSensitivityMultiplier());
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "[BARS:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    if (!audioPlayer.isPlaying() && peakLevel < 5.0f) {
        if (audioPlayer.isPaused()) {
            u8g2.drawStr(36, showBar ? 36 : 32, "❚❚ PAUSED");
        } else {
            u8g2.drawStr(24, showBar ? 36 : 32, "No Active Stream");
        }
    }

    int maxBarH = showBar ? 48 : 62;
    int minCapY = showBar ? 14 : 1;
    int barWidth = 6;
    for (int i = 0; i < 16; i++) {
        int h = bands[i];
        if (h > maxBarH) h = maxBarH;
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
            if (capY < minCapY) capY = minCapY;
            u8g2.drawHLine(x, capY, barWidth);
        }
    }
    display.sendBuffer();
}

void SpectrumAnalyzer::drawWaveform() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    bool showBar = isTopBarVisible();
    if (showBar) {
        String track = audioPlayer.getCurrentTrackName();
        if (track.length() == 0) {
            track = audioPlayer.isPlaying() ? (audioPlayer.isFLAC() ? "FLAC Audio" : "Live Audio") : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
        }
        if (track.length() > 13) {
            track = track.substring(0, 11) + "..";
        }
        char headerBuf[32];
        if (millis() < sensToastExpiryMs) {
            snprintf(headerBuf, sizeof(headerBuf), "SENS: %s (%.1fx)", getSensitivityName(), getSensitivityMultiplier());
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "[WAVE:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    int centerY = showBar ? 38 : 32;
    int minY = showBar ? 14 : 1;
    int maxY = showBar ? 62 : 63;
    float maxAmp = showBar ? 22.0f : 29.0f;

    float scale = 0.01f;
    if (peakLevel > 20.0f) {
        scale = maxAmp / peakLevel;
        if (scale > 0.18f) scale = 0.18f;
    }

    int prevY = centerY;
    for (int x = 0; x < 128; x++) {
        int idx = (x * SAMPLE_SIZE) / 128;
        int val = (int)(micBuffer[idx] * scale);
        int y = centerY - val;
        if (y < minY) y = minY;
        if (y > maxY) y = maxY;

        if (x > 0) {
            u8g2.drawLine(x - 1, prevY, x, y);
        } else {
            u8g2.drawPixel(x, y);
        }
        prevY = y;
    }

    // Center baseline dots
    for (int x = 0; x < 128; x += 16) {
        u8g2.drawPixel(x, centerY);
    }

    display.sendBuffer();
}

void SpectrumAnalyzer::drawPlasma() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    bool showBar = isTopBarVisible();
    if (showBar) {
        String track = audioPlayer.getCurrentTrackName();
        if (track.length() == 0) {
            track = audioPlayer.isPlaying() ? (audioPlayer.isFLAC() ? "FLAC Audio" : "Live Audio") : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
        }
        if (track.length() > 12) {
            track = track.substring(0, 10) + "..";
        }
        char headerBuf[32];
        if (millis() < sensToastExpiryMs) {
            snprintf(headerBuf, sizeof(headerBuf), "SENS: %s (%.1fx)", getSensitivityName(), getSensitivityMultiplier());
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "[PLS:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    float energy = (rmsLevel / 300.0f);
    if (energy > 2.5f) energy = 2.5f;
    float t = millis() * 0.002f * (1.0f + energy);

    int startY = showBar ? 16 : 0;
    for (int x = 0; x < 128; x += 4) {
        for (int y = startY; y < 64; y += 4) {
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

    bool showBar = isTopBarVisible();
    if (showBar) {
        String track = audioPlayer.getCurrentTrackName();
        if (track.length() == 0) {
            track = audioPlayer.isPlaying() ? (audioPlayer.isFLAC() ? "FLAC Audio" : "Live Audio") : (audioPlayer.isPaused() ? "PAUSED" : "IDLE");
        }
        if (track.length() > 13) {
            track = track.substring(0, 11) + "..";
        }
        char headerBuf[32];
        if (millis() < sensToastExpiryMs) {
            snprintf(headerBuf, sizeof(headerBuf), "SENS: %s (%.1fx)", getSensitivityName(), getSensitivityMultiplier());
        } else {
            snprintf(headerBuf, sizeof(headerBuf), "[STR:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    int warp = (int)(rmsLevel / 60.0f);
    if (warp > 12) warp = 12;

    for (int i = 0; i < 20; i++) {
        int x = (i * 17 + ((millis() / 10) * (1 + warp))) % 128;
        int y = showBar ? (16 + (i * 7) % 48) : ((i * 7) % 64);
        if (warp > 2) {
            u8g2.drawHLine(x, y, min(warp, 5));
        } else {
            u8g2.drawPixel(x, y);
        }
    }
    display.sendBuffer();
}

SpectrumAnalyzer spectrumAnalyzer;
