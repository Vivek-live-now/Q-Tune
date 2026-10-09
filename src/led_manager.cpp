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

static const uint8_t BRIGHTNESS_LEVELS[] = { 50, 100, 160, 210, 255 };
static const size_t BRIGHTNESS_LEVELS_COUNT = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

LEDManager::LEDManager() :
    current_mode(LedMode::BOOT_PULSE),
    previous_mode(LedMode::OFF),
    user_mode(LedMode::REACT_BASS_PULSE),
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
    total_max(15.0f)
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

    // If audio is not playing (e.g. idle or previewing in RGB settings menu),
    // provide an active, dynamic preview/demo of each mode!
    if (!isPlaying) {
        anim_phase += dt * 2.5f;
        switch (current_mode) {
            case LedMode::REACT_BASS_PULSE: {
                // Rhythmic beat demo pulse (1.25 Hz pulse)
                float pulse = 0.5f + 0.5f * sinf(anim_phase * 2.5f);
                pulse = pulse * pulse * pulse; // sharp kick attack curve
                uint8_t val = (uint8_t)(35 + pulse * 220);
                CRGB c = current_color;
                c.nscale8(val);
                leds[0] = c;
                break;
            }
            case LedMode::REACT_ENERGY_VU: {
                // Smooth demo sweep across VU color gradient (Cyan -> Green -> Yellow -> Red)
                float sweep = 0.5f + 0.5f * sinf(anim_phase * 1.5f);
                uint8_t vuHue = (uint8_t)(140.0f - sweep * 140.0f);
                uint8_t val = (uint8_t)(60 + sweep * 195);
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
                uint8_t val = (uint8_t)(30 + reactive_flash * 225);
                leds[0] = CHSV((uint8_t)reactive_hue, 255, val);
                break;
            }
            default:
                break;
        }
        return;
    }

    // =========================================================================
    // REAL-TIME AUDIO REACTIVE ENGINE (With Dynamic AGC Normalization)
    // =========================================================================

    // 1. Composite Bass Energy (low frequency FFT bands + raw audio envelope)
    float bassRaw = (bands[0] * 2.2f + bands[1] * 1.6f + bands[2] * 1.1f + rms * 0.9f + peak * 0.3f);
    if (bassRaw > bass_max) {
        bass_max = bassRaw; // instant attack
    } else {
        bass_max -= dt * (bass_max * 0.22f); // adaptive decay
    }
    if (bass_max < 8.0f) bass_max = 8.0f;
    float bassNorm = constrain((bassRaw / bass_max) * sens, 0.0f, 1.0f);

    // 2. RMS Energy / Volume (Overall track loudness)
    float energyRaw = (rms * 1.8f + peak * 0.4f);
    if (energyRaw > rms_max) {
        rms_max = energyRaw;
    } else {
        rms_max -= dt * (rms_max * 0.20f);
    }
    if (rms_max < 6.0f) rms_max = 6.0f;
    float energyNorm = constrain((energyRaw / rms_max) * sens, 0.0f, 1.0f);

    // 3. Harmonic Spectrum Centroid (Bass vs Mids vs Highs)
    float b_val = (bands[0] + bands[1] + bands[2] + bands[3]) + 0.1f;
    float m_val = (bands[4] + bands[5] + bands[6] + bands[7]) + 0.1f;
    float h_val = (bands[8] + bands[9] + bands[10] + bands[11] + bands[12]) + 0.1f;
    float totalHarmonics = b_val + m_val + h_val;
    if (totalHarmonics > total_max) {
        total_max = totalHarmonics;
    } else {
        total_max -= dt * (total_max * 0.20f);
    }
    if (total_max < 8.0f) total_max = 8.0f;
    float harmonicNorm = constrain((totalHarmonics / total_max) * sens, 0.0f, 1.0f);

    switch (current_mode) {
        case LedMode::REACT_BASS_PULSE: {
            // Fast attack on bass hit, punchy exponential decay (~250ms)
            if (bassNorm > reactive_energy) {
                reactive_energy = bassNorm;
            } else {
                reactive_energy -= dt * 3.6f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            // Pulse intensity: dynamic range between 25 and 255
            uint8_t val = (uint8_t)(25 + reactive_energy * 230);
            CRGB c = current_color;
            c.nscale8(val);
            leds[0] = c;
            break;
        }

        case LedMode::REACT_ENERGY_VU: {
            // Real-time VU meter with smooth needle tracking
            if (energyNorm > reactive_energy) {
                reactive_energy = energyNorm;
            } else {
                reactive_energy -= dt * 3.0f;
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

            uint8_t val = (uint8_t)(40 + reactive_energy * 215);
            leds[0] = CHSV(vuHue, 255, val);
            break;
        }

        case LedMode::REACT_SPECTRUM_HUE: {
            // Centroid mapping:
            // Bass dominant -> Red (0) / Orange (15)
            // Mids dominant -> Green (96) / Gold (60)
            // Highs dominant -> Cyan (140) / Blue (175)
            float targetHue = (b_val * 10.0f + m_val * 96.0f + h_val * 175.0f) / totalHarmonics;
            reactive_hue += (targetHue - reactive_hue) * (dt * 10.0f);

            if (harmonicNorm > reactive_energy) {
                reactive_energy = harmonicNorm;
            } else {
                reactive_energy -= dt * 3.2f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            uint8_t val = (uint8_t)(45 + reactive_energy * 210);
            leds[0] = CHSV((uint8_t)reactive_hue, 245, val);
            break;
        }

        case LedMode::REACT_RAINBOW_FLOW: {
            // Rainbow wheel rotation accelerates dynamically with music energy
            float spinSpeed = 35.0f + (energyNorm * 185.0f);
            anim_phase += dt * spinSpeed;

            if (bassNorm > reactive_energy) {
                reactive_energy = bassNorm;
            } else {
                reactive_energy -= dt * 3.5f;
                if (reactive_energy < 0.0f) reactive_energy = 0.0f;
            }

            uint8_t val = (uint8_t)(50 + reactive_energy * 205);
            leds[0] = CHSV((uint8_t)anim_phase, 255, val);
            break;
        }

        case LedMode::REACT_FIRE: {
            // Warm campfire flame that surges into intense gold/white flares on audio peaks
            uint8_t microFlicker = (uint8_t)(rand() % 28);
            uint8_t fireHue = (uint8_t)constrain(8 + (int)(energyNorm * 26.0f), 0, 35);
            uint8_t fireVal = (uint8_t)constrain(60 + (int)(energyNorm * 180.0f) + microFlicker, 0, 255);
            leds[0] = CHSV(fireHue, 245, fireVal);
            break;
        }

        case LedMode::REACT_DISCO_FLASH: {
            // Dynamic transient beat detection (spikes in bass energy)
            float delta = bassRaw - bass_avg;
            bass_avg += (bassRaw - bass_avg) * (dt * 3.5f);

            uint32_t now = millis();
            if (delta > (bass_avg * 0.22f + 1.8f) && (now - last_beat_time > 140)) {
                last_beat_time = now;
                reactive_flash = 1.0f;
                reactive_hue += 77.0f;
                if (reactive_hue >= 256.0f) reactive_hue -= 256.0f;
            }

            reactive_flash -= dt * 4.5f;
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
