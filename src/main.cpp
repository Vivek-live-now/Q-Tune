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
#include "anim_engine.h"
#include "ai_companion.h"
#include "gemini_client.h"

// Multi-core thread-safe SPI arbitration mutex (OLED vs microSD)
SemaphoreHandle_t spiBusMutex = NULL;

enum AppMode {
    MODE_MAIN_MENU,
    MODE_PLAYER,
    MODE_VISUALIZER,
    MODE_WIFI_STREAMER,
    MODE_RGB_EFFECTS,
    MODE_AI_COMPANION,
    MODE_SETTINGS,
    MODE_DIAGNOSTICS,
    MODE_SYSTEM_INFO
};

AppMode currentMode = MODE_MAIN_MENU;
int menuSelection = 0;
int menuScrollOffset = 0;
const int MAIN_MENU_COUNT = 8;
const char* menuLabels[] = {
    "Music Player",
    "Spectrum Visualizer",
    "Wi-Fi Audio Stream",
    "RGB Light Effects",
    "AI Companion",
    "Settings",
    "Diagnostics",
    "System Info"
};

int settingsMenuSelection = 0;
int settingsMenuScrollOffset = 0;
bool inWifiStatsView = false;
uint8_t wifiStatsPage = 0;

void renderMainMenu() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    String vals[MAIN_MENU_COUNT];
    vals[0] = audioPlayer.isPlaying() ? "[PLAY]" : (audioPlayer.isPaused() ? "[PAUS]" : "[IDLE]");
    vals[1] = String("[") + spectrumAnalyzer.getPresetShortName() + ":" + spectrumAnalyzer.getSensitivityShortName() + "]";
    vals[2] = wifiStreamer.isStreaming() ? "[STRM]" : (wifiStreamer.getWiFiState() == WIFI_STATE_CONNECTED ? "[CONN]" : "[WIFI]");
    vals[3] = ledManager.isEnabled() ? (String("[") + ledManager.getShortModeName() + "]") : "[OFF]";
    vals[4] = String("[") + (aiCompanion.getCompanion() == COMPANION_CAT ? "NEKO" : "MOCHI") + "]";
    vals[5] = wifiStreamer.isWiFiEnabled() ? "[ON]" : "[OFF]";
    vals[6] = "[11]";
    vals[7] = "[INFO]";

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
                currentMode = MODE_AI_COMPANION;
                extern unsigned long lastAiButtonTime;
                lastAiButtonTime = millis();
                if (!geminiClient.hasApiKey()) {
                    geminiClient.loadSDConfig();
                }
                break;
            case 5:
                currentMode = MODE_SETTINGS;
                settingsMenuSelection = 0;
                settingsMenuScrollOffset = 0;
                inWifiStatsView = false;
                break;
            case 6:
                currentMode = MODE_DIAGNOSTICS;
                diagnostics.begin();
                break;
            case 7:
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

int aiActionSelection = 0;
const int AI_ACTION_COUNT = 9;
const char* aiActionLabels[AI_ACTION_COUNT] = {
    "Ask DJ / Mic",
    "How's Weather?",
    "Cycle Face/Anim",
    "Anim Speed",
    "Visual Mode",
    "Song Recommend",
    "Analyze Audio",
    "Switch Pet",
    "Bass Boost / EQ"
};
String aiStatusBubble = "Ready to groove!";
unsigned long lastAiButtonTime = 0;

static String toastMessage = "";
static uint32_t toastEndTime = 0;
static bool toastListening = false;
static float toastAudioLevel = 0.0f;

void showToast(const String &msg, uint32_t duration_ms = 3000, bool isListening = false) {
    toastMessage = msg;
    toastEndTime = millis() + duration_ms;
    toastListening = isListening;
}

static void renderSpeechBubbleCard(const String &msg) {
    U8G2 &u8g2 = display.getU8g2();
    int boxX = 2;
    int boxY = 4;
    int boxW = 124;
    int boxH = 58;

    // Solid background with rounded frame
    u8g2.setDrawColor(0);
    u8g2.drawRBox(boxX - 1, boxY - 1, boxW + 2, boxH + 2, 3);
    u8g2.setDrawColor(1);
    u8g2.drawRFrame(boxX, boxY, boxW, boxH, 3);

    // Header banner with companion name
    u8g2.drawBox(boxX, boxY, boxW, 10);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tr);
    const char* cName = (aiCompanion.getCompanion() == COMPANION_CAT) ? "DJ NEKO [CAT]" : "MOCHI";
    u8g2.drawStr(boxX + 4, boxY + 8, cName);
    u8g2.setDrawColor(1);

    // Multi-line word wrapping with 4x6 font (fits 24 chars per line)
    u8g2.setFont(u8g2_font_4x6_tr);
    const int maxChars = 24;
    const int maxLines = 4;
    int lineY = boxY + 19;
    int lineCount = 0;

    int p = 0;
    int len = msg.length();
    while (p < len && lineCount < maxLines) {
        while (p < len && (msg[p] == ' ' || msg[p] == '\n' || msg[p] == '\r')) p++;
        if (p >= len) break;

        int lineEnd = p;
        int nextSpace = p;
        while (nextSpace < len && (nextSpace - p) <= maxChars) {
            if (msg[nextSpace] == ' ' || msg[nextSpace] == '\n' || msg[nextSpace] == '\r') {
                lineEnd = nextSpace;
            }
            nextSpace++;
        }
        if (nextSpace >= len && (nextSpace - p) <= maxChars) {
            lineEnd = nextSpace;
        } else if (lineEnd == p) {
            lineEnd = p + maxChars;
            if (lineEnd > len) lineEnd = len;
        }

        String line = msg.substring(p, lineEnd);
        line.trim();
        if (lineCount == maxLines - 1 && lineEnd < len) {
            if (line.length() > maxChars - 2) line = line.substring(0, maxChars - 2) + "..";
            else line += "..";
        }
        u8g2.drawStr(boxX + 4, lineY, line.c_str());
        lineY += 9;
        lineCount++;
        p = lineEnd;
    }

    // Bottom dismiss hint
    u8g2.drawHLine(boxX + 2, boxY + boxH - 9, boxW - 4);
    u8g2.drawStr(boxX + 26, boxY + boxH - 2, "[OK/CANCEL] Close");
}

void renderActiveToast() {
    if (millis() < toastEndTime || toastListening) {
        if (toastMessage.length() == 0) return;

        // Multi-line card for full AI responses (> 20 characters)
        if (!toastListening && toastMessage.length() > 20) {
            renderSpeechBubbleCard(toastMessage);
            return;
        }

        // Compact pill overlay (for listening or short status like "Speed: 1.25x")
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_5x7_tr);
        String msg = toastMessage;
        msg.replace('\n', ' ');
        msg.replace('\r', ' ');
        msg.trim();
        if (msg.length() > 22) msg = msg.substring(0, 20) + "..";

        int textW = u8g2.getStrWidth(msg.c_str());
        int boxW = textW + (toastListening ? 26 : 14);
        if (boxW < 52) boxW = 52;
        if (boxW > 126) boxW = 126;
        int boxX = (128 - boxW) / 2;
        int boxY = 48;
        int boxH = 15;

        // Solid background pill with rounded corners (erasing underlying elements)
        u8g2.setDrawColor(0);
        u8g2.drawRBox(boxX - 1, boxY - 1, boxW + 2, boxH + 2, 3);
        u8g2.setDrawColor(1);
        u8g2.drawRFrame(boxX, boxY, boxW, boxH, 3);

        if (toastListening) {
            // Blinking recording dot
            if ((millis() / 300) % 2 == 0) {
                u8g2.drawDisc(boxX + 6, boxY + 7, 2);
            } else {
                u8g2.drawCircle(boxX + 6, boxY + 7, 2);
            }

            u8g2.drawStr(boxX + 11, boxY + 11, msg.c_str());

            // Live mic audio bars
            int bars = (int)(toastAudioLevel * 5.0f);
            if (bars < 1) bars = 1;
            if (bars > 3) bars = 3;
            for (int b = 0; b < 3; b++) {
                int bh = (b < bars) ? (2 + b * 2) : 1;
                u8g2.drawVLine(boxX + boxW - 8 + b * 2, boxY + 11 - bh, bh);
            }
        } else {
            int strX = boxX + (boxW - textW) / 2;
            u8g2.drawStr(strX, boxY + 11, msg.c_str());
        }
    }
}

void startMicListeningSession(const char* promptHint = nullptr) {
    if (!geminiClient.hasApiKey()) {
        geminiClient.loadSDConfig();
    }
    if (!geminiClient.hasApiKey()) {
        showToast("Set key in /gemini.txt", 3000);
        aiCompanion.playAnim("surprised", 3000);
        return;
    }
    if (wifiStreamer.getWiFiState() != WIFI_STATE_CONNECTED) {
        showToast("Connect Wi-Fi first!", 3000);
        aiCompanion.playAnim("crying", 3000);
        return;
    }

    // 0. Audio prompt beep/chirp to announce listening!
    aiCompanion.playChirp();

    // 1. Immediately pop up live toast notification on screen!
    showToast("Listening (Mic)...", 8000, true);
    aiCompanion.setMood(MOOD_THINKING, 8000);
    ledManager.triggerButtonPulse(CRGB::Cyan, 3, 40);

    display.clear();
    aiCompanion.render(0, 0, 128, 64);
    renderActiveToast();
    display.sendBuffer();

    // 2. Pause audio task if playing so INMP441 can sample without I2S collision
    bool wasPlaying = audioPlayer.isPlaying();
    if (wasPlaying) {
        audioPlayer.pause();
    }

    float maxRms = 0.0f;
#ifdef ARDUINO
    spectrumAnalyzer.start();

    // 3. Sample INMP441 microphone with live audio-reactive toast!
    unsigned long listenStart = millis();
    while (millis() - listenStart < 2200) {
        spectrumAnalyzer.sampleMicrophone();
        toastAudioLevel = spectrumAnalyzer.getRMSLevel();
        if (toastAudioLevel > maxRms) maxRms = toastAudioLevel;

        display.clear();
        aiCompanion.render(0, 0, 128, 64);
        renderActiveToast();
        display.sendBuffer();

        ButtonEvent cancelEvt = buttonManager.update();
        if (cancelEvt == BTN_EVENT_CANCEL_PRESS) break;

        delay(30);
    }

    spectrumAnalyzer.stop();
#endif

    // 4. Update toast to Thinking...
    toastListening = false;
    showToast("Thinking...", 8000, false);
    display.clear();
    aiCompanion.render(0, 0, 128, 64);
    renderActiveToast();
    display.sendBuffer();

    // 5. Query Gemini with user's voice / intent
    String query;
    if (promptHint && strlen(promptHint) > 0) {
        query = promptHint;
    } else if (maxRms > 0.02f) {
        query = "User spoke into microphone: Answer their question, weather request, music control, or give friendly companion advice.";
    } else {
        query = "User activated microphone: Answer question, weather request, or music tip, and execute actions if requested.";
    }

    String outMsg, outEmote, outAct;
    bool ok = geminiClient.ask(query.c_str(), outMsg, outEmote, outAct);

    if (ok) {
        // Audio voice feedback on answer!
        if (aiCompanion.getCompanion() == COMPANION_CAT) {
            aiCompanion.playMeow();
        } else {
            aiCompanion.playChirp();
        }
        aiStatusBubble = outMsg;
        showToast(outMsg.length() > 0 ? outMsg : "Done!", 8000, false);
    } else {
        aiStatusBubble = outMsg.length() > 0 ? outMsg : "AI request failed";
        showToast(aiStatusBubble, 4500, false);
    }

    if (wasPlaying && !audioPlayer.isPlaying()) {
        if (!outAct.equalsIgnoreCase("PAUSE") && !outAct.equalsIgnoreCase("STOP")) {
            audioPlayer.resume();
        }
    }
}

void updateAICompanionMode() {
    static unsigned long lastAiRender = 0;
    ButtonEvent evt = buttonManager.update();
    unsigned long now = millis();

    // Check 3-second button inactivity for full-screen mode (Mochi & DJ Neko)
    bool isInactive = (now - lastAiButtonTime >= 3000);

    if (evt != BTN_EVENT_NONE) {
        // Dismiss active speech bubble card on any button press
        if (!toastListening && toastMessage.length() > 20 && millis() < toastEndTime) {
            toastEndTime = 0;
            return;
        }

        bool wasInactive = isInactive;
        lastAiButtonTime = now;

        // In full-screen mode, UP/DN immediately cycles all 65 anims & procedural faces!
        // SEL or SEL_HOLD starts live mic listening session with toast notification.
        // UP_HOLD toggles companion (DJ Neko ⇄ Mochi), and CANCEL returns to menu.
        if (wasInactive) {
            if (evt == BTN_EVENT_SEL_PRESS || evt == BTN_EVENT_SEL_HOLD) {
                startMicListeningSession();
                return;
            } else if (evt == BTN_EVENT_UP_PRESS) {
                aiCompanion.cyclePrevFace();
                showToast("Face: " + String(aiCompanion.getCurrentFaceDisplayName()), 1800);
                ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
                return;
            } else if (evt == BTN_EVENT_DN_PRESS) {
                aiCompanion.cycleNextFace();
                showToast("Face: " + String(aiCompanion.getCurrentFaceDisplayName()), 1800);
                ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
                return;
            } else if (evt == BTN_EVENT_UP_HOLD) {
                aiCompanion.toggleCompanion();
                showToast((aiCompanion.getCompanion() == COMPANION_CAT) ? "DJ Neko!" : "Mochi!", 2000);
                ledManager.triggerButtonPulse(CRGB::Purple, 2, 80);
                return;
            } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
                currentMode = MODE_MAIN_MENU;
                return;
            }
            // other holds or presses wake up the interactive menu
            return;
        }

        if (evt == BTN_EVENT_UP_PRESS) {
            aiActionSelection = (aiActionSelection == 0) ? (AI_ACTION_COUNT - 1) : (aiActionSelection - 1);
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            aiActionSelection = (aiActionSelection + 1) % AI_ACTION_COUNT;
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_UP_HOLD) {
            aiCompanion.toggleCompanion();
            showToast((aiCompanion.getCompanion() == COMPANION_CAT) ? "DJ Neko!" : "Mochi!", 2000);
            aiStatusBubble = (aiCompanion.getCompanion() == COMPANION_CAT) ? "Meow! DJ Neko here!" : "Mochi activated!";
            ledManager.triggerButtonPulse(CRGB::Purple, 2, 80);
        } else if (evt == BTN_EVENT_SEL_HOLD) {
            startMicListeningSession();
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            ledManager.triggerButtonPulse(CRGB::Green, 1, 50);
            switch (aiActionSelection) {
                case 0: { // Ask DJ / Mic
                    startMicListeningSession();
                    break;
                }
                case 1: { // How's Weather?
                    startMicListeningSession("What is the weather today? Give a friendly companion answer with current vibe.");
                    break;
                }
                case 2: { // Cycle Face / Anim (Browses all 65 .anim + 15 procedural faces)
                    aiCompanion.cycleNextFace();
                    showToast("Face: " + String(aiCompanion.getCurrentFaceDisplayName()), 1800);
                    aiStatusBubble = "Face: " + String(aiCompanion.getCurrentFaceDisplayName());
                    break;
                }
                case 3: { // Anim Speed (0.5x, 0.75x, 1.0x, 1.25x, 1.5x, 2.0x)
                    aiCompanion.cycleSpeed();
                    showToast("Speed: " + String(aiCompanion.getSpeedString()), 1800);
                    aiStatusBubble = "Speed: " + String(aiCompanion.getSpeedString());
                    break;
                }
                case 4: { // Visual Mode (Hybrid / Live Reactive / Anim Only)
                    aiCompanion.cycleVisualMode();
                    showToast("Mode: " + String(aiCompanion.getVisualModeName()), 1800);
                    aiStatusBubble = "Mode: " + String(aiCompanion.getVisualModeName());
                    break;
                }
                case 5: { // Song Recommend
                    startMicListeningSession("Recommend a track to play based on current vibe!");
                    break;
                }
                case 6: { // Analyze Audio
                    startMicListeningSession("Analyze current playback acoustics and tune the equalizer preset!");
                    break;
                }
                case 7: { // Switch Pet
                    aiCompanion.toggleCompanion();
                    showToast((aiCompanion.getCompanion() == COMPANION_CAT) ? "DJ Neko!" : "Mochi!", 2000);
                    aiStatusBubble = (aiCompanion.getCompanion() == COMPANION_CAT) ? "Switched to DJ Neko!" : "Switched to Mochi!";
                    break;
                }
                case 8: { // Bass Boost / EQ
                    aiCompanion.setMood(MOOD_BASS_BOOSTED, 6000);
                    geminiClient.executeAction("SET_EQ:BASS_BOOST");
                    showToast("Bass Boost engaged!", 2000);
                    aiStatusBubble = "Bass Boost engaged!";
                    break;
                }
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
            currentMode = MODE_MAIN_MENU;
        }
    }

    if (now - lastAiRender >= 25) {
        lastAiRender = now;
        display.clear();

        if (isInactive) {
            // Full-screen mode (128x64 centered, zero clutter) after 3s button inactivity
            aiCompanion.render(0, 0, 128, 64);
        } else {
            // Interactive UI mode with menu and status
            display.drawTopStatusBar(aiCompanion.getCompanionName(), battery.getPercentage());
            aiCompanion.render(0, 10, 60, 54);

            U8G2 &u8g2 = display.getU8g2();
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawFrame(61, 10, 66, 38);
            u8g2.setDrawColor(1);

            for (int i = 0; i < 3; i++) {
                int idx = (aiActionSelection + i) % AI_ACTION_COUNT;
                int yPos = 21 + i * 11;
                if (i == 0) {
                    u8g2.drawBox(63, yPos - 8, 62, 10);
                    u8g2.setDrawColor(0);
                    u8g2.drawStr(64, yPos, aiActionLabels[idx]);
                    u8g2.setDrawColor(1);
                } else {
                    u8g2.drawStr(64, yPos, aiActionLabels[idx]);
                }
            }

            u8g2.setFont(u8g2_font_4x6_tr);
            String dispBubble = aiStatusBubble;
            dispBubble.replace('\n', ' ');
            dispBubble.replace('\r', ' ');
            dispBubble.trim();
            u8g2.drawStr(2, 62, dispBubble.substring(0, 28).c_str());
        }

        renderActiveToast();
        display.sendBuffer();
    }
}

// ==============================================================================
// Outside Settings Menu & Wi-Fi Stats Mode
// ==============================================================================
const int SETTINGS_MENU_COUNT = 7;
const char* settingsMenuLabels[] = {
    "Wi-Fi Power",
    "Wi-Fi Stats",
    "Wi-Fi SoftAP",
    "Audio Output",
    "RGB Backlight",
    "AI Companion",
    "Back to Menu"
};

void renderWifiStats() {
    display.clear();
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "WIFI STATS (%d/2)", (int)(wifiStatsPage + 1));
    display.drawTopStatusBar(titleBuf, battery.getPercentage());
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setDrawColor(1);

    if (!wifiStreamer.isWiFiEnabled()) {
        u8g2.drawStr(6, 25, "Wi-Fi radio is OFF");
        u8g2.drawStr(6, 38, "Press SEL to power ON");
        u8g2.drawStr(6, 51, "Press CANCEL to return");
        u8g2.drawHLine(4, 56, 120);
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(4, 62, "[SEL] Power ON   [CANCEL] Back");
        display.sendBuffer();
        return;
    }

    if (wifiStatsPage == 0) {
        // Page 1: Live Connection & Network
        char buf[48];

        // Line 1: State + Signal Quality
        const char* stateStr = wifiStreamer.getWiFiStateName();
        snprintf(buf, sizeof(buf), "State: %s", stateStr);
        u8g2.drawStr(4, 20, buf);

        if (wifiStreamer.getWiFiState() == WIFI_STATE_CONNECTED) {
            int qual = wifiStreamer.getSignalQualityPercent();
            snprintf(buf, sizeof(buf), "%d%%", qual);
            u8g2.drawStr(96, 20, buf);
            // Draw mini signal meter
            int bars = (qual > 75) ? 4 : (qual > 50 ? 3 : (qual > 25 ? 2 : 1));
            for (int b = 0; b < 4; b++) {
                int barH = 2 + b * 2;
                if (b < bars) {
                    u8g2.drawBox(116 + b * 3, 20 - barH, 2, barH);
                } else {
                    u8g2.drawHLine(116 + b * 3, 20 - 1, 2);
                }
            }
        }

        // Line 2: SSID
        String ssid = (wifiStreamer.getWiFiState() == WIFI_STATE_AP_MODE) ? "Q-Tune-Audio" : wifiStreamer.getSSID();
        if (ssid.length() == 0) ssid = "Not Configured";
        if (ssid.length() > 15) ssid = ssid.substring(0, 14) + "..";
        snprintf(buf, sizeof(buf), "SSID:  %s", ssid.c_str());
        u8g2.drawStr(4, 30, buf);

        // Line 3: IP Address
        String ip = wifiStreamer.getIPAddress();
        snprintf(buf, sizeof(buf), "IP:    %s", ip.c_str());
        u8g2.drawStr(4, 40, buf);

        // Line 4: Signal dBm & Channel
        if (wifiStreamer.getWiFiState() == WIFI_STATE_CONNECTED) {
            snprintf(buf, sizeof(buf), "RSSI:  %d dBm (CH %d)", (int)wifiStreamer.getRSSI(), (int)wifiStreamer.getChannel());
        } else if (wifiStreamer.getWiFiState() == WIFI_STATE_AP_MODE) {
            snprintf(buf, sizeof(buf), "Mode:  SoftAP (CH 1)");
        } else {
            snprintf(buf, sizeof(buf), "Status: Connecting...");
        }
        u8g2.drawStr(4, 50, buf);

        // Bottom Help Row
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawHLine(0, 54, 128);
        u8g2.drawStr(2, 62, "[DN] Hardware Info   [SEL] Toggle");
    } else {
        // Page 2: Hardware & Routing
        char buf[48];

        // Line 1: MAC Address
        String mac = wifiStreamer.getMACAddress();
        snprintf(buf, sizeof(buf), "MAC:  %s", mac.c_str());
        u8g2.drawStr(4, 20, buf);

        // Line 2: Gateway
        String gw = wifiStreamer.getGatewayIP();
        snprintf(buf, sizeof(buf), "GW:   %s", gw.c_str());
        u8g2.drawStr(4, 30, buf);

        // Line 3: Subnet Mask
        String mask = wifiStreamer.getSubnetMask();
        snprintf(buf, sizeof(buf), "MASK: %s", mask.c_str());
        u8g2.drawStr(4, 40, buf);

        // Line 4: Hostname & Audio Stream Status
        const char* strm = wifiStreamer.isStreaming() ? "STREAMING" : "IDLE";
        snprintf(buf, sizeof(buf), "Host: %s [%s]", wifiStreamer.getMDNSHostname().c_str(), strm);
        u8g2.drawStr(4, 50, buf);

        // Bottom Help Row
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawHLine(0, 54, 128);
        u8g2.drawStr(2, 62, "[UP] Network Info    [SEL] Toggle");
    }

    display.sendBuffer();
}

void renderSettingsMenu() {
    display.clear();
    char batBuf[16];
    snprintf(batBuf, sizeof(batBuf), "%d%%", battery.getPercentage());

    String vals[SETTINGS_MENU_COUNT];
    vals[0] = wifiStreamer.isWiFiEnabled() ? "[ON]" : "[OFF]";
    vals[1] = wifiStreamer.isWiFiEnabled() ? (wifiStreamer.getWiFiState() == WIFI_STATE_CONNECTED ? "[CONN]" : "[INFO]") : "[OFF]";
    vals[2] = (wifiStreamer.getWiFiState() == WIFI_STATE_AP_MODE) ? "[ACTV]" : "[START]";
    vals[3] = String("[") + audioPlayer.getOutputModeShortName() + "]";
    vals[4] = ledManager.isEnabled() ? "[ON]" : "[OFF]";
    vals[5] = String("[") + (aiCompanion.getCompanion() == COMPANION_CAT ? "NEKO" : "MOCHI") + "]";
    vals[6] = "[EXIT]";

    display.drawStandardMenu("SETTINGS", settingsMenuLabels, SETTINGS_MENU_COUNT, settingsMenuSelection, settingsMenuScrollOffset, vals, batBuf);
    display.sendBuffer();
}

void updateSettingsMode() {
    static unsigned long lastSettingsRender = 0;
    static int lastSel = -1, lastOff = -1;
    static bool lastInStats = false;
    static uint8_t lastPage = 0;
    ButtonEvent evt = buttonManager.update();
    unsigned long now = millis();

    if (inWifiStatsView) {
        // --- Wi-Fi Stats View ---
        if (evt != BTN_EVENT_NONE || (now - lastSettingsRender >= 300) || lastInStats != inWifiStatsView || lastPage != wifiStatsPage) {
            lastSettingsRender = now;
            lastInStats = inWifiStatsView;
            lastPage = wifiStatsPage;
            renderWifiStats();
        }

        if (evt == BTN_EVENT_UP_PRESS) {
            wifiStatsPage = 0;
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            wifiStatsPage = 1;
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_SEL_PRESS) {
            wifiStreamer.toggleWiFi();
            ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
        } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
            inWifiStatsView = false;
            lastSel = -1;
            lastOff = -1;
            ledManager.triggerButtonPulse(CRGB::Orange, 1, 40);
        }
        return;
    }

    // --- Main Settings Menu ---
    bool scrolling = Display::isMenuScrolling();
    unsigned long refreshRate = scrolling ? 60 : 250;
    if (evt != BTN_EVENT_NONE || settingsMenuSelection != lastSel || settingsMenuScrollOffset != lastOff || (now - lastSettingsRender >= refreshRate)) {
        lastSettingsRender = now;
        lastSel = settingsMenuSelection;
        lastOff = settingsMenuScrollOffset;
        renderSettingsMenu();
    }

    if (evt == BTN_EVENT_UP_PRESS) {
        Display::navigateMenu(settingsMenuSelection, settingsMenuScrollOffset, SETTINGS_MENU_COUNT, -1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_DN_PRESS) {
        Display::navigateMenu(settingsMenuSelection, settingsMenuScrollOffset, SETTINGS_MENU_COUNT, +1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
        switch (settingsMenuSelection) {
            case 0: // Wi-Fi Power
                wifiStreamer.toggleWiFi();
                break;
            case 1: // Wi-Fi Stats
                inWifiStatsView = true;
                wifiStatsPage = 0;
                break;
            case 2: // Wi-Fi SoftAP
                wifiStreamer.startAPMode();
                break;
            case 3: { // Audio Output
                AudioOutputMode cur = audioPlayer.getOutputMode();
                audioPlayer.setOutputMode(cur == OUTPUT_MODE_SPEAKER_I2S ? OUTPUT_MODE_FIIO_USB_DAC : OUTPUT_MODE_SPEAKER_I2S);
                break;
            }
            case 4: // RGB Backlight
                ledManager.toggleEnabled();
                break;
            case 5: // AI Companion
                aiCompanion.toggleCompanion();
                break;
            case 6: // Back
                currentMode = MODE_MAIN_MENU;
                break;
        }
    } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        currentMode = MODE_MAIN_MENU;
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
    animEngine.begin();
    aiCompanion.begin();
    geminiClient.begin();
    Serial.println("[SYSTEM] Boot sequence complete. Entering main loop.\n");
}

void loop() {
    usbManager.loop();
    ledManager.loop();
    aiCompanion.update(0.033f);
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
        case MODE_AI_COMPANION:
            updateAICompanionMode();
            break;
        case MODE_SETTINGS:
            updateSettingsMode();
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
