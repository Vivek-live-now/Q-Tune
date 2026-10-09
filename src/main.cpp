#include <Arduino.h>
#include "hw_config.h"
#include "display.h"
#include "button_manager.h"
#include "sd_manager.h"
#include "audio_player.h"
#include "battery.h"
#include "led_manager.h"
#include "diagnostics.h"
#include "ui_player.h"
#include "power_manager.h"
#include "spectrum_analyzer.h"

// Multi-core thread-safe SPI arbitration mutex (OLED vs microSD)
SemaphoreHandle_t spiBusMutex = NULL;

enum AppMode {
    MODE_MAIN_MENU,
    MODE_PLAYER,
    MODE_VISUALIZER,
    MODE_DIAGNOSTICS,
    MODE_SYSTEM_INFO
};

AppMode currentMode = MODE_MAIN_MENU;
int menuSelection = 0;
int menuScrollOffset = 0;
const int MAIN_MENU_COUNT = 4;
const char* menuLabels[] = {
    "1. Music Player",
    "2. Spectrum Visualizer",
    "3. Diagnostics",
    "4. System Info"
};

void renderMainMenu() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    String vals[MAIN_MENU_COUNT];
    vals[0] = audioPlayer.isPlaying() ? "[PLAY]" : (audioPlayer.isPaused() ? "[PAUS]" : "[IDLE]");
    vals[1] = String("[") + spectrumAnalyzer.getPresetName() + "]";
    vals[2] = "[10]";
    vals[3] = "[INFO]";

    display.drawStandardMenu("Q-TUNES", menuLabels, MAIN_MENU_COUNT, menuSelection, menuScrollOffset, vals, batBuf);
    display.sendBuffer();
}

void updateMainMenu() {
    renderMainMenu();
    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_UP_PRESS) {
        Display::navigateMenu(menuSelection, menuScrollOffset, MAIN_MENU_COUNT, -1);
        ledManager.triggerPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_DN_PRESS) {
        Display::navigateMenu(menuSelection, menuScrollOffset, MAIN_MENU_COUNT, +1);
        ledManager.triggerPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        ledManager.triggerPulse(CRGB::Green, 1, 60);
        switch (menuSelection) {
            case 0:
                currentMode = MODE_PLAYER;
                uiPlayer.begin();
                break;
            case 1:
                currentMode = MODE_VISUALIZER;
                spectrumAnalyzer.start();
                if (!audioPlayer.isPlaying() && !audioPlayer.isPaused()) {
                    uiPlayer.playNextTrack();
                }
                break;
            case 2:
                currentMode = MODE_DIAGNOSTICS;
                diagnostics.begin();
                break;
            case 3:
                currentMode = MODE_SYSTEM_INFO;
                break;
        }
    } else if (evt == BTN_EVENT_CANCEL_HOLD) {
        ledManager.triggerPulse(CRGB::Red, 2, 100);
        powerManager.safeShutdown("USER POWER OFF");
    }
}

void updateVisualizerMode() {
    spectrumAnalyzer.sampleAudioStream();
    spectrumAnalyzer.render();

    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        spectrumAnalyzer.stop();
        currentMode = MODE_MAIN_MENU;
    } else if (evt == BTN_EVENT_DN_PRESS) {
        spectrumAnalyzer.nextPreset();
        ledManager.triggerPulse(CRGB::Magenta, 1, 80);
    } else if (evt == BTN_EVENT_UP_PRESS) {
        spectrumAnalyzer.previousPreset();
        ledManager.triggerPulse(CRGB::Magenta, 1, 80);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        if (audioPlayer.isPlaying()) {
            audioPlayer.pause();
            ledManager.setColor(CRGB::Orange);
        } else if (audioPlayer.isPaused()) {
            audioPlayer.resume();
            ledManager.setMode(LedMode::BREATHING);
        } else {
            uiPlayer.playNextTrack();
            ledManager.setMode(LedMode::BREATHING);
        }
    } else if (evt == BTN_EVENT_UP_HOLD) {
        audioPlayer.volumeUp(5);
        ledManager.triggerPulse(CRGB::Green, 1, 60);
    } else if (evt == BTN_EVENT_DN_HOLD) {
        audioPlayer.volumeDown(5);
        ledManager.triggerPulse(CRGB::Red, 1, 60);
    }
}

void updateSystemInfoMode() {
    display.clear();
    display.drawTopStatusBar("SYSTEM INFO", battery.getPercentage());
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);

    char buf[32];
    snprintf(buf, sizeof(buf), "VBAT: %.2fV (%d%%)", battery.getVoltage(), battery.getPercentage());
    u8g2.drawStr(4, 23, buf);

    snprintf(buf, sizeof(buf), "CPU:  %d MHz", (int)powerManager.getTargetCpuFreqMhz());
    u8g2.drawStr(4, 35, buf);

    snprintf(buf, sizeof(buf), "VOL:  %d%%", audioPlayer.getVolume());
    u8g2.drawStr(4, 47, buf);

    const char* sdStatusStr = sdManager.isMounted() ? "MOUNTED (OK)" : (sdManager.isSafeToRemove() ? "EJECTED (SAFE)" : "UNMOUNTED");
    snprintf(buf, sizeof(buf), "SD: %s", sdStatusStr);
    u8g2.drawStr(4, 59, buf);

    display.sendBuffer();

    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_SEL_PRESS) {
        if (sdManager.isMounted()) {
            sdManager.unmount();
            ledManager.triggerPulse(CRGB::Orange, 2, 80);
        } else {
            sdManager.remount();
            ledManager.triggerPulse(CRGB::Green, 2, 80);
        }
    } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        currentMode = MODE_MAIN_MENU;
    }
}

void setup() {
    Serial.begin(115200);

    // Initialize FreeRTOS shared SPI mutex before any subsystem begins
    spiBusMutex = xSemaphoreCreateMutex();

    powerManager.begin();
    display.begin();
    buttonManager.begin();
    battery.begin();
    ledManager.begin();
    sdManager.begin();
    audioPlayer.begin();
    spectrumAnalyzer.begin();

    diagnostics.begin();
    uiPlayer.begin();
}

void loop() {
    ledManager.loop();
    powerManager.checkBatterySafety();

    switch (currentMode) {
        case MODE_MAIN_MENU:
            updateMainMenu();
            break;
        case MODE_PLAYER:
            if (!uiPlayer.update()) {
                currentMode = MODE_MAIN_MENU;
            }
            break;
        case MODE_VISUALIZER:
            updateVisualizerMode();
            break;
        case MODE_DIAGNOSTICS:
            if (!diagnostics.runMenu()) {
                currentMode = MODE_MAIN_MENU;
            }
            break;
        case MODE_SYSTEM_INFO:
            updateSystemInfoMode();
            break;
    }
    delay(10);
}
