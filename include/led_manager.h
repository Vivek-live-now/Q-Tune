#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <FastLED.h>
#include "hw_config.h"

class LEDManager {
public:
    LEDManager();
    void begin();
    void setColor(CRGB color);
    void off();

private:
    CRGB leds[1];
};

extern LEDManager ledManager;

#endif // LED_MANAGER_H
