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
    bool unmount();
    bool remount();
    bool isMounted() const;
    bool isSafeToRemove() const;
    void notifyCardRemoved();
    std::vector<String> listMusicFiles();
    bool hasLibraryIndex();
    bool buildLibraryIndex(void (*progressCallback)(int count) = nullptr);
    bool rescanLibrary(void (*progressCallback)(int count) = nullptr);

    // Categorization & Navigation
    bool listFolder(const String &folderPath, std::vector<String> &subdirs, std::vector<String> &audioFiles);
    std::vector<String> listArtists();
    std::vector<String> listAlbums();
    std::vector<String> listPlaylists();
    std::vector<String> loadPlaylist(const String &m3uPath);

    // Recently Played
    std::vector<String> getRecentTracks();
    void addRecentTrack(const String &trackPath);

private:
    bool mounted;
    bool safeToRemove;
    void scanDirRecursive(File &dir, const String &currentPath, File &indexFile, int &count, void (*progressCallback)(int));
};

extern SDManager sdManager;

#endif // SD_MANAGER_H
