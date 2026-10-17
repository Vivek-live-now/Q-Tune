#include "diagnostics.h"
#include "usb_manager.h"

static const char* testNames[] = {
    "1. OLED Test",
    "2. SD Card Detect",
    "3. SD Filesystem",
    "4. WAV Discovery",
    "5. Button Inputs",
    "6. Pin Matrix Map",
    "7. Battery ADC",
    "8. RGB LED Test",
    "9. Both Speaker Test",
    "10. INMP441 Mic Test",
    "11. FiiO KA11 USB DAC",
    "12. Vocal Reactive Check",
    "13. Return to Menu"
};

Diagnostics::Diagnostics() : selectedIndex(0), scrollOffset(0) {}

void Diagnostics::begin() {
    selectedIndex = 0;
    scrollOffset = 0;
}

void Diagnostics::renderMenu() {
    display.clear();
    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "%d/%d", selectedIndex + 1, TOTAL_TESTS);
    display.drawStandardMenu("DIAGNOSTICS", testNames, TOTAL_TESTS, selectedIndex, scrollOffset, (const String*)nullptr, countBuf);
    display.sendBuffer();
}

bool Diagnostics::runMenu() {
    renderMenu();
    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_UP_PRESS) {
        Display::navigateMenu(selectedIndex, scrollOffset, TOTAL_TESTS, -1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        renderMenu();
    } else if (evt == BTN_EVENT_DN_PRESS) {
        Display::navigateMenu(selectedIndex, scrollOffset, TOTAL_TESTS, +1);
        ledManager.triggerButtonPulse(CRGB::Blue, 1, 40);
        renderMenu();
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        if (selectedIndex == 12) {
            return false; // Return to Menu
        }
        executeTest(selectedIndex);
        renderMenu();
    } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
        return false; // Exit to Main Menu
    }
    return true;
}

void Diagnostics::executeTest(int index) {
    switch(index) {
        case 0: testOLED(); break;
        case 1: testSDDetect(); break;
        case 2: testSDFilesystem(); break;
        case 3: testWAVDiscovery(); break;
        case 4: testButtons(); break;
        case 5: testPinMap(); break;
        case 6: testBattery(); break;
        case 7: testRGBLED(); break;
        case 8: testI2SAudio(); break;
        case 9: testINMP441Mic(); break;
        case 10: testFiiOKA11USB(); break;
        case 11: testVocalReactiveCheck(); break;
    }
}

void Diagnostics::testOLED() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawStr(10, 20, "OLED Display OK");
    u8g2.drawStr(10, 40, "Press SEL to exit");
    display.sendBuffer();
    unsigned long lastPoll = millis();
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        while (millis() - lastPoll < 10) {
            yield();
        }
        lastPoll = millis();
    }
}

void Diagnostics::testSDDetect() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    bool mounted = sdManager.begin();
    u8g2.drawStr(0, 15, "SD Card Detect:");
    u8g2.drawStr(0, 35, mounted ? "STATUS: MOUNTED" : "STATUS: NOT FOUND");
    u8g2.drawStr(0, 55, "Press SEL to exit");
    display.sendBuffer();
    unsigned long lastPoll = millis();
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        while (millis() - lastPoll < 10) {
            yield();
        }
        lastPoll = millis();
    }
}

void Diagnostics::testSDFilesystem() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 15, "SD Filesystem:");
    if (!sdManager.isMounted()) sdManager.begin();
    if (sdManager.isMounted()) {
        uint64_t total = SD.totalBytes() / (1024 * 1024);
        uint64_t used = SD.usedBytes() / (1024 * 1024);
        char buf[32];
        snprintf(buf, sizeof(buf), "Cap: %lluMB", total);
        u8g2.drawStr(0, 30, buf);
        snprintf(buf, sizeof(buf), "Used: %lluMB", used);
        u8g2.drawStr(0, 45, buf);
    } else {
        u8g2.drawStr(0, 30, "SD Card Not Mounted");
    }
    u8g2.drawStr(0, 60, "Press SEL to exit");
    display.sendBuffer();
    unsigned long lastPoll = millis();
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        while (millis() - lastPoll < 10) {
            yield();
        }
        lastPoll = millis();
    }
}

void Diagnostics::testWAVDiscovery() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 10, "WAV Files in /music:");
    if (!sdManager.isMounted()) sdManager.begin();
    std::vector<String> files = sdManager.listMusicFiles();
    if (files.empty()) {
        u8g2.drawStr(0, 30, "No WAV files found!");
    } else {
        int y = 24;
        for (size_t i = 0; i < min((size_t)3, files.size()); i++) {
            u8g2.drawStr(0, y, files[i].c_str());
            y += 12;
        }
    }
    u8g2.drawStr(0, 60, "Press SEL to exit");
    display.sendBuffer();
    unsigned long lastPoll = millis();
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        while (millis() - lastPoll < 10) {
            yield();
        }
        lastPoll = millis();
    }
}

void Diagnostics::testButtons() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 10, "Test Buttons:");
    u8g2.drawStr(0, 25, "Press UP/DN/OK/CANCEL");
    u8g2.drawStr(0, 55, "Press CANCEL/Hold OK");
    display.sendBuffer();

    unsigned long selPressStart = 0;
    while (true) {
        ButtonEvent evt = buttonManager.update();
        if (evt == BTN_EVENT_UP_PRESS) {
            u8g2.drawStr(0, 40, "Last: UP Pressed    ");
            display.sendBuffer();
        } else if (evt == BTN_EVENT_DN_PRESS) {
            u8g2.drawStr(0, 40, "Last: DOWN Pressed  ");
            display.sendBuffer();
        } else if (evt == BTN_EVENT_OK_PRESS) {
            u8g2.drawStr(0, 40, "Last: OK/SEL Pressed");
            display.sendBuffer();
            selPressStart = millis();
        } else if (evt == BTN_EVENT_CANCEL_PRESS) {
            u8g2.drawStr(0, 40, "Last: CANCEL Pressed");
            display.sendBuffer();
            unsigned long t0 = millis();
            while (millis() - t0 < 300) { yield(); }
            break;
        }

        if (digitalRead(BTN_OK) == LOW && selPressStart > 0) {
            if (millis() - selPressStart > 2000) break;
        } else {
            selPressStart = 0;
        }
        unsigned long tPoll = millis();
        while (millis() - tPoll < 10) { yield(); }
    }
}

void Diagnostics::testPinMap() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "GPIO Pin Matrix:");
    u8g2.drawStr(0, 21, "MIC: DIN 15 (Freed I2C)");
    u8g2.drawStr(0, 31, "DAC: DOUT 10, B17, W18");
    u8g2.drawStr(0, 41, "SD: CS 8 | SPI 5,6,7");
    u8g2.drawStr(0, 51, "UART0: 43/44 (Freed)");
    u8g2.drawStr(0, 62, "SEL: Exit");
    display.sendBuffer();
    unsigned long lastPoll = millis();
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        while (millis() - lastPoll < 10) {
            yield();
        }
        lastPoll = millis();
    }
}

void Diagnostics::testBattery() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 10, "Battery ADC Test:");
    while (true) {
        float v = battery.getVoltage();
        int pct = battery.getPercentage();
        char buf[32];
        snprintf(buf, sizeof(buf), "Voltage: %.2f V", v);
        u8g2.drawStr(0, 30, buf);
        snprintf(buf, sizeof(buf), "Percent: %d %%", pct);
        u8g2.drawStr(0, 45, buf);
        u8g2.drawStr(0, 60, "Press SEL to exit");
        display.sendBuffer();

        if (buttonManager.update() == BTN_EVENT_SEL_PRESS) break;
        unsigned long lastBatPoll = millis();
        while (millis() - lastBatPoll < 100) {
            if (buttonManager.update() == BTN_EVENT_SEL_PRESS) break;
            yield();
        }
    }
}

void Diagnostics::testRGBLED() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 10, "WS2812 RGB LED Test");
    u8g2.drawStr(0, 30, "Cycling R-G-B");
    u8g2.drawStr(0, 55, "Press SEL to exit");
    display.sendBuffer();

    int colorIdx = 0;
    CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};
    while (true) {
        ledManager.setColor(colors[colorIdx]);
        colorIdx = (colorIdx + 1) % 4;
        for (int i = 0; i < 5; i++) {
            if (buttonManager.update() == BTN_EVENT_SEL_PRESS) {
                ledManager.off();
                return;
            }
            unsigned long tLed = millis();
            while (millis() - tLed < 100) {
                if (buttonManager.update() == BTN_EVENT_SEL_PRESS) {
                    ledManager.off();
                    return;
                }
                yield();
            }
        }
    }
}

void Diagnostics::testI2SAudio() {
    auto drawSpeakerScreen = [](const char* status, uint8_t activeChannel) {
        display.clear();
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "DUAL SPEAKER TEST");
        u8g2.drawHLine(0, 12, 128);

        // Left Speaker Box: x=6, y=17, w=54, h=18
        if (activeChannel == AudioPlayer::AUDIO_TEST_LEFT || activeChannel == AudioPlayer::AUDIO_TEST_BOTH) {
            u8g2.drawBox(6, 17, 54, 18);
            u8g2.setDrawColor(0);
            u8g2.drawStr(12, 30, "[L] SPKR");
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawFrame(6, 17, 54, 18);
            u8g2.drawStr(12, 30, "[L] SPKR");
        }

        // Right Speaker Box: x=68, y=17, w=54, h=18
        if (activeChannel == AudioPlayer::AUDIO_TEST_RIGHT || activeChannel == AudioPlayer::AUDIO_TEST_BOTH) {
            u8g2.drawBox(68, 17, 54, 18);
            u8g2.setDrawColor(0);
            u8g2.drawStr(74, 30, "[R] SPKR");
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawFrame(68, 17, 54, 18);
            u8g2.drawStr(74, 30, "[R] SPKR");
        }

        // Status text
        u8g2.drawStr(0, 48, status);

        // Navigation hints
        u8g2.drawStr(0, 61, "UP:L  DN:R  OK:Both");
        display.sendBuffer();
    };

    auto runToneTest = [&](const char* status, uint16_t freq, uint16_t durMs, uint8_t ch) {
        drawSpeakerScreen(status, ch);
        if (ch == AudioPlayer::AUDIO_TEST_LEFT) {
            ledManager.triggerButtonPulse(CRGB::Red, 1, 50);
        } else if (ch == AudioPlayer::AUDIO_TEST_RIGHT) {
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 50);
        } else {
            ledManager.triggerButtonPulse(CRGB::Green, 1, 50);
        }
        audioPlayer.playTestTone(freq, durMs, ch);
    };

    // Stage 1: Auto Cycle Sequence (Left -> Right -> Both)
    runToneTest("1/3: LEFT SPKR (800Hz)", 800, 1000, AudioPlayer::AUDIO_TEST_LEFT);
    unsigned long tGap = millis();
    while (millis() - tGap < 250) {
        if (buttonManager.update() == BTN_EVENT_CANCEL_PRESS) return;
        yield();
    }

    runToneTest("2/3: RIGHT SPKR (1.2k)", 1200, 1000, AudioPlayer::AUDIO_TEST_RIGHT);
    tGap = millis();
    while (millis() - tGap < 250) {
        if (buttonManager.update() == BTN_EVENT_CANCEL_PRESS) return;
        yield();
    }

    runToneTest("3/3: BOTH SPKRS (1.0k)", 1000, 1200, AudioPlayer::AUDIO_TEST_BOTH);
    tGap = millis();
    while (millis() - tGap < 200) {
        if (buttonManager.update() == BTN_EVENT_CANCEL_PRESS) return;
        yield();
    }

    // Stage 2: Interactive Manual Mode
    drawSpeakerScreen("Ready! Choose Speaker", 3); // 3 = idle (both outlined)

    while (true) {
        ButtonEvent evt = buttonManager.update();
        if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
            break;
        } else if (evt == BTN_EVENT_UP_PRESS) {
            runToneTest("Manual: LEFT (800Hz)", 800, 800, AudioPlayer::AUDIO_TEST_LEFT);
            drawSpeakerScreen("Ready! Choose Speaker", 3);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            runToneTest("Manual: RIGHT (1.2k)", 1200, 800, AudioPlayer::AUDIO_TEST_RIGHT);
            drawSpeakerScreen("Ready! Choose Speaker", 3);
        } else if (evt == BTN_EVENT_SEL_PRESS || evt == BTN_EVENT_OK_PRESS) {
            runToneTest("Manual: BOTH (1.0k)", 1000, 1000, AudioPlayer::AUDIO_TEST_BOTH);
            drawSpeakerScreen("Ready! Choose Speaker", 3);
        }

        unsigned long tPoll = millis();
        while (millis() - tPoll < 10) {
            yield();
        }
    }
}

void Diagnostics::testINMP441Mic() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 10, "Starting INMP441...");
    u8g2.drawStr(0, 30, "DIN:15  SCK:17  WS:18");
    u8g2.drawStr(0, 50, "Tie L/R to GND or 3V3");
    display.sendBuffer();
    unsigned long tMicStart = millis();
    while (millis() - tMicStart < 400) { yield(); }

    audioPlayer.stopAudioTask();
    audioPlayer.stop();

    spectrumAnalyzer.start();

    VisualizerPreset p = PRESET_BAR_SPECTRUM;
    while (true) {
        spectrumAnalyzer.sampleMicrophone();
        spectrumAnalyzer.render(p);

        ButtonEvent evt = buttonManager.update();
        if (evt == BTN_EVENT_SEL_PRESS || evt == BTN_EVENT_CANCEL_PRESS) break;
        if (evt == BTN_EVENT_DN_PRESS) {
            p = (VisualizerPreset)((p + 1) % PRESET_COUNT);
        }
        unsigned long tMicPoll = millis();
        while (millis() - tMicPoll < 15) { yield(); }
    }

    spectrumAnalyzer.stop();
    audioPlayer.begin();
}

void Diagnostics::testFiiOKA11USB() {
    bool inTest = true;
    while (inTest) {
        display.clear();
        U8G2 &u8g2 = display.getU8g2();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "FiiO KA11 USB DAC");
        u8g2.drawHLine(0, 12, 128);

        char stateBuf[32];
        snprintf(stateBuf, sizeof(stateBuf), "State: %s", usbManager.getStateString());
        u8g2.drawStr(0, 24, stateBuf);

        char outBuf[32];
        snprintf(outBuf, sizeof(outBuf), "Active Out: [%s]", audioPlayer.getOutputModeShortName());
        u8g2.drawStr(0, 36, outBuf);

        u8g2.drawStr(0, 48, "UP:Mount  DN:SafeEject");
        u8g2.drawStr(0, 60, "OK:Toggle CANCEL:Exit");
        display.sendBuffer();

        ButtonEvent evt = buttonManager.update();
        if (evt == BTN_EVENT_UP_PRESS) {
            usbManager.mount();
            ledManager.triggerButtonPulse(CRGB::Green, 1, 60);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            usbManager.safeEject();
            ledManager.triggerButtonPulse(CRGB::Red, 1, 60);
        } else if (evt == BTN_EVENT_SEL_PRESS || evt == BTN_EVENT_OK_PRESS) {
            if (audioPlayer.getOutputMode() == OUTPUT_MODE_SPEAKER_I2S) {
                if (!usbManager.isMounted()) {
                    usbManager.mount();
                }
                audioPlayer.setOutputMode(OUTPUT_MODE_FIIO_USB_DAC);
                ledManager.triggerButtonPulse(CRGB::Cyan, 1, 60);
            } else {
                audioPlayer.setOutputMode(OUTPUT_MODE_SPEAKER_I2S);
                ledManager.triggerButtonPulse(CRGB::Orange, 1, 60);
            }
        } else if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) {
            inTest = false;
        }
        unsigned long tUsbPoll = millis();
        while (millis() - tUsbPoll < 20) { yield(); }
    }
}

Diagnostics diagnostics;

void Diagnostics::testVocalReactiveCheck() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();

    bool prevEnabled = ledManager.isEnabled();
    LedMode prevMode = ledManager.getUserMode();
    ledManager.setEnabled(true);
    ledManager.setMode(LedMode::REACT_VOCAL_LIGHTNING);

    bool wasPlaying = audioPlayer.isPlaying();
    String savedTrack = audioPlayer.getCurrentTrackPath();
    bool useMicrophone = !wasPlaying;
    if (useMicrophone) {
        audioPlayer.stopAudioTask();
        audioPlayer.stop();
        spectrumAnalyzer.start();
    }

    while (true) {
        if (useMicrophone) {
            spectrumAnalyzer.sampleMicrophone();
        } else {
            spectrumAnalyzer.sampleAudioStream();
        }

        ledManager.loop();

        float pitch = spectrumAnalyzer.getVoicePitch();
        float conf = spectrumAnalyzer.getVoiceConfidence();
        const uint8_t* bands = spectrumAnalyzer.getBands();
        float vocalCore = (bands[1] * 0.9f + bands[2] * 1.5f + bands[3] * 1.6f + bands[4] * 1.4f + bands[5] * 0.9f);
        float highSizzle = (bands[7] * 0.8f + bands[8] * 1.0f + bands[9] * 1.0f + bands[10] * 0.7f);
        float flank = (bands[0] * 1.4f + highSizzle * 0.9f) + 1.5f;
        float vocalDominance = vocalCore / flank;

        float pitchGate = constrain((conf - 0.22f) / 0.24f, 0.0f, 1.0f);
        float formantGate = constrain((vocalDominance - 0.40f) / 0.38f, 0.0f, 1.0f);
        float gateScore = pitchGate * (0.35f + 0.65f * formantGate);
        bool gateOpen = gateScore > 0.08f;

        display.clear();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "VOCAL REACTIVE CHECK");
        const char* srcBadge = useMicrophone ? "[MIC]" : "[FILE]";
        int bw = u8g2.getStrWidth(srcBadge);
        u8g2.drawStr(128 - bw, 10, srcBadge);
        u8g2.drawHLine(0, 12, 128);

        char buf[32];
        if (pitch > 60.0f) {
            snprintf(buf, sizeof(buf), "Pitch: %.0f Hz", pitch);
        } else {
            snprintf(buf, sizeof(buf), "Pitch: --- Hz (SILENT)");
        }
        u8g2.drawStr(0, 23, buf);

        snprintf(buf, sizeof(buf), "Conf:  %d%%", (int)(conf * 100.0f));
        u8g2.drawStr(0, 33, buf);
        int barW = (int)(conf * 45.0f);
        u8g2.drawFrame(75, 26, 48, 7);
        if (barW > 0) u8g2.drawBox(76, 27, barW, 5);

        int vocalPct = (int)constrain((vocalCore / 35.0f) * 100.0f, 0.0f, 100.0f);
        snprintf(buf, sizeof(buf), "Form:  %d%%", vocalPct);
        u8g2.drawStr(0, 43, buf);
        int formW = (int)((vocalPct / 100.0f) * 45.0f);
        u8g2.drawFrame(75, 36, 48, 7);
        if (formW > 0) u8g2.drawBox(76, 37, formW, 5);

        snprintf(buf, sizeof(buf), "Gate: %s | %s", gateOpen ? "OPEN " : "CLOSED", ledManager.getSensitivityName());
        u8g2.drawStr(0, 53, buf);

        snprintf(buf, sizeof(buf), "UP/DN:Sens OK:%s CANCEL", useMicrophone ? "FILE" : "MIC");
        u8g2.drawStr(0, 63, buf);
        display.sendBuffer();

        ButtonEvent evt = buttonManager.update();
        if (evt == BTN_EVENT_CANCEL_PRESS || evt == BTN_EVENT_CANCEL_HOLD) break;
        if (evt == BTN_EVENT_UP_PRESS) {
            ledManager.cycleSensitivity();
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_DN_PRESS) {
            ledManager.cycleSensitivity();
            ledManager.triggerButtonPulse(CRGB::Blue, 1, 30);
        } else if (evt == BTN_EVENT_SEL_PRESS || evt == BTN_EVENT_OK_PRESS) {
            if (useMicrophone) {
                spectrumAnalyzer.stop();
                audioPlayer.begin();
                useMicrophone = false;
                if (savedTrack.length() > 0) {
                    audioPlayer.playFile(savedTrack);
                } else {
                    std::vector<String> files = sdManager.listMusicFiles();
                    if (!files.empty()) {
                        savedTrack = files[0];
                        audioPlayer.playFile(savedTrack);
                    }
                }
                ledManager.triggerButtonPulse(CRGB::Green, 1, 50);
            } else {
                if (audioPlayer.isPlaying()) {
                    savedTrack = audioPlayer.getCurrentTrackPath();
                }
                audioPlayer.stopAudioTask();
                audioPlayer.stop();
                spectrumAnalyzer.start();
                useMicrophone = true;
                ledManager.triggerButtonPulse(CRGB::Cyan, 1, 50);
            }
        }
        unsigned long tVocPoll = millis();
        while (millis() - tVocPoll < 20) { yield(); }
    }

    if (useMicrophone) {
        spectrumAnalyzer.stop();
        audioPlayer.begin();
    }
    if (wasPlaying && savedTrack.length() > 0 && !audioPlayer.isPlaying()) {
        audioPlayer.playFile(savedTrack);
    }
    ledManager.setUserMode(prevMode);
    ledManager.setEnabled(prevEnabled);
}
