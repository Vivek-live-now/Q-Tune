#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include "hw_config.h"

class SDManager {
public:
    SDManager();
    bool begin();
    bool isMounted() const;
    std::vector<String> listMusicFiles();

private:
    bool mounted;
};

extern SDManager sdManager;

#endif // SD_MANAGER_H
