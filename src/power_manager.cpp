#include "power_manager.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <esp_sleep.h>
#include <esp_pm.h>
#endif

PowerManager powerManager;

PowerManager::PowerManager() : currentProfile(PowerProfile::BALANCED) {}

void PowerManager::begin() {
    applyCpuFrequency();
}

void PowerManager::setProfile(PowerProfile profile) {
    currentProfile = profile;
    applyCpuFrequency();
}

const char* PowerManager::getProfileName(PowerProfile profile) const {
    switch (profile) {
        case PowerProfile::PERFORMANCE: return "PERFORMANCE (240MHz)";
        case PowerProfile::BALANCED:    return "BALANCED (160MHz)";
        case PowerProfile::ENDURANCE:   return "ENDURANCE (80MHz)";
    }
    return "UNKNOWN";
}

uint32_t PowerManager::getTargetCpuFreqMhz() const {
    switch (currentProfile) {
        case PowerProfile::PERFORMANCE: return 240;
        case PowerProfile::BALANCED:    return 160;
        case PowerProfile::ENDURANCE:   return 80;
    }
    return 160;
}

void PowerManager::applyCpuFrequency() {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    uint32_t target_mhz = getTargetCpuFreqMhz();
    if (getCpuFrequencyMhz() != target_mhz) {
        setCpuFrequencyMhz(target_mhz);
    }
#endif
}

float PowerManager::calculateEstimatedRuntimeHours(float voltage, int percentage) const {
    // Nominal battery capacity: 500 mAh LiPo
    float capacity_mah = 500.0f;
    float current_ma = 45.0f; // Default balanced active

    switch (currentProfile) {
        case PowerProfile::PERFORMANCE: current_ma = 75.0f; break;
        case PowerProfile::BALANCED:    current_ma = 45.0f; break;
        case PowerProfile::ENDURANCE:   current_ma = 28.0f; break;
    }

    float remaining_mah = (percentage / 100.0f) * capacity_mah;
    return remaining_mah / current_ma;
}

void PowerManager::enterLightSleep(uint32_t ms) {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    if (ms > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
    }
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_CANCEL, 0); // Wake on CANCEL press
    esp_light_sleep_start();
#endif
}

void PowerManager::enterDeepSleep() {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_CANCEL, 0); // Wake on CANCEL press (RTC_GPIO16)
    esp_deep_sleep_start();
#endif
}
