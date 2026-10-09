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
    { CRGB::White,   "WHTE" }
};
static const size_t PALETTE_COUNT = sizeof(COLOR_PALETTE) / sizeof(COLOR_PALETTE[0]);

static const LedMode SELECTABLE_MODES[] = {
    LedMode::REACT_BASS_PULSE,
    LedMode::REACT_ENERGY_VU,
    LedMode::REACT_SPECTRUM_HUE,
    LedMode::REACT_RAINBOW_FLOW,
    LedMode::REACT_FIRE,
    LedMode::REACT_DISCO_FLASH,
    LedMode::BREATHING,
    LedMode::RAINBOW,
    LedMode::SOLID,
    LedMode::OFF
};
static const size_t SELECTABLE_MODES_COUNT = sizeof(SELECTABLE_MODES) / sizeof(SELECTABLE_MODES[0]);

static const uint8_t BRIGHTNESS_LEVELS[] = { 25, 60, 120, 180, 255 };
static const size_t BRIGHTNESS_LEVELS_COUNT = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

LEDManager::LEDManager() :
    current_mode(LedMode::BOOT_PULSE),
    previous_mode(LedMode::OFF),
    user_mode(LedMode::REACT_BASS_PULSE),
    current_color(CRGB::Cyan),
    color_index(0),
    current_brightness(60),
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
    last_beat_time(0)
{}

void LEDManager::begin() {
    FastLED.addLeds<WS2812, RGB_LED, GRB>(leds, 1);
    FastLED.setBrightness(current_brightness);
    triggerPulse(CRGB::Blue, 2, 200); // 2 short blue pulses on boot
}

bool LEDManager::isReactiveMode(LedMode mode) {
    return (mode == LedMode::REACT_BASS_PULSE ||
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

const char* LEDManager::getModeName() const {
    LedMode m = (current_mode == LedMode::PULSE || current_mode == LedMode::BOOT_PULSE || current_mode == LedMode::LOW_BATTERY_PULSE) ? user_mode : current_mode;
    switch (m) {
        case LedMode::REACT_BASS_PULSE:   return "BASS PULSE";
        case LedMode::REACT_ENERGY_VU:    return "ENERGY VU";
        case LedMode::REACT_SPECTRUM_HUE: return "SPECTRUM HUE";
        case LedMode::REACT_RAINBOW_FLOW: return "RAINBOW FLOW";
        case LedMode::REACT_FIRE:         return "FIRE FLAME";
        case LedMode::REACT_DISCO_FLASH:  return "DISCO STROBE";
        case LedMode::BREATHING:          return "BREATHING";
        case LedMode::RAINBOW:            return "RAINBOW WHEEL";
        case LedMode::SOLID:              return "SOLID COLOR";
        case LedMode::OFF:                return "OFF";
        case LedMode::BOOT_PULSE:         return "BOOT PULSE";
        case LedMode::LOW_BATTERY_PULSE:  return "LOW BATTERY";
        case LedMode::PULSE:
        case LedMode::BEAT_PULSE:         return "PULSE";
        default:                          return "MUSIC SYNC";
    }
}

const char* LEDManager::getShortModeName() const {
    LedMode m = (current_mode == LedMode::PULSE || current_mode == LedMode::BOOT_PULSE || current_mode == LedMode::LOW_BATTERY_PULSE) ? user_mode : current_mode;
    switch (m) {
        case LedMode::REACT_BASS_PULSE:   return "BASS";
        case LedMode::REACT_ENERGY_VU:    return "VU";
        case LedMode::REACT_SPECTRUM_HUE: return "SPEC";
        case LedMode::REACT_RAINBOW_FLOW: return "FLOW";
        case LedMode::REACT_FIRE:         return "FIRE";
        case LedMode::REACT_DISCO_FLASH:  return "DSCO";
        case LedMode::BREATHING:          return "BRTH";
        case LedMode::RAINBOW:            return "RAIN";
        case LedMode::SOLID:              return "SLID";
        case LedMode::OFF:                return "OFF";
        default:                          return "RGB";
    }
}

void LEDManager::setColor(CRGB color) {
    current_color = color;
    setMode(LedMode::SOLID);
    leds[0] = color;
    FastLED.show();
}

void LEDManager::cycleColor() {
    color_index = (color_index + 1) % PALETTE_COUNT;
    current_color = COLOR_PALETTE[color_index].color;
    if (user_mode == LedMode::SOLID || user_mode == LedMode::BREATHING || user_mode == LedMode::REACT_BASS_PULSE) {
        leds[0] = current_color;
        FastLED.show();
    }
}

const char* LEDManager::getColorName() const {
    return COLOR_PALETTE[color_index].name;
}

void LEDManager::setBrightness(uint8_t brightness) {
    current_brightness = brightness;
    FastLED.setBrightness(brightness);
    FastLED.show();
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

void LEDManager::cycleSensitivity() {
    if (current_sensitivity == LedSensitivity::SENS_LOW) {
        current_sensitivity = LedSensitivity::SENS_NORMAL;
    } else if (current_sensitivity == LedSensitivity::SENS_NORMAL) {
        current_sensitivity = LedSensitivity::SENS_HIGH;
    } else {
        current_sensitivity = LedSensitivity::SENS_LOW;
    }
}

const char* LEDManager::getSensitivityName() const {
    switch (current_sensitivity) {
        case LedSensitivity::SENS_LOW:    return "LOW";
        case LedSensitivity::SENS_NORMAL: return "NORM";
        case LedSensitivity::SENS_HIGH:   return "HIGH";
        default:                          return "NORM";
    }
}

void LEDManager::off() {
    setMode(LedMode::OFF);
    leds[0] = CRGB::Black;
    FastLED.show();
}

void LEDManager::triggerPulse(CRGB color, int count, int speed_ms) {
    pulse_color = color;
    pulse_count = count * 2; // ON + OFF phases
    pulse_speed = speed_ms;
    setMode(LedMode::PULSE);
}

void LEDManager::onPlaybackStart() {
    setMode(user_mode);
}

void LEDManager::onPlaybackResume() {
    setMode(user_mode);
}

void LEDManager::onPlaybackPause() {
    setColor(CRGB::Orange);
}

void LEDManager::onPlaybackStop() {
    setMode(user_mode);
}

void LEDManager::updateReactiveModes(float dt) {
    const uint8_t* bands = spectrumAnalyzer.getBands();
    float rms = spectrumAnalyzer.getRMSLevel();
    float peak = spectrumAnalyzer.getPeakLevel();
    bool isPlaying = audioPlayer.isPlaying();

    float sens = (current_sensitivity == LedSensitivity::SENS_LOW) ? 0.70f :
                 ((current_sensitivity == LedSensitivity::SENS_HIGH) ? 1.45f : 1.0f);

    // Resting ambient breath when audio stream is stopped, paused, or silent
    if (!isPlaying || (rms < 1.2f && peak < 3.0f)) {
        anim_phase += dt * 1.5f; // Gentle 0.24 Hz idle breath
        float breath = 0.5f + 0.5f * sinf(anim_phase);
        uint8_t restVal = (uint8_t)(14 + breath * 32); // 14..46 brightness

        if (current_mode == LedMode::REACT_RAINBOW_FLOW) {
            leds[0] = CHSV((uint8_t)(anim_phase * 18.0f), 220, restVal);
        } else if (current_mode == LedMode::REACT_FIRE) {
            leds[0] = CHSV(10, 240, restVal);
        } else if (current_mode == LedMode::REACT_ENERGY_VU) {
            leds[0] = CHSV(135, 230, restVal);
        } else {
            CRGB c = current_color;
            c.nscale8(restVal);
            leds[0] = c;
        }
        return;
    }

    switch (current_mode) {
        case LedMode::REACT_BASS_PULSE: {
            // Bass band energy: bands 0, 1, 2 (Sub-bass, kick drum, bass guitar)
            float bassRaw = ((bands[0] * 1.6f + bands[1] * 1.2f + bands[2] * 0.8f) / 3.6f) * sens;
            float bassNorm = bassRaw / 36.0f;
            if (bassNorm > 1.0f) bassNorm = 1.0f;

            // Fast attack, smooth organic exponential decay
            if (bassNorm > reactive_energy) {
                reactive_energy = bassNorm;
            } else {
                reactive_energy -= dt * 3.8f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            // Dynamic brightness mapping: 20 baseline up to 255 peak
            uint8_t val = (uint8_t)(20 + reactive_energy * 235);
            CRGB c = current_color;
            c.nscale8(val);
            leds[0] = c;
            break;
        }

        case LedMode::REACT_ENERGY_VU: {
            // RMS loudness mapping
            float energyNorm = (rms / 110.0f) * sens;
            if (energyNorm > 1.0f) energyNorm = 1.0f;

            if (energyNorm > reactive_energy) {
                reactive_energy = energyNorm;
            } else {
                reactive_energy -= dt * 4.2f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            // VU Color gradient mapping:
            // 0.00 .. 0.35: Cyan to Green (hue 140 -> 96)
            // 0.35 .. 0.70: Green to Yellow/Amber (hue 96 -> 32)
            // 0.70 .. 1.00: Yellow/Amber to Red (hue 32 -> 0)
            uint8_t vuHue;
            if (reactive_energy < 0.35f) {
                float frac = reactive_energy / 0.35f;
                vuHue = (uint8_t)(140.0f - frac * 44.0f);
            } else if (reactive_energy < 0.70f) {
                float frac = (reactive_energy - 0.35f) / 0.35f;
                vuHue = (uint8_t)(96.0f - frac * 64.0f);
            } else {
                float frac = (reactive_energy - 0.70f) / 0.30f;
                vuHue = (uint8_t)(32.0f - frac * 32.0f);
            }

            uint8_t val = (uint8_t)(30 + reactive_energy * 225);
            leds[0] = CHSV(vuHue, 255, val);
            break;
        }

        case LedMode::REACT_SPECTRUM_HUE: {
            // Frequency centroid across Low / Mid / High
            float b_val = (bands[0] + bands[1] + bands[2]) / 3.0f;
            float m_val = (bands[4] + bands[5] + bands[6] + bands[7]) / 4.0f;
            float h_val = (bands[10] + bands[11] + bands[12] + bands[13]) / 4.0f;
            float total = b_val + m_val + h_val;

            if (total > 2.0f) {
                // Low = Red (0), Mid = Green (96), High = Cyan/Blue (160)
                float targetHue = (b_val * 0.0f + m_val * 96.0f + h_val * 160.0f) / total;
                reactive_hue += (targetHue - reactive_hue) * (dt * 8.0f);
            }

            float norm = (total / 45.0f) * sens;
            if (norm > 1.0f) norm = 1.0f;
            if (norm > reactive_energy) {
                reactive_energy = norm;
            } else {
                reactive_energy -= dt * 3.5f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            uint8_t val = (uint8_t)(35 + reactive_energy * 220);
            leds[0] = CHSV((uint8_t)reactive_hue, 240, val);
            break;
        }

        case LedMode::REACT_RAINBOW_FLOW: {
            // High frequency content accelerates rainbow spin
            float hiFreq = ((bands[8] + bands[9] + bands[10] + bands[11]) / 4.0f) * sens;
            float spinSpeed = 35.0f + hiFreq * 5.0f; // 35 deg/s up to 250+ deg/s
            anim_phase += dt * spinSpeed;

            // Bass energy modulates pulse brightness
            float bass = ((bands[0] + bands[1]) / 2.0f / 35.0f) * sens;
            if (bass > 1.0f) bass = 1.0f;
            if (bass > reactive_energy) {
                reactive_energy = bass;
            } else {
                reactive_energy -= dt * 3.5f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            uint8_t val = (uint8_t)(45 + reactive_energy * 210);
            leds[0] = CHSV((uint8_t)anim_phase, 255, val);
            break;
        }

        case LedMode::REACT_FIRE: {
            // Flame flicker driven by RMS & bass transients
            float flare = (rms / 95.0f) * sens;
            if (flare > 1.0f) flare = 1.0f;

            // Micro flicker noise (0..20)
            uint8_t microFlicker = (uint8_t)(rand() % 22);

            // Baseline ember: Deep orange/red (hue 8)
            // On flares: Shifts up to gold/yellow (hue 28..32)
            uint8_t fireHue = (uint8_t)constrain(8 + (int)(flare * 20.0f) + (microFlicker >> 2), 0, 32);
            uint8_t fireVal = (uint8_t)constrain(50 + (int)(flare * 180.0f) + microFlicker, 0, 255);

            leds[0] = CHSV(fireHue, 250, fireVal);
            break;
        }

        case LedMode::REACT_DISCO_FLASH: {
            // Beat transient detection (rapid jump in bass or RMS)
            float bass = ((bands[0] * 1.5f + bands[1]) / 2.5f) * sens;
            float delta = bass - last_bass;
            last_bass = bass * 0.85f; // decay baseline

            uint32_t now = millis();
            if (delta > 7.0f && (now - last_beat_time > 130)) {
                last_beat_time = now;
                reactive_flash = 1.0f;
                // Shift hue by golden ratio angle (~77 deg) for vibrant contrast
                reactive_hue += 77.0f;
                if (reactive_hue >= 256.0f) reactive_hue -= 256.0f;
            }

            reactive_flash -= dt * 5.5f;
            if (reactive_flash < 0.0f) reactive_flash = 0.0f;

            uint8_t val = (uint8_t)(25 + reactive_flash * 230);
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

    // Low battery cue override (< 10%)
    int bat_pct = battery.getPercentage();
    if (bat_pct <= 10 && current_mode != LedMode::LOW_BATTERY_PULSE && current_mode != LedMode::PULSE) {
        setMode(LedMode::LOW_BATTERY_PULSE);
    } else if (bat_pct > 10 && current_mode == LedMode::LOW_BATTERY_PULSE) {
        setMode(previous_mode);
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
            leds[0] = current_color;
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
                setMode(previous_mode);
            }
            break;

        case LedMode::BEAT_PULSE:
            // Fast decay pulse for audio beats
            anim_phase += dt * 4.0f;
            if (anim_phase > 1.0f) {
                anim_phase = 1.0f;
                setMode(previous_mode);
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
