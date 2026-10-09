#ifndef LYRICS_PARSER_H
#define LYRICS_PARSER_H

#include <Arduino.h>
#include <vector>

struct LyricLine {
    uint32_t timestampMs;
    String text;
};

class LyricsParser {
public:
    LyricsParser();
    bool loadForTrack(const String &audioPath);
    void clear();
    bool hasLyrics() const;
    size_t getLineCount() const;
    int getCurrentLineIndex(uint32_t positionMs) const;
    String getLineText(int index) const;
    uint32_t getLineTimestamp(int index) const;

private:
    std::vector<LyricLine> lines;
    String loadedPath;
    bool parseLRCLine(const String &line, uint32_t &timestampMs, String &text);
};

extern LyricsParser lyricsParser;

#endif // LYRICS_PARSER_H
