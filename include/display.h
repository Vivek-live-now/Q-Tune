#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "hw_config.h"
#include "gdma_manager.h"

class Display {
public:
    Display();
    bool begin();
    U8G2 &getU8g2();
    void clear();
    void clearBufferFast();
    void invertBuffer();
    void sendBuffer();

    // Hardware GDMA Asynchronous Display Refresh
    bool isGdmaEnabled() const { return gdma_enabled; }
    void setGdmaEnabled(bool enable) { gdma_enabled = enable; }
    void flushAsync();
    const GdmaTransferStats& getGdmaStats() const;

    // Q-Watch Signature Menu System Implementation
    void drawScrollBar(int offset, int item_count);
    void drawStandardMenu(const char* title, const char** items, int item_count, int selection, int offset, const String* values = nullptr, const char* headerRight = nullptr);
    void drawStandardMenu(const char* title, const char** items, int item_count, int selection, int offset, const char* const* values, const char* headerRight = nullptr);
    void drawMenu(const char* title, const char** items, int item_count, int selection, int offset, const char* headerRight = nullptr);
    void drawTopStatusBar(const char* title, int batteryPct);

    // Menu Navigation & Windowing engine (identical to Q-Watch processNavUp / processNavDown)
    static void navigateMenu(int &selection, int &offset, int count, int dir, bool wrap = true);

private:
    U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2;
    bool gdma_enabled;
};

extern Display display;

#endif // DISPLAY_H
