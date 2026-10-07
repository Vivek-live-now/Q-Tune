#include "led_manager.h"
#include "battery.h"

LEDManager ledManager;

LEDManager::LEDManager() :
    current_mode(LedMode::BOOT_PULSE),
    previous_mode(LedMode::OFF),
    current_color(CRGB::Blue),
    current_brightness(50),
    last_update(0),
    anim_phase(0.0f),
    pulse_count(0),
    pulse_speed(150),
    pulse_color(CRGB::Cyan)
{}

void LEDManager::begin() {
    FastLED.addLeds<WS2812, RGB_LED, GRB>(leds, 1);
    FastLED.setBrightness(current_brightness);
    triggerPulse(CRGB::Blue, 2, 200); // 2 short blue pulses on boot
}

void LEDManager::setMode(LedMode mode) {
    if (current_mode != mode) {
        previous_mode = current_mode;
        current_mode = mode;
        anim_phase = 0.0f;
    }
}

void LEDManager::setColor(CRGB color) {
    current_color = color;
    setMode(LedMode::SOLID);
    leds[0] = color;
    FastLED.show();
}

void LEDManager::setBrightness(uint8_t brightness) {
    current_brightness = brightness;
    FastLED.setBrightness(brightness);
    FastLED.show();
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
    }

    FastLED.show();
}
