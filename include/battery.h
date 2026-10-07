#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include "hw_config.h"

class Battery {
public:
    Battery();
    void begin();
    float getVoltage();
    int getPercentage();
    float getCoreTemperature();

private:
    float cached_voltage;
    uint32_t last_read_time;
    static const float CALIBRATION_MULTIPLIER;
};

extern Battery battery;

#endif // BATTERY_H
