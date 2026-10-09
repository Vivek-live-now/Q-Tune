#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>
#include "display.h"
#include "button_manager.h"
#include "sd_manager.h"
#include "audio_player.h"
#include "battery.h"
#include "led_manager.h"
#include "spectrum_analyzer.h"

class Diagnostics {
public:
    Diagnostics();
    void begin();
    bool runMenu();

private:
    int selectedIndex;
    static const int TOTAL_TESTS = 11;
    void renderMenu();
    void executeTest(int index);

    void testOLED();
    void testSDDetect();
    void testSDFilesystem();
    void testWAVDiscovery();
    void testButtons();
    void testPinMap();
    void testBattery();
    void testRGBLED();
    void testI2SAudio();
    void testINMP441Mic();
};

extern Diagnostics diagnostics;

#endif // DIAGNOSTICS_H
