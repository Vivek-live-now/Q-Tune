#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
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
    REACT_VOCAL_LIGHTNING,
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

    // Persistent settings storage
    void loadSettings();
    void saveSettings();

    // Master on/off control for reactive lights
    bool isEnabled() const { return master_enabled; }
    void setEnabled(bool enabled);
    void toggleEnabled();
    const char* getEnabledName() const { return master_enabled ? "ON" : "OFF"; }

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

    void setSensitivity(LedSensitivity sens);
    LedSensitivity getSensitivity() const { return current_sensitivity; }
    void cycleSensitivity();
    const char* getSensitivityName() const;

    // Button reactive lights control
    bool isButtonFeedbackEnabled() const { return button_feedback_enabled; }
    void setButtonFeedbackEnabled(bool enabled);
    void toggleButtonFeedback();
    const char* getButtonFeedbackName() const { return button_feedback_enabled ? "ON" : "OFF"; }
    void triggerButtonPulse(CRGB color, int count = 1, int speed_ms = 40);

    // Auto-off when music play is complete
    bool isTurnOffOnComplete() const { return off_on_complete; }
    void setTurnOffOnComplete(bool enable);
    void toggleTurnOffOnComplete();
    const char* getTurnOffOnCompleteName() const { return off_on_complete ? "ON" : "OFF"; }
    void setMenuPreview(bool active) { in_menu_preview = active; }

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

    bool master_enabled;
    bool button_feedback_enabled;
    bool off_on_complete;
    bool in_menu_preview;

    CRGB current_color;
    uint8_t color_index;
    uint8_t current_brightness;
    LedSensitivity current_sensitivity;

    uint32_t last_update;
    float anim_phase;
    int pulse_count;
    int pulse_speed;
    CRGB pulse_color;

    // Reactive audio animation states & Dynamic AGC trackers
    float reactive_energy;
    float reactive_hue;
    float reactive_flash;
    float last_bass;
    uint32_t last_beat_time;
    float bass_max;
    float bass_avg;
    float rms_max;
    float total_max;

    // Vocal lightning trackers
    float vocal_avg;
    float vocal_max;
    float lightning_intensity;
    uint32_t last_lightning_time;
    uint8_t lightning_burst_count;

    void updateReactiveModes(float dt);
};

extern LEDManager ledManager;

#endif // LED_MANAGER_H
