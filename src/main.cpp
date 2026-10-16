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
    "Music Player",
    "Spectrum Visualizer",
    "Wi-Fi Audio Stream",
    "RGB Light Effects",
    "Diagnostics",
    "System Info"
};

void renderMainMenu() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    String vals[MAIN_MENU_COUNT];
    vals[0] = audioPlayer.isPlaying() ? "[PLAY]" : (audioPlayer.isPaused() ? "[PAUS]" : "[IDLE]");
    vals[1] = String("[") + spectrumAnalyzer.getPresetShortName() + ":" + spectrumAnalyzer.getSensitivityShortName() + "]";
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
    bool scrolling = Display::isMenuScrolling();
    unsigned long refreshRate = scrolling ? 60 : 300;
    if (evt != BTN_EVENT_NONE || menuSelection != lastSel || menuScrollOffset != lastOff || (now - lastMainRender >= refreshRate)) {
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
const int RGB_MENU_COUNT = 10;
const char* rgbMenuLabels[] = {
    "Reactive Lights",
    "Mode",
    "Color",
    "Brightness",
    "Sensitivity",
    "Vocal Check Tool",
    "Button Lights",
    "Off on Finish",
    "Test Pulse",
    "Back to Menu"
};

const char* rgbMenuLabelsBand[] = {
    "Reactive Lights",
    "Mode",
    "Target Band",
    "Color",
    "Brightness",
    "Sensitivity",
    "Vocal Check Tool",
    "Button Lights",
    "Off on Finish",
    "Test Pulse",
    "Back to Menu"
};

void showBandChoosingInterface() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();

    LedTargetBand initialBand = ledManager.getTargetBand();
    int selBand = (int)initialBand;
    int scrollOffset = 0;
    const int BAND_COUNT = 6;

    struct BandInfo {
        const char* name;
        const char* freq;
    };
    static const BandInfo BANDS[6] = {
        { "Sub-Bass", "30-80 Hz" },
        { "Bass",     "80-250 Hz" },
        { "Low-Mid",  "250-600 Hz" },
        { "Midrange", "600-2 kHz" },
        { "High-Mid", "2-6 kHz" },
        { "Treble",   "6-16+ kHz" }
    };

    while (true) {
        if (audioPlayer.isPlaying()) {
            spectrumAnalyzer.sampleAudioStream();
        } else {
            spectrumAnalyzer.sampleMicrophone();
        }

        ledManager.setTargetBand((LedTargetBand)selBand);
        ledManager.loop();

        display.clear();
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(2, 7, "CHOOSE FREQ BAND");
        char batBuf[16];
        snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());
        int bw = u8g2.getStrWidth(batBuf);
        u8g2.drawStr(126 - bw, 7, batBuf);
        u8g2.drawLine(0, 9, 128, 9);

        if (selBand < scrollOffset) scrollOffset = selBand;
        if (selBand >= scrollOffset + 4) scrollOffset = selBand - 3;

        int y_pos = 20;
        const uint8_t* specBands = spectrumAnalyzer.getBands();

        for (int i = scrollOffset; i < scrollOffset + 4 && i < BAND_COUNT; i++) {
            float e = 0.0f;
            switch ((LedTargetBand)i) {
                case LedTargetBand::SUB_BASS: e = (float)specBands[0]; break;
                case LedTargetBand::BASS:     e = (specBands[1] * 1.2f + specBands[2]) * 0.5f; break;
                case LedTargetBand::LOW_MID:  e = (specBands[3] * 1.1f + specBands[4]) * 0.5f; break;
                case LedTargetBand::MIDRANGE: e = (specBands[5] + specBands[6] * 1.1f + specBands[7]) / 3.0f; break;
                case LedTargetBand::HIGH_MID: e = (specBands[8] + specBands[9] * 1.1f + specBands[10]) / 3.0f; break;
                case LedTargetBand::TREBLE:   e = (specBands[11] + specBands[12] + specBands[13] + specBands[14] + specBands[15]) / 5.0f; break;
            }

            int meterW = (int)constrain((e / 28.0f) * 28.0f, 0.0f, 28.0f);

            if (i == selBand) {
                u8g2.drawBox(2, y_pos - 8, 118, 10);
                u8g2.setDrawColor(0);
                u8g2.setFont(u8g2_font_5x7_tr);
                u8g2.drawStr(4, y_pos, BANDS[i].name);
                u8g2.drawStr(50, y_pos, BANDS[i].freq);
                u8g2.drawFrame(88, y_pos - 7, 30, 8);
                if (meterW > 0) {
                    int fill = (meterW > 28) ? 28 : meterW;
                    u8g2.drawBox(89, y_pos - 6, fill, 6);
                }
                u8g2.setDrawColor(1);
            } else {
                u8g2.setFont(u8g2_font_5x7_tr);
                u8g2.drawStr(4, y_pos, BANDS[i].name);
                u8g2.drawStr(50, y_pos, BANDS[i].freq);
                u8g2.drawFrame(88, y_pos - 7, 30, 8);
                if (meterW > 0) {
                    int fill = (meterW > 28) ? 28 : meterW;
                    u8g2.drawBox(89, y_pos - 6, fill, 6);
                }
            }
            y_pos += 11;
        }

        display.drawScrollBar(scrollOffset, BAND_COUNT);

        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(2, 63, "UP/DN:Select  OK:Save  CANCEL");
        display.sendBuffer();

        ButtonEvent evt = buttonManager.update();
        if (evt == BTN_EVENT_UP_PRESS) {
            if (selBand > 0) selBand--;
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            if (selBand < BAND_COUNT - 1) selBand++;
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_SEL_PRESS || evt == BTN_EVENT_OK_PRESS) {
            ledManager.setTargetBand((LedTargetBand)selBand);
            ledManager.triggerButtonPulse(CRGB::Green, 2, 60);
            break;
        } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
            ledManager.setTargetBand(initialBand);
            break;
        }

        unsigned long tPoll = millis();
        while (millis() - tPoll < 20) { yield(); }
    }
}

void updateRgbEffectsMode() {
    static unsigned long lastRgbRender = 0;
    static int lastSel = -1;
    ButtonEvent evt = buttonManager.update();
    unsigned long now = millis();
    bool scrolling = Display::isMenuScrolling();
    unsigned long refreshRate = scrolling ? 60 : 250;

    bool isBandMode = (ledManager.getUserMode() == LedMode::REACT_BAND_TARGET);
    int currentMenuCount = isBandMode ? 11 : 10;
    if (rgbMenuSelection >= currentMenuCount) {
        rgbMenuSelection = currentMenuCount - 1;
    }

    if (evt != BTN_EVENT_NONE || rgbMenuSelection != lastSel || (now - lastRgbRender >= refreshRate)) {
        lastRgbRender = now;
        lastSel = rgbMenuSelection;

        display.clear();
        char batBuf[16];
        snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

        char brBuf[12];
        snprintf(brBuf, sizeof(brBuf), "%d%%", (ledManager.getBrightness() * 100) / 255);

        String vals[11];
        if (isBandMode) {
            vals[0] = String("[") + ledManager.getEnabledName() + "]";
            vals[1] = String("[") + ledManager.getShortModeName() + "]";
            vals[2] = String("[") + ledManager.getTargetBandShortName() + "] >";
            vals[3] = String("[") + ledManager.getColorName() + "]";
            vals[4] = String("[") + brBuf + "]";
            vals[5] = String("[") + ledManager.getSensitivityName() + "]";
            vals[6] = "[TOOL]";
            vals[7] = String("[") + ledManager.getButtonFeedbackName() + "]";
            vals[8] = String("[") + ledManager.getTurnOffOnCompleteName() + "]";
            vals[9] = "[PULSE]";
            vals[10] = "[EXIT]";

            display.drawStandardMenu("RGB LIGHTS", rgbMenuLabelsBand, currentMenuCount, rgbMenuSelection, rgbMenuScrollOffset, vals, batBuf);
        } else {
            vals[0] = String("[") + ledManager.getEnabledName() + "]";
            vals[1] = String("[") + ledManager.getShortModeName() + "]";
            vals[2] = String("[") + ledManager.getColorName() + "]";
            vals[3] = String("[") + brBuf + "]";
            vals[4] = String("[") + ledManager.getSensitivityName() + "]";
            vals[5] = "[TOOL]";
            vals[6] = String("[") + ledManager.getButtonFeedbackName() + "]";
            vals[7] = String("[") + ledManager.getTurnOffOnCompleteName() + "]";
            vals[8] = "[PULSE]";
            vals[9] = "[EXIT]";

            display.drawStandardMenu("RGB LIGHTS", rgbMenuLabels, currentMenuCount, rgbMenuSelection, rgbMenuScrollOffset, vals, batBuf);
        }
        display.sendBuffer();
    }
    if (evt == BTN_EVENT_UP_PRESS) {
        Display::navigateMenu(rgbMenuSelection, rgbMenuScrollOffset, currentMenuCount, -1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_DN_PRESS) {
        Display::navigateMenu(rgbMenuSelection, rgbMenuScrollOffset, currentMenuCount, +1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        if (isBandMode) {
            switch (rgbMenuSelection) {
                case 0:
                    ledManager.toggleEnabled();
                    break;
                case 1:
                    ledManager.cycleMode();
                    break;
                case 2:
                    showBandChoosingInterface();
                    break;
                case 3:
                    ledManager.cycleColor();
                    break;
                case 4:
                    ledManager.cycleBrightness();
                    break;
                case 5:
                    ledManager.cycleSensitivity();
                    break;
                case 6:
                    diagnostics.testVocalReactiveCheck();
                    break;
                case 7:
                    ledManager.toggleButtonFeedback();
                    break;
                case 8:
                    ledManager.toggleTurnOffOnComplete();
                    break;
                case 9:
                    ledManager.triggerPulse(CRGB::White, 2, 70);
                    break;
                case 10:
                    ledManager.setMenuPreview(false);
                    currentMode = MODE_MAIN_MENU;
                    break;
            }
        } else {
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
                    diagnostics.testVocalReactiveCheck();
                    break;
                case 6:
                    ledManager.toggleButtonFeedback();
                    break;
                case 7:
                    ledManager.toggleTurnOffOnComplete();
                    break;
                case 8:
                    ledManager.triggerPulse(CRGB::White, 2, 70);
                    break;
                case 9:
                    ledManager.setMenuPreview(false);
                    currentMode = MODE_MAIN_MENU;
                    break;
            }
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

        const char* sdStatusStr = sdManager.isMounted() ? "MOUNTED (OK)" : (sdManager.isGPT() ? "64GB+ (GPT)" : (sdManager.isExFAT() ? "64GB+ (exFAT)" : (sdManager.isSafeToRemove() ? "EJECTED (SAFE)" : "UNMOUNTED")));
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
            } else if (sdManager.isExFAT() || sdManager.isGPT()) {
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

    // Global background track auto-advance: ensures continuous playback across visualizer, menus, and RGB effects
    if (currentMode != MODE_PLAYER && audioPlayer.hasFinished()) {
        audioPlayer.clearFinished();
        uiPlayer.playNextTrack();
    }

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
