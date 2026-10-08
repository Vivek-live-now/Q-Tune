#include "sd_manager.h"

SDManager::SDManager() : mounted(false) {}

bool SDManager::begin() {
    // Arbitrate shared SPI bus: ensure both CS lines are driven high
    pinMode(OLED_CS, OUTPUT);
    digitalWrite(OLED_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (mounted) {
        SD.end();
        mounted = false;
    }

    // Try standard 10 MHz SPI clock for high reliability on shared bus, fallback to 4 MHz
    if (!SD.begin(SD_CS, SPI, 10000000)) {
        if (!SD.begin(SD_CS, SPI, 4000000)) {
            mounted = false;
            return false;
        }
    }
    mounted = true;
    return true;
}

bool SDManager::isMounted() const {
    return mounted;
}

std::vector<String> SDManager::listMusicFiles() {
    std::vector<String> musicFiles;
    if (!mounted) {
        if (!begin()) return musicFiles;
    }

    File dir = SD.open("/music");
    if (!dir || !dir.isDirectory()) {
        return musicFiles;
    }

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            String lower = filename;
            lower.toLowerCase();
            if (lower.endsWith(".wav") || lower.endsWith(".mp3") || lower.endsWith(".flac")) {
                String fullPath = filename;
                if (!fullPath.startsWith("/music/")) {
                    if (fullPath.startsWith("/")) {
                        fullPath = "/music" + fullPath;
                    } else {
                        fullPath = "/music/" + fullPath;
                    }
                }
                musicFiles.push_back(fullPath);
            }
        }
        file = dir.openNextFile();
    }
    dir.close();
    return musicFiles;
}

SDManager sdManager;
