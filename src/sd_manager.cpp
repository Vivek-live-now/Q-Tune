#include "sd_manager.h"

SDManager::SDManager() : mounted(false) {}

bool SDManager::begin() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!SD.begin(SD_CS, SPI, 16000000)) {
        mounted = false;
        return false;
    }
    mounted = true;
    return true;
}

bool SDManager::isMounted() const {
    return mounted;
}

std::vector<String> SDManager::listMusicFiles() {
    std::vector<String> musicFiles;
    if (!mounted) return musicFiles;

    File dir = SD.open("/music");
    if (!dir || !dir.isDirectory()) {
        return musicFiles;
    }

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
                musicFiles.push_back("/music/" + filename);
            }
        }
        file = dir.openNextFile();
    }
    dir.close();
    return musicFiles;
}

SDManager sdManager;
