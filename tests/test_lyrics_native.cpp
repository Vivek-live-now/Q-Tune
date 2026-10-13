#include "../include/lyrics_id3_codec.h"
#include "../include/fast_math.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using LyricsID3Codec::Line;

static void appendBE32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

static std::vector<uint8_t> makeSylt(const std::vector<std::pair<std::string, uint32_t>>& lines,
                                     uint8_t encoding = 0, uint8_t timeFormat = 2) {
    std::vector<uint8_t> frame = {encoding, 'e', 'n', 'g', timeFormat, 1, 0};
    for (const auto& item : lines) {
        frame.insert(frame.end(), item.first.begin(), item.first.end());
        frame.push_back(0);
        appendBE32(frame, item.second);
    }
    return frame;
}

int main() {
    std::cout << "=== Production Native Unit Tests (ID3 Codec & FastMath) ===\n";

    // Test the exact shared implementation used by src/lyrics_parser.cpp.
    const uint8_t latin1[] = {'C', 'a', 'f', 0xE9, 0};
    assert(LyricsID3Codec::decodeString(latin1, sizeof(latin1), 0) == "Caf\xC3\xA9");

    const uint8_t utf16le[] = {0xFF, 0xFE, 'H', 0, 'i', 0, 0, 0};
    assert(LyricsID3Codec::decodeString(utf16le, sizeof(utf16le), 1) == "Hi");

    // UTF-16 surrogate pair for U+1F600 GRINNING FACE.
    const uint8_t surrogate[] = {0xFE, 0xFF, 0xD8, 0x3D, 0xDE, 0x00};
    assert(LyricsID3Codec::decodeString(surrogate, sizeof(surrogate), 1) == "\xF0\x9F\x98\x80");
    assert(LyricsID3Codec::decodeString(utf16le, sizeof(utf16le), 9).empty());
    std::cout << "[PASS] Latin-1, UTF-16 BOM/endian, surrogate pair, invalid encoding\n";

    const auto valid = makeSylt({{"Verse 1", 5000}, {"Verse 2", 12000}});
    std::vector<Line> lines;
    assert(LyricsID3Codec::parseSYLT(valid.data(), valid.size(), lines));
    assert(lines.size() == 2);
    assert(lines[0].timestampMs == 5000 && lines[0].text == "Verse 1");
    assert(lines[1].timestampMs == 12000 && lines[1].text == "Verse 2");

    // Frame-based timestamps are converted using the parser's 38 fps assumption.
    const auto frames = makeSylt({{"Frame timed", 38}}, 0, 1);
    assert(LyricsID3Codec::parseSYLT(frames.data(), frames.size(), lines));
    assert(lines.size() == 1 && lines[0].timestampMs == 1000);

    // Reject invalid header, unterminated descriptor/text, invalid time format,
    // and a text record with a missing timestamp. Never return partial lines.
    assert(!LyricsID3Codec::parseSYLT(nullptr, 0, lines));
    const std::vector<uint8_t> badEncoding = {4, 'e', 'n', 'g', 2, 1, 0};
    assert(!LyricsID3Codec::parseSYLT(badEncoding.data(), badEncoding.size(), lines));
    const std::vector<uint8_t> badTimeFormat = {0, 'e', 'n', 'g', 0, 1, 0};
    assert(!LyricsID3Codec::parseSYLT(badTimeFormat.data(), badTimeFormat.size(), lines));
    const std::vector<uint8_t> unterminatedText = {0, 'e', 'n', 'g', 2, 1, 0, 'x', 'y'};
    assert(!LyricsID3Codec::parseSYLT(unterminatedText.data(), unterminatedText.size(), lines));
    auto truncatedTimestamp = makeSylt({{"short", 1}});
    truncatedTimestamp.resize(truncatedTimestamp.size() - 3);
    assert(!LyricsID3Codec::parseSYLT(truncatedTimestamp.data(), truncatedTimestamp.size(), lines));
    assert(lines.empty());
    std::cout << "[PASS] SYLT timestamps and malformed-frame rejection\n";

    // FastMath unit tests
    // 1. fast_sin and fast_cos precision (< 0.0001 error)
    constexpr float PI_F = 3.14159265358979323846f;
    assert(std::fabs(FastMath::fast_sin(0.0f) - 0.0f) < 0.0001f);
    assert(std::fabs(FastMath::fast_sin(PI_F * 0.5f) - 1.0f) < 0.0001f);
    assert(std::fabs(FastMath::fast_sin(PI_F) - 0.0f) < 0.0001f);
    assert(std::fabs(FastMath::fast_sin(PI_F * 1.5f) - (-1.0f)) < 0.0001f);
    assert(std::fabs(FastMath::fast_cos(0.0f) - 1.0f) < 0.0001f);
    assert(std::fabs(FastMath::fast_cos(PI_F * 0.5f) - 0.0f) < 0.0001f);
    assert(std::fabs(FastMath::fast_cos(PI_F) - (-1.0f)) < 0.0001f);

    // Negative angle & wrap-around
    assert(std::fabs(FastMath::fast_sin(-PI_F * 0.5f) - (-1.0f)) < 0.0001f);
    assert(std::fabs(FastMath::fast_cos(-PI_F) - (-1.0f)) < 0.0001f);
    assert(std::fabs(FastMath::fast_sin(10.0f * PI_F + PI_F * 0.5f) - 1.0f) < 0.0001f);

    // 2. fast_log10 approximation (< 0.003 max error)
    assert(FastMath::fast_log10(0.0f) == -100.0f);
    assert(FastMath::fast_log10(-5.0f) == -100.0f);
    assert(std::fabs(FastMath::fast_log10(1.0f) - 0.0f) < 0.003f);
    assert(std::fabs(FastMath::fast_log10(10.0f) - 1.0f) < 0.003f);
    assert(std::fabs(FastMath::fast_log10(100.0f) - 2.0f) < 0.003f);
    assert(std::fabs(FastMath::fast_log10(1000.0f) - 3.0f) < 0.003f);
    assert(std::fabs(FastMath::fast_log10(0.1f) - (-1.0f)) < 0.003f);

    // 3. fast_db20
    assert(std::fabs(FastMath::fast_db20(1.0f) - 0.0f) < 0.06f);
    assert(std::fabs(FastMath::fast_db20(0.1f) - (-20.0f)) < 0.06f);
    assert(std::fabs(FastMath::fast_db20(0.01f) - (-40.0f)) < 0.06f);

    // 4. fast_inv_sqrt
    assert(FastMath::fast_inv_sqrt(0.0f) == 0.0f);
    assert(FastMath::fast_inv_sqrt(-1.0f) == 0.0f);
    assert(std::fabs(FastMath::fast_inv_sqrt(4.0f) - 0.5f) < 0.001f);
    assert(std::fabs(FastMath::fast_inv_sqrt(16.0f) - 0.25f) < 0.001f);
    std::cout << "[PASS] FastMath LUT trig, log10, db20, and inv_sqrt precision verified\n";

    std::cout << "All production native tests passed.\n";
    return 0;
}
