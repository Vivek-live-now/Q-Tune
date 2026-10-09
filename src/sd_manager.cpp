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

    // Multi-frequency retry loop: prioritize 4MHz for noise immunity on shared SPI bus
    bool ok = false;
    const uint32_t freqs[] = { 4000000, 8000000, 1000000 };
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
