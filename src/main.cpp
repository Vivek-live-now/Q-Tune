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
#include "usb_manager.h"
#include "wifi_streamer.h"

// Multi-core thread-safe SPI arbitration mutex (OLED vs microSD)
SemaphoreHandle_t spiBusMutex = NULL;

enum AppMode {
    MODE_MAIN_MENU,
    MODE_PLAYER,
    MODE_VISUALIZER,
    MODE_WIFI_STREAMER,
    MODE_RGB_EFFECTS,
    MODE_DIAGNOSTICS,
    MODE_SYSTEM_INFO
};

AppMode currentMode = MODE_MAIN_MENU;
int menuSelection = 0;
int menuScrollOffset = 0;
const int MAIN_MENU_COUNT = 6;
const char* menuLabels[] = {
    "1. Music Player",
    "2. Spectrum Visualizer",
    "3. Wi-Fi Audio Stream",
    "4. RGB Light Effects",
    "5. Diagnostics",
    "6. System Info"
};

void renderMainMenu() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    String vals[MAIN_MENU_COUNT];
    vals[0] = audioPlayer.isPlaying() ? "[PLAY]" : (audioPlayer.isPaused() ? "[PAUS]" : "[IDLE]");
    vals[1] = String("[") + spectrumAnalyzer.getPresetName() + ":" + spectrumAnalyzer.getSensitivityShortName() + "]";
    vals[2] = wifiStreamer.isStreaming() ? "[STRM]" : (wifiStreamer.getWiFiState() == WIFI_STATE_CONNECTED ? "[CONN]" : "[WIFI]");
    vals[3] = ledManager.isEnabled() ? (String("[") + ledManager.getShortModeName() + "]") : "[OFF]";
    vals[4] = "[11]";
    vals[5] = "[INFO]";

    display.drawStandardMenu("Q-TUNES", menuLabels, MAIN_MENU_COUNT, menuSelection, menuScrollOffset, vals, batBuf);
    display.sendBuffer();
}

void updateMainMenu() {
    static unsigned long lastMainRender = 0;
    static int lastSel = -1, lastOff = -1;
    ButtonEvent evt = buttonManager.update();
    unsigned long now = millis();
    if (evt != BTN_EVENT_NONE || menuSelection != lastSel || menuScrollOffset != lastOff || (now - lastMainRender >= 300)) {
        lastMainRender = now;
        lastSel = menuSelection;
        lastOff = menuScrollOffset;
        renderMainMenu();
    }
    if (evt == BTN_EVENT_UP_PRESS) {
        Display::navigateMenu(menuSelection, menuScrollOffset, MAIN_MENU_COUNT, -1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_DN_PRESS) {
        Display::navigateMenu(menuSelection, menuScrollOffset, MAIN_MENU_COUNT, +1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
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
                currentMode = MODE_WIFI_STREAMER;
                wifiStreamer.start();
                break;
            case 3:
                currentMode = MODE_RGB_EFFECTS;
                ledManager.setMenuPreview(true);
                break;
            case 4:
                currentMode = MODE_DIAGNOSTICS;
                diagnostics.begin();
                break;
            case 5:
                currentMode = MODE_SYSTEM_INFO;
                break;
        }
    } else if (evt == BTN_EVENT_CANCEL_HOLD) {
        ledManager.triggerPulse(CRGB::Red, 2, 100);
        powerManager.safeShutdown("USER POWER OFF");
    }
}

int rgbMenuSelection = 0;
int rgbMenuScrollOffset = 0;
const int RGB_MENU_COUNT = 9;
const char* rgbMenuLabels[] = {
    "Reactive Lights",
    "Mode",
    "Color",
    "Brightness",
    "Sensitivity",
    "Button Lights",
    "Off on Finish",
    "Test Pulse",
    "Back to Menu"
};

void updateRgbEffectsMode() {
    static unsigned long lastRgbRender = 0;
    static int lastSel = -1;
    ButtonEvent evt = buttonManager.update();
    unsigned long now = millis();
    if (evt != BTN_EVENT_NONE || rgbMenuSelection != lastSel || (now - lastRgbRender >= 250)) {
        lastRgbRender = now;
        lastSel = rgbMenuSelection;

        display.clear();
        char batBuf[16];
        snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

        char brBuf[12];
        snprintf(brBuf, sizeof(brBuf), "%d%%", (ledManager.getBrightness() * 100) / 255);

        String vals[RGB_MENU_COUNT];
        vals[0] = String("[") + ledManager.getEnabledName() + "]";
        vals[1] = String("[") + ledManager.getModeName() + "]";
        vals[2] = String("[") + ledManager.getColorName() + "]";
        vals[3] = String("[") + brBuf + "]";
        vals[4] = String("[") + ledManager.getSensitivityName() + "]";
        vals[5] = String("[") + ledManager.getButtonFeedbackName() + "]";
        vals[6] = String("[") + ledManager.getTurnOffOnCompleteName() + "]";
        vals[7] = "[PULSE]";
        vals[8] = "[EXIT]";

        display.drawStandardMenu("RGB LIGHTS", rgbMenuLabels, RGB_MENU_COUNT, rgbMenuSelection, rgbMenuScrollOffset, vals, batBuf);
        display.sendBuffer();
    }
    if (evt == BTN_EVENT_UP_PRESS) {
        Display::navigateMenu(rgbMenuSelection, rgbMenuScrollOffset, RGB_MENU_COUNT, -1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_DN_PRESS) {
        Display::navigateMenu(rgbMenuSelection, rgbMenuScrollOffset, RGB_MENU_COUNT, +1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        switch (rgbMenuSelection) {
            case 0:
                ledManager.toggleEnabled();
                break;
            case 1:
                ledManager.cycleMode();
                break;
            case 2:
                ledManager.cycleColor();
                break;
            case 3:
                ledManager.cycleBrightness();
                break;
            case 4:
                ledManager.cycleSensitivity();
                break;
            case 5:
                ledManager.toggleButtonFeedback();
                break;
            case 6:
                ledManager.toggleTurnOffOnComplete();
                break;
            case 7:
                ledManager.triggerPulse(CRGB::White, 2, 70);
                break;
            case 8:
                ledManager.setMenuPreview(false);
                currentMode = MODE_MAIN_MENU;
                break;
        }
    } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        ledManager.setMenuPreview(false);
        currentMode = MODE_MAIN_MENU;
    }
}

void updateVisualizerMode() {
    static unsigned long lastVisRender = 0;
    unsigned long now = millis();
    if (now - lastVisRender >= 33) {
        lastVisRender = now;
        spectrumAnalyzer.sampleAudioStream();
        spectrumAnalyzer.render();
    }

    ButtonEvent evt = buttonManager.update();
    if (evt != BTN_EVENT_NONE) {
        spectrumAnalyzer.wakeTopBar(5000);
    }

    if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        spectrumAnalyzer.stop();
        currentMode = MODE_MAIN_MENU;
    } else if (evt == BTN_EVENT_DN_PRESS) {
        spectrumAnalyzer.nextPreset();
        ledManager.triggerButtonPulse(CRGB::Magenta, 1, 80);
    } else if (evt == BTN_EVENT_UP_PRESS) {
        spectrumAnalyzer.previousPreset();
        ledManager.triggerButtonPulse(CRGB::Magenta, 1, 80);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        if (audioPlayer.isPlaying()) {
            audioPlayer.pause();
            ledManager.onPlaybackPause();
        } else if (audioPlayer.isPaused()) {
            audioPlayer.resume();
            ledManager.onPlaybackResume();
        } else {
            uiPlayer.playNextTrack();
            ledManager.onPlaybackStart();
        }
    } else if (evt == BTN_EVENT_SEL_HOLD) {
        spectrumAnalyzer.cycleSensitivity();
        ledManager.triggerButtonPulse(CRGB::Cyan, 1, 60);
    } else if (evt == BTN_EVENT_UP_HOLD) {
        audioPlayer.volumeUp(5);
        ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
    } else if (evt == BTN_EVENT_DN_HOLD) {
        audioPlayer.volumeDown(5);
        ledManager.triggerButtonPulse(CRGB::Red, 1, 60);
    }
}

void updateSystemInfoMode() {
    static unsigned long lastSysRender = 0;
    ButtonEvent evt = buttonManager.update();
    unsigned long now = millis();
    if (evt != BTN_EVENT_NONE || (now - lastSysRender >= 500)) {
        lastSysRender = now;
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

        const char* sdStatusStr = sdManager.isMounted() ? "MOUNTED (OK)" : (sdManager.isExFAT() ? "64GB+ (exFAT)" : (sdManager.isSafeToRemove() ? "EJECTED (SAFE)" : "UNMOUNTED"));
        snprintf(buf, sizeof(buf), "SD: %s", sdStatusStr);
        u8g2.drawStr(4, 59, buf);

        display.sendBuffer();
    }

    if (evt == BTN_EVENT_SEL_PRESS) {
        if (sdManager.isMounted()) {
            sdManager.unmount();
            ledManager.triggerButtonPulse(CRGB::Orange, 2, 80);
        } else {
            sdManager.remount();
            if (sdManager.isMounted()) {
                ledManager.triggerButtonPulse(CRGB::Green, 2, 80);
            } else if (sdManager.isExFAT()) {
                ledManager.triggerButtonPulse(CRGB::Orange, 2, 80);
            } else {
                ledManager.triggerButtonPulse(CRGB::Red, 2, 80);
            }
        }
    } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        currentMode = MODE_MAIN_MENU;
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n========================================");
    Serial.println("         Q-TUNE HI-FI AUDIO SYSTEM       ");
    Serial.println("========================================");

    // Initialize FreeRTOS shared SPI mutex before any subsystem begins
    spiBusMutex = xSemaphoreCreateMutex();

    powerManager.begin();
    display.begin();
    buttonManager.begin();
    battery.begin();
    ledManager.begin();
    sdManager.begin();
    Serial.printf("[SD] Mounted: %s | Card: %s | FS: %s | %u MB\n",
                  sdManager.isMounted() ? "YES" : "NO",
                  sdManager.getCardTypeName(),
                  sdManager.getFilesystemName(),
                  (unsigned int)sdManager.getCardCapacityMB());
    audioPlayer.begin();
    spectrumAnalyzer.begin();

    diagnostics.begin();
    uiPlayer.begin();
    usbManager.begin();
    wifiStreamer.begin();
    Serial.println("[SYSTEM] Boot sequence complete. Entering main loop.\n");
}

void loop() {
    usbManager.loop();
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
        case MODE_WIFI_STREAMER:
            if (!wifiStreamer.updateUI()) {
                wifiStreamer.stop();
                currentMode = MODE_MAIN_MENU;
            }
            break;
        case MODE_RGB_EFFECTS:
            updateRgbEffectsMode();
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
    delay(2);
}
