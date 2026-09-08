#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "hw_config.h"

class Display {
public:
    Display();
    bool begin();
    U8G2 &getU8g2();
    void clear();
    void sendBuffer();

private:
    U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2;
};

extern Display display;

#endif // DISPLAY_H
