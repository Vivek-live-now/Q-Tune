#ifndef LYRICS_ID3_CODEC_H
#define LYRICS_ID3_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace LyricsID3Codec {

struct Line {
    uint32_t timestampMs;
    std::string text;
};

inline void appendUtf8(std::string &out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

inline std::string decodeString(const uint8_t *data, size_t len, uint8_t encoding) {
    std::string out;
    if (!data || len == 0 || encoding > 3) return out;

    if (encoding == 0) {
        for (size_t i = 0; i < len && data[i] != 0; ++i) {
            appendUtf8(out, data[i]);
        }
        return out;
    }
    if (encoding == 3) {
        for (size_t i = 0; i < len && data[i] != 0; ++i) {
            out.push_back(static_cast<char>(data[i]));
        }
        return out;
    }

    bool bigEndian = encoding == 2;
    size_t i = 0;
    if (encoding == 1) {
        if (len < 2) return out;
        if (data[0] == 0xFE && data[1] == 0xFF) {
            bigEndian = true;
            i = 2;
        } else if (data[0] == 0xFF && data[1] == 0xFE) {
            bigEndian = false;
            i = 2;
        } // Existing files without a per-string BOM use little-endian UTF-16.
    }

    while (i + 1 < len) {
        uint16_t unit = bigEndian
            ? static_cast<uint16_t>((data[i] << 8) | data[i + 1])
            : static_cast<uint16_t>((data[i + 1] << 8) | data[i]);
        i += 2;
        if (unit == 0) break;

        uint32_t cp = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 1 >= len) { appendUtf8(out, 0xFFFD); break; }
            uint16_t low = bigEndian
                ? static_cast<uint16_t>((data[i] << 8) | data[i + 1])
                : static_cast<uint16_t>((data[i + 1] << 8) | data[i]);
            if (low < 0xDC00 || low > 0xDFFF) {
                appendUtf8(out, 0xFFFD);
                continue;
            }
            i += 2;
            cp = 0x10000u + ((static_cast<uint32_t>(unit) - 0xD800u) << 10)
                 + (static_cast<uint32_t>(low) - 0xDC00u);
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            appendUtf8(out, 0xFFFD);
            continue;
        }
        appendUtf8(out, cp);
    }
    return out;
}

inline bool parseSYLT(const uint8_t *data, size_t size, std::vector<Line> &outLines) {
    outLines.clear();
    if (!data || size < 7) return false;

    const uint8_t encoding = data[0];
    const uint8_t timeFormat = data[4];
    if (encoding > 3 || (timeFormat != 1 && timeFormat != 2)) return false;

    size_t idx = 6; // encoding, language, time format, content type
    bool descriptorTerminated = false;
    if (encoding == 1 || encoding == 2) {
        while (idx + 1 < size) {
            if (data[idx] == 0 && data[idx + 1] == 0) {
                idx += 2;
                descriptorTerminated = true;
                break;
            }
            idx += 2;
        }
    } else {
        while (idx < size) {
            if (data[idx++] == 0) {
                descriptorTerminated = true;
                break;
            }
        }
    }
    if (!descriptorTerminated) return false;

    while (idx < size) {
        if (outLines.size() >= 300) return false;
        const size_t textStart = idx;
        size_t textEnd = idx;
        if (encoding == 1 || encoding == 2) {
            while (idx + 1 < size && !(data[idx] == 0 && data[idx + 1] == 0)) idx += 2;
            if (idx + 1 >= size) { outLines.clear(); return false; }
            textEnd = idx;
            idx += 2;
        } else {
            while (idx < size && data[idx] != 0) ++idx;
            if (idx >= size) { outLines.clear(); return false; }
            textEnd = idx++;
        }

        if (idx + 4 > size) { outLines.clear(); return false; }
        const uint32_t rawTimestamp =
            (static_cast<uint32_t>(data[idx]) << 24) |
            (static_cast<uint32_t>(data[idx + 1]) << 16) |
            (static_cast<uint32_t>(data[idx + 2]) << 8) |
            static_cast<uint32_t>(data[idx + 3]);
        idx += 4;

        uint64_t timestampMs = rawTimestamp;
        if (timeFormat == 1) timestampMs = (static_cast<uint64_t>(rawTimestamp) * 1000ULL) / 38ULL;
        if (timestampMs > UINT32_MAX) { outLines.clear(); return false; }

        Line line;
        line.timestampMs = static_cast<uint32_t>(timestampMs);
        line.text = decodeString(data + textStart, textEnd - textStart, encoding);
        outLines.push_back(line);
    }
    return !outLines.empty();
}

} // namespace LyricsID3Codec

#endif
