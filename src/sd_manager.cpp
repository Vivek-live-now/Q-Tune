#include "sd_manager.h"
#include "audio_player.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#endif

SDManager::SDManager() : mounted(false), safeToRemove(true) {}

bool SDManager::begin() {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    // Release any deep-sleep hardware pad holds so SD_CS can toggle freely
    gpio_hold_dis((gpio_num_t)SD_CS);
    gpio_deep_sleep_hold_dis();
#endif

    if (spiBusMutex != NULL) {
        xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    }

    // Arbitrate shared SPI bus: ensure both CS lines are driven high (deselected)
    pinMode(OLED_CS, OUTPUT);
    digitalWrite(OLED_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Always reset FATFS driver before re-initialization
    SD.end();
    mounted = false;
    delay(50);

    // Re-initialize SPI hardware bus on shared pins
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, OLED_CS);

    // Provide >80 clock cycles with CS=HIGH for SD card state synchronization per SD specification
    digitalWrite(SD_CS, HIGH);
    digitalWrite(OLED_CS, HIGH);
    for (int i = 0; i < 16; i++) {
        SPI.transfer(0xFF);
    }
    delay(50);

    // Multi-frequency retry loop: try standard 10MHz, then 4MHz, then 1MHz
    bool ok = false;
    const uint32_t freqs[] = { 10000000, 4000000, 1000000 };
    for (int retry = 0; retry < 3 && !ok; retry++) {
        for (uint32_t freq : freqs) {
            if (SD.begin(SD_CS, SPI, freq)) {
                ok = true;
                break;
            }
            delay(25);
        }
        if (!ok) {
            delay(50);
            digitalWrite(SD_CS, HIGH);
            for (int i = 0; i < 16; i++) {
                SPI.transfer(0xFF);
            }
        }
    }

    if (!ok) {
        mounted = false;
        safeToRemove = true;
        pinMode(SD_CS, OUTPUT);
        digitalWrite(SD_CS, HIGH);
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    mounted = true;
    safeToRemove = false;
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return true;
}

bool SDManager::unmount() {
    if (!mounted && safeToRemove) return true;

    // Gracefully stop audio playback to close active file handles and release streams
    audioPlayer.stop();

    if (spiBusMutex != NULL) {
        xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    }

    SD.end();
    mounted = false;
    safeToRemove = true;

    // Force SD_CS HIGH (deselected/idle state) to protect card flash during standby
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Give card 50ms settling time after bus disconnect
    delay(50);

    if (spiBusMutex != NULL) {
        xSemaphoreGive(spiBusMutex);
    }
    return true;
}

bool SDManager::remount() {
    unmount();
    delay(100);
    return begin();
}

bool SDManager::isMounted() const {
    return mounted;
}

bool SDManager::isSafeToRemove() const {
    return safeToRemove;
}

void SDManager::notifyCardRemoved() {
    audioPlayer.stop();
    if (spiBusMutex != NULL) {
        xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    }
    SD.end();
    mounted = false;
    safeToRemove = true;
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    if (spiBusMutex != NULL) {
        xSemaphoreGive(spiBusMutex);
    }
}

std::vector<String> SDManager::listMusicFiles() {
    std::vector<String> musicFiles;
    if (!mounted) {
        if (!begin()) return musicFiles;
    }

    if (spiBusMutex != NULL) {
        xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    }

    File dir = SD.open("/music");
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
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
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return musicFiles;
}

SDManager sdManager;
