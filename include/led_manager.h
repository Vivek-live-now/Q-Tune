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
    BEAT_PULSE,
    // Music-Reactive Modes
    REACT_BASS_PULSE,
    REACT_ENERGY_VU,
    REACT_SPECTRUM_HUE,
    REACT_RAINBOW_FLOW,
    REACT_FIRE,
    REACT_DISCO_FLASH
};

enum class LedSensitivity {
    SENS_LOW = 1,
    SENS_NORMAL = 2,
    SENS_HIGH = 3
};

class LEDManager {
public:
    LEDManager();
    void begin();
    void loop();

    void setMode(LedMode mode);
    LedMode getMode() const { return current_mode; }
    LedMode getUserMode() const { return user_mode; }
    void setUserMode(LedMode mode);
    void cycleMode();
    const char* getModeName() const;
    const char* getShortModeName() const;
    bool isReactiveMode() const { return isReactiveMode(current_mode); }
    static bool isReactiveMode(LedMode mode);

    void setColor(CRGB color);
    CRGB getColor() const { return current_color; }
    void cycleColor();
    const char* getColorName() const;

    void setBrightness(uint8_t brightness);
    uint8_t getBrightness() const { return current_brightness; }
    void cycleBrightness();

    void setSensitivity(LedSensitivity sens) { current_sensitivity = sens; }
    LedSensitivity getSensitivity() const { return current_sensitivity; }
    void cycleSensitivity();
    const char* getSensitivityName() const;

    void off();
    void triggerPulse(CRGB color, int count = 1, int speed_ms = 150);

    // Audio playback lifecycle integration
    void onPlaybackStart();
    void onPlaybackResume();
    void onPlaybackPause();
    void onPlaybackStop();

private:
    CRGB leds[1];
    LedMode current_mode;
    LedMode previous_mode;
    LedMode user_mode;

    CRGB current_color;
    uint8_t color_index;
    uint8_t current_brightness;
    LedSensitivity current_sensitivity;

    uint32_t last_update;
    float anim_phase;
    int pulse_count;
    int pulse_speed;
    CRGB pulse_color;

    // Reactive audio animation states
    float reactive_energy;
    float reactive_hue;
    float reactive_flash;
    float last_bass;
    uint32_t last_beat_time;

    void updateReactiveModes(float dt);
};

extern LEDManager ledManager;

#endif // LED_MANAGER_H
