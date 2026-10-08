#include "diagnostics.h"

static const char* testNames[] = {
    "1. OLED Test",
    "2. SD Card Detect",
    "3. SD Filesystem",
    "4. WAV Discovery",
    "5. Button Inputs",
    "6. Pin Matrix Map",
    "7. Battery ADC",
    "8. RGB LED Test",
    "9. I2S Audio Test",
    "10. INMP441 Mic Test"
};

Diagnostics::Diagnostics() : selectedIndex(0) {}

void Diagnostics::begin() {
    selectedIndex = 0;
}

void Diagnostics::renderMenu() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, "--- Q-TUNE DIAGNOSTICS ---");

    int startY = 24;
    int visibleItems = 4;
    int topIndex = selectedIndex;
    if (topIndex > TOTAL_TESTS - visibleItems) {
        topIndex = TOTAL_TESTS - visibleItems;
    }
    if (topIndex < 0) topIndex = 0;

    for (int i = 0; i < visibleItems && (topIndex + i) < TOTAL_TESTS; i++) {
        int idx = topIndex + i;
        int y = startY + (i * 10);
        if (idx == selectedIndex) {
            u8g2.drawStr(0, y, ">");
            u8g2.drawStr(10, y, testNames[idx]);
        } else {
            u8g2.drawStr(10, y, testNames[idx]);
        }
    }
    display.sendBuffer();
}

void Diagnostics::runMenu() {
    renderMenu();
    ButtonEvent evt = buttonManager.update();
    if (evt == BTN_EVENT_UP_PRESS) {
        selectedIndex = (selectedIndex - 1 + TOTAL_TESTS) % TOTAL_TESTS;
        renderMenu();
    } else if (evt == BTN_EVENT_DN_PRESS) {
        selectedIndex = (selectedIndex + 1) % TOTAL_TESTS;
        renderMenu();
    } else if (evt == BTN_EVENT_SEL_PRESS) {
        executeTest(selectedIndex);
        renderMenu();
    }
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
    }
}

void Diagnostics::testOLED() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawStr(10, 20, "OLED Display OK");
    u8g2.drawStr(10, 40, "Press SEL to exit");
    display.sendBuffer();
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        delay(10);
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
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        delay(10);
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
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        delay(10);
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
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        delay(10);
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
            delay(300);
            break;
        }

        if (digitalRead(BTN_OK) == LOW && selPressStart > 0) {
            if (millis() - selPressStart > 2000) break;
        } else {
            selPressStart = 0;
        }
        delay(10);
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
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        delay(10);
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
        delay(100);
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
            delay(100);
        }
    }
}

void Diagnostics::testI2SAudio() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 10, "I2S Audio Test:");
    u8g2.drawStr(0, 30, "Playing 1kHz Tone...");
    u8g2.drawStr(0, 55, "MAX98357A Output");
    display.sendBuffer();

    audioPlayer.playTestTone(1000, 1500);

    u8g2.drawStr(0, 42, "Tone Finished!");
    display.sendBuffer();
    while (buttonManager.update() != BTN_EVENT_SEL_PRESS) {
        delay(10);
    }
}

void Diagnostics::testINMP441Mic() {
    display.clear();
    U8G2 &u8g2 = display.getU8g2();
    u8g2.drawStr(0, 10, "Starting INMP441...");
    u8g2.drawStr(0, 30, "DIN:15  SCK:17  WS:18");
    u8g2.drawStr(0, 50, "Tie L/R to GND or 3V3");
    display.sendBuffer();
    delay(400);

    spectrumAnalyzer.start();

    VisualizerPreset p = PRESET_BAR_SPECTRUM;
    while (true) {
        spectrumAnalyzer.sampleMicrophone();
        spectrumAnalyzer.render(p);

        ButtonEvent evt = buttonManager.update();
        if (evt == BTN_EVENT_SEL_PRESS) break;
        if (evt == BTN_EVENT_DN_PRESS) {
            p = (VisualizerPreset)((p + 1) % 4);
        }
        delay(15);
    }

    spectrumAnalyzer.stop();
}

Diagnostics diagnostics;
