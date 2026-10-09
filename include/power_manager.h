#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include "hw_config.h"

enum class PowerProfile : uint8_t {
    PERFORMANCE = 0, // 240 MHz - Highest compute / fast UI
    BALANCED = 1,    // 160 MHz - Optimal audio playback & energy efficiency
    ENDURANCE = 2    // 80 MHz - Maximum battery longevity
};

class PowerManager {
public:
    static constexpr float CRITICAL_SHUTDOWN_VOLTAGE = 3.35f; // LiPo cutoff brownout threshold
    static constexpr float LOW_BATTERY_WARNING_VOLTAGE = 3.50f;

    PowerManager();
    void begin();

    PowerProfile getProfile() const { return currentProfile; }
    void setProfile(PowerProfile profile);
    const char* getProfileName(PowerProfile profile) const;

    uint32_t getTargetCpuFreqMhz() const;
    void applyCpuFrequency();

    float calculateEstimatedRuntimeHours(float voltage, int percentage) const;
    void enterLightSleep(uint32_t ms = 0);
    void enterDeepSleep();

    // SD Card Safety & Graceful Power-Down
    void safeShutdown(const char* reason = "POWER OFF");
    void checkBatterySafety();
    bool isShutdownInitiated() const { return shutdownInitiated; }

private:
    PowerProfile currentProfile;
    bool shutdownInitiated;
    uint8_t lowVoltageHitCount;
};

extern PowerManager powerManager;

#endif // POWER_MANAGER_H
