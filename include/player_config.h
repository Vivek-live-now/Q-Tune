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
    DITHER_THRESHOLD = 2,      // High-contrast 2-tone thresholding
    DITHER_BAYER4 = 3,         // Bayer 4x4 matrix dithering
    DITHER_SIERRA_LITE = 4,    // Sierra Lite error diffusion
    DITHER_STUCKI = 5          // Stucki error diffusion
};

enum ArtSource {
    ART_SRC_EMBEDDED_FIRST = 0,// Embedded ID3/FLAC/M4A first, then folder art
    ART_SRC_FOLDER_FIRST = 1,  // Folder cover.jpg/folder.jpg first, then embedded
    ART_SRC_DISABLED = 2       // Disable album art decoding
};

enum LyricsFont {
    LYRICS_FONT_CLASSIC     = 0,  // Clean standard monochrome
    LYRICS_FONT_CUTE        = 1,  // Cute Japanese pixel art & baby
    LYRICS_FONT_CHIPTUNE    = 2,  // 8-bit demoparty & chiptune
    LYRICS_FONT_TYPEWRITER  = 3,  // Retro typewriter monospace
    LYRICS_FONT_ARCADE      = 4,  // 80s arcade gaming
    LYRICS_FONT_PIXIE       = 5,  // Fairytale tiny pixie
    LYRICS_FONT_HINDI       = 6   // Devanagari Hindi Unifont
};

enum LyricsSize {
    LYRICS_SIZE_SMALL       = 0,  // Compact / small text (more characters per line)
    LYRICS_SIZE_MEDIUM      = 1,  // Medium / standard text (default)
    LYRICS_SIZE_LARGE       = 2   // Large / bold prominent text
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

    // Art image adjustments
    int8_t getArtBrightness() const;
    void setArtBrightness(int8_t val);
    int8_t getArtContrast() const;
    void setArtContrast(int8_t val);
    uint8_t getArtThreshold() const;
    void setArtThreshold(uint8_t val);
    static constexpr uint8_t DITHER_COUNT = 6;

    ArtSource getArtSource() const;
    void setArtSource(ArtSource src);
    void cycleArtSource();
    const char* getArtSourceName() const;

    bool isLyricsAutoScroll() const;
    void setLyricsAutoScroll(bool enable);
    void toggleLyricsAutoScroll();
    const char* getLyricsAutoScrollName() const;

    LyricsFont getLyricsFont() const;
    void setLyricsFont(LyricsFont font);
    void cycleLyricsFont();
    const char* getLyricsFontName() const;
    static constexpr uint8_t LYRICS_FONT_COUNT = 7;

    LyricsSize getLyricsSize() const;
    void setLyricsSize(LyricsSize size);
    void cycleLyricsSize();
    const char* getLyricsSizeName() const;
    static constexpr uint8_t LYRICS_SIZE_COUNT = 3;

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

    // Volume & Output / Playback Preferences
    uint8_t getSpeakerVolume() const;
    void setSpeakerVolume(uint8_t vol);
    uint8_t getFiioVolume() const;
    void setFiioVolume(uint8_t vol);
    uint8_t getPlaybackMode() const;
    void setPlaybackMode(uint8_t mode);
    uint8_t getOutputMode() const;
    void setOutputMode(uint8_t mode);

private:
    PlayerLayout layout;
    DitherMode dither;
    int8_t artBrightness; // -128..+127, default 0
    int8_t artContrast;   // -128..+127, default 0
    uint8_t artThreshold; // 0..255, default 128
    ArtSource artSource;
    bool lyricsAutoScroll;
    LyricsFont lyricsFont;
    LyricsSize lyricsSize;
    bool eqEnabled;
    uint8_t eqPreset;
    int8_t eqBands[5];
    uint8_t crossfeedMode;
    uint8_t speakerVolume; // 0..100, default 80
    uint8_t fiioVolume;    // 0..100, default 10 (Ear Safety Protection!)
    uint8_t playbackMode;  // 0..3, default 0 (PLAY_MODE_ALL)
    uint8_t outputMode;    // 0..1, default 0 (OUTPUT_MODE_SPEAKER_I2S)
    Preferences prefs;
};

extern PlayerConfig playerConfig;

#endif // PLAYER_CONFIG_H
