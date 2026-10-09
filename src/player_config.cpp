#include "player_config.h"

PlayerConfig::PlayerConfig() :
    layout(LAYOUT_AUTO),
    dither(DITHER_ATKINSON),
    artSource(ART_SRC_EMBEDDED_FIRST),
    lyricsAutoScroll(true) {}

void PlayerConfig::begin() {
    prefs.begin("qtune_cfg", false);
    layout = (PlayerLayout)prefs.getUChar("layout", (uint8_t)LAYOUT_AUTO);
    dither = (DitherMode)prefs.getUChar("dither", (uint8_t)DITHER_ATKINSON);
    artSource = (ArtSource)prefs.getUChar("art_src", (uint8_t)ART_SRC_EMBEDDED_FIRST);
    lyricsAutoScroll = prefs.getBool("lrc_scroll", true);
    prefs.end();
}

void PlayerConfig::save() {
    prefs.begin("qtune_cfg", false);
    prefs.putUChar("layout", (uint8_t)layout);
    prefs.putUChar("dither", (uint8_t)dither);
    prefs.putUChar("art_src", (uint8_t)artSource);
    prefs.putBool("lrc_scroll", lyricsAutoScroll);
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
    dither = (DitherMode)((dither + 1) % 3);
    save();
}

const char* PlayerConfig::getDitherName() const {
    switch (dither) {
        case DITHER_ATKINSON:       return "ATKINSON";
        case DITHER_FLOYD_STEINBERG:return "FLOYD-ST";
        case DITHER_THRESHOLD:      return "THRESHOLD";
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

PlayerConfig playerConfig;
