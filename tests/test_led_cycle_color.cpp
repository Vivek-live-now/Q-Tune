#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#ifndef PI
#define PI M_PI
#endif

template <typename T>
T constrain(T amt, T low, T high) {
    return (amt < low) ? low : ((amt > high) ? high : amt);
}

struct CHSV {
    uint8_t h, s, v;
    uint8_t hue, sat, val;
    CHSV() : h(0), s(0), v(0), hue(0), sat(0), val(0) {}
    CHSV(uint8_t h, uint8_t s, uint8_t v) : h(h), s(s), v(v), hue(h), sat(s), val(v) {}
};

struct CRGB {
    uint8_t r, g, b;

    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    CRGB(const CHSV& hsv) : r(hsv.h), g(hsv.s), b(hsv.v) {}

    bool operator==(const CRGB& rhs) const {
        return r == rhs.r && g == rhs.g && b == rhs.b;
    }
    bool operator!=(const CRGB& rhs) const {
        return !(*this == rhs);
    }

    void nscale8(uint8_t scale) {
        r = (uint16_t(r) * scale) >> 8;
        g = (uint16_t(g) * scale) >> 8;
        b = (uint16_t(b) * scale) >> 8;
    }

    static const CRGB Cyan;
    static const CRGB Blue;
    static const CRGB Green;
    static const CRGB Yellow;
    static const CRGB Orange;
    static const CRGB Red;
    static const CRGB Magenta;
    static const CRGB White;
    static const CRGB Violet;
    static const CRGB Black;
};

const CRGB CRGB::Cyan(0, 255, 255);
const CRGB CRGB::Blue(0, 0, 255);
const CRGB CRGB::Green(0, 255, 0);
const CRGB CRGB::Yellow(255, 255, 0);
const CRGB CRGB::Orange(255, 165, 0);
const CRGB CRGB::Red(255, 0, 0);
const CRGB CRGB::Magenta(255, 0, 255);
const CRGB CRGB::White(255, 255, 255);
const CRGB CRGB::Violet(238, 130, 238);
const CRGB CRGB::Black(0, 0, 0);

inline CHSV rgb2hsv_approximate(const CRGB& rgb) {
    return CHSV(rgb.r, rgb.g, rgb.b);
}

enum EOrder { RGB };
enum LEDType { WS2812 };
#define RGB_LED 48

class MockFastLED {
public:
    int showCallCount = 0;
    uint8_t brightness = 255;

    template <LEDType type, int pin, EOrder order>
    void addLeds(CRGB* data, int count) {}

    void show() {
        showCallCount++;
    }
    void setBrightness(uint8_t b) {
        brightness = b;
    }
};

static MockFastLED FastLED;

class Preferences {
public:
    static std::map<std::string, uint8_t> ucharStorage;
    static std::map<std::string, bool> boolStorage;
    static int saveCount;

    bool begin(const char* name, bool readOnly) {
        return true;
    }
    void end() {}

    uint8_t getUChar(const char* key, uint8_t defaultValue) {
        if (ucharStorage.find(key) != ucharStorage.end()) {
            return ucharStorage[key];
        }
        return defaultValue;
    }
    bool getBool(const char* key, bool defaultValue) {
        if (boolStorage.find(key) != boolStorage.end()) {
            return boolStorage[key];
        }
        return defaultValue;
    }

    size_t putUChar(const char* key, uint8_t value) {
        ucharStorage[key] = value;
        saveCount++;
        return 1;
    }
    size_t putBool(const char* key, bool value) {
        boolStorage[key] = value;
        saveCount++;
        return 1;
    }
};

std::map<std::string, uint8_t> Preferences::ucharStorage;
std::map<std::string, bool> Preferences::boolStorage;
int Preferences::saveCount = 0;

uint32_t millis() { return 1000; }

struct MockSpectrumAnalyzer {
    const uint8_t* getBands() { static uint8_t dummy[8] = {0}; return dummy; }
    float getBassEnergy() { return 0.0f; }
    float getBassLevel() { return 0.0f; }
    float getRMSLevel() { return 0.0f; }
    float getRmsLevel() { return 0.0f; }
    float getPeakLevel() { return 0.0f; }
    float getVocalEnergy() { return 0.0f; }
    float getVocalPunch() { return 0.0f; }
    float getVoicePitch() { return 200.0f; }
    float getVoiceConfidence() { return 0.8f; }
    bool isVoicePresent() { return false; }
    void sampleAudioStream() {}
};
static MockSpectrumAnalyzer spectrumAnalyzer;

struct MockAudioPlayer {
    bool isPlaying() { return false; }
    bool isPaused() { return false; }
};
static MockAudioPlayer audioPlayer;

struct MockBattery {
    float getVoltage() { return 3.8f; }
    int getPercentage() { return 100; }
};
static MockBattery battery;

#define ARDUINO_H
#define FastLED_h
#define Preferences_h
#define BATTERY_H
#define SPECTRUM_ANALYZER_H
#define AUDIO_PLAYER_H
#define HW_CONFIG_H

#define LED_MANAGER_H

enum class LedMode {
    OFF,
    SOLID,
    BREATHING,
    PULSE,
    RAINBOW,
    BOOT_PULSE,
    LOW_BATTERY_PULSE,
    BEAT_PULSE,
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

    void loadSettings();
    void saveSettings();

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

    bool isButtonFeedbackEnabled() const { return button_feedback_enabled; }
    void setButtonFeedbackEnabled(bool enabled);
    void toggleButtonFeedback();
    const char* getButtonFeedbackName() const { return button_feedback_enabled ? "ON" : "OFF"; }
    void triggerButtonPulse(CRGB color, int count = 1, int speed_ms = 40);

    bool isTurnOffOnComplete() const { return off_on_complete; }
    void setTurnOffOnComplete(bool enable);
    void toggleTurnOffOnComplete();
    const char* getTurnOffOnCompleteName() const { return off_on_complete ? "ON" : "OFF"; }
    void setMenuPreview(bool active) { in_menu_preview = active; }

    void off();
    void triggerPulse(CRGB color, int count = 1, int speed_ms = 150);

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

    float reactive_energy;
    float reactive_hue;
    float reactive_flash;
    float last_bass;
    uint32_t last_beat_time;
    float bass_max;
    float bass_avg;
    float rms_max;
    float total_max;

    float vocal_avg;
    float vocal_max;
    float lightning_intensity;
    uint32_t last_lightning_time;
    uint8_t lightning_burst_count;

    void updateReactiveModes(float dt);
};

#include "../src/led_manager.cpp"

void test_cycleColor_palette_progression() {
    std::cout << "[RUN] Testing cycleColor palette progression and wrap-around...\n";
    LEDManager mgr;
    mgr.setUserMode(LedMode::SOLID);

    // Default initial color index is 0 (Cyan)
    assert(mgr.getColor() == CRGB::Cyan);
    assert(std::strcmp(mgr.getColorName(), "CYAN") == 0);

    const char* expectedNames[] = {
        "BLUE", "GREN", "YELW", "ORNG", "RED", "MGTA", "WHTE", "PTCH", "CYAN"
    };
    CRGB expectedColors[] = {
        CRGB::Blue, CRGB::Green, CRGB::Yellow, CRGB::Orange, CRGB::Red,
        CRGB::Magenta, CRGB::White, CRGB::Violet, CRGB::Cyan
    };

    for (size_t i = 0; i < 9; i++) {
        int initialSaveCount = Preferences::saveCount;
        mgr.cycleColor();
        assert(mgr.getColor() == expectedColors[i]);
        assert(std::strcmp(mgr.getColorName(), expectedNames[i]) == 0);
        assert(Preferences::saveCount > initialSaveCount);
    }

    std::cout << "  [PASS] Palette progression (9 colors) and wrap-around verified.\n";
}

void test_cycleColor_mode_conditional_led_update() {
    std::cout << "[RUN] Testing cycleColor mode-conditional FastLED updates...\n";
    LEDManager mgr;

    LedMode supportedModes[] = {
        LedMode::SOLID,
        LedMode::BREATHING,
        LedMode::REACT_BASS_PULSE,
        LedMode::REACT_VOCAL_LIGHTNING
    };

    for (LedMode mode : supportedModes) {
        mgr.setUserMode(mode);
        FastLED.showCallCount = 0;
        CRGB prevColor = mgr.getColor();

        mgr.cycleColor();

        assert(mgr.getColor() != prevColor);
        assert(FastLED.showCallCount == 1);
    }

    LedMode nonSupportedModes[] = {
        LedMode::RAINBOW,
        LedMode::REACT_FIRE,
        LedMode::REACT_ENERGY_VU,
        LedMode::REACT_DISCO_FLASH
    };

    for (LedMode mode : nonSupportedModes) {
        mgr.setUserMode(mode);
        FastLED.showCallCount = 0;

        mgr.setUserMode(LedMode::SOLID);
        mgr.setColor(CRGB::Black);
        mgr.setUserMode(mode);
        FastLED.showCallCount = 0;

        CRGB colorBeforeCycle = mgr.getColor();
        mgr.cycleColor();

        assert(mgr.getColor() != colorBeforeCycle);
        assert(FastLED.showCallCount == 0);
    }

    std::cout << "  [PASS] Mode-conditional LED updates verified.\n";
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "       LEDManager::cycleColor() UNIT TESTS       \n";
    std::cout << "==================================================\n";

    test_cycleColor_palette_progression();
    test_cycleColor_mode_conditional_led_update();

    std::cout << "\nAll LEDManager::cycleColor() unit tests PASSED (100%)!\n";
    return 0;
}
