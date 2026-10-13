#include "spectrum_analyzer.h"
#include "audio_player.h"
#include "fast_math.h"
#include "simd_accel.h"
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

// Precomputed 128-point Hanning window (eliminates 128 dynamic cosf calls per frame)
static const float HANNING_128[128] = {
     0.00000000f,      0.00061179f,      0.00244567f,      0.00549715f,      0.00975876f,      0.01522007f,      0.02186772f,      0.02968544f,
     0.03865409f,      0.04875174f,      0.05995367f,      0.07223246f,      0.08555808f,      0.09989790f,      0.11521684f,      0.13147741f,
     0.14863981f,      0.16666206f,      0.18550003f,      0.20510764f,      0.22543690f,      0.24643807f,      0.26805974f,      0.29024901f,
     0.31295157f,      0.33611188f,      0.35967324f,      0.38357801f,      0.40776768f,      0.43218306f,      0.45676440f,      0.48145154f,
     0.50618408f,      0.53090148f,      0.55554326f,      0.58004912f,      0.60435908f,      0.62841366f,      0.65215399f,      0.67552198f,
     0.69846043f,      0.72091321f,      0.74282539f,      0.76414333f,      0.78481486f,      0.80478941f,      0.82401809f,      0.84245384f,
     0.86005154f,      0.87676814f,      0.89256273f,      0.90739665f,      0.92123360f,      0.93403973f,      0.94578368f,      0.95643673f,
     0.96597280f,      0.97436855f,      0.98160345f,      0.98765978f,      0.99252273f,      0.99618039f,      0.99862382f,      0.99984703f,
     0.99984703f,      0.99862382f,      0.99618039f,      0.99252273f,      0.98765978f,      0.98160345f,      0.97436855f,      0.96597280f,
     0.95643673f,      0.94578368f,      0.93403973f,      0.92123360f,      0.90739665f,      0.89256273f,      0.87676814f,      0.86005154f,
     0.84245384f,      0.82401809f,      0.80478941f,      0.78481486f,      0.76414333f,      0.74282539f,      0.72091321f,      0.69846043f,
     0.67552198f,      0.65215399f,      0.62841366f,      0.60435908f,      0.58004912f,      0.55554326f,      0.53090148f,      0.50618408f,
     0.48145154f,      0.45676440f,      0.43218306f,      0.40776768f,      0.38357801f,      0.35967324f,      0.33611188f,      0.31295157f,
     0.29024901f,      0.26805974f,      0.24643807f,      0.22543690f,      0.20510764f,      0.18550003f,      0.16666206f,      0.14863981f,
     0.13147741f,      0.11521684f,      0.09989790f,      0.08555808f,      0.07223246f,      0.05995367f,      0.04875174f,      0.03865409f,
     0.02968544f,      0.02186772f,      0.01522007f,      0.00975876f,      0.00549715f,      0.00244567f,      0.00061179f,      0.00000000f
};

// Precomputed twiddle factors for 7 Radix-2 butterfly stages (len = 2, 4, 8, 16, 32, 64, 128)
struct TwiddleStage { float r; float i; };
static const TwiddleStage TWIDDLE_STAGES[7] = {
    {-1.00000000f,  0.00000000f}, // len=2: -pi
    { 0.00000000f, -1.00000000f}, // len=4: -pi/2
    { 0.70710678f, -0.70710678f}, // len=8: -pi/4
    { 0.92387953f, -0.38268343f}, // len=16: -pi/8
    { 0.98078528f, -0.19509032f}, // len=32: -pi/16
    { 0.99518473f, -0.09801714f}, // len=64: -pi/32
    { 0.99879546f, -0.04906767f}  // len=128: -pi/64
};

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
    leftPeakDb(-30.0f),
    rightPeakDb(-30.0f),
    clipExpiryMs(0),
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

bool SpectrumAnalyzer::isClipping() const {
    return millis() < clipExpiryMs;
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

    if (count > RING_BUFFER_SIZE) {
        size_t skip = count - RING_BUFFER_SIZE;
        samples += (channels == 2 ? skip * 2 : skip);
        count = RING_BUFFER_SIZE;
    }

    size_t head = ringHead;
    size_t firstPart = (count <= (RING_BUFFER_SIZE - head)) ? count : (RING_BUFFER_SIZE - head);
    size_t secondPart = count - firstPart;

    if (channels == 2) {
        simd_stereo_downmix_16(&ringBuffer[head], &ringBufferL[head], &ringBufferR[head], samples, firstPart);
        if (secondPart > 0) {
            simd_stereo_downmix_16(&ringBuffer[0], &ringBufferL[0], &ringBufferR[0], samples + firstPart * 2, secondPart);
        }
    } else {
        memcpy(&ringBuffer[head], samples, firstPart * sizeof(int16_t));
        memcpy(&ringBufferL[head], samples, firstPart * sizeof(int16_t));
        memcpy(&ringBufferR[head], samples, firstPart * sizeof(int16_t));
        if (secondPart > 0) {
            memcpy(&ringBuffer[0], samples + firstPart, secondPart * sizeof(int16_t));
            memcpy(&ringBufferL[0], samples + firstPart, secondPart * sizeof(int16_t));
            memcpy(&ringBufferR[0], samples + firstPart, secondPart * sizeof(int16_t));
        }
    }

    ringHead = (head + count) % RING_BUFFER_SIZE;
    lastSampleFeedMs = millis();
}

void SpectrumAnalyzer::clearSamples() {
    simd_clear_128((uint8_t*)ringBuffer, sizeof(ringBuffer));
    simd_clear_128((uint8_t*)ringBufferL, sizeof(ringBufferL));
    simd_clear_128((uint8_t*)ringBufferR, sizeof(ringBufferR));
    simd_clear_128((uint8_t*)micBuffer, sizeof(micBuffer));
    simd_clear_128((uint8_t*)bands, sizeof(bands));
    simd_clear_128((uint8_t*)peakHold, sizeof(peakHold));
    simd_clear_128((uint8_t*)peakDecayTimer, sizeof(peakDecayTimer));
    simd_clear_128((uint8_t*)spec8PeakHold, sizeof(spec8PeakHold));
    simd_clear_128((uint8_t*)spec8DecayTimer, sizeof(spec8DecayTimer));
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
    leftPeakDb = -30.0f;
    rightPeakDb = -30.0f;
    clipExpiryMs = 0;
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
        leftPeakDb = -30.0f;
        rightPeakDb = -30.0f;
        clipExpiryMs = 0;
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

    // Real-world mastering calibration (0 VU = -11 dBFS, RMS amplitude ~145.0)
    const float VU_REF_RMS = 145.0f;
    float vL = leftRmsLevel / VU_REF_RMS;
    float vR = rightRmsLevel / VU_REF_RMS;

    leftDb = (vL > 0.05f) ? (20.0f * FastMath::fast_log10(vL)) : -20.0f;
    if (leftDb > 3.5f) leftDb = 3.5f;
    if (leftDb < -20.0f) leftDb = -20.0f;

    rightDb = (vR > 0.05f) ? (20.0f * FastMath::fast_log10(vR)) : -20.0f;
    if (rightDb > 3.5f) rightDb = 3.5f;
    if (rightDb < -20.0f) rightDb = -20.0f;

    // True Peak in dBFS (relative to 511 full-scale ceiling)
    float peakRatioL = (float)lPeak / 511.0f;
    float peakRatioR = (float)rPeak / 511.0f;
    leftPeakDb = (peakRatioL > 0.01f) ? (20.0f * FastMath::fast_log10(peakRatioL)) : -30.0f;
    if (leftPeakDb > 0.0f) leftPeakDb = 0.0f;
    if (leftPeakDb < -30.0f) leftPeakDb = -30.0f;

    rightPeakDb = (peakRatioR > 0.01f) ? (20.0f * FastMath::fast_log10(peakRatioR)) : -30.0f;
    if (rightPeakDb > 0.0f) rightPeakDb = 0.0f;
    if (rightPeakDb < -30.0f) rightPeakDb = -30.0f;

    // True Digital Peak Clipping: triggers ONLY when signal hits digital full-scale ceiling (504+ out of 511)
    if (lPeak >= 504 || rPeak >= 504) {
        clipExpiryMs = now + 750;
    }

    // Ballistic simulation (300ms rise time to 99%, ANSI C16.5 mechanical response)
    float dt = (lastBallisticMs > 0 && now > lastBallisticMs) ? ((float)(now - lastBallisticMs) / 1000.0f) : 0.020f;
    if (dt > 0.1f) dt = 0.020f;
    lastBallisticMs = now;

    float vMax = (vL >= vR) ? vL : vR;
    // Map voltage ratio to dial deflection: 0.0 at -20 VU (0.10 ratio), 0.69 at 0 VU (1.00 ratio), 1.0 at +3 VU (1.413 ratio)
    float targetDeflection = (vMax > 0.10f) ? ((vMax - 0.10f) / 1.313f * 1.0f) : 0.0f;
    if (targetDeflection > 1.08f) targetDeflection = 1.08f;
    if (targetDeflection < 0.0f) targetDeflection = 0.0f;

    float err = targetDeflection - needlePos;
    needleVel += (err * 120.0f - needleVel * 15.0f) * dt;
    needlePos += needleVel * dt;
    if (needlePos < 0.0f) { needlePos = 0.0f; needleVel = 0.0f; }
    if (needlePos > 1.08f) { needlePos = 1.08f; needleVel = 0.0f; }

    // Horizontal bar peak hold update
    auto dbToWidth = [](float db) -> uint8_t {
        if (db <= -20.0f) return 0;
        float norm = (db <= 0.0f) ? ((db + 20.0f) / 20.0f * 0.69f) : (0.69f + (db / 3.0f) * 0.31f);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        return (uint8_t)(norm * 84.0f);
    };
    uint8_t barWL = dbToWidth(leftDb);
    uint8_t barWR = dbToWidth(rightDb);
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

    leftPeakLevel = peakLevel;
    rightPeakLevel = peakLevel;
    leftRmsLevel = rmsLevel;
    rightRmsLevel = rmsLevel;
    const float VU_REF_RMS = 145.0f;
    float v = rmsLevel / VU_REF_RMS;
    leftDb = (v > 0.05f) ? (20.0f * FastMath::fast_log10(v)) : -20.0f;
    if (leftDb > 3.5f) leftDb = 3.5f;
    if (leftDb < -20.0f) leftDb = -20.0f;
    rightDb = leftDb;
    float peakRatio = peakLevel / 511.0f;
    leftPeakDb = (peakRatio > 0.01f) ? (20.0f * FastMath::fast_log10(peakRatio)) : -30.0f;
    if (leftPeakDb > 0.0f) leftPeakDb = 0.0f;
    if (leftPeakDb < -30.0f) leftPeakDb = -30.0f;
    rightPeakDb = leftPeakDb;
    if (peak >= 504) {
        clipExpiryMs = millis() + 750;
    }

    processFFT();
    detectVoicePitch();
}

void SpectrumAnalyzer::processFFT() {
    float xr[SAMPLE_SIZE];
    float xi[SAMPLE_SIZE];

    // Precomputed Hanning window to prevent spectral leakage
    for (size_t i = 0; i < SAMPLE_SIZE; i++) {
        xr[i] = (float)micBuffer[i] * HANNING_128[i];
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

    // Cooley-Tukey Radix-2 butterflies with precomputed twiddles
    size_t stage = 0;
    for (size_t len = 2; len <= SAMPLE_SIZE; len <<= 1, stage++) {
        float wlen_r = TWIDDLE_STAGES[stage].r;
        float wlen_i = TWIDDLE_STAGES[stage].i;
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
            float scaled = FastMath::fast_log10(val / 40.0f) * 28.0f;
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
    alignas(16) int16_t down[N_SAMPLES];

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

    float downF[N_SAMPLES];
    for (size_t i = 0; i < N_SAMPLES; i++) {
        downF[i] = (float)down[i];
    }

    // Correlation window length N = 100 samples (~9.1 ms, covers full fundamental cycles down to 110 Hz)
    const size_t CORR_LEN = 100;
    const size_t MIN_LAG = 14;  // ~787 Hz
    const size_t MAX_LAG = 138; // ~80 Hz

    int64_t energy0_i = simd_dot_product_16(down, down, CORR_LEN);
    float energy0 = (float)energy0_i;

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
        int64_t num_i = simd_dot_product_16(down, &down[lag], CORR_LEN);
        int64_t energyLag_i = simd_dot_product_16(&down[lag], &down[lag], CORR_LEN);
        float num = (float)num_i;
        float energyLag = (float)energyLag_i;

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
            int64_t num_i = simd_dot_product_16(down, &down[l], CORR_LEN);
            int64_t el_i = simd_dot_product_16(&down[l], &down[l], CORR_LEN);
            return (float)num_i / (sqrtf(energy0 * (float)el_i) + 1.0f);
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
        case PRESET_VINYL_VU:        return "VINYL VU";
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
        case PRESET_VINYL_VU: drawVinylVU(); break;
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
        int x = pivotX + (int)(FastMath::fast_cos(rad) * arcR);
        int y = pivotY - (int)(FastMath::fast_sin(rad) * arcR);
        u8g2.drawPixel(x, y);

        // Danger sector from 68 down to 30 deg (0 dB to +3 dB): reinforced double arc
        if (deg <= 68) {
            int xIn = pivotX + (int)(FastMath::fast_cos(rad) * (arcR - 1));
            int yIn = pivotY - (int)(FastMath::fast_sin(rad) * (arcR - 1));
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
        int x0 = pivotX + (int)(FastMath::fast_cos(rad) * rIn);
        int y0 = pivotY - (int)(FastMath::fast_sin(rad) * rIn);
        int x1 = pivotX + (int)(FastMath::fast_cos(rad) * rOut);
        int y1 = pivotY - (int)(FastMath::fast_sin(rad) * rOut);
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
    int tipX = pivotX + (int)(FastMath::fast_cos(needleRad) * needleR);
    int tipY = pivotY - (int)(FastMath::fast_sin(needleRad) * needleR);

    // Needle stroke with solid visual presence
    u8g2.drawLine(pivotX, pivotY, tipX, tipY);
    int baseOffX = (needleDeg < 90.0f) ? -1 : 1;
    u8g2.drawLine(pivotX + baseOffX, pivotY, tipX, tipY);

    // Center pivot hub disk
    u8g2.drawDisc(pivotX, pivotY, 3);
    u8g2.setDrawColor(0);
    u8g2.drawPixel(pivotX, pivotY);
    u8g2.setDrawColor(1);

    // Lower corners: digital true peak readouts (Klanghelm VUMT style: dBFS)
    u8g2.setFont(u8g2_font_4x6_tr);
    char lBuf[16], rBuf[16];
    if (leftPeakDb <= -29.0f) {
        snprintf(lBuf, sizeof(lBuf), "L:--.-");
    } else {
        snprintf(lBuf, sizeof(lBuf), "L:%.1f", leftPeakDb);
    }
    if (rightPeakDb <= -29.0f) {
        snprintf(rBuf, sizeof(rBuf), "R:--.-");
    } else {
        snprintf(rBuf, sizeof(rBuf), "R:%.1f", rightPeakDb);
    }
    u8g2.drawStr(2, 62, lBuf);
    u8g2.drawStr(98, 62, rBuf);

    // Peak clipping / overload indicator (digital full-scale ceiling latch)
    if (isClipping()) {
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

    int startY = 15; // Fixed vertical anchor so horizontal bars do not shift up when full screen happens
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
        float norm = (db <= 0.0f) ? ((db + 20.0f) / 20.0f * 0.69f) : (0.69f + (db / 3.0f) * 0.31f);
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
        if (isClipping()) {
            snprintf(botBuf, sizeof(botBuf), "!CLIP! PK:%.1f/%.1f dBFS", leftPeakDb, rightPeakDb);
        } else {
            snprintf(botBuf, sizeof(botBuf), "RMS:%-4.0f PEAK:%-4.0f CAL:-11VU", rmsLevel, peakLevel);
        }
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
        float norm = (db <= 0.0f) ? ((db + 20.0f) / 20.0f * 0.69f) : (0.69f + (db / 3.0f) * 0.31f);
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
    if (isClipping()) {
        snprintf(vDbBuf, sizeof(vDbBuf), "CLIP");
    } else {
        float maxDb = (leftDb >= rightDb) ? leftDb : rightDb;
        snprintf(vDbBuf, sizeof(vDbBuf), (maxDb <= -19.5f) ? "--.-" : "%+.1f", maxDb);
    }
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

void SpectrumAnalyzer::drawVinylVU() {
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
            snprintf(headerBuf, sizeof(headerBuf), "[VNL:%s] %s", getSensitivityShortName(), track.c_str());
        }
        u8g2.drawStr(0, 10, headerBuf);
        u8g2.drawHLine(0, 12, 128);
    }

    // 1. Turntable Platter Geometry
    int platterX = 39;
    int platterY = showBar ? 38 : 32;
    int platterR = showBar ? 23 : 28;

    // Spinning Strobe Calibration Rim (rotates when playing)
    static float strobeAngle = 0.0f;
    if (audioPlayer.isPlaying()) {
        strobeAngle += 0.22f; // ~33 1/3 RPM visual rotational rate
        if (strobeAngle >= (2.0f * (float)M_PI)) {
            strobeAngle -= (2.0f * (float)M_PI);
        }
    }

    // Outer metal platter edge
    u8g2.drawCircle(platterX, platterY, platterR);

    // 12 strobe calibration notches around perimeter
    const int numStrobes = 12;
    for (int s = 0; s < numStrobes; s++) {
        float ang = strobeAngle + (float)s * ((2.0f * (float)M_PI) / (float)numStrobes);
        int sx = platterX + (int)(FastMath::fast_cos(ang) * (platterR - 1));
        int sy = platterY + (int)(FastMath::fast_sin(ang) * (platterR - 1));
        u8g2.drawPixel(sx, sy);
    }

    // 2. Vinyl Record Disc Body
    int vinylR = platterR - 3;
    u8g2.drawCircle(platterX, platterY, vinylR);

    // 3. Audio-Reactive Sound Grooves
    // Bass energy causes micro-groove vibration / ripple
    float bassMod = (bassLevel > 18.0f) ? ((bassLevel - 18.0f) / 70.0f) : 0.0f;
    if (bassMod > 1.0f) bassMod = 1.0f;
    int modOffset = (bassMod > 0.45f) ? 1 : 0;

    int grooveR1 = vinylR - 4 + modOffset;
    int grooveR2 = vinylR - 8 - modOffset;
    int grooveR3 = vinylR - 13;
    if (grooveR1 > 12) u8g2.drawCircle(platterX, platterY, grooveR1);
    if (grooveR2 > 10) u8g2.drawCircle(platterX, platterY, grooveR2);
    if (grooveR3 > 8)  u8g2.drawCircle(platterX, platterY, grooveR3);

    // Vinyl light reflection sheen (sheen segments across grooves)
    int sheenOffset = vinylR / 2;
    u8g2.drawPixel(platterX - sheenOffset, platterY - sheenOffset);
    u8g2.drawPixel(platterX - sheenOffset + 1, platterY - sheenOffset - 1);
    u8g2.drawPixel(platterX + sheenOffset, platterY + sheenOffset);
    u8g2.drawPixel(platterX + sheenOffset - 1, platterY + sheenOffset + 1);

    // 4. Center Record Label & Spindle
    int labelR = 7;
    u8g2.drawDisc(platterX, platterY, labelR);
    u8g2.setDrawColor(0);
    u8g2.drawDisc(platterX, platterY, 2); // Spindle hole
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(platterX - 4, platterY - 2, "33"); // 33 RPM stamp
    u8g2.setDrawColor(1);
    u8g2.drawPixel(platterX, platterY); // Center brass spindle pin

    // Strobe Lamp Housing (Bottom-left deck prism)
    int lampY = platterY + platterR - 5;
    if (lampY <= 61) {
        u8g2.drawFrame(1, lampY, 6, 5);
        u8g2.drawPixel(3, lampY + 2); // Bulb filament
    }

    // 5. Tonearm Gimbal Pivot & Counterweight (Top-Right of Turntable Deck)
    int pivX = 92;
    int pivY = showBar ? 20 : 15;

    // Gimbal mounting ring
    u8g2.drawCircle(pivX, pivY, 3);
    u8g2.drawPixel(pivX, pivY);
    // Counterweight at rear
    u8g2.drawBox(pivX + 3, pivY - 3, 5, 4);
    // Cueing rest post
    u8g2.drawVLine(pivX - 4, pivY + 4, 5);

    // 6. Dynamic VU-Driven Tonearm Tracking & Needle Action
    // When audio is quiet, needle rests near outer groove.
    // When music hits peaks / 0VU, tonearm moves inward across the groove,
    // with cartridge compliance micro-jitter on transients!
    float defl = needlePos; // 0.0 to 1.05 from ANSI ballistic simulation
    if (defl < 0.0f) defl = 0.0f;
    if (defl > 1.05f) defl = 1.05f;

    // Stylus target location on the vinyl record
    // Deflects between outer track radius and inner track radius
    float trackR = (float)(vinylR - 4) - defl * 8.0f;
    float stylusAngle = 0.65f - defl * 0.22f; // Radians along record arc
    
    // High-frequency transient micro-jitter (cartridge cantilever compliance)
    float jitter = 0.0f;
    if (peakLevel > 35.0f && audioPlayer.isPlaying()) {
        jitter = ((millis() % 2 == 0) ? 0.6f : -0.6f);
    }

    int stylusX = platterX + (int)(FastMath::fast_cos(stylusAngle) * trackR + jitter);
    int stylusY = platterY + (int)(FastMath::fast_sin(stylusAngle) * trackR);

    // Tonearm S-Curve Wand (Curved two-segment arm tube from pivot to stylus)
    int elbowX = (pivX + stylusX) / 2 + 5;
    int elbowY = (pivY + stylusY) / 2 - 4;
    u8g2.drawLine(pivX, pivY, elbowX, elbowY);
    u8g2.drawLine(elbowX, elbowY, stylusX, stylusY);

    // Headshell Cartridge Body & Stylus Contact
    u8g2.drawBox(stylusX - 3, stylusY - 1, 4, 3);
    u8g2.drawPixel(stylusX - 4, stylusY + 1); // Stylus tip touching groove

    // 7. Right Panel: Dual Stereo Vertical VU Meters & Scale
    int meterX1 = 111; // Left channel bar
    int meterX2 = 120; // Right channel bar
    int meterTopY = showBar ? 20 : 14;
    int meterH = showBar ? 34 : 40;
    int barW = 5;

    // Scale tick marks and dB labels
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(102, meterTopY + 4, "+3");
    u8g2.drawStr(102, meterTopY + (int)(meterH * 0.28f) + 2, " 0");
    u8g2.drawStr(102, meterTopY + (int)(meterH * 0.60f) + 2, "-6");
    u8g2.drawStr(100, meterTopY + meterH, "-20");

    // Horizontal reference ticks
    u8g2.drawHLine(108, meterTopY + 2, 2);
    u8g2.drawHLine(108, meterTopY + (int)(meterH * 0.28f), 2);
    u8g2.drawHLine(108, meterTopY + (int)(meterH * 0.60f), 2);
    u8g2.drawHLine(108, meterTopY + meterH - 2, 2);

    // Meter frames
    u8g2.drawFrame(meterX1, meterTopY, barW, meterH);
    u8g2.drawFrame(meterX2, meterTopY, barW, meterH);

    // Convert dB to vertical fill height
    auto dbToHeight = [&](float db) -> int {
        if (db <= -20.0f) return 0;
        float norm = (db + 20.0f) / 23.0f; // -20dB to +3dB range
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        return (int)(norm * (meterH - 2));
    };

    int hL = dbToHeight(leftDb);
    int hR = dbToHeight(rightDb);

    // Fill Left vertical bar
    if (hL > 0) {
        int fillY = meterTopY + meterH - 1 - hL;
        u8g2.drawBox(meterX1 + 1, fillY, barW - 2, hL);
        // Segment divider lines every 3px
        u8g2.setDrawColor(0);
        for (int sy = meterTopY + meterH - 4; sy > fillY; sy -= 3) {
            u8g2.drawHLine(meterX1 + 1, sy, barW - 2);
        }
        u8g2.setDrawColor(1);
    }

    // Fill Right vertical bar
    if (hR > 0) {
        int fillY = meterTopY + meterH - 1 - hR;
        u8g2.drawBox(meterX2 + 1, fillY, barW - 2, hR);
        // Segment divider lines every 3px
        u8g2.setDrawColor(0);
        for (int sy = meterTopY + meterH - 4; sy > fillY; sy -= 3) {
            u8g2.drawHLine(meterX2 + 1, sy, barW - 2);
        }
        u8g2.setDrawColor(1);
    }

    // Floating Peak Hold indicators
    static uint8_t vLeftPeakHold = 0;
    static uint8_t vRightPeakHold = 0;
    static uint8_t vPeakTimer = 0;

    if (hL >= vLeftPeakHold) {
        vLeftPeakHold = hL;
    }
    if (hR >= vRightPeakHold) {
        vRightPeakHold = hR;
    }
    if (++vPeakTimer >= 2) {
        vPeakTimer = 0;
        if (vLeftPeakHold > 0) vLeftPeakHold--;
        if (vRightPeakHold > 0) vRightPeakHold--;
    }

    if (vLeftPeakHold > 0 && vLeftPeakHold <= meterH - 2) {
        int capY = meterTopY + meterH - 1 - vLeftPeakHold;
        u8g2.drawHLine(meterX1 + 1, capY, barW - 2);
    }
    if (vRightPeakHold > 0 && vRightPeakHold <= meterH - 2) {
        int capY = meterTopY + meterH - 1 - vRightPeakHold;
        u8g2.drawHLine(meterX2 + 1, capY, barW - 2);
    }

    // Channel labels L and R at bottom of meters
    u8g2.setFont(u8g2_font_4x6_tr);
    int labelY = meterTopY + meterH + 6;
    if (labelY <= 63) {
        u8g2.drawStr(meterX1 + 1, labelY, "L");
        u8g2.drawStr(meterX2 + 1, labelY, "R");
    }

    // 8. CLIP Alert / Peak Readout
    float maxDb = (leftDb >= rightDb) ? leftDb : rightDb;
    if (maxDb >= 0.0f) {
        u8g2.drawFrame(72, showBar ? 46 : 50, 26, 8);
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(74, showBar ? 52 : 56, "CLIP");
    } else {
        char pkBuf[12];
        snprintf(pkBuf, sizeof(pkBuf), (maxDb <= -19.5f) ? "--.-" : "%+.1f", maxDb);
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(72, showBar ? 52 : 56, pkBuf);
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
            float v = FastMath::fast_sin(x * 0.1f + t) + FastMath::fast_cos(y * 0.1f + t);
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
