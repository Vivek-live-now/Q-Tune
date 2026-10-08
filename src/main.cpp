#include <Arduino.h>
#include "hw_config.h"
#include "display.h"
#include "button_manager.h"
#include "sd_manager.h"
#include "audio_player.h"
#include "battery.h"
#include "led_manager.h"
#include "i2c_scanner.h"
#include "diagnostics.h"
#include "ui_player.h"
#include "power_manager.h"
#include "spectrum_analyzer.h"

enum AppMode {
    MODE_DIAGNOSTICS,
    MODE_PLAYER
};

AppMode currentMode = MODE_DIAGNOSTICS;

void setup() {
    Serial.begin(115200);

    powerManager.begin();
    display.begin();
    buttonManager.begin();
    battery.begin();
    ledManager.begin();
    i2cScanner.begin();
    sdManager.begin();
    audioPlayer.begin();
    spectrumAnalyzer.begin();

    diagnostics.begin();
    uiPlayer.begin();
}

void loop() {
    ledManager.loop();

    if (currentMode == MODE_DIAGNOSTICS) {
        diagnostics.runMenu();
    } else {
        uiPlayer.update();
    }
    delay(10);
}
