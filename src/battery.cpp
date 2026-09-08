#include "battery.h"

Battery::Battery() {}

void Battery::begin() {
    analogReadResolution(12);
}

float Battery::getVoltage() {
    uint32_t raw = analogRead(BATTERY_ADC);
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
    return voltage;
}

int Battery::getPercentage() {
    float v = getVoltage();
    if (v >= 4.2f) return 100;
    if (v <= 3.2f) return 0;
    return (int)((v - 3.2f) / (4.2f - 3.2f) * 100.0f);
}

Battery battery;
