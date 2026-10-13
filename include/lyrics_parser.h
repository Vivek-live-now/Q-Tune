#ifndef LYRICS_PARSER_H
#define LYRICS_PARSER_H

#include <Arduino.h>
#include <SD.h>
#include <vector>

// Lyrics Synchronization Classification
enum LyricsSyncType {
    LYRICS_SYNC_NONE = 0,    // Missing or invalid lyrics
    LYRICS_SYNC_UNTIMED = 1, // Plain text lyrics without timestamps
    LYRICS_SYNC_TIMED = 2    // Synchronized lyrics with valid timestamps
};

// Lyrics Source Priority Policy
enum LyricsSourcePriority {
    LYRICS_SRC_NONE = 0,
    LYRICS_SRC_EMB_UNSYNC = 1, // Embedded Unsynchronized (USLT, Vorbis, M4A)
    LYRICS_SRC_EXT_UNSYNC = 2, // External .txt or untimed .lrc
    LYRICS_SRC_EMB_SYNC = 3,   // Embedded Synchronized (SYLT)
    LYRICS_SRC_EXT_SYNC = 4    // External .lrc with timestamps (highest priority)
};

// Lyric Line Data Model
struct LyricLine {
    uint32_t timestampMs; // Timestamp in milliseconds (0 for untimed)
    String text;          // Lyric line UTF-8 text string
};

// Memory & Safety Bounds
constexpr size_t MAX_LYRIC_LINES = 300;
constexpr size_t MAX_LINE_CODE_POINTS = 128;
constexpr size_t MAX_LYRIC_FILE_SIZE = 128 * 1024; // 128 KB max file limit

class LyricsParser {
public:
    LyricsParser();

    // Reset and clear loaded lyrics state
    void clear();

    // Load and normalize lyrics for an audio track path
    bool loadForTrack(const String &audioPath);

    // Queries
    bool hasLyrics() const;
    const String& getLoadedAudioPath() const { return loadedAudioPath; }
    LyricsSyncType getSyncType() const;
    LyricsSourcePriority getSourcePriority() const;
    bool isSynchronized() const;
    size_t getLineCount() const;

    // Line and Timestamp Retrieval
    int getCurrentLineIndex(uint32_t positionMs) const;
    String getLineText(int index) const;
    uint32_t getLineTimestamp(int index) const;

    // UTF-8 Helper Functions
    static size_t countUtf8CodePoints(const String &str);
    static String truncateUtf8CodePoints(const String &str, size_t maxCodePoints);

    // ID3 Encoding Helper
    static String decodeID3String(const uint8_t *data, size_t len, uint8_t encoding);

    // Embedded Lyrics Extraction Helpers (Inspects audio file containers)
    static bool extractEmbeddedLyrics(File &file, const String &audioPath, String &outLyricsText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority, std::vector<LyricLine> &outSyltLines);

    // Format Content Parsers
    bool parseLRCContent(const String &content);
    bool parseTXTContent(const String &content);
    static bool parseLRCLine(const String &line, uint32_t &timestampMs, String &text);

private:
    std::vector<LyricLine> lines;
    String loadedAudioPath;
    LyricsSyncType syncType;
    LyricsSourcePriority sourcePriority;

    // Embedded Extractor Helpers
    static bool extractID3v2Lyrics(File &file, uint32_t tagOffset, uint32_t tagSize, uint8_t ver, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority, std::vector<LyricLine> &outSyltLines);
    static bool extractMP3Lyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority, std::vector<LyricLine> &outSyltLines);
    static bool extractWAVLyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority, std::vector<LyricLine> &outSyltLines);
    static bool extractFLACLyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority);
    static bool extractM4ALyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority);

    // ID3 SYLT Binary Parser
    static bool parseID3SYLT(const uint8_t *data, size_t size, std::vector<LyricLine> &outSyltLines);
};

extern LyricsParser lyricsParser;

#endif // LYRICS_PARSER_H
