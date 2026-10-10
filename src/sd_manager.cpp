#include "sd_manager.h"
#include "audio_player.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#endif

SDManager::SDManager() :
    mounted(false), safeToRemove(true),
    cardIsSDXC(false), cardIsExFAT(false), cardCapacityMB(0),
    cardTypeName("None"), filesystemName("None") {}

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
    cardIsSDXC = false;
    cardIsExFAT = false;
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

    // Multi-frequency retry loop: strictly start with 400kHz per SDXC specification, then ramp up
    bool ok = false;
    const uint32_t freqs[] = { 400000, 1000000, 4000000, 8000000 };
    for (uint32_t freq : freqs) {
        if (SD.begin(SD_CS, SPI, freq)) {
            ok = true;
            break;
        }
        delay(15);
    }

    if (!ok) {
        mounted = false;
        safeToRemove = true;
        probeCardDetails();
        pinMode(SD_CS, OUTPUT);
        digitalWrite(SD_CS, HIGH);
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    mounted = true;
    safeToRemove = false;
    probeCardDetails();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return true;
}

void SDManager::probeCardDetails() {
    if (mounted) {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
        uint8_t cType = SD.cardType();
        cardCapacityMB = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
        if (cardCapacityMB >= 60000 || cType == CARD_SDHC) {
            if (cardCapacityMB >= 60000) {
                cardIsSDXC = true;
                cardTypeName = "SDXC (64GB+)";
            } else {
                cardIsSDXC = false;
                cardTypeName = "SDHC";
            }
        } else {
            cardIsSDXC = false;
            cardTypeName = "SDSC";
        }
        filesystemName = "FAT32";
#endif
        return;
    }

    // Direct SPI probing for unmounted / exFAT SDXC cards per SD Physical Layer Specification
    // Strictly clock at <= 400000 Hz (250kHz) during card identification and initialization
    SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));

    // Provide >= 80 clock cycles with CS HIGH to wake controller and synchronize SPI mode
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    for (int i = 0; i < 20; i++) {
        SPI.transfer(0xFF);
    }
    delay(5);

    auto sendSdCmd = [](uint8_t cmd, uint32_t arg, uint8_t crc) -> uint8_t {
        digitalWrite(SD_CS, HIGH);
        SPI.transfer(0xFF);
        digitalWrite(SD_CS, LOW);
        SPI.transfer(0xFF);

        uint8_t pkt[6] = {
            (uint8_t)(0x40 | cmd),
            (uint8_t)(arg >> 24),
            (uint8_t)(arg >> 16),
            (uint8_t)(arg >> 8),
            (uint8_t)(arg),
            crc
        };
        for (int i = 0; i < 6; i++) SPI.transfer(pkt[i]);

        uint8_t resp = 0xFF;
        for (int i = 0; i < 200 && (resp & 0x80); i++) {
            resp = SPI.transfer(0xFF);
        }
        return resp;
    };

    // 1. Send CMD0 to place card into SPI IDLE state (0x01)
    uint8_t r1 = 0xFF;
    for (int attempt = 0; attempt < 10 && (r1 != 0x01 && r1 != 0x00); attempt++) {
        r1 = sendSdCmd(0, 0x00000000, 0x95);
        delay(2);
    }

    if (r1 == 0x01 || r1 == 0x00) {
        // Card is responding in SPI mode.
        // 2. Send CMD8 (SEND_IF_COND) to verify SDv2 voltage & high capacity support
        uint8_t r7 = sendSdCmd(8, 0x000001AA, 0x87);
        bool isV2 = false;
        if (r7 == 0x01) {
            uint8_t r7_bytes[4];
            for (int i = 0; i < 4; i++) r7_bytes[i] = SPI.transfer(0xFF);
            if (r7_bytes[2] == 0x01 && r7_bytes[3] == 0xAA) {
                isV2 = true;
                cardIsSDXC = true;
                cardTypeName = "SDXC (64GB+)";
                cardCapacityMB = 64000;
            }
        }

        // 3. ACMD41 initialization loop (CMD55 + ACMD41 with HCS=1) with 400000 Hz spec compliance
        uint32_t startMs = millis();
        bool ready = false;
        while ((millis() - startMs < 1500) && !ready) {
            // CMD55 (APP_CMD)
            sendSdCmd(55, 0x00000000, 0x65);
            // ACMD41 with HCS (bit 30 = 0x40000000) for SDHC/SDXC (opcode 0x69)
            uint8_t r41 = sendSdCmd(41, isV2 ? 0x40000000 : 0x00000000, 0x77);

            if (r41 == 0x00) {
                ready = true;
                break;
            }
            delay(10);
        }

        if (ready) {
            // 4. Send CMD58 (READ_OCR) to verify Card Capacity Status (CCS bit 30)
            uint8_t r58 = sendSdCmd(58, 0x00000000, 0xFD);
            if (r58 == 0x00) {
                uint8_t ocr[4];
                for (int i = 0; i < 4; i++) ocr[i] = SPI.transfer(0xFF);
                if (ocr[0] & 0x40) {
                    cardIsSDXC = true;
                    cardTypeName = "SDXC (64GB+)";
                    cardCapacityMB = 64000;
                }
            }

            // Set 512-byte block length via CMD16
            sendSdCmd(16, 512, 0xFF);

            // 5. Read Sector 0 via CMD17 (READ_SINGLE_BLOCK) to inspect filesystem signature
            uint8_t r17 = sendSdCmd(17, 0x00000000, 0xFF);
            if (r17 == 0x00) {
                uint8_t token = 0xFF;
                for (int i = 0; i < 5000 && (token != 0xFE); i++) token = SPI.transfer(0xFF);
                if (token == 0xFE) {
                    uint8_t sector[512];
                    for (int i = 0; i < 512; i++) sector[i] = SPI.transfer(0xFF);
                    SPI.transfer(0xFF); SPI.transfer(0xFF); // CRC
                    digitalWrite(SD_CS, HIGH);
                    SPI.transfer(0xFF);

                    // Check for "EXFAT   " at offset 3 (Superfloppy VBR)
                    if (memcmp(&sector[3], "EXFAT   ", 8) == 0) {
                        cardIsExFAT = true;
                        filesystemName = "exFAT";
                        cardIsSDXC = true;
                        cardTypeName = "SDXC (64GB+)";
                        if (cardCapacityMB < 64000) cardCapacityMB = 64000;
                    }

                    // Check all 4 MBR partition table entries (offsets 446, 462, 478, 494)
                    if (!cardIsExFAT && sector[510] == 0x55 && sector[511] == 0xAA) {
                        for (int p = 0; p < 4; p++) {
                            int pOffset = 446 + (p * 16);
                            uint8_t pType = sector[pOffset + 4];
                            uint32_t partLba = sector[pOffset + 8] |
                                               ((uint32_t)sector[pOffset + 9] << 8) |
                                               ((uint32_t)sector[pOffset + 10] << 16) |
                                               ((uint32_t)sector[pOffset + 11] << 24);

                            if (pType == 0x07 || pType == 0xEE) {
                                // MBR Type 0x07 = exFAT/NTFS, Type 0xEE = GPT Protective MBR
                                cardIsExFAT = true;
                                filesystemName = "exFAT";
                                cardIsSDXC = true;
                                cardTypeName = "SDXC (64GB+)";
                                if (cardCapacityMB < 64000) cardCapacityMB = 64000;

                                if (partLba > 0) {
                                    uint8_t r17p = sendSdCmd(17, partLba, 0xFF);
                                    if (r17p == 0x00) {
                                        uint8_t tok2 = 0xFF;
                                        for (int i = 0; i < 5000 && (tok2 != 0xFE); i++) tok2 = SPI.transfer(0xFF);
                                        if (tok2 == 0xFE) {
                                            uint8_t partSec[512];
                                            for (int i = 0; i < 512; i++) partSec[i] = SPI.transfer(0xFF);
                                            SPI.transfer(0xFF); SPI.transfer(0xFF); // CRC
                                            digitalWrite(SD_CS, HIGH);
                                            SPI.transfer(0xFF);

                                            if (memcmp(&partSec[3], "EXFAT   ", 8) == 0) {
                                                cardIsExFAT = true;
                                                filesystemName = "exFAT";
                                            }
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    digitalWrite(SD_CS, HIGH);
    for (int i = 0; i < 8; i++) SPI.transfer(0xFF);
    SPI.endTransaction();

    if (cardIsExFAT) {
        Serial.println("\n[SD] ========================================================");
        Serial.println("[SD] SDXC (64GB+) exFAT Card Detected!");
        Serial.println("[SD] Note: ESP32 hardware FatFs requires FAT32 for playback.");
        Serial.println("[SD] Please format card as FAT32 (32KB clusters) via GUIFormat/Rufus.");
        Serial.println("[SD] ========================================================\n");
    }
}

bool SDManager::isSDXC() const { return cardIsSDXC; }
bool SDManager::isExFAT() const { return cardIsExFAT; }
uint32_t SDManager::getCardCapacityMB() const { return cardCapacityMB; }
const char* SDManager::getCardTypeName() const { return cardTypeName.c_str(); }
const char* SDManager::getFilesystemName() const { return filesystemName.c_str(); }

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

void SDManager::scanDirRecursive(File &dir, const String &currentPath, File &indexFile, int &count, void (*progressCallback)(int)) {
    File file = dir.openNextFile();
    while (file) {
        String fname = String(file.name());
        int lastSlash = fname.lastIndexOf('/');
        String baseName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;

        if (!baseName.startsWith(".") && !baseName.startsWith("_") && baseName != "System Volume Information") {
            if (file.isDirectory()) {
                String subPath = currentPath + "/" + baseName;
                File subDir = SD.open(subPath);
                if (subDir) {
                    scanDirRecursive(subDir, subPath, indexFile, count, progressCallback);
                    subDir.close();
                }
            } else {
                String lower = baseName;
                lower.toLowerCase();
                if (lower.endsWith(".wav") || lower.endsWith(".mp3") || lower.endsWith(".flac") || lower.endsWith(".m4a") || lower.endsWith(".aac")) {
                    String fullPath = currentPath + "/" + baseName;
                    indexFile.println(fullPath);
                    count++;
                    if (progressCallback != nullptr && (count % 10 == 0)) {
                        progressCallback(count);
                    }
                }
            }
        }
        file.close();
        file = dir.openNextFile();
    }
}

bool SDManager::hasLibraryIndex() {
    if (!mounted) {
        if (!begin()) return false;
    }
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    bool exists = SD.exists("/music/library.txt");
    if (exists) {
        File f = SD.open("/music/library.txt", FILE_READ);
        if (f) {
            exists = (f.size() > 0);
            f.close();
        } else {
            exists = false;
        }
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return exists;
}

bool SDManager::buildLibraryIndex(void (*progressCallback)(int count)) {
    if (!mounted) {
        if (!begin()) return false;
    }
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);

    if (SD.exists("/music/library.txt")) {
        SD.remove("/music/library.txt");
    }

    File indexFile = SD.open("/music/library.txt", FILE_WRITE);
    if (!indexFile) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    File musicDir = SD.open("/music");
    if (!musicDir || !musicDir.isDirectory()) {
        if (musicDir) musicDir.close();
        indexFile.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    int count = 0;
    scanDirRecursive(musicDir, "/music", indexFile, count, progressCallback);
    musicDir.close();
    indexFile.close();

    if (progressCallback != nullptr) {
        progressCallback(count);
    }

    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return (count > 0);
}

bool SDManager::rescanLibrary(void (*progressCallback)(int count)) {
    return buildLibraryIndex(progressCallback);
}

std::vector<String> SDManager::listMusicFiles() {
    std::vector<String> musicFiles;
    if (!mounted) {
        if (!begin()) return musicFiles;
    }

    if (!hasLibraryIndex()) {
        buildLibraryIndex();
    }

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    File indexFile = SD.open("/music/library.txt", FILE_READ);
    if (indexFile) {
        while (indexFile.available()) {
            String line = indexFile.readStringUntil('\n');
            line.trim();
            if (line.length() > 0) {
                musicFiles.push_back(line);
            }
        }
        indexFile.close();
    }

    // Fallback direct scan if library was empty
    if (musicFiles.empty()) {
        File dir = SD.open("/music");
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    String filename = String(file.name());
                    String lower = filename;
                    lower.toLowerCase();
                    if (lower.endsWith(".wav") || lower.endsWith(".mp3") || lower.endsWith(".flac") || lower.endsWith(".m4a") || lower.endsWith(".aac")) {
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
                file.close();
                file = dir.openNextFile();
            }
            dir.close();
        }
    }

    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return musicFiles;
}

bool SDManager::listFolder(const String &folderPath, std::vector<String> &subdirs, std::vector<String> &audioFiles) {
    if (!mounted) {
        if (!begin()) return false;
    }
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);

    File dir = SD.open(folderPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    File file = dir.openNextFile();
    while (file) {
        String fname = String(file.name());
        int lastSlash = fname.lastIndexOf('/');
        String baseName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;

        if (!baseName.startsWith(".") && !baseName.startsWith("_") && baseName != "System Volume Information") {
            if (file.isDirectory()) {
                subdirs.push_back(baseName);
            } else {
                String lower = baseName;
                lower.toLowerCase();
                if (lower.endsWith(".wav") || lower.endsWith(".mp3") || lower.endsWith(".flac") || lower.endsWith(".m4a") || lower.endsWith(".aac")) {
                    String fullPath = folderPath;
                    if (!fullPath.endsWith("/")) fullPath += "/";
                    fullPath += baseName;
                    audioFiles.push_back(fullPath);
                }
            }
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();

    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return true;
}

std::vector<String> SDManager::listArtists() {
    std::vector<String> artists;
    std::vector<String> allSongs = listMusicFiles();
    for (size_t i = 0; i < allSongs.size(); i++) {
        String path = allSongs[i];
        if (path.startsWith("/music/")) {
            String rel = path.substring(7);
            int slash = rel.indexOf('/');
            if (slash > 0) {
                String artist = rel.substring(0, slash);
                bool found = false;
                for (size_t j = 0; j < artists.size(); j++) {
                    if (artists[j] == artist) { found = true; break; }
                }
                if (!found) artists.push_back(artist);
            }
        }
    }
    return artists;
}

std::vector<String> SDManager::listAlbums() {
    std::vector<String> albums;
    std::vector<String> allSongs = listMusicFiles();
    for (size_t i = 0; i < allSongs.size(); i++) {
        String path = allSongs[i];
        if (path.startsWith("/music/")) {
            String rel = path.substring(7);
            int firstSlash = rel.indexOf('/');
            if (firstSlash > 0) {
                String remainder = rel.substring(firstSlash + 1);
                int secondSlash = remainder.indexOf('/');
                String album = (secondSlash > 0) ? remainder.substring(0, secondSlash) : remainder;
                int dotIdx = album.lastIndexOf('.');
                if (dotIdx > 0 && secondSlash <= 0) continue; // It was a file, not album
                bool found = false;
                for (size_t j = 0; j < albums.size(); j++) {
                    if (albums[j] == album) { found = true; break; }
                }
                if (!found) albums.push_back(album);
            }
        }
    }
    return albums;
}

std::vector<String> SDManager::listPlaylists() {
    std::vector<String> playlists;
    if (!mounted) {
        if (!begin()) return playlists;
    }
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);

    // Check /music and /playlists
    const char* dirsToCheck[] = {"/music", "/playlists"};
    for (int d = 0; d < 2; d++) {
        File dir = SD.open(dirsToCheck[d]);
        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    String fname = String(file.name());
                    String lower = fname;
                    lower.toLowerCase();
                    if (lower.endsWith(".m3u") || lower.endsWith(".m3u8")) {
                        String fullPath = String(dirsToCheck[d]) + "/" + fname;
                        playlists.push_back(fullPath);
                    }
                }
                file.close();
                file = dir.openNextFile();
            }
            dir.close();
        }
    }

    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return playlists;
}

std::vector<String> SDManager::loadPlaylist(const String &m3uPath) {
    std::vector<String> playlistSongs;
    if (!mounted) {
        if (!begin()) return playlistSongs;
    }
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);

    File file = SD.open(m3uPath, FILE_READ);
    if (file) {
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() > 0 && !line.startsWith("#")) {
                if (!line.startsWith("/")) {
                    line = "/music/" + line;
                }
                playlistSongs.push_back(line);
            }
        }
        file.close();
    }

    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return playlistSongs;
}

std::vector<String> SDManager::getRecentTracks() {
    std::vector<String> recents;
    if (!mounted) {
        if (!begin()) return recents;
    }
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);

    if (SD.exists("/music/recent.txt")) {
        File f = SD.open("/music/recent.txt", FILE_READ);
        if (f) {
            while (f.available() && recents.size() < 20) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() > 0) {
                    recents.push_back(line);
                }
            }
            f.close();
        }
    }

    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return recents;
}

void SDManager::addRecentTrack(const String &trackPath) {
    if (!mounted || trackPath.length() == 0) return;

    std::vector<String> recents = getRecentTracks();
    std::vector<String> updated;
    updated.push_back(trackPath);

    for (size_t i = 0; i < recents.size() && updated.size() < 20; i++) {
        if (recents[i] != trackPath) {
            updated.push_back(recents[i]);
        }
    }

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    File f = SD.open("/music/recent.txt", FILE_WRITE);
    if (f) {
        for (size_t i = 0; i < updated.size(); i++) {
            f.println(updated[i]);
        }
        f.close();
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
}

SDManager sdManager;
