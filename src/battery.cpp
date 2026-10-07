#include "battery.h"

const float Battery::CALIBRATION_MULTIPLIER = 1.0f;

Battery::Battery() : cached_voltage(0.0f), last_read_time(0) {}

void Battery::begin() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_ADC, INPUT);
}

float Battery::getVoltage() {
    uint32_t now = millis();
    if (cached_voltage > 0.0f && (now - last_read_time < 2000)) {
        return cached_voltage;
    }
    last_read_time = now;

    uint32_t mv_sum = 0;
    const int NUM_SAMPLES = 10;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        mv_sum += analogReadMilliVolts(BATTERY_ADC);
    }

    uint32_t mv_avg = mv_sum / NUM_SAMPLES;
    // 100k/100k voltage divider halves the voltage at the pin -> multiply by 2.0
    float actual_voltage = (mv_avg * 2.0f) / 1000.0f;
    cached_voltage = actual_voltage * CALIBRATION_MULTIPLIER;
    return cached_voltage;
}

int Battery::getPercentage() {
    float v = getVoltage();
    // Piecewise Linear (PWL) estimation for standard 3.7V/4.2V LiPo (reused from Q-Watch)
    if (v >= 4.20f) return 100;
    if (v >= 4.10f) return 90;
    if (v >= 4.00f) return 80;
    if (v >= 3.90f) return 60;
    if (v >= 3.80f) return 40;
    if (v >= 3.70f) return 20;
    if (v >= 3.60f) return 10;
    if (v >= 3.50f) return 5;
    return 0;
}

float Battery::getCoreTemperature() {
#ifdef ARDUINO
    return temperatureRead();
#else
    return 25.0f;
#endif
}

Battery battery;
