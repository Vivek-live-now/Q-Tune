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

    // Load lyrics directly from a specified file path or embedded source
    bool loadFromFile(const String &filePath, LyricsSourcePriority priorityHint = LYRICS_SRC_NONE);

    // Queries
    bool hasLyrics() const;
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

    // Embedded Lyrics Extraction Helpers (Inspects audio file containers)
    static bool extractEmbeddedLyrics(File &file, const String &audioPath, String &outLyricsText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority);

private:
    std::vector<LyricLine> lines;
    String loadedAudioPath;
    LyricsSyncType syncType;
    LyricsSourcePriority sourcePriority;

    // Format Parsers
    bool parseLRCContent(const String &content);
    bool parseTXTContent(const String &content);
    static bool parseLRCLine(const String &line, uint32_t &timestampMs, String &text);

    // Embedded Extractor Helpers
    static bool extractMP3Lyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority);
    static bool extractFLACLyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority);
    static bool extractM4ALyrics(File &file, String &outText, LyricsSyncType &outSync, LyricsSourcePriority &outPriority);
};

extern LyricsParser lyricsParser;

#endif // LYRICS_PARSER_H
