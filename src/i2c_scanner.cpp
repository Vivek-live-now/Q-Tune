#include "i2c_scanner.h"

I2CScanner::I2CScanner() {}

void I2CScanner::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
}

std::vector<uint8_t> I2CScanner::scan() {
    std::vector<uint8_t> devices;
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            devices.push_back(address);
        }
    }
    return devices;
}

I2CScanner i2cScanner;
