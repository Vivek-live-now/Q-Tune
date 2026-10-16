#include "display.h"
#include "simd_accel.h"
#include "gdma_manager.h"

Display::Display() : u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST), gdma_enabled(true) {}

bool Display::begin() {
    pinMode(OLED_CS, OUTPUT);
    digitalWrite(OLED_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, OLED_CS);
    u8g2.begin();
    u8g2.setBusClock(8000000UL);
    gdmaManager.begin(SPI_SCK, SPI_MOSI, OLED_CS, OLED_DC, OLED_RST, 8000000UL);
    clear();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Q-Tune Init...");
    sendBuffer();
    return true;
}

U8G2 &Display::getU8g2() {
    return u8g2;
}

void Display::clear() {
    clearBufferFast();
}

void Display::clearBufferFast() {
    uint8_t *buf = u8g2.getBufferPtr();
    if (buf) {
        simd_clear_128(buf, 1024);
    } else {
        u8g2.clearBuffer();
    }
}

void Display::invertBuffer() {
    uint8_t *buf = u8g2.getBufferPtr();
    if (buf) {
        simd_invert_128(buf, buf, 1024);
    }
}

void Display::flushAsync() {
    if (gdma_enabled && gdmaManager.isInitialized()) {
        gdmaManager.flushAsync(u8g2.getBufferPtr());
        gdmaManager.waitIdle();
    }
    sendBuffer();
}

const GdmaTransferStats& Display::getGdmaStats() const {
    return gdmaManager.getStats();
}

void Display::sendBuffer() {
    if (spiBusMutex != NULL) {
        xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    }
    digitalWrite(SD_CS, HIGH);
    u8g2.sendBuffer();
    digitalWrite(OLED_CS, HIGH);
    if (spiBusMutex != NULL) {
        xSemaphoreGive(spiBusMutex);
    }
}

// ----------------------------------------------------------------------------
// Q-Watch Signature Menu System Implementation
// ----------------------------------------------------------------------------

void Display::drawScrollBar(int offset, int item_count) {
    if (item_count > 4) {
        int scroll_h = 46;
        int scroll_y = 12 + ((float)offset / (item_count - 4)) * (scroll_h - 10);
        u8g2.drawFrame(123, 12, 3, scroll_h);
        u8g2.drawBox(123, scroll_y, 3, 10);
    }
}

static String s_lastMenuTitle = "";
static int s_lastMenuSelection = -1;
static String s_lastSelectedItemText = "";
static unsigned long s_selectionStartTime = 0;
static bool s_menuScrollingActive = false;

bool Display::isMenuScrolling() {
    return s_menuScrollingActive;
}

void Display::resetMenuScrolling() {
    s_lastMenuSelection = -1;
    s_lastSelectedItemText = "";
    s_selectionStartTime = millis();
    s_menuScrollingActive = false;
}

void Display::drawScrollingText(U8G2 &u8g2, const char* text, int x, int y, int maxW, unsigned long activeStartMs, bool isSelected) {
    if (!text || text[0] == '\0') return;

    int fullW = u8g2.getStrWidth(text);
    if (fullW <= maxW) {
        u8g2.drawStr(x, y, text);
        return;
    }

    unsigned long now = millis();
    unsigned long elapsed = (now >= activeStartMs) ? (now - activeStartMs) : 0;

    // First 3 seconds or unselected: show static starting text truncated to maxW
    if (elapsed < 3000 || !isSelected) {
        String label = text;
        while (label.length() > 0 && (u8g2.getStrWidth(label.c_str()) + 4) > maxW) {
            label.remove(label.length() - 1);
        }
        if (label.length() > 0) label += ".";
        u8g2.drawStr(x, y, label.c_str());
        return;
    }

    // Active scrolling after 3 seconds on pointer/item
    int strLen = strlen(text);
    unsigned long scrollElapsed = elapsed - 3000;

    int charIndices[128];
    int totalChars = 0;
    for (int bi = 0; bi < strLen && totalChars < 127; ) {
        charIndices[totalChars++] = bi;
        uint8_t c = (uint8_t)text[bi];
        int cpLen = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        bi += cpLen;
    }
    charIndices[totalChars] = strLen;

    int maxStartChar = 0;
    for (int i = 0; i < totalChars; i++) {
        const char* sub = text + charIndices[i];
        if (u8g2.getStrWidth(sub) > maxW) {
            maxStartChar = i + 1;
        } else {
            break;
        }
    }
    if (maxStartChar <= 0) {
        u8g2.drawStr(x, y, text);
        return;
    }

    const unsigned long STEP_MS = 180;
    const unsigned long PAUSE_MS = 1500;
    unsigned long scrollDuration = (unsigned long)maxStartChar * STEP_MS;
    unsigned long cycleDuration = scrollDuration + PAUSE_MS + PAUSE_MS;
    unsigned long cycleTime = scrollElapsed % cycleDuration;

    int curStartChar = 0;
    if (cycleTime < scrollDuration) {
        curStartChar = (int)(cycleTime / STEP_MS);
    } else if (cycleTime < scrollDuration + PAUSE_MS) {
        curStartChar = maxStartChar;
    } else {
        curStartChar = 0;
    }

    if (curStartChar > maxStartChar) curStartChar = maxStartChar;
    int startByte = charIndices[curStartChar];
    const char* visibleSub = text + startByte;

    String visibleStr = visibleSub;
    while (visibleStr.length() > 0 && u8g2.getStrWidth(visibleStr.c_str()) > maxW) {
        visibleStr.remove(visibleStr.length() - 1);
    }

    u8g2.drawStr(x, y, visibleStr.c_str());
}

void Display::drawStandardMenu(const char* title, const char** items, int item_count, int selection, int offset, const String* values, const char* headerRight) {
    u8g2.setFont(u8g2_font_5x7_tr);
    int hw = (headerRight && headerRight[0] != '\0') ? u8g2.getStrWidth(headerRight) : 0;
    int maxTitleW = (hw > 0) ? (124 - hw) : 124;
    String cleanTitle = title ? title : "";
    if (u8g2.getStrWidth(cleanTitle.c_str()) > maxTitleW) {
        while (cleanTitle.length() > 0 && (u8g2.getStrWidth(cleanTitle.c_str()) + 4) > maxTitleW) {
            cleanTitle.remove(cleanTitle.length() - 1);
        }
        if (cleanTitle.length() > 0) cleanTitle += ".";
    }
    u8g2.drawStr(2, 7, cleanTitle.c_str());
    if (hw > 0) {
        u8g2.drawStr(126 - hw, 7, headerRight);
    }
    u8g2.drawLine(0, 9, 128, 9);
    u8g2.setFont(u8g2_font_6x10_tr);

    const char* curSelItem = (items && selection >= 0 && selection < item_count && items[selection]) ? items[selection] : "";
    String curTitle = title ? title : "";
    if (curTitle != s_lastMenuTitle || selection != s_lastMenuSelection || s_lastSelectedItemText != curSelItem) {
        s_lastMenuTitle = curTitle;
        s_lastMenuSelection = selection;
        s_lastSelectedItemText = curSelItem;
        s_selectionStartTime = millis();
        s_menuScrollingActive = false;
    }

    int y_pos = 21;

    for (int i = offset; i < offset + 4 && i < item_count; i++) {
        const char* valStr = (values && values[i].length() > 0) ? values[i].c_str() : nullptr;
        int vw = valStr ? u8g2.getStrWidth(valStr) : 0;
        int valX = 118 - vw;
        int maxLabelW = (valStr && vw > 0) ? (valX - 4 - 3) : 114;
        if (maxLabelW < 24) maxLabelW = 24;

        const char* rawLabel = (items && items[i]) ? items[i] : "";
        int fullW = u8g2.getStrWidth(rawLabel);

        if (i == selection) {
            u8g2.drawBox(2, y_pos - 9, 118, 11);
            u8g2.setDrawColor(0);
            if (fullW > maxLabelW) {
                unsigned long elapsed = (millis() >= s_selectionStartTime) ? (millis() - s_selectionStartTime) : 0;
                if (elapsed >= 3000) {
                    s_menuScrollingActive = true;
                }
                drawScrollingText(u8g2, rawLabel, 4, y_pos, maxLabelW, s_selectionStartTime, true);
            } else {
                u8g2.drawStr(4, y_pos, rawLabel);
            }
            if (valStr) {
                u8g2.drawStr(valX, y_pos, valStr);
            }
            u8g2.setDrawColor(1);
        } else {
            String label = rawLabel;
            if (fullW > maxLabelW) {
                while (label.length() > 0 && (u8g2.getStrWidth(label.c_str()) + 4) > maxLabelW) {
                    label.remove(label.length() - 1);
                }
                if (label.length() > 0) label += ".";
            }
            u8g2.drawStr(4, y_pos, label.c_str());
            if (valStr) {
                u8g2.drawStr(valX, y_pos, valStr);
            }
        }
        y_pos += 12;
    }

    drawScrollBar(offset, item_count);
}

void Display::drawStandardMenu(const char* title, const char** items, int item_count, int selection, int offset, const char* const* values, const char* headerRight) {
    u8g2.setFont(u8g2_font_5x7_tr);
    int hw = (headerRight && headerRight[0] != '\0') ? u8g2.getStrWidth(headerRight) : 0;
    int maxTitleW = (hw > 0) ? (124 - hw) : 124;
    String cleanTitle = title ? title : "";
    if (u8g2.getStrWidth(cleanTitle.c_str()) > maxTitleW) {
        while (cleanTitle.length() > 0 && (u8g2.getStrWidth(cleanTitle.c_str()) + 4) > maxTitleW) {
            cleanTitle.remove(cleanTitle.length() - 1);
        }
        if (cleanTitle.length() > 0) cleanTitle += ".";
    }
    u8g2.drawStr(2, 7, cleanTitle.c_str());
    if (hw > 0) {
        u8g2.drawStr(126 - hw, 7, headerRight);
    }
    u8g2.drawLine(0, 9, 128, 9);
    u8g2.setFont(u8g2_font_6x10_tr);

    const char* curSelItem = (items && selection >= 0 && selection < item_count && items[selection]) ? items[selection] : "";
    String curTitle = title ? title : "";
    if (curTitle != s_lastMenuTitle || selection != s_lastMenuSelection || s_lastSelectedItemText != curSelItem) {
        s_lastMenuTitle = curTitle;
        s_lastMenuSelection = selection;
        s_lastSelectedItemText = curSelItem;
        s_selectionStartTime = millis();
        s_menuScrollingActive = false;
    }

    int y_pos = 21;

    for (int i = offset; i < offset + 4 && i < item_count; i++) {
        const char* valStr = (values && values[i] && values[i][0] != '\0') ? values[i] : nullptr;
        int vw = valStr ? u8g2.getStrWidth(valStr) : 0;
        int valX = 118 - vw;
        int maxLabelW = (valStr && vw > 0) ? (valX - 4 - 3) : 114;
        if (maxLabelW < 24) maxLabelW = 24;

        const char* rawLabel = (items && items[i]) ? items[i] : "";
        int fullW = u8g2.getStrWidth(rawLabel);

        if (i == selection) {
            u8g2.drawBox(2, y_pos - 9, 118, 11);
            u8g2.setDrawColor(0);
            if (fullW > maxLabelW) {
                unsigned long elapsed = (millis() >= s_selectionStartTime) ? (millis() - s_selectionStartTime) : 0;
                if (elapsed >= 3000) {
                    s_menuScrollingActive = true;
                }
                drawScrollingText(u8g2, rawLabel, 4, y_pos, maxLabelW, s_selectionStartTime, true);
            } else {
                u8g2.drawStr(4, y_pos, rawLabel);
            }
            if (valStr) {
                u8g2.drawStr(valX, y_pos, valStr);
            }
            u8g2.setDrawColor(1);
        } else {
            String label = rawLabel;
            if (fullW > maxLabelW) {
                while (label.length() > 0 && (u8g2.getStrWidth(label.c_str()) + 4) > maxLabelW) {
                    label.remove(label.length() - 1);
                }
                if (label.length() > 0) label += ".";
            }
            u8g2.drawStr(4, y_pos, label.c_str());
            if (valStr) {
                u8g2.drawStr(valX, y_pos, valStr);
            }
        }
        y_pos += 12;
    }

    drawScrollBar(offset, item_count);
}

void Display::drawMenu(const char* title, const char** items, int item_count, int selection, int offset, const char* headerRight) {
    drawStandardMenu(title, items, item_count, selection, offset, (const String*)nullptr, headerRight);
}

void Display::drawTopStatusBar(const char* title, int batteryPct) {
    u8g2.setFont(u8g2_font_5x7_tr);
    char bStr[8];
    snprintf(bStr, sizeof(bStr), "%d%%", batteryPct);
    int bw = u8g2.getStrWidth(bStr);
    int maxTitleW = 124 - bw;
    String cleanTitle = title ? title : "";
    if (u8g2.getStrWidth(cleanTitle.c_str()) > maxTitleW) {
        while (cleanTitle.length() > 0 && (u8g2.getStrWidth(cleanTitle.c_str()) + 4) > maxTitleW) {
            cleanTitle.remove(cleanTitle.length() - 1);
        }
        if (cleanTitle.length() > 0) cleanTitle += ".";
    }
    u8g2.drawStr(2, 7, cleanTitle.c_str());
    u8g2.drawStr(126 - bw, 7, bStr);
    u8g2.drawLine(0, 9, 128, 9);
}

void Display::navigateMenu(int &selection, int &offset, int count, int dir, bool wrap) {
    if (count <= 0) return;
    if (dir < 0) { // UP
        selection--;
        if (selection < 0) {
            if (wrap) {
                selection = count - 1;
                offset = (count > 4) ? (count - 4) : 0;
            } else {
                selection = 0;
            }
        }
        if (selection < offset) {
            offset = selection;
        }
    } else if (dir > 0) { // DOWN
        selection++;
        if (selection >= count) {
            if (wrap) {
                selection = 0;
                offset = 0;
            } else {
                selection = count - 1;
            }
        }
        if (selection >= offset + 4) {
            offset = selection - 3;
        }
    }
}

Display display;
