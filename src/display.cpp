#include "display.h"

Display::Display() : u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST) {}

bool Display::begin() {
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, OLED_CS);
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Q-Tune Init...");
    u8g2.sendBuffer();
    return true;
}

U8G2 &Display::getU8g2() {
    return u8g2;
}

void Display::clear() {
    u8g2.clearBuffer();
}

void Display::sendBuffer() {
    u8g2.sendBuffer();
}

Display display;
