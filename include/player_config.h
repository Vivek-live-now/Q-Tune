#ifndef PLAYER_CONFIG_H
#define PLAYER_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

enum PlayerLayout {
    LAYOUT_AUTO = 0,         // Auto: Show album art if present, else Spectrum HUD
    LAYOUT_SPLIT_ART = 1,    // 56x56 Album Art + Song Info side-by-side
    LAYOUT_SPECTRUM_HUD = 2, // Classic Full-Width Spectrum HUD
    LAYOUT_COVER_HERO = 3    // Centered 64x64 Cover View
};

enum DitherMode {
    DITHER_ATKINSON = 0,       // Atkinson error diffusion (high contrast, sharp details)
    DITHER_FLOYD_STEINBERG = 1,// Floyd-Steinberg error diffusion (smooth gradient)
    DITHER_THRESHOLD = 2       // High-contrast 2-tone thresholding
};

enum ArtSource {
    ART_SRC_EMBEDDED_FIRST = 0,// Embedded ID3/FLAC/M4A first, then folder art
    ART_SRC_FOLDER_FIRST = 1,  // Folder cover.jpg/folder.jpg first, then embedded
    ART_SRC_DISABLED = 2       // Disable album art decoding
};

class PlayerConfig {
public:
    PlayerConfig();
    void begin();
    void save();

    PlayerLayout getLayout() const;
    void setLayout(PlayerLayout layout);
    void cycleLayout();
    const char* getLayoutName() const;

    DitherMode getDitherMode() const;
    void setDitherMode(DitherMode mode);
    void cycleDitherMode();
    const char* getDitherName() const;

    ArtSource getArtSource() const;
    void setArtSource(ArtSource src);
    void cycleArtSource();
    const char* getArtSourceName() const;

    bool isLyricsAutoScroll() const;
    void setLyricsAutoScroll(bool enable);
    void toggleLyricsAutoScroll();
    const char* getLyricsAutoScrollName() const;

    // Audiophile DSP Configuration
    bool isEqEnabled() const;
    void setEqEnabled(bool enable);
    void toggleEq();

    uint8_t getEqPreset() const;
    void setEqPreset(uint8_t preset);
    void cycleEqPreset();
    const char* getEqPresetName() const;

    int8_t getEqBand(uint8_t band) const;
    void setEqBand(uint8_t band, int8_t gain);

    uint8_t getCrossfeedMode() const;
    void setCrossfeedMode(uint8_t mode);
    void cycleCrossfeedMode();
    const char* getCrossfeedModeName() const;

private:
    PlayerLayout layout;
    DitherMode dither;
    ArtSource artSource;
    bool lyricsAutoScroll;
    bool eqEnabled;
    uint8_t eqPreset;
    int8_t eqBands[5];
    uint8_t crossfeedMode;
    Preferences prefs;
};

extern PlayerConfig playerConfig;

#endif // PLAYER_CONFIG_H
