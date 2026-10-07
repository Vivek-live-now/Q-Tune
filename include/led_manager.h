#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <FastLED.h>
#include "hw_config.h"

enum class LedMode {
    OFF,
    SOLID,
    BREATHING,
    PULSE,
    RAINBOW,
    BOOT_PULSE,
    LOW_BATTERY_PULSE,
    BEAT_PULSE
};

class LEDManager {
public:
    LEDManager();
    void begin();
    void loop();

    void setMode(LedMode mode);
    LedMode getMode() const { return current_mode; }

    void setColor(CRGB color);
    void setBrightness(uint8_t brightness);
    void off();
    void triggerPulse(CRGB color, int count = 1, int speed_ms = 150);

private:
    CRGB leds[1];
    LedMode current_mode;
    LedMode previous_mode;
    CRGB current_color;
    uint8_t current_brightness;
    uint32_t last_update;
    float anim_phase;
    int pulse_count;
    int pulse_speed;
    CRGB pulse_color;
};

extern LEDManager ledManager;

#endif // LED_MANAGER_H
