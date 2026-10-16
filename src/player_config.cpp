#include "player_config.h"
#include "audio_dsp.h"

PlayerConfig::PlayerConfig() :
    layout(LAYOUT_AUTO),
    dither(DITHER_ATKINSON),
    artBrightness(0),
    artContrast(0),
    artThreshold(128),
    artSource(ART_SRC_EMBEDDED_FIRST),
    lyricsAutoScroll(true),
    lyricsFont(LYRICS_FONT_CLASSIC),
    eqEnabled(true),
    eqPreset(0),
    crossfeedMode(0),
    speakerVolume(80),
    fiioVolume(10), // Ear protection safe default
    playbackMode(0),
    outputMode(0) {
    for (int b = 0; b < 5; b++) eqBands[b] = 0;
}

void PlayerConfig::begin() {
    prefs.begin("qtune_cfg", false);
    layout = (PlayerLayout)prefs.getUChar("layout", (uint8_t)LAYOUT_AUTO);
    dither = (DitherMode)prefs.getUChar("dither", (uint8_t)DITHER_ATKINSON);
    artBrightness = (int8_t)prefs.getChar("art_bri", 0);
    artContrast   = (int8_t)prefs.getChar("art_con", 0);
    artThreshold  = prefs.getUChar("art_thr", 128);
    artSource = (ArtSource)prefs.getUChar("art_src", (uint8_t)ART_SRC_EMBEDDED_FIRST);
    lyricsAutoScroll = prefs.getBool("lrc_scroll", true);
    lyricsFont = (LyricsFont)prefs.getUChar("lrc_font", (uint8_t)LYRICS_FONT_CLASSIC);
    lyricsSize = (LyricsSize)prefs.getUChar("lrc_size", (uint8_t)LYRICS_SIZE_MEDIUM);
    eqEnabled = prefs.getBool("eq_en", true);
    eqPreset = prefs.getUChar("eq_pre", 0);
    crossfeedMode = prefs.getUChar("xfeed", 0);
    speakerVolume = prefs.getUChar("vol_spk", 80);
    fiioVolume    = prefs.getUChar("vol_fiio", 10);
    playbackMode  = prefs.getUChar("play_mode", 0);
    outputMode    = prefs.getUChar("out_mode", 0);
    for (int b = 0; b < 5; b++) {
        char key[8];
        snprintf(key, sizeof(key), "eq_b%d", b);
        eqBands[b] = (int8_t)prefs.getChar(key, 0);
    }
    prefs.end();

    audioDSP.setEqEnabled(eqEnabled);
    if (eqPreset == 8) {
        for (uint8_t b = 0; b < 5; b++) {
            audioDSP.setBandGain(b, eqBands[b]);
        }
    } else {
        audioDSP.setPreset((EqPreset)eqPreset);
    }
    audioDSP.setCrossfeedMode((CrossfeedMode)crossfeedMode);
}

void PlayerConfig::save() {
    prefs.begin("qtune_cfg", false);
    prefs.putUChar("layout", (uint8_t)layout);
    prefs.putUChar("dither", (uint8_t)dither);
    prefs.putChar("art_bri", artBrightness);
    prefs.putChar("art_con", artContrast);
    prefs.putUChar("art_thr", artThreshold);
    prefs.putUChar("art_src", (uint8_t)artSource);
    prefs.putBool("lrc_scroll", lyricsAutoScroll);
    prefs.putUChar("lrc_font", (uint8_t)lyricsFont);
    prefs.putUChar("lrc_size", (uint8_t)lyricsSize);
    prefs.putBool("eq_en", eqEnabled);
    prefs.putUChar("eq_pre", eqPreset);
    prefs.putUChar("xfeed", crossfeedMode);
    prefs.putUChar("vol_spk", speakerVolume);
    prefs.putUChar("vol_fiio", fiioVolume);
    prefs.putUChar("play_mode", playbackMode);
    prefs.putUChar("out_mode", outputMode);
    for (int b = 0; b < 5; b++) {
        char key[8];
        snprintf(key, sizeof(key), "eq_b%d", b);
        prefs.putChar(key, eqBands[b]);
    }
    prefs.end();
}

PlayerLayout PlayerConfig::getLayout() const { return layout; }

void PlayerConfig::setLayout(PlayerLayout l) {
    layout = l;
    save();
}

void PlayerConfig::cycleLayout() {
    layout = (PlayerLayout)((layout + 1) % 4);
    save();
}

const char* PlayerConfig::getLayoutName() const {
    switch (layout) {
        case LAYOUT_AUTO:         return "AUTO";
        case LAYOUT_SPLIT_ART:    return "SPLIT ART";
        case LAYOUT_SPECTRUM_HUD: return "SPECTRUM";
        case LAYOUT_COVER_HERO:   return "COVER HERO";
        default:                  return "AUTO";
    }
}

DitherMode PlayerConfig::getDitherMode() const { return dither; }

void PlayerConfig::setDitherMode(DitherMode m) {
    dither = m;
    save();
}

void PlayerConfig::cycleDitherMode() {
    dither = (DitherMode)((dither + 1) % DITHER_COUNT);
    save();
}

const char* PlayerConfig::getDitherName() const {
    switch (dither) {
        case DITHER_ATKINSON:       return "ATKINSON";
        case DITHER_FLOYD_STEINBERG:return "FLOYD-ST";
        case DITHER_THRESHOLD:      return "THRESHOLD";
        case DITHER_BAYER4:         return "BAYER 4X4";
        case DITHER_SIERRA_LITE:    return "SIERRA";
        case DITHER_STUCKI:         return "STUCKI";
        default:                    return "ATKINSON";
    }
}

ArtSource PlayerConfig::getArtSource() const { return artSource; }

void PlayerConfig::setArtSource(ArtSource s) {
    artSource = s;
    save();
}

void PlayerConfig::cycleArtSource() {
    artSource = (ArtSource)((artSource + 1) % 3);
    save();
}

const char* PlayerConfig::getArtSourceName() const {
    switch (artSource) {
        case ART_SRC_EMBEDDED_FIRST: return "EMBEDDED";
        case ART_SRC_FOLDER_FIRST:   return "FOLDER";
        case ART_SRC_DISABLED:       return "OFF";
        default:                     return "EMBEDDED";
    }
}

bool PlayerConfig::isLyricsAutoScroll() const { return lyricsAutoScroll; }

void PlayerConfig::setLyricsAutoScroll(bool enable) {
    lyricsAutoScroll = enable;
    save();
}

void PlayerConfig::toggleLyricsAutoScroll() {
    lyricsAutoScroll = !lyricsAutoScroll;
    save();
}

const char* PlayerConfig::getLyricsAutoScrollName() const {
    return lyricsAutoScroll ? "AUTO" : "MANUAL";
}

bool PlayerConfig::isEqEnabled() const { return eqEnabled; }
void PlayerConfig::setEqEnabled(bool enable) {
    eqEnabled = enable;
    audioDSP.setEqEnabled(enable);
    save();
}
void PlayerConfig::toggleEq() {
    setEqEnabled(!eqEnabled);
}

uint8_t PlayerConfig::getEqPreset() const { return eqPreset; }
void PlayerConfig::setEqPreset(uint8_t preset) {
    eqPreset = preset;
    audioDSP.setPreset((EqPreset)preset);
    for (uint8_t b = 0; b < 5; b++) {
        eqBands[b] = audioDSP.getBandGain(b);
    }
    save();
}
void PlayerConfig::cycleEqPreset() {
    uint8_t next = (eqPreset + 1) % 9;
    setEqPreset(next);
}
const char* PlayerConfig::getEqPresetName() const {
    return audioDSP.getPresetName();
}

int8_t PlayerConfig::getEqBand(uint8_t band) const {
    return audioDSP.getBandGain(band);
}
void PlayerConfig::setEqBand(uint8_t band, int8_t gain) {
    if (band < 5) {
        eqBands[band] = gain;
        audioDSP.setBandGain(band, gain);
        eqPreset = 8; // CUSTOM
        save();
    }
}

uint8_t PlayerConfig::getCrossfeedMode() const { return crossfeedMode; }
void PlayerConfig::setCrossfeedMode(uint8_t mode) {
    crossfeedMode = mode;
    audioDSP.setCrossfeedMode((CrossfeedMode)mode);
    save();
}
void PlayerConfig::cycleCrossfeedMode() {
    uint8_t next = (crossfeedMode + 1) % 3;
    setCrossfeedMode(next);
}
const char* PlayerConfig::getCrossfeedModeName() const {
    return audioDSP.getCrossfeedModeName();
}

LyricsFont PlayerConfig::getLyricsFont() const { return lyricsFont; }

void PlayerConfig::setLyricsFont(LyricsFont font) {
    lyricsFont = font;
    save();
}

void PlayerConfig::cycleLyricsFont() {
    lyricsFont = (LyricsFont)((lyricsFont + 1) % LYRICS_FONT_COUNT);
    save();
}

const char* PlayerConfig::getLyricsFontName() const {
    switch (lyricsFont) {
        case LYRICS_FONT_CLASSIC:    return "CLASSIC";
        case LYRICS_FONT_CUTE:       return "CUTE";
        case LYRICS_FONT_CHIPTUNE:   return "CHIPTUNE";
        case LYRICS_FONT_TYPEWRITER: return "TYPEWRITER";
        case LYRICS_FONT_ARCADE:     return "ARCADE";
        case LYRICS_FONT_PIXIE:      return "PIXIE";
        case LYRICS_FONT_HINDI:      return "HINDI";
        default:                     return "CLASSIC";
    }
}

// --- LyricsSize methods ---
LyricsSize PlayerConfig::getLyricsSize() const { return lyricsSize; }

void PlayerConfig::setLyricsSize(LyricsSize size) {
    lyricsSize = size;
    save();
}

void PlayerConfig::cycleLyricsSize() {
    lyricsSize = (LyricsSize)((lyricsSize + 1) % LYRICS_SIZE_COUNT);
    save();
}

const char* PlayerConfig::getLyricsSizeName() const {
    switch (lyricsSize) {
        case LYRICS_SIZE_SMALL:  return "SMALL";
        case LYRICS_SIZE_MEDIUM: return "MEDIUM";
        case LYRICS_SIZE_LARGE:  return "LARGE";
        default:                 return "MEDIUM";
    }
}

int8_t PlayerConfig::getArtBrightness() const { return artBrightness; }
void PlayerConfig::setArtBrightness(int8_t val) {
    artBrightness = (val < -100) ? -100 : (val > 100) ? 100 : val;
    save();
}

int8_t PlayerConfig::getArtContrast() const { return artContrast; }
void PlayerConfig::setArtContrast(int8_t val) {
    artContrast = (val < -100) ? -100 : (val > 100) ? 100 : val;
    save();
}

uint8_t PlayerConfig::getArtThreshold() const { return artThreshold; }
void PlayerConfig::setArtThreshold(uint8_t val) {
    artThreshold = val;
    save();
}

uint8_t PlayerConfig::getSpeakerVolume() const { return speakerVolume; }
void PlayerConfig::setSpeakerVolume(uint8_t vol) {
    if (vol > 100) vol = 100;
    if (speakerVolume != vol) {
        speakerVolume = vol;
        save();
    }
}

uint8_t PlayerConfig::getFiioVolume() const { return fiioVolume; }
void PlayerConfig::setFiioVolume(uint8_t vol) {
    if (vol > 100) vol = 100;
    if (fiioVolume != vol) {
        fiioVolume = vol;
        save();
    }
}

uint8_t PlayerConfig::getPlaybackMode() const { return playbackMode; }
void PlayerConfig::setPlaybackMode(uint8_t mode) {
    if (mode > 3) mode = 0;
    if (playbackMode != mode) {
        playbackMode = mode;
        save();
    }
}

uint8_t PlayerConfig::getOutputMode() const { return outputMode; }
void PlayerConfig::setOutputMode(uint8_t mode) {
    if (mode > 1) mode = 0;
    if (outputMode != mode) {
        outputMode = mode;
        save();
    }
}

PlayerConfig playerConfig;
