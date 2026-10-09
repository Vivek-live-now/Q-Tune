#include "power_manager.h"
#include "sd_manager.h"
#include "audio_player.h"
#include "display.h"
#include "led_manager.h"
#include "battery.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#endif

PowerManager powerManager;

PowerManager::PowerManager() :
    currentProfile(PowerProfile::BALANCED),
    shutdownInitiated(false),
    lowVoltageHitCount(0) {}

void PowerManager::begin() {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    // Release any deep-sleep GPIO pad holds so SD_CS and all pins can be toggled
    gpio_hold_dis((gpio_num_t)SD_CS);
    gpio_deep_sleep_hold_dis();
#endif
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
    // Guarantee SD card is unmounted and CS is held HIGH before entering sleep
    if (sdManager.isMounted()) {
        sdManager.unmount();
    }
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Apply GPIO hold so SD_CS stays HIGH during deep sleep (prevents bus float & glitch)
    gpio_hold_en((gpio_num_t)SD_CS);
    gpio_deep_sleep_hold_en();

    // Clear and turn off display
    display.clear();
    display.sendBuffer();
    display.getU8g2().setPowerSave(1);

    // Ensure CANCEL button is physically released before arming wakeup (prevents instant reboot loop)
    uint32_t waitStart = millis();
    while (digitalRead(BTN_CANCEL) == LOW && (millis() - waitStart < 5000)) {
        delay(25);
    }
    delay(150); // Debounce mechanical release

    // Maintain RTC pull-up on wake pin so it does not float during deep sleep
    rtc_gpio_pullup_en((gpio_num_t)BTN_CANCEL);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_CANCEL);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_CANCEL, 0); // Wake on CANCEL press (RTC_GPIO16)
    esp_deep_sleep_start();
#endif
}

void PowerManager::checkBatterySafety() {
    if (shutdownInitiated) return;

    float v = battery.getVoltage();
    if (v > 0.5f && v <= CRITICAL_SHUTDOWN_VOLTAGE) {
        lowVoltageHitCount++;
        // Debounce 3 consecutive checks to prevent single-sample transient ADC noise
        if (lowVoltageHitCount >= 3) {
            safeShutdown("LOW BATTERY (CRITICAL)");
        }
    } else {
        if (lowVoltageHitCount > 0) lowVoltageHitCount--;
    }
}

void PowerManager::safeShutdown(const char* reason) {
    if (shutdownInitiated) return;
    shutdownInitiated = true;

    // 1. Immediately cut audio to eliminate speaker amplifier load spike
    audioPlayer.stop();

    // 2. Display shutdown warning & SD unmount status on OLED
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(8, 16, "[POWER OFF]");
    char buf[32];
    snprintf(buf, sizeof(buf), "Reason: %s", reason ? reason : "Shutdown");
    u8g2.drawStr(8, 28, buf);
    u8g2.drawStr(8, 42, "Unmounting SD...");
    display.sendBuffer();

    // 3. Gracefully flush and unmount SD card via SDManager
    sdManager.unmount();

    // 4. Update screen indicating SD is safe to remove
    u8g2.drawStr(8, 54, "SD Safe to Remove");
    display.sendBuffer();

    // 5. LED shutdown indicator
    ledManager.setColor(CRGB::Red);
    ledManager.loop();
    delay(400);
    ledManager.off();

    // 6. Enter deep sleep with GPIO 8 (SD_CS) held HIGH (deselected)
    enterDeepSleep();
}
