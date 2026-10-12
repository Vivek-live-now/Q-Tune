#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include "hw_config.h"
#include "lyrics_parser.h"

// Binary Persistent Lyrics Index Header (Version 1)
constexpr uint32_t LYRICS_INDEX_MAGIC = 0x51544C59; // 'QTLY'
constexpr uint16_t LYRICS_INDEX_VERSION = 1;

struct LyricsIndexEntry {
    char audioPath[128];             // Track relative path
    uint8_t syncType;                // LyricsSyncType enum value
    uint8_t sourcePriority;          // LyricsSourcePriority enum value
    uint32_t fileSize;               // Audio file size for change detection
};

class SDManager {
public:
    SDManager();
    bool begin();
    bool unmount();
    bool remount();
    bool isMounted() const;
    bool isSafeToRemove() const;
    void notifyCardRemoved();
    std::vector<String> listMusicFiles();
    bool hasLibraryIndex();
    bool buildLibraryIndex(void (*progressCallback)(int count) = nullptr);
    bool rescanLibrary(void (*progressCallback)(int count) = nullptr);

    // Persistent Lyrics Scan Index Subsystem
    bool hasLyricsIndex();
    bool buildLyricsIndex(void (*progressCallback)(int count) = nullptr);
    bool lookupLyricsIndex(const String &audioPath, LyricsSyncType &outSync, LyricsSourcePriority &outPri);

    // Categorization & Navigation
    bool listFolder(const String &folderPath, std::vector<String> &subdirs, std::vector<String> &audioFiles);
    std::vector<String> listArtists();
    std::vector<String> listAlbums();
    std::vector<String> listPlaylists();
    std::vector<String> loadPlaylist(const String &m3uPath);

    // Recently Played
    std::vector<String> getRecentTracks();
    void addRecentTrack(const String &trackPath);

    // High-Capacity SDXC (64GB+), exFAT & GPT Detection
    bool isSDXC() const;
    bool isExFAT() const;
    bool isGPT() const;
    uint32_t getCardCapacityMB() const;
    const char* getCardTypeName() const;
    const char* getFilesystemName() const;

private:
    bool mounted;
    bool safeToRemove;
    bool cardIsSDXC;
    bool cardIsExFAT;
    bool cardIsGPT;
    uint32_t cardCapacityMB;
    String cardTypeName;
    String filesystemName;
    void probeCardDetails();
    void scanDirRecursive(File &dir, const String &currentPath, File &indexFile, int &count, void (*progressCallback)(int));
};

extern SDManager sdManager;

#endif // SD_MANAGER_H
