#include "led_manager.h"
#include "battery.h"
#include "spectrum_analyzer.h"
#include "audio_player.h"

LEDManager ledManager;

static const struct {
    CRGB color;
    const char* name;
} COLOR_PALETTE[] = {
    { CRGB::Cyan,    "CYAN" },
    { CRGB::Blue,    "BLUE" },
    { CRGB::Green,   "GREN" },
    { CRGB::Yellow,  "YELW" },
    { CRGB::Orange,  "ORNG" },
    { CRGB::Red,     "RED" },
    { CRGB::Magenta, "MGTA" },
    { CRGB::White,   "WHTE" },
    { CRGB::Violet,  "PTCH" }
};
static const size_t PALETTE_COUNT = sizeof(COLOR_PALETTE) / sizeof(COLOR_PALETTE[0]);

static const LedMode SELECTABLE_MODES[] = {
    LedMode::REACT_BASS_PULSE,
    LedMode::REACT_VOCAL_LIGHTNING,
    LedMode::REACT_ENERGY_VU,
    LedMode::REACT_SPECTRUM_HUE,
    LedMode::REACT_RAINBOW_FLOW,
    LedMode::REACT_FIRE,
    LedMode::REACT_DISCO_FLASH,
    LedMode::BREATHING,
    LedMode::RAINBOW,
    LedMode::SOLID
};
static const size_t SELECTABLE_MODES_COUNT = sizeof(SELECTABLE_MODES) / sizeof(SELECTABLE_MODES[0]);

static const uint8_t BRIGHTNESS_LEVELS[] = { 50, 100, 160, 210, 255 };
static const size_t BRIGHTNESS_LEVELS_COUNT = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

LEDManager::LEDManager() :
    current_mode(LedMode::BOOT_PULSE),
    previous_mode(LedMode::OFF),
    user_mode(LedMode::REACT_BASS_PULSE),
    master_enabled(true),
    button_feedback_enabled(true),
    off_on_complete(true),
    in_menu_preview(false),
    current_color(CRGB::Cyan),
    color_index(0),
    current_brightness(160),
    current_sensitivity(LedSensitivity::SENS_NORMAL),
    last_update(0),
    anim_phase(0.0f),
    pulse_count(0),
    pulse_speed(150),
    pulse_color(CRGB::Cyan),
    reactive_energy(0.0f),
    reactive_hue(0.0f),
    reactive_flash(0.0f),
    last_bass(0.0f),
    last_beat_time(0),
    bass_max(15.0f),
    bass_avg(5.0f),
    rms_max(15.0f),
    total_max(15.0f),
    vocal_avg(5.0f),
    vocal_max(25.0f),
    lightning_intensity(0.0f),
    last_lightning_time(0),
    lightning_burst_count(0)
{}

void LEDManager::loadSettings() {
    Preferences prefs;
    if (prefs.begin("qtune_led", true)) {
        current_brightness = prefs.getUChar("bright", 160);
        uint8_t m = prefs.getUChar("mode", (uint8_t)LedMode::REACT_BASS_PULSE);
        user_mode = (LedMode)m;
        color_index = prefs.getUChar("col_idx", 0);
        if (color_index >= PALETTE_COUNT) color_index = 0;
        current_color = COLOR_PALETTE[color_index].color;
        uint8_t s = prefs.getUChar("sens", (uint8_t)LedSensitivity::SENS_NORMAL);
        current_sensitivity = (LedSensitivity)s;
        master_enabled = prefs.getBool("enabled", true);
        button_feedback_enabled = prefs.getBool("btn_fb", true);
        off_on_complete = prefs.getBool("off_done", true);
        prefs.end();
    }
}

void LEDManager::saveSettings() {
    Preferences prefs;
    if (prefs.begin("qtune_led", false)) {
        prefs.putUChar("bright", current_brightness);
        prefs.putUChar("mode", (uint8_t)user_mode);
        prefs.putUChar("col_idx", color_index);
        prefs.putUChar("sens", (uint8_t)current_sensitivity);
        prefs.putBool("enabled", master_enabled);
        prefs.putBool("btn_fb", button_feedback_enabled);
        prefs.putBool("off_done", off_on_complete);
        prefs.end();
    }
}

void LEDManager::begin() {
    loadSettings();
    FastLED.addLeds<WS2812, RGB_LED, RGB>(leds, 1);
    FastLED.setBrightness(current_brightness);
    triggerPulse(CRGB::Blue, 2, 200); // 2 short blue pulses on boot
}

bool LEDManager::isReactiveMode(LedMode mode) {
    return (mode == LedMode::REACT_BASS_PULSE ||
            mode == LedMode::REACT_VOCAL_LIGHTNING ||
            mode == LedMode::REACT_ENERGY_VU ||
            mode == LedMode::REACT_SPECTRUM_HUE ||
            mode == LedMode::REACT_RAINBOW_FLOW ||
            mode == LedMode::REACT_FIRE ||
            mode == LedMode::REACT_DISCO_FLASH);
}

void LEDManager::setMode(LedMode mode) {
    if (current_mode != mode) {
        previous_mode = current_mode;
        current_mode = mode;
        anim_phase = 0.0f;
        reactive_energy = 0.0f;
    }
}

void LEDManager::setUserMode(LedMode mode) {
    user_mode = mode;
    setMode(mode);
    saveSettings();
}

void LEDManager::cycleMode() {
    size_t curIdx = 0;
    for (size_t i = 0; i < SELECTABLE_MODES_COUNT; i++) {
        if (SELECTABLE_MODES[i] == user_mode) {
            curIdx = i;
            break;
        }
    }
    size_t nextIdx = (curIdx + 1) % SELECTABLE_MODES_COUNT;
    setUserMode(SELECTABLE_MODES[nextIdx]);
}

void LEDManager::setEnabled(bool enabled) {
    master_enabled = enabled;
    saveSettings();
    if (!master_enabled) {
        off();
    } else {
        setMode(user_mode);
    }
}

void LEDManager::toggleEnabled() {
    setEnabled(!master_enabled);
}

const char* LEDManager::getModeName() const {
    LedMode m = (current_mode == LedMode::PULSE || current_mode == LedMode::BOOT_PULSE || current_mode == LedMode::LOW_BATTERY_PULSE || current_mode == LedMode::OFF) ? user_mode : current_mode;
    switch (m) {
        case LedMode::REACT_BASS_PULSE:       return "BASS PULSE";
        case LedMode::REACT_VOCAL_LIGHTNING:  return "VOCAL LIGHTNING";
        case LedMode::REACT_ENERGY_VU:        return "ENERGY VU";
        case LedMode::REACT_SPECTRUM_HUE:     return "SPECTRUM HUE";
        case LedMode::REACT_RAINBOW_FLOW:     return "RAINBOW FLOW";
        case LedMode::REACT_FIRE:             return "FIRE FLAME";
        case LedMode::REACT_DISCO_FLASH:      return "DISCO STROBE";
        case LedMode::BREATHING:              return "BREATHING";
        case LedMode::RAINBOW:                return "RAINBOW WHEEL";
        case LedMode::SOLID:                  return "SOLID COLOR";
        case LedMode::OFF:                    return "OFF";
        case LedMode::BOOT_PULSE:             return "BOOT PULSE";
        case LedMode::LOW_BATTERY_PULSE:      return "LOW BATTERY";
        case LedMode::PULSE:
        case LedMode::BEAT_PULSE:             return "PULSE";
        default:                              return "MUSIC SYNC";
    }
}

const char* LEDManager::getShortModeName() const {
    LedMode m = (current_mode == LedMode::PULSE || current_mode == LedMode::BOOT_PULSE || current_mode == LedMode::LOW_BATTERY_PULSE || current_mode == LedMode::OFF) ? user_mode : current_mode;
    switch (m) {
        case LedMode::REACT_BASS_PULSE:       return "BASS";
        case LedMode::REACT_VOCAL_LIGHTNING:  return "VOCL";
        case LedMode::REACT_ENERGY_VU:        return "VU";
        case LedMode::REACT_SPECTRUM_HUE:     return "SPEC";
        case LedMode::REACT_RAINBOW_FLOW:     return "FLOW";
        case LedMode::REACT_FIRE:             return "FIRE";
        case LedMode::REACT_DISCO_FLASH:      return "DSCO";
        case LedMode::BREATHING:              return "BRTH";
        case LedMode::RAINBOW:                return "RAIN";
        case LedMode::SOLID:                  return "SLID";
        case LedMode::OFF:                    return "OFF";
        default:                              return "RGB";
    }
}

void LEDManager::setColor(CRGB color) {
    current_color = color;
    setMode(LedMode::SOLID);
    leds[0] = color;
    FastLED.show();
    saveSettings();
}

void LEDManager::cycleColor() {
    color_index = (color_index + 1) % PALETTE_COUNT;
    current_color = COLOR_PALETTE[color_index].color;
    if (user_mode == LedMode::SOLID || user_mode == LedMode::BREATHING || user_mode == LedMode::REACT_BASS_PULSE || user_mode == LedMode::REACT_VOCAL_LIGHTNING) {
        leds[0] = current_color;
        FastLED.show();
    }
    saveSettings();
}

const char* LEDManager::getColorName() const {
    return COLOR_PALETTE[color_index].name;
}

void LEDManager::setBrightness(uint8_t brightness) {
    current_brightness = brightness;
    FastLED.setBrightness(brightness);
    FastLED.show();
    saveSettings();
}

void LEDManager::cycleBrightness() {
    size_t curIdx = 0;
    for (size_t i = 0; i < BRIGHTNESS_LEVELS_COUNT; i++) {
        if (current_brightness <= BRIGHTNESS_LEVELS[i]) {
            curIdx = i;
            break;
        }
    }
    size_t nextIdx = (curIdx + 1) % BRIGHTNESS_LEVELS_COUNT;
    setBrightness(BRIGHTNESS_LEVELS[nextIdx]);
}

void LEDManager::setSensitivity(LedSensitivity sens) {
    current_sensitivity = sens;
    saveSettings();
}

void LEDManager::cycleSensitivity() {
    if (current_sensitivity == LedSensitivity::SENS_LOW) {
        current_sensitivity = LedSensitivity::SENS_NORMAL;
    } else if (current_sensitivity == LedSensitivity::SENS_NORMAL) {
        current_sensitivity = LedSensitivity::SENS_HIGH;
    } else {
        current_sensitivity = LedSensitivity::SENS_LOW;
    }
    saveSettings();
}

const char* LEDManager::getSensitivityName() const {
    switch (current_sensitivity) {
        case LedSensitivity::SENS_LOW:    return "LOW";
        case LedSensitivity::SENS_NORMAL: return "NORM";
        case LedSensitivity::SENS_HIGH:   return "HIGH";
        default:                          return "NORM";
    }
}

void LEDManager::setButtonFeedbackEnabled(bool enabled) {
    button_feedback_enabled = enabled;
    saveSettings();
}

void LEDManager::toggleButtonFeedback() {
    button_feedback_enabled = !button_feedback_enabled;
    saveSettings();
}

void LEDManager::setTurnOffOnComplete(bool enable) {
    off_on_complete = enable;
    saveSettings();
}

void LEDManager::toggleTurnOffOnComplete() {
    off_on_complete = !off_on_complete;
    saveSettings();
}

void LEDManager::off() {
    setMode(LedMode::OFF);
    leds[0] = CRGB::Black;
    FastLED.show();
}

void LEDManager::triggerButtonPulse(CRGB color, int count, int speed_ms) {
    if (!master_enabled || !button_feedback_enabled) return;
    triggerPulse(color, count, speed_ms);
}

void LEDManager::triggerPulse(CRGB color, int count, int speed_ms) {
    if (!master_enabled) return;
    pulse_color = color;
    pulse_count = count * 2; // ON + OFF phases
    pulse_speed = speed_ms;
    setMode(LedMode::PULSE);
}

void LEDManager::onPlaybackStart() {
    if (!master_enabled) return;
    setMode(user_mode);
}

void LEDManager::onPlaybackResume() {
    if (!master_enabled) return;
    setMode(user_mode);
}

void LEDManager::onPlaybackPause() {
    if (!master_enabled) return;
    setMode(LedMode::BREATHING);
    leds[0] = CRGB::Orange;
    FastLED.show();
}

void LEDManager::onPlaybackStop() {
    if (off_on_complete) {
        off();
    } else {
        if (master_enabled) {
            setMode(user_mode);
        } else {
            off();
        }
    }
}

void LEDManager::updateReactiveModes(float dt) {
    const uint8_t* bands = spectrumAnalyzer.getBands();
    float rms = spectrumAnalyzer.getRMSLevel();
    float peak = spectrumAnalyzer.getPeakLevel();
    bool isPlaying = audioPlayer.isPlaying();

    float sens = (current_sensitivity == LedSensitivity::SENS_LOW) ? 0.70f :
                 ((current_sensitivity == LedSensitivity::SENS_HIGH) ? 1.45f : 1.0f);

    // If audio is not playing (e.g. idle or previewing in RGB settings menu),
    // provide an active, dynamic preview/demo of each mode!
    if (!isPlaying) {
        if (off_on_complete && !in_menu_preview) {
            leds[0] = CRGB::Black;
            return;
        }
        anim_phase += dt * 2.5f;
        switch (current_mode) {
            case LedMode::REACT_BASS_PULSE: {
                // Rhythmic beat demo pulse (1.25 Hz pulse)
                float pulse = 0.5f + 0.5f * sinf(anim_phase * 2.5f);
                pulse = pulse * pulse * pulse; // sharp kick attack curve
                uint8_t val = (uint8_t)(25 + pulse * 230);
                CRGB c = current_color;
                c.nscale8(val);
                leds[0] = c;
                break;
            }
            case LedMode::REACT_VOCAL_LIGHTNING: {
                // Pitch lightning demo: sweep pitch scale and flash selected palette color
                float pitchSweep = 0.5f + 0.5f * sinf(anim_phase * 1.8f);
                float strike = 0.5f + 0.5f * sinf(anim_phase * 3.6f);
                strike = strike * strike;
                float crackle = 0.88f + 0.12f * ((rand() % 100) / 100.0f);
                uint8_t val = (uint8_t)(strike * 240.0f * crackle);

                bool isPitchMode = (color_index == 8);
                uint8_t demoHue, demoSat;
                if (isPitchMode) {
                    demoHue = (pitchSweep < 0.5f) ? (uint8_t)(165.0f - (pitchSweep / 0.5f) * 35.0f)
                                                  : (uint8_t)(130.0f + ((pitchSweep - 0.5f) / 0.5f) * 105.0f);
                    demoSat = (uint8_t)(255 - pitchSweep * 90);
                } else {
                    CHSV baseHsv = rgb2hsv_approximate(current_color);
                    demoHue = baseHsv.hue;
                    demoSat = baseHsv.sat;
                    if (demoSat > 20) {
                        int16_t shifted = (int16_t)baseHsv.hue + (int16_t)((pitchSweep - 0.5f) * 36.0f);
                        if (shifted < 0) shifted += 256;
                        if (shifted >= 256) shifted -= 256;
                        demoHue = (uint8_t)shifted;
                        demoSat = (uint8_t)constrain(baseHsv.sat - (int)(pitchSweep * 80.0f), 100, 255);
                    } else {
                        demoSat = 0;
                    }
                }
                if (strike > 0.80f) demoSat = (uint8_t)(demoSat * 0.35f);
                leds[0] = CHSV(demoHue, demoSat, val);
                break;
            }
            case LedMode::REACT_ENERGY_VU: {
                // Smooth demo sweep across VU color gradient (Cyan -> Green -> Yellow -> Red)
                float sweep = 0.5f + 0.5f * sinf(anim_phase * 1.5f);
                uint8_t vuHue = (uint8_t)(140.0f - sweep * 140.0f);
                uint8_t val = (uint8_t)(40 + sweep * 215);
                leds[0] = CHSV(vuHue, 255, val);
                break;
            }
            case LedMode::REACT_SPECTRUM_HUE: {
                // Demo sweep across harmonic spectrum
                uint8_t specHue = (uint8_t)(anim_phase * 35.0f);
                leds[0] = CHSV(specHue, 245, 230);
                break;
            }
            case LedMode::REACT_RAINBOW_FLOW: {
                // Continuous rainbow flow demo
                uint8_t rainHue = (uint8_t)(anim_phase * 45.0f);
                leds[0] = CHSV(rainHue, 255, 240);
                break;
            }
            case LedMode::REACT_FIRE: {
                // Organic flickering fire demo
                uint8_t microFlicker = (uint8_t)(rand() % 35);
                uint8_t fireHue = (uint8_t)(8 + (microFlicker >> 2));
                uint8_t fireVal = (uint8_t)(160 + microFlicker * 2);
                leds[0] = CHSV(fireHue, 245, fireVal);
                break;
            }
            case LedMode::REACT_DISCO_FLASH: {
                // Rhythmic disco strobe flash demo
                uint32_t now = millis();
                if (now - last_beat_time > 380) {
                    last_beat_time = now;
                    reactive_flash = 1.0f;
                    reactive_hue += 77.0f;
                    if (reactive_hue >= 256.0f) reactive_hue -= 256.0f;
                }
                reactive_flash -= dt * 4.2f;
                if (reactive_flash < 0.0f) reactive_flash = 0.0f;
                uint8_t val = (uint8_t)(20 + reactive_flash * 235);
                leds[0] = CHSV((uint8_t)reactive_hue, 255, val);
                break;
            }
            default:
                break;
        }
        return;
    }

    // =========================================================================
    // REAL-TIME AUDIO REACTIVE ENGINE (With Dynamic Transient Beat Tracking)
    // =========================================================================

    // 1. Dedicated Sub-Bass / Kick Energy & Dynamic Baseline Tracking
    // Combine 160Hz IIR low-passed bass with low-frequency FFT bins
    float bassInst = spectrumAnalyzer.getBassLevel() * 1.6f + bands[0] * 1.5f + bands[1] * 0.8f;

    // Smooth baseline tracks ongoing background acoustic energy (tau ~ 0.35s)
    bass_avg += (bassInst - bass_avg) * (dt * 2.8f);

    // Dynamic transient difference above baseline:
    float diff = bassInst - bass_avg;
    if (diff < 0.0f) diff = 0.0f;

    // Adaptive AGC headroom tracker for transient punch
    if (diff > bass_max) {
        bass_max = diff; // instant attack
    } else {
        bass_max -= dt * (bass_max * 0.35f); // dynamic decay
    }
    if (bass_max < 6.0f) bass_max = 6.0f;

    // Beat punch normalized from 0.0 (between beats) to 1.0 (on peak beat hits)
    float punchNorm = constrain((diff / bass_max) * sens, 0.0f, 1.0f);

    // 2. RMS Energy / Volume (Overall track loudness)
    float energyRaw = (rms * 1.6f + peak * 0.4f + bassInst * 0.4f);
    if (energyRaw > rms_max) {
        rms_max = energyRaw;
    } else {
        rms_max -= dt * (rms_max * 0.25f);
    }
    if (rms_max < 6.0f) rms_max = 6.0f;
    float energyNorm = constrain((energyRaw / rms_max) * sens, 0.0f, 1.0f);

    // 3. Harmonic Spectrum Centroid (Bass vs Mids vs Highs)
    float b_val = (bassInst * 1.5f + bands[0] + bands[1]) + 0.1f;
    float m_val = (bands[3] + bands[4] + bands[5] + bands[6] + bands[7]) + 0.1f;
    float h_val = (bands[8] + bands[9] + bands[10] + bands[11] + bands[12] + bands[13]) + 0.1f;
    float totalHarmonics = b_val + m_val + h_val;
    if (totalHarmonics > total_max) {
        total_max = totalHarmonics;
    } else {
        total_max -= dt * (total_max * 0.25f);
    }
    if (total_max < 8.0f) total_max = 8.0f;
    float harmonicNorm = constrain((totalHarmonics / total_max) * sens, 0.0f, 1.0f);

    // 4. Vocal Formant Extraction (~500 Hz to 2.5 kHz) & Percussion Ducking Discrimination
    // Human vowel formants F1 (300-900 Hz, Bands 1-2) and F2 (900-2500 Hz, Bands 2-5).
    // Bands 6, 7, 8 (3 kHz - 6.2 kHz) are strictly excluded to reject snare snap and cymbals.
    float vocalCore = (bands[1] * 0.9f + bands[2] * 1.5f + bands[3] * 1.6f + bands[4] * 1.4f + bands[5] * 0.9f);

    // Percussion Discrimination:
    // a) Kick drum transient punch ducking
    float drumPunchMask = punchNorm * 0.65f;

    // b) High-frequency percussion sizzle (snare wire snap, crash/hi-hat cymbals in 4-10 kHz)
    float highSizzle = (bands[7] * 0.8f + bands[8] * 1.0f + bands[9] * 1.0f + bands[10] * 0.7f);
    float snareRatio = (vocalCore > 1.0f) ? (highSizzle / vocalCore) : 0.0f;
    float snareMask = 0.0f;
    if (snareRatio > 0.85f) {
        snareMask = constrain((snareRatio - 0.85f) * 1.2f, 0.0f, 0.75f);
    }

    // c) Mid-band Formant Dominance Ratio (vocal body vs extreme low-bass and high-treble flanks)
    float flankEnergy = (bands[0] * 1.2f + highSizzle * 0.8f) + 2.0f;
    float vocalDominance = vocalCore / flankEnergy;
    float dominanceGate = constrain((vocalDominance - 0.35f) / 0.45f, 0.15f, 1.0f);

    // Duck percussion transients and scale by vocal dominance
    float totalInhibition = constrain(drumPunchMask + snareMask, 0.0f, 0.85f);
    float cleanVocal = vocalCore * (1.0f - totalInhibition) * dominanceGate;

    // 5. True Voice Pitch & Periodicity Voicing Gate
    float voicePitch = spectrumAnalyzer.getVoicePitch();
    float voiceConfidence = spectrumAnalyzer.getVoiceConfidence();

    // Fallback pitch from vocal formant centroid if voice pitch tracking is below vocal fundamental
    // but strong mid-band vocal formant resonance is present (Bands 1-5 cover 689 Hz - 3100 Hz formants)
    if (voicePitch < 70.0f && vocalCore > 3.0f) {
        float centroid = (bands[1] * 120.0f + bands[2] * 180.0f + bands[3] * 250.0f + bands[4] * 330.0f + bands[5] * 420.0f) / (vocalCore + 0.1f);
        voicePitch = constrain(centroid, 80.0f, 600.0f);
        if (voiceConfidence < 0.22f) voiceConfidence = 0.22f;
    }

    // Voice pitch gate: active when vocal periodicity is detected or strong vocal formant dominance is present
    float pitchPeriodicityGate = constrain((voiceConfidence - 0.14f) / 0.22f, 0.0f, 1.0f);
    float formantVoicingGate = constrain((vocalDominance - 0.28f) / 0.35f, 0.0f, 1.0f);
    float voicePitchGate = constrain(pitchPeriodicityGate * 0.75f + formantVoicingGate * 0.55f, 0.0f, 1.0f);
    cleanVocal *= (0.25f + 0.75f * voicePitchGate);

    // Soft-knee noise floor gate to suppress low-level instrumental bleed and room noise
    if (cleanVocal < 6.0f) {
        cleanVocal = (cleanVocal * cleanVocal) / 6.0f;
    }

    // Dynamic vocal baseline tracker (tau ~ 0.40s)
    vocal_avg += (cleanVocal - vocal_avg) * (dt * 2.5f);
    float vocalDiff = cleanVocal - vocal_avg;
    if (vocalDiff < 0.0f) vocalDiff = 0.0f;

    // Dynamic AGC tracker with adaptive headroom (floor at 10.0f)
    if (vocalDiff > vocal_max) {
        vocal_max = vocalDiff;
    } else {
        vocal_max -= dt * (vocal_max * 0.35f);
    }
    if (vocal_max < 10.0f) vocal_max = 10.0f;
    float vocalPunch = constrain((vocalDiff / vocal_max) * sens * (0.3f + 0.7f * voicePitchGate), 0.0f, 1.0f);

    switch (current_mode) {
        case LedMode::REACT_BASS_PULSE: {
            // Instant attack on beat transient, punchy exponential decay (~220ms)
            if (punchNorm >= reactive_energy) {
                reactive_energy = punchNorm;
            } else {
                reactive_energy -= dt * 4.5f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            // Perceptual quadratic curve: near dark between beats (value 8), explosive peak (255) on beat
            float curved = reactive_energy * reactive_energy;
            uint8_t val = (uint8_t)(8 + curved * 247.0f);
            CRGB c = current_color;
            c.nscale8(val);
            leds[0] = c;
            break;
        }

        case LedMode::REACT_VOCAL_LIGHTNING: {
            // Strictly react to voice pitch and vocal delivery: extinguish when no vocals
            if (voicePitchGate < 0.10f || voicePitch < 70.0f || vocalPunch < 0.03f) {
                lightning_intensity -= dt * 6.5f;
                if (lightning_intensity < 0.0f) lightning_intensity = 0.0f;
                if (lightning_intensity <= 0.01f) {
                    leds[0] = CRGB::Black;
                    break;
                }
            } else {
                // Active Voice Pitch! Explosive lightning attack on vocal delivery
                if (vocalPunch > lightning_intensity) {
                    lightning_intensity += (vocalPunch - lightning_intensity) * (dt * 28.0f);
                } else {
                    lightning_intensity -= dt * 4.2f;
                    if (lightning_intensity < 0.0f) lightning_intensity = 0.0f;
                }
            }

            if (lightning_intensity <= 0.01f) {
                leds[0] = CRGB::Black;
                break;
            }

            // Human singing pitch normalization across vocal octaves (80 Hz to 650 Hz):
            float clampedPitch = constrain(voicePitch, 80.0f, 650.0f);
            float pitchNorm = constrain(log2f(clampedPitch / 80.0f) / log2f(650.0f / 80.0f), 0.0f, 1.0f);

            // Dynamic lightning brightness curve (zero resting aura - black when not singing)
            float vocalCurve = lightning_intensity * lightning_intensity;
            uint8_t vocalBright = (uint8_t)constrain((int)(vocalCurve * 255.0f), 0, 255);

            // Electrical micro-crackle jitter proportional to pitch
            float crackle = 0.88f + 0.12f * ((rand() % 100) / 100.0f);
            vocalBright = (uint8_t)(vocalBright * crackle);

            // Determine Lightning Hue and Saturation based on User Color Palette Option
            bool isPitchMode = (color_index == 8); // "PTCH" dynamic pitch spectrum
            uint8_t pitchHue, pitchSat;

            if (isPitchMode) {
                // Dynamic pitch spectrum: deep blue (low notes) -> electric cyan -> violet -> magenta
                if (pitchNorm < 0.5f) {
                    pitchHue = (uint8_t)(165.0f - (pitchNorm / 0.5f) * 35.0f);
                } else {
                    pitchHue = (uint8_t)(130.0f + ((pitchNorm - 0.5f) / 0.5f) * 105.0f);
                }
                pitchSat = (uint8_t)constrain(255.0f - pitchNorm * 90.0f, 120.0f, 255.0f);
            } else {
                // Use user-selected palette color with pitch-modulated hue shift (+/- 25 deg)
                CHSV baseHsv = rgb2hsv_approximate(current_color);
                pitchHue = baseHsv.hue;
                pitchSat = baseHsv.sat;

                if (pitchSat > 20) {
                    // Modulate hue dynamically around chosen color based on vocal pitch
                    int16_t shifted = (int16_t)baseHsv.hue + (int16_t)((pitchNorm - 0.5f) * 36.0f);
                    if (shifted < 0) shifted += 256;
                    if (shifted >= 256) shifted -= 256;
                    pitchHue = (uint8_t)shifted;
                    pitchSat = (uint8_t)constrain(baseHsv.sat - (int)(pitchNorm * 80.0f), 100, 255);
                } else {
                    // White palette: pure incandescent lightning with subtle ionization
                    pitchSat = (uint8_t)constrain((fabsf(pitchNorm - 0.5f) * 30.0f), 0.0f, 30.0f);
                }
            }

            // Instant blinding white strike on powerful vocal transients
            if (vocalPunch > 0.65f) {
                pitchSat = (uint8_t)(pitchSat * 0.35f);
            }

            leds[0] = CHSV(pitchHue, pitchSat, vocalBright);
            break;
        }

        case LedMode::REACT_ENERGY_VU: {
            // Real-time VU meter with smooth needle tracking
            if (energyNorm >= reactive_energy) {
                reactive_energy = energyNorm;
            } else {
                reactive_energy -= dt * 3.2f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            // Color gradient across VU level:
            // 0.00 .. 0.35: Cyan to Forest Green (140 -> 96)
            // 0.35 .. 0.70: Forest Green to Amber / Gold (96 -> 32)
            // 0.70 .. 1.00: Amber to Crimson Red (32 -> 0)
            uint8_t vuHue;
            if (reactive_energy < 0.35f) {
                float f = reactive_energy / 0.35f;
                vuHue = (uint8_t)(140.0f - f * 44.0f);
            } else if (reactive_energy < 0.70f) {
                float f = (reactive_energy - 0.35f) / 0.35f;
                vuHue = (uint8_t)(96.0f - f * 64.0f);
            } else {
                float f = (reactive_energy - 0.70f) / 0.30f;
                vuHue = (uint8_t)(32.0f - f * 32.0f);
            }

            uint8_t val = (uint8_t)(25 + reactive_energy * 230.0f);
            leds[0] = CHSV(vuHue, 255, val);
            break;
        }

        case LedMode::REACT_SPECTRUM_HUE: {
            // Centroid mapping:
            // Bass dominant -> Red (0) / Orange (15)
            // Mids dominant -> Green (96) / Gold (60)
            // Highs dominant -> Cyan (140) / Blue (175)
            float targetHue = (b_val * 0.0f + m_val * 96.0f + h_val * 175.0f) / totalHarmonics;
            reactive_hue += (targetHue - reactive_hue) * (dt * 12.0f);

            if (harmonicNorm >= reactive_energy) {
                reactive_energy = harmonicNorm;
            } else {
                reactive_energy -= dt * 3.6f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            uint8_t val = (uint8_t)(30 + reactive_energy * 225.0f);
            leds[0] = CHSV((uint8_t)reactive_hue, 245, val);
            break;
        }

        case LedMode::REACT_RAINBOW_FLOW: {
            // Rainbow wheel rotation accelerates dynamically with music energy
            float spinSpeed = 35.0f + (energyNorm * 215.0f);
            anim_phase += dt * spinSpeed;

            if (punchNorm >= reactive_energy) {
                reactive_energy = punchNorm;
            } else {
                reactive_energy -= dt * 4.0f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            uint8_t val = (uint8_t)(30 + reactive_energy * 225.0f);
            leds[0] = CHSV((uint8_t)anim_phase, 255, val);
            break;
        }

        case LedMode::REACT_FIRE: {
            // Warm campfire flame that flares into intense gold/white on audio transients
            uint8_t microFlicker = (uint8_t)(rand() % 28);
            uint8_t fireHue = (uint8_t)constrain(8 + (int)(punchNorm * 26.0f), 0, 35);
            uint8_t fireVal = (uint8_t)constrain(40 + (int)(punchNorm * 190.0f) + microFlicker, 0, 255);
            leds[0] = CHSV(fireHue, 245, fireVal);
            break;
        }

        case LedMode::REACT_DISCO_FLASH: {
            // Dynamic transient beat detection (spikes in bass energy above baseline)
            uint32_t now = millis();
            if (punchNorm > 0.40f && diff > (bass_avg * 0.20f + 2.0f) && (now - last_beat_time > 130)) {
                last_beat_time = now;
                reactive_flash = 1.0f;
                reactive_hue += 77.0f; // Golden ratio color jump
                if (reactive_hue >= 256.0f) reactive_hue -= 256.0f;
            }

            reactive_flash -= dt * 5.0f;
            if (reactive_flash < 0.0f) reactive_flash = 0.0f;

            uint8_t val = (uint8_t)(10 + reactive_flash * 245.0f);
            leds[0] = CHSV((uint8_t)reactive_hue, 255, val);
            break;
        }

        default:
            break;
    }
}

void LEDManager::loop() {
    uint32_t now = millis();
    if (now - last_update < 20) return; // ~50 fps
    float dt = (now - last_update) / 1000.0f;
    last_update = now;

    // Low battery cue override (< 10%) - ONLY if physical battery is connected (>0.5V)
    float bat_v = battery.getVoltage();
    int bat_pct = battery.getPercentage();
    if (bat_v > 0.5f && bat_pct <= 10 && current_mode != LedMode::LOW_BATTERY_PULSE && current_mode != LedMode::PULSE) {
        setMode(LedMode::LOW_BATTERY_PULSE);
    } else if ((bat_v <= 0.5f || bat_pct > 10) && current_mode == LedMode::LOW_BATTERY_PULSE) {
        setMode(user_mode);
    }

    if (!master_enabled && current_mode != LedMode::LOW_BATTERY_PULSE) {
        leds[0] = CRGB::Black;
        FastLED.show();
        return;
    }

    if (isReactiveMode(current_mode)) {
        if (audioPlayer.isPlaying()) {
            spectrumAnalyzer.sampleAudioStream();
        }
        updateReactiveModes(dt);
        FastLED.show();
        return;
    }

    switch (current_mode) {
        case LedMode::OFF:
            leds[0] = CRGB::Black;
            break;

        case LedMode::SOLID:
            leds[0] = current_color;
            break;

        case LedMode::BREATHING:
            anim_phase += dt * PI; // 0.5Hz breathing rhythm
            leds[0] = audioPlayer.isPaused() ? CRGB::Orange : current_color;
            leds[0].nscale8(128 + 127 * sinf(anim_phase));
            break;

        case LedMode::RAINBOW:
            anim_phase += dt * 50.0f;
            leds[0] = CHSV((uint8_t)anim_phase, 255, 255);
            break;

        case LedMode::PULSE:
        case LedMode::BOOT_PULSE:
            if (pulse_count > 0) {
                anim_phase += dt * 1000.0f;
                if (anim_phase > pulse_speed) {
                    anim_phase = 0;
                    pulse_count--;
                    if (pulse_count % 2 == 1) {
                        leds[0] = pulse_color; // ON phase
                    } else {
                        leds[0] = CRGB::Black; // OFF phase
                    }
                }
            } else {
                setMode(user_mode);
            }
            break;

        case LedMode::BEAT_PULSE:
            // Fast decay pulse for audio beats
            anim_phase += dt * 4.0f;
            if (anim_phase > 1.0f) {
                anim_phase = 1.0f;
                setMode(user_mode);
            } else {
                CRGB pulse = pulse_color;
                pulse.nscale8((uint8_t)((1.0f - anim_phase) * 255.0f));
                leds[0] = pulse;
            }
            break;

        case LedMode::LOW_BATTERY_PULSE:
            anim_phase += dt;
            if (anim_phase > 2.0f) anim_phase = 0.0f;
            if (anim_phase < 0.2f) {
                leds[0] = CRGB::Red;
            } else {
                leds[0] = CRGB::Black;
            }
            break;

        default:
            break;
    }

    FastLED.show();
}
