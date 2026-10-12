#include "lyrics_parser.h"
#include "sd_manager.h"
#include <SD.h>

extern SemaphoreHandle_t spiBusMutex;

LyricsParser::LyricsParser() :
    loadedAudioPath(""),
    syncType(LYRICS_SYNC_NONE),
    sourcePriority(LYRICS_SRC_NONE) {}

void LyricsParser::clear() {
    lines.clear();
    loadedAudioPath = "";
    syncType = LYRICS_SYNC_NONE;
    sourcePriority = LYRICS_SRC_NONE;
}

bool LyricsParser::hasLyrics() const {
    return !lines.empty() && syncType != LYRICS_SYNC_NONE;
}

LyricsSyncType LyricsParser::getSyncType() const {
    return syncType;
}

LyricsSourcePriority LyricsParser::getSourcePriority() const {
    return sourcePriority;
}

bool LyricsParser::isSynchronized() const {
    return syncType == LYRICS_SYNC_TIMED;
}

size_t LyricsParser::getLineCount() const {
    return lines.size();
}

size_t LyricsParser::countUtf8CodePoints(const String &str) {
    size_t count = 0;
    const char *ptr = str.c_str();
    while (*ptr) {
        uint8_t c = (uint8_t)(*ptr);
        if ((c & 0xC0) != 0x80) { // Start byte of UTF-8 character
            count++;
        }
        ptr++;
    }
    return count;
}

String LyricsParser::truncateUtf8CodePoints(const String &str, size_t maxCodePoints) {
    if (maxCodePoints == 0) return "";
    size_t codePoints = 0;
    const char *start = str.c_str();
    const char *ptr = start;

    while (*ptr) {
        uint8_t c = (uint8_t)(*ptr);
        if ((c & 0xC0) != 0x80) {
            if (codePoints >= maxCodePoints) {
                break;
            }
            codePoints++;
        }
        ptr++;
    }

    return str.substring(0, (unsigned int)(ptr - start));
}

bool LyricsParser::parseLRCLine(const String &line, uint32_t &timestampMs, String &text) {
    if (!line.startsWith("[")) return false;

    int closeBracket = line.indexOf(']');
    if (closeBracket <= 1) return false;

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

bool LyricsParser::parseLRCContent(const String &content) {
    lines.clear();
    int start = 0;
    int len = content.length();
    bool foundTimedLine = false;

    while (start < len && lines.size() < MAX_LYRIC_LINES) {
        int nextNewline = content.indexOf('\n', start);
        if (nextNewline < 0) nextNewline = len;

        String rawLine = content.substring(start, nextNewline);
        rawLine.trim();
        start = nextNewline + 1;

        if (rawLine.length() == 0) continue;

        uint32_t ts = 0;
        String text = "";
        if (parseLRCLine(rawLine, ts, text)) {
            LyricLine item;
            item.timestampMs = ts;
            item.text = truncateUtf8CodePoints(text, MAX_LINE_CODE_POINTS);
            lines.push_back(item);
            foundTimedLine = true;
        } else if (!rawLine.startsWith("[")) { // Plain line without timestamp bracket
            LyricLine item;
            item.timestampMs = 0;
            item.text = truncateUtf8CodePoints(rawLine, MAX_LINE_CODE_POINTS);
            lines.push_back(item);
        }
    }

    if (lines.empty()) {
        syncType = LYRICS_SYNC_NONE;
        return false;
    }

    syncType = foundTimedLine ? LYRICS_SYNC_TIMED : LYRICS_SYNC_UNTIMED;
    return true;
}

bool LyricsParser::parseTXTContent(const String &content) {
    lines.clear();
    int start = 0;
    int len = content.length();

    while (start < len && lines.size() < MAX_LYRIC_LINES) {
        int nextNewline = content.indexOf('\n', start);
        if (nextNewline < 0) nextNewline = len;

        String rawLine = content.substring(start, nextNewline);
        rawLine.trim();
        start = nextNewline + 1;

        if (rawLine.length() == 0) continue;

        LyricLine item;
        item.timestampMs = 0;
        item.text = truncateUtf8CodePoints(rawLine, MAX_LINE_CODE_POINTS);
        lines.push_back(item);
    }

    if (lines.empty()) {
        syncType = LYRICS_SYNC_NONE;
        return false;
    }

    syncType = LYRICS_SYNC_UNTIMED;
    return true;
}

bool LyricsParser::loadForTrack(const String &audioPath) {
    if (audioPath.length() == 0) {
        clear();
        return false;
    }

    if (loadedAudioPath == audioPath && hasLyrics()) {
        return true; // Cached
    }

    clear();
    loadedAudioPath = audioPath;

    if (!sdManager.isMounted()) return false;

    int dot = audioPath.lastIndexOf('.');
    if (dot <= 0) return false;
    String basePath = audioPath.substring(0, dot);

    // 1. Priority 1: External .lrc file
    String lrcPath = basePath + ".lrc";
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    bool lrcExists = SD.exists(lrcPath);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    if (lrcExists) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        File f = SD.open(lrcPath, FILE_READ);
        String content = "";
        if (f) {
            size_t bytesToRead = min((size_t)f.size(), MAX_LYRIC_FILE_SIZE);
            content.reserve(bytesToRead);
            while (f.available() && content.length() < bytesToRead) {
                content += (char)f.read();
            }
            f.close();
        }
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

        if (content.length() > 0 && parseLRCContent(content)) {
            sourcePriority = (syncType == LYRICS_SYNC_TIMED) ? LYRICS_SRC_EXT_SYNC : LYRICS_SRC_EXT_UNSYNC;
            if (syncType == LYRICS_SYNC_TIMED) return true; // Highest priority found
        }
    }

    // Backup current external .lrc parse result if it was untimed
    std::vector<LyricLine> extUnsyncBackup = lines;
    LyricsSyncType extSyncBackup = syncType;

    // 2. Priority 2: Embedded Lyrics (SYLT / Vorbis / M4A)
    String embText = "";
    LyricsSyncType embSync = LYRICS_SYNC_NONE;
    LyricsSourcePriority embPri = LYRICS_SRC_NONE;

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    File audioFile = SD.open(audioPath, FILE_READ);
    if (audioFile) {
        extractEmbeddedLyrics(audioFile, audioPath, embText, embSync, embPri);
        audioFile.close();
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    if (embSync != LYRICS_SYNC_NONE && embText.length() > 0) {
        if (embSync == LYRICS_SYNC_TIMED) {
            if (parseLRCContent(embText)) {
                syncType = LYRICS_SYNC_TIMED;
                sourcePriority = LYRICS_SRC_EMB_SYNC;
                return true;
            }
        }
    }

    // 3. Priority 3: External .txt file or untimed .lrc
    if (!extUnsyncBackup.empty()) {
        lines = extUnsyncBackup;
        syncType = extSyncBackup;
        sourcePriority = LYRICS_SRC_EXT_UNSYNC;
        return true;
    }

    String txtPath = basePath + ".txt";
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    bool txtExists = SD.exists(txtPath);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    if (txtExists) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        File f = SD.open(txtPath, FILE_READ);
        String content = "";
        if (f) {
            size_t bytesToRead = min((size_t)f.size(), MAX_LYRIC_FILE_SIZE);
            content.reserve(bytesToRead);
            while (f.available() && content.length() < bytesToRead) {
                content += (char)f.read();
            }
            f.close();
        }
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

        if (content.length() > 0 && parseTXTContent(content)) {
            sourcePriority = LYRICS_SRC_EXT_UNSYNC;
            return true;
        }
    }

    // 4. Priority 4: Embedded Untimed Lyrics (USLT, Vorbis LYRICS, M4A ©lyr)
    if (embSync == LYRICS_SYNC_UNTIMED && embText.length() > 0) {
        if (parseTXTContent(embText)) {
            syncType = LYRICS_SYNC_UNTIMED;
            sourcePriority = LYRICS_SRC_EMB_UNSYNC;
            return true;
        }
    }

    clear();
    return false;
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

// ============================================================================
// Embedded Metadata Extraction Implementation
// ============================================================================

bool LyricsParser::extractEmbeddedLyrics(File &file, const String &audioPath, String &outLyricsText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority) {
    outLyricsText = "";
    outSync = LYRICS_SYNC_NONE;
    outPriority = LYRICS_SRC_NONE;

    String lower = audioPath;
    lower.toLowerCase();

    if (lower.endsWith(".mp3") || lower.endsWith(".wav")) {
        return extractMP3Lyrics(file, outLyricsText, outSync, outPriority);
    } else if (lower.endsWith(".flac")) {
        return extractFLACLyrics(file, outLyricsText, outSync, outPriority);
    } else if (lower.endsWith(".m4a") || lower.endsWith(".aac")) {
        return extractM4ALyrics(file, outLyricsText, outSync, outPriority);
    }

    return false;
}

bool LyricsParser::extractMP3Lyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority) {
    if (!file || file.size() < 128) return false;
    file.seek(0);

    uint8_t hdr[10];
    if (file.read(hdr, 10) != 10) return false;
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') return false;

    uint32_t tagSize = ((uint32_t)(hdr[6] & 0x7F) << 21) |
                       ((uint32_t)(hdr[7] & 0x7F) << 14) |
                       ((uint32_t)(hdr[8] & 0x7F) << 7)  |
                       (uint32_t)(hdr[9] & 0x7F);

    uint32_t tagEnd = 10 + tagSize;
    if (tagEnd > file.size()) tagEnd = file.size();

    uint8_t ver = hdr[3];

    while (file.position() + 10 < tagEnd) {
        uint8_t fHdr[10];
        if (file.read(fHdr, 10) != 10) break;

        char fId[5] = {0};
        memcpy(fId, fHdr, 4);
        if (fId[0] == 0) break;

        uint32_t fSize = 0;
        if (ver == 4) {
            fSize = ((uint32_t)(fHdr[4] & 0x7F) << 21) |
                    ((uint32_t)(fHdr[5] & 0x7F) << 14) |
                    ((uint32_t)(fHdr[6] & 0x7F) << 7)  |
                    (uint32_t)(fHdr[7] & 0x7F);
        } else {
            fSize = ((uint32_t)fHdr[4] << 24) | ((uint32_t)fHdr[5] << 16) | ((uint32_t)fHdr[6] << 8) | (uint32_t)fHdr[7];
        }

        if (fSize == 0 || file.position() + fSize > tagEnd) break;
        uint32_t frameStart = file.position();

        // Check for USLT (Unsynchronised lyrics/text transcription)
        if (strcmp(fId, "USLT") == 0 && fSize > 4) {
            uint8_t encoding = file.read();
            char lang[4] = {0};
            file.read((uint8_t*)lang, 3);

            // Skip descriptor until NULL
            while (file.position() < frameStart + fSize && file.read() != 0);

            size_t textLen = (frameStart + fSize) - file.position();
            if (textLen > 0 && textLen < MAX_LYRIC_FILE_SIZE) {
                outText.reserve(textLen);
                while (file.position() < frameStart + fSize) {
                    char c = (char)file.read();
                    outText += c;
                }
                outSync = LYRICS_SYNC_UNTIMED;
                outPriority = LYRICS_SRC_EMB_UNSYNC;
                return true;
            }
        }

        file.seek(frameStart + fSize);
    }
    return false;
}

bool LyricsParser::extractFLACLyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority) {
    if (!file || file.size() < 128) return false;
    file.seek(0);

    uint8_t magic[4];
    if (file.read(magic, 4) != 4) return false;
    if (memcmp(magic, "fLaC", 4) != 0) return false;

    bool isLast = false;
    while (!isLast && file.available() >= 4) {
        uint8_t bHdr[4];
        if (file.read(bHdr, 4) != 4) break;

        isLast = (bHdr[0] & 0x80) != 0;
        uint8_t blockType = bHdr[0] & 0x7F;
        uint32_t blockLen = ((uint32_t)bHdr[1] << 16) | ((uint32_t)bHdr[2] << 8) | (uint32_t)bHdr[3];

        uint32_t blockStart = file.position();

        if (blockType == 4) { // VORBIS_COMMENT
            // Vendor string length
            uint8_t lenBuf[4];
            if (file.read(lenBuf, 4) != 4) break;
            uint32_t vendorLen = lenBuf[0] | ((uint32_t)lenBuf[1] << 8) | ((uint32_t)lenBuf[2] << 16) | ((uint32_t)lenBuf[3] << 24);
            file.seek(file.position() + vendorLen);

            // User comment list length
            if (file.read(lenBuf, 4) != 4) break;
            uint32_t commentCount = lenBuf[0] | ((uint32_t)lenBuf[1] << 8) | ((uint32_t)lenBuf[2] << 16) | ((uint32_t)lenBuf[3] << 24);

            for (uint32_t i = 0; i < commentCount && file.position() < blockStart + blockLen; i++) {
                if (file.read(lenBuf, 4) != 4) break;
                uint32_t cLen = lenBuf[0] | ((uint32_t)lenBuf[1] << 8) | ((uint32_t)lenBuf[2] << 16) | ((uint32_t)lenBuf[3] << 24);

                if (cLen > 0 && cLen < MAX_LYRIC_FILE_SIZE) {
                    String comment = "";
                    comment.reserve(cLen);
                    for (uint32_t j = 0; j < cLen && file.available(); j++) {
                        comment += (char)file.read();
                    }

                    String upper = comment;
                    upper.toUpperCase();
                    if (upper.startsWith("LYRICS=") || upper.startsWith("UNSYNCEDLYRICS=")) {
                        int eq = comment.indexOf('=');
                        outText = comment.substring(eq + 1);
                        outSync = LYRICS_SYNC_UNTIMED;
                        outPriority = LYRICS_SRC_EMB_UNSYNC;
                        return true;
                    }
                } else {
                    file.seek(file.position() + cLen);
                }
            }
        }

        file.seek(blockStart + blockLen);
    }
    return false;
}

bool LyricsParser::extractM4ALyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority) {
    if (!file || file.size() < 128) return false;
    file.seek(0);

    while (file.available() >= 8) {
        uint32_t curPos = file.position();
        uint8_t aHdr[8];
        if (file.read(aHdr, 8) != 8) break;

        uint32_t aSize = ((uint32_t)aHdr[0] << 24) | ((uint32_t)aHdr[1] << 16) | ((uint32_t)aHdr[2] << 8) | (uint32_t)aHdr[3];
        char aType[5] = {0};
        memcpy(aType, &aHdr[4], 4);

        if (aSize < 8) break;

        if (strcmp(aType, "moov") == 0 || strcmp(aType, "udta") == 0 || strcmp(aType, "meta") == 0 || strcmp(aType, "ilst") == 0) {
            if (strcmp(aType, "meta") == 0) {
                file.seek(curPos + 12);
            }
            continue;
        } else if (strcmp(aType, "\xA9" "lyr") == 0 || strcmp(aType, "©lyr") == 0) {
            uint32_t atomEnd = curPos + aSize;
            while (file.position() + 8 <= atomEnd) {
                uint32_t subPos = file.position();
                uint8_t subHdr[8];
                if (file.read(subHdr, 8) != 8) break;
                uint32_t subSize = ((uint32_t)subHdr[0] << 24) | ((uint32_t)subHdr[1] << 16) | ((uint32_t)subHdr[2] << 8) | (uint32_t)subHdr[3];
                char subType[5] = {0};
                memcpy(subType, &subHdr[4], 4);

                if (strcmp(subType, "data") == 0) {
                    file.seek(subPos + 16); // Header + flags
                    size_t dataLen = (subSize > 16) ? (subSize - 16) : 0;
                    if (dataLen > 0 && dataLen < MAX_LYRIC_FILE_SIZE) {
                        outText.reserve(dataLen);
                        while (file.position() < subPos + subSize && file.available()) {
                            outText += (char)file.read();
                        }
                        outSync = LYRICS_SYNC_UNTIMED;
                        outPriority = LYRICS_SRC_EMB_UNSYNC;
                        return true;
                    }
                }
                file.seek(subPos + subSize);
            }
            file.seek(curPos + aSize);
        } else {
            file.seek(curPos + aSize);
        }
    }
    return false;
}

LyricsParser lyricsParser;
