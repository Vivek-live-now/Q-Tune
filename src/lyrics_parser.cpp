#include "lyrics_parser.h"
#include "sd_manager.h"
#include <SD.h>

extern SemaphoreHandle_t spiBusMutex;

LyricsParser::LyricsParser() : loadedPath("") {}

void LyricsParser::clear() {
    lines.clear();
    loadedPath = "";
}

bool LyricsParser::hasLyrics() const {
    return !lines.empty();
}

size_t LyricsParser::getLineCount() const {
    return lines.size();
}

bool LyricsParser::parseLRCLine(const String &line, uint32_t &timestampMs, String &text) {
    // Format: [mm:ss.xx] or [mm:ss] followed by text
    if (!line.startsWith("[") || line.length() < 7) return false;

    int closeBracket = line.indexOf(']');
    if (closeBracket <= 0) return false;

    String tag = line.substring(1, closeBracket);
    int colon = tag.indexOf(':');
    if (colon <= 0) return false;

    int mm = tag.substring(0, colon).toInt();
    String secPart = tag.substring(colon + 1);

    int dot = secPart.indexOf('.');
    int ss = 0;
    int ms = 0;
    if (dot > 0) {
        ss = secPart.substring(0, dot).toInt();
        String msStr = secPart.substring(dot + 1);
        if (msStr.length() == 1) ms = msStr.toInt() * 100;
        else if (msStr.length() == 2) ms = msStr.toInt() * 10;
        else ms = msStr.substring(0, 3).toInt();
    } else {
        ss = secPart.toInt();
        ms = 0;
    }

    timestampMs = (mm * 60 + ss) * 1000 + ms;
    text = line.substring(closeBracket + 1);
    text.trim();
    return true;
}

bool LyricsParser::loadForTrack(const String &audioPath) {
    if (audioPath.length() == 0) {
        clear();
        return false;
    }

    // Determine .lrc filename
    int dot = audioPath.lastIndexOf('.');
    if (dot <= 0) return false;
    String lrcPath = audioPath.substring(0, dot) + ".lrc";

    if (loadedPath == lrcPath && !lines.empty()) {
        return true; // Already loaded
    }

    clear();
    loadedPath = lrcPath;

    if (!sdManager.isMounted()) return false;

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    if (!SD.exists(lrcPath)) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    File f = SD.open(lrcPath, FILE_READ);
    if (!f) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        uint32_t ts = 0;
        String lyricText = "";
        if (parseLRCLine(line, ts, lyricText)) {
            LyricLine item;
            item.timestampMs = ts;
            item.text = lyricText;
            lines.push_back(item);
        }
    }
    f.close();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    return !lines.empty();
}

int LyricsParser::getCurrentLineIndex(uint32_t positionMs) const {
    if (lines.empty()) return -1;
    if (positionMs < lines[0].timestampMs) return 0;

    int lastMatch = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].timestampMs <= positionMs) {
            lastMatch = (int)i;
        } else {
            break;
        }
    }
    return lastMatch;
}

String LyricsParser::getLineText(int index) const {
    if (index >= 0 && index < (int)lines.size()) {
        return lines[index].text;
    }
    return "";
}

uint32_t LyricsParser::getLineTimestamp(int index) const {
    if (index >= 0 && index < (int)lines.size()) {
        return lines[index].timestampMs;
    }
    return 0;
}

LyricsParser lyricsParser;
