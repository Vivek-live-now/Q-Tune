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
const int MAIN_MENU_COUNT = 4;
const char* menuLabels[] = {
    "1. Music Player",
    "2. Spectrum Visualizer",
    "3. Diagnostics",
    "4. System Info"
};

void renderMainMenu() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "--- 007 Q-TUNE ---");

    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());
    u8g2.drawStr(104, 10, batBuf);
    u8g2.drawHLine(0, 12, 128);

    for (int i = 0; i < MAIN_MENU_COUNT; i++) {
        int y = 24 + (i * 10);
        if (i == menuSelection) {
            u8g2.drawStr(0, y, ">");
            u8g2.drawStr(10, y, menuLabels[i]);
        } else {
            u8g2.drawStr(10, y, menuLabels[i]);
        }
    }
    display.sendBuffer();
}

void updateMainMenu() {
    renderMainMenu();
    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_UP_PRESS) {
        menuSelection = (menuSelection - 1 + MAIN_MENU_COUNT) % MAIN_MENU_COUNT;
    } else if (evt == BTN_EVENT_DN_PRESS) {
        menuSelection = (menuSelection + 1) % MAIN_MENU_COUNT;
    } else if (evt == BTN_EVENT_SEL_PRESS) {
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
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "--- SYSTEM INFO ---");
    u8g2.drawHLine(0, 12, 128);

    char buf[32];
    snprintf(buf, sizeof(buf), "VBAT: %.2fV (%d%%)", battery.getVoltage(), battery.getPercentage());
    u8g2.drawStr(0, 24, buf);

    snprintf(buf, sizeof(buf), "CPU:  %d MHz", (int)powerManager.getTargetCpuFreqMhz());
    u8g2.drawStr(0, 35, buf);

    snprintf(buf, sizeof(buf), "VOL:  %d%%", audioPlayer.getVolume());
    u8g2.drawStr(0, 46, buf);

    snprintf(buf, sizeof(buf), "MODE: %s | SD: %s",
             uiPlayer.getPlaybackModeString(),
             sdManager.isMounted() ? "OK" : "NO");
    u8g2.drawStr(0, 57, buf);

    display.sendBuffer();

    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_SEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
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
