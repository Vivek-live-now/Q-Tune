#ifndef METADATA_PARSER_H
#define METADATA_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <vector>

#ifdef ARDUINO
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include "hw_config.h"
#else
#include "gemini_client.h" // Provides the host String class
#include <string>
#endif

// ==============================================================================
// Unified Stream Abstraction for Microcontroller Files and Memory Fixtures
// ==============================================================================
class MetadataStream {
public:
    virtual ~MetadataStream() {}
    virtual size_t read(uint8_t *buf, size_t len) = 0;
    virtual int read() = 0;
    virtual bool seek(uint32_t pos) = 0;
    virtual uint32_t position() = 0;
    virtual uint32_t size() = 0;
    virtual uint32_t available() = 0;
};

#ifdef ARDUINO
class ArduinoFileStream : public MetadataStream {
    File &f;
public:
    ArduinoFileStream(File &file) : f(file) {}
    size_t read(uint8_t *buf, size_t len) override { return f.read(buf, len); }
    int read() override { return f.read(); }
    bool seek(uint32_t pos) override { return f.seek(pos); }
    uint32_t position() override { return f.position(); }
    uint32_t size() override { return f.size(); }
    uint32_t available() override { return f.available(); }
};
#endif

class MemoryStream : public MetadataStream {
    const uint8_t *data;
    uint32_t dataSize;
    uint32_t pos;
public:
    MemoryStream(const uint8_t *d, uint32_t s) : data(d), dataSize(s), pos(0) {}
    size_t read(uint8_t *buf, size_t len) override {
        if (pos >= dataSize) return 0;
        size_t toRead = (pos + len <= dataSize) ? len : (dataSize - pos);
        memcpy(buf, data + pos, toRead);
        pos += toRead;
        return toRead;
    }
    int read() override {
        if (pos >= dataSize) return -1;
        return data[pos++];
    }
    bool seek(uint32_t p) override {
        if (p > dataSize) return false;
        pos = p;
        return true;
    }
    uint32_t position() override { return pos; }
    uint32_t size() override { return dataSize; }
    uint32_t available() override { return (pos < dataSize) ? (dataSize - pos) : 0; }
};

// ==============================================================================
// Track Metadata Model (Title, Artist, Album, Year, Genre, Track Number, Comment)
// ==============================================================================
struct TrackMetadata {
    String title;
    String artist;
    String album;
    String year;
    String genre;
    String trackNumber;
    String comment;
    bool hasTags;

    TrackMetadata() : title(""), artist(""), album(""), year(""), genre(""), trackNumber(""), comment(""), hasTags(false) {}

    void clear() {
        title = "";
        artist = "";
        album = "";
        year = "";
        genre = "";
        trackNumber = "";
        comment = "";
        hasTags = false;
    }
};

// ==============================================================================
// Binary Persistent Metadata Index Specification ('QTMD' Spec v1, 256B Stride)
// ==============================================================================
constexpr uint32_t METADATA_INDEX_MAGIC = 0x51544D44; // 'QTMD'
constexpr uint16_t METADATA_INDEX_VERSION = 1;

struct MetadataIndexEntry {
    char audioPath[128];       // Track relative path (max 127 chars + NULL)
    uint32_t audioFileSize;    // File size for change detection
    uint32_t fileMTime;        // Last modification timestamp
    char title[36];            // Track Title (max 35 chars + NULL)
    char artist[28];           // Track Artist (max 27 chars + NULL)
    char album[28];            // Album Name (max 27 chars + NULL)
    char genre[16];            // Genre (max 15 chars + NULL)
    char year[6];              // Release Year (e.g. "1976")
    char trackNumber[4];       // Track (e.g. "01")
    uint8_t format;            // Audio format (1=WAV, 2=FLAC, 3=MP3, 4=M4A)
    uint8_t reserved[1];       // Alignment padding to 256 bytes
};
static_assert(sizeof(MetadataIndexEntry) == 256, "MetadataIndexEntry must be 256 bytes");

// ==============================================================================
// Metadata Parser Engine (Supports MP3, FLAC, M4A/AAC, WAV)
// ==============================================================================
class MetadataParser {
public:
    MetadataParser();

    void clear();
    bool loadForTrack(const String &audioPath);

    const TrackMetadata& getMetadata() const { return metadata; }
    const String& getLoadedAudioPath() const { return loadedAudioPath; }
    bool hasMetadata() const { return metadata.hasTags; }

    const String& getTitle() const { return metadata.title; }
    const String& getArtist() const { return metadata.artist; }
    const String& getAlbum() const { return metadata.album; }
    const String& getYear() const { return metadata.year; }
    const String& getGenre() const { return metadata.genre; }
    const String& getTrackNumber() const { return metadata.trackNumber; }
    const String& getComment() const { return metadata.comment; }

    // Unified Cross-Platform Stream Parsers
    static bool parseMP3(MetadataStream &stream, TrackMetadata &meta);
    static bool parseFLAC(MetadataStream &stream, TrackMetadata &meta);
    static bool parseM4A(MetadataStream &stream, TrackMetadata &meta);
    static bool parseWAV(MetadataStream &stream, TrackMetadata &meta);

    // ID3 Format Helpers
    static bool parseID3v2(MetadataStream &stream, uint32_t tagOffset, uint32_t tagSize, uint8_t ver, TrackMetadata &meta);
    static bool parseID3v1(MetadataStream &stream, TrackMetadata &meta);

#ifdef ARDUINO
    // Convenience Arduino File Overloads
    static bool parseMP3(File &file, TrackMetadata &meta) { ArduinoFileStream s(file); return parseMP3(s, meta); }
    static bool parseFLAC(File &file, TrackMetadata &meta) { ArduinoFileStream s(file); return parseFLAC(s, meta); }
    static bool parseM4A(File &file, TrackMetadata &meta) { ArduinoFileStream s(file); return parseM4A(s, meta); }
    static bool parseWAV(File &file, TrackMetadata &meta) { ArduinoFileStream s(file); return parseWAV(s, meta); }
#endif

    // Metadata Normalization & Genre Table
    static const char* getID3v1Genre(uint8_t id);
    static String cleanString(const String &str);
    static String normalizeGenre(const String &genre);

private:
    TrackMetadata metadata;
    String loadedAudioPath;
};

extern MetadataParser metadataParser;

#endif // METADATA_PARSER_H
