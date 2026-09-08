#include "led_manager.h"

LEDManager::LEDManager() {}

void LEDManager::begin() {
    FastLED.addLeds<WS2812, RGB_LED, GRB>(leds, 1);
    FastLED.setBrightness(30);
    off();
}

void LEDManager::setColor(CRGB color) {
    leds[0] = color;
    FastLED.show();
}

void LEDManager::off() {
    leds[0] = CRGB::Black;
    FastLED.show();
}

LEDManager ledManager;
