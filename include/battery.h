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
};

extern Battery battery;

#endif // BATTERY_H
