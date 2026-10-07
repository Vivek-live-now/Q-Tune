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

private:
    PowerProfile currentProfile;
};

extern PowerManager powerManager;

#endif // POWER_MANAGER_H
