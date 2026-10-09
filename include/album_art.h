#ifndef ALBUM_ART_H
#define ALBUM_ART_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <FS.h>
#include <SD.h>
#include "audio_decoder.h"
#include "player_config.h"

class AlbumArtManager {
public:
    AlbumArtManager();
    void begin();

    bool loadForTrack(const String &audioPath);
    void clear();

    bool hasArt() const;
    int getWidth() const;
    int getHeight() const;

    void draw(U8G2 &u8g2, int x, int y);
    void drawDefaultIcon(U8G2 &u8g2, int x, int y);

    // Dithering algorithms
    static void atkinsonDither(uint8_t *gray, int w, int h, uint8_t *out1Bit);
    static void floydSteinbergDither(uint8_t *gray, int w, int h, uint8_t *out1Bit);
    static void thresholdDither(const uint8_t *gray, int w, int h, uint8_t *out1Bit);

    // Embedded art locators
    static bool findMP3Art(File &file, uint32_t &offset, uint32_t &size);
    static bool findFLACArt(File &file, uint32_t &offset, uint32_t &size);
    static bool findM4AArt(File &file, uint32_t &offset, uint32_t &size);
    static bool findFolderArt(const String &audioPath, String &artFilePath);

private:
    uint8_t bitmap[512]; // Up to 64x64 1-bit monochrome bitmap (512 bytes)
    int artWidth;
    int artHeight;
    bool artLoaded;
    String loadedTrackPath;

    bool decodeJPEGFromStream(File &file, uint32_t offset, uint32_t size, int targetDim, DitherMode dither);
};

extern AlbumArtManager albumArtManager;

#endif // ALBUM_ART_H
