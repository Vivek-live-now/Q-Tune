#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include <Arduino.h>
#include <Wire.h>
#include <vector>
#include "hw_config.h"

class I2CScanner {
public:
    I2CScanner();
    void begin();
    std::vector<uint8_t> scan();
};

extern I2CScanner i2cScanner;

#endif // I2C_SCANNER_H
