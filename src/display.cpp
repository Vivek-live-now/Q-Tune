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

void Display::drawStandardMenu(const char* title, const char** items, int item_count, int selection, int offset, const String* values, const char* headerRight) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, 7, title);
    if (headerRight && headerRight[0] != '\0') {
        int hw = u8g2.getStrWidth(headerRight);
        u8g2.drawStr(126 - hw, 7, headerRight);
    }
    u8g2.drawLine(0, 9, 128, 9);
    u8g2.setFont(u8g2_font_6x10_tr);

    int y_pos = 21;

    for (int i = offset; i < offset + 4 && i < item_count; i++) {
        if (i == selection) {
            u8g2.drawBox(2, y_pos - 9, 118, 11);
            u8g2.setDrawColor(0);
            u8g2.drawStr(4, y_pos, items[i]);
            if (values && values[i].length() > 0) {
                int vw = u8g2.getStrWidth(values[i].c_str());
                u8g2.drawStr(118 - vw, y_pos, values[i].c_str());
            }
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(4, y_pos, items[i]);
            if (values && values[i].length() > 0) {
                int vw = u8g2.getStrWidth(values[i].c_str());
                u8g2.drawStr(118 - vw, y_pos, values[i].c_str());
            }
        }
        y_pos += 12;
    }

    drawScrollBar(offset, item_count);
}

void Display::drawStandardMenu(const char* title, const char** items, int item_count, int selection, int offset, const char* const* values, const char* headerRight) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, 7, title);
    if (headerRight && headerRight[0] != '\0') {
        int hw = u8g2.getStrWidth(headerRight);
        u8g2.drawStr(126 - hw, 7, headerRight);
    }
    u8g2.drawLine(0, 9, 128, 9);
    u8g2.setFont(u8g2_font_6x10_tr);

    int y_pos = 21;

    for (int i = offset; i < offset + 4 && i < item_count; i++) {
        if (i == selection) {
            u8g2.drawBox(2, y_pos - 9, 118, 11);
            u8g2.setDrawColor(0);
            u8g2.drawStr(4, y_pos, items[i]);
            if (values && values[i] && values[i][0] != '\0') {
                int vw = u8g2.getStrWidth(values[i]);
                u8g2.drawStr(118 - vw, y_pos, values[i]);
            }
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(4, y_pos, items[i]);
            if (values && values[i] && values[i][0] != '\0') {
                int vw = u8g2.getStrWidth(values[i]);
                u8g2.drawStr(118 - vw, y_pos, values[i]);
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
    u8g2.drawStr(2, 7, title);

    char bStr[8];
    snprintf(bStr, sizeof(bStr), "%d%%", batteryPct);
    int bw = u8g2.getStrWidth(bStr);
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
