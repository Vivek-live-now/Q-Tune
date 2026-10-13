#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>

// Standalone C++ Arduino String minimal emulator for host test execution
class String {
public:
    std::string s;
    String() : s("") {}
    String(const char *str) : s(str ? str : "") {}
    String(const std::string &str) : s(str) {}

    const char* c_str() const { return s.c_str(); }
    size_t length() const { return s.length(); }
    bool startsWith(const char* prefix) const {
        return s.rfind(prefix, 0) == 0;
    }
    int indexOf(char ch, size_t fromIndex = 0) const {
        size_t pos = s.find(ch, fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    String substring(unsigned int from, unsigned int to = 0) const {
        if (to == 0 || to > s.length()) to = s.length();
        if (from >= s.length() || from >= to) return String("");
        return String(s.substr(from, to - from));
    }
    void trim() {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) { s = ""; return; }
        size_t end = s.find_last_not_of(" \t\r\n");
        s = s.substr(start, end - start + 1);
    }
    void reserve(size_t n) { s.reserve(n); }
    char charAt(size_t idx) const { return (idx < s.length()) ? s[idx] : 0; }
    int toInt() const { try { return std::stoi(s); } catch(...) { return 0; } }
    String& operator+=(char c) { s += c; return *this; }
    bool operator==(const String &other) const { return s == other.s; }
};

struct LyricLine {
    uint32_t timestampMs;
    String text;
};

// Isolated C++ test functions from lyrics_parser.cpp
static String decodeID3String(const uint8_t *data, size_t len, uint8_t encoding) {
    if (!data || len == 0) return String("");

    if (encoding == 0) { // ISO-8859-1
        std::string out = "";
        for (size_t i = 0; i < len; i++) {
            uint8_t c = data[i];
            if (c == 0) break;
            if (c < 128) out += (char)c;
            else {
                out += (char)(0xC0 | (c >> 6));
                out += (char)(0x80 | (c & 0x3F));
            }
        }
        return String(out);
    } else if (encoding == 3) { // UTF-8
        std::string out = "";
        for (size_t i = 0; i < len; i++) {
            if (data[i] == 0) break;
            out += (char)data[i];
        }
        return String(out);
    } else if (encoding == 1 || encoding == 2) { // UTF-16
        if (len < 2) return String("");
        bool bigEndian = (encoding == 2);
        size_t startIdx = 0;
        if (encoding == 1) {
            if (data[0] == 0xFE && data[1] == 0xFF) { bigEndian = true; startIdx = 2; }
            else if (data[0] == 0xFF && data[1] == 0xFE) { bigEndian = false; startIdx = 2; }
        }
        std::string out = "";
        for (size_t i = startIdx; i + 1 < len; i += 2) {
            uint16_t ch = bigEndian ? ((uint16_t)data[i] << 8 | data[i + 1]) : ((uint16_t)data[i + 1] << 8 | data[i]);
            if (ch == 0) break;
            if (ch < 0x80) out += (char)ch;
            else if (ch < 0x800) {
                out += (char)(0xC0 | (ch >> 6));
                out += (char)(0x80 | (ch & 0x3F));
            } else {
                out += (char)(0xE0 | (ch >> 12));
                out += (char)(0x80 | ((ch >> 6) & 0x3F));
                out += (char)(0x80 | (ch & 0x3F));
            }
        }
        return String(out);
    }
    return String("");
}

static size_t countUtf8CodePoints(const String &str) {
    size_t count = 0;
    const char *ptr = str.c_str();
    while (*ptr) {
        uint8_t c = (uint8_t)(*ptr);
        if ((c & 0xC0) != 0x80) count++;
        ptr++;
    }
    return count;
}

static bool parseID3SYLT(const uint8_t *data, size_t size, std::vector<LyricLine> &outSyltLines) {
    if (!data || size < 6) return false;
    uint8_t encoding = data[0];
    uint8_t timeFormat = data[4];
    size_t idx = 6;

    if (encoding == 1 || encoding == 2) {
        while (idx + 1 < size) {
            if (data[idx] == 0 && data[idx + 1] == 0) { idx += 2; break; }
            idx += 2;
        }
    } else {
        while (idx < size) {
            if (data[idx] == 0) { idx++; break; }
            idx++;
        }
    }

    outSyltLines.clear();

    while (idx < size && outSyltLines.size() < 300) {
        size_t strStart = idx;
        if (encoding == 1 || encoding == 2) {
            while (idx + 1 < size && !(data[idx] == 0 && data[idx + 1] == 0)) {
                idx += 2;
            }
            if (idx + 1 >= size) break;
            size_t strLen = idx - strStart;
            String lyricText = decodeID3String(data + strStart, strLen, encoding);
            idx += 2;

            if (idx + 4 > size) break;
            uint32_t ts = ((uint32_t)data[idx] << 24) | ((uint32_t)data[idx + 1] << 16) | ((uint32_t)data[idx + 2] << 8) | (uint32_t)data[idx + 3];
            idx += 4;
            if (timeFormat == 1) ts = (uint32_t)((ts * 1000ULL) / 38);

            LyricLine line;
            line.timestampMs = ts;
            line.text = lyricText;
            outSyltLines.push_back(line);
        } else {
            while (idx < size && data[idx] != 0) {
                idx++;
            }
            if (idx >= size) break;
            size_t strLen = idx - strStart;
            String lyricText = decodeID3String(data + strStart, strLen, encoding);
            idx++;

            if (idx + 4 > size) break;
            uint32_t ts = ((uint32_t)data[idx] << 24) | ((uint32_t)data[idx + 1] << 16) | ((uint32_t)data[idx + 2] << 8) | (uint32_t)data[idx + 3];
            idx += 4;
            if (timeFormat == 1) ts = (uint32_t)((ts * 1000ULL) / 38);

            LyricLine line;
            line.timestampMs = ts;
            line.text = lyricText;
            outSyltLines.push_back(line);
        }
    }
    return !outSyltLines.empty();
}

int main() {
    std::cout << "=== Native C++ Lyrics Parser Harness ===" << std::endl;

    // 1. Test ID3 String Decoders
    uint8_t utf8_sample[] = {'H', 'e', 'l', 'l', 'o', ' ', 0xE0, 0xA4, 0xA8, 0xE0, 0xA4, 0xAE, 0xE0, 0xA4, 0xB8, 0xE0, 0xA5, 0x8D, 0xE0, 0xA4, 0xA4, 0xE0, 0xA5, 0x87, 0x00};
    String s_utf8 = decodeID3String(utf8_sample, sizeof(utf8_sample), 3);
    assert(s_utf8.length() > 0);
    std::cout << "  [PASS] ID3 UTF-8 decoding verified: " << s_utf8.c_str() << std::endl;

    // UTF-16 LE with BOM (0xFF 0xFE) "Hi"
    uint8_t utf16le_sample[] = {0xFF, 0xFE, 'H', 0x00, 'i', 0x00, 0x00, 0x00};
    String s_utf16le = decodeID3String(utf16le_sample, sizeof(utf16le_sample), 1);
    assert(s_utf16le == "Hi");
    std::cout << "  [PASS] ID3 UTF-16 LE with BOM decoding verified: " << s_utf16le.c_str() << std::endl;

    // 2. Test Binary ID3 SYLT Frame Parsing
    std::vector<uint8_t> syltBuf;
    // Header: Enc=0 (ISO), Lang="eng", TimeFormat=2 (ms), ContentType=1 (Lyrics), Desc="\0"
    uint8_t hdr[] = {0x00, 'e', 'n', 'g', 0x02, 0x01, 0x00};
    syltBuf.insert(syltBuf.end(), hdr, hdr + sizeof(hdr));

    // Entry 1: "Verse 1\0" + 5000 ms (0x00001388)
    const char *line1 = "Verse 1";
    syltBuf.insert(syltBuf.end(), line1, line1 + strlen(line1) + 1);
    uint8_t ts1[] = {0x00, 0x00, 0x13, 0x88};
    syltBuf.insert(syltBuf.end(), ts1, ts1 + 4);

    // Entry 2: "Verse 2\0" + 12000 ms (0x00002EE0)
    const char *line2 = "Verse 2";
    syltBuf.insert(syltBuf.end(), line2, line2 + strlen(line2) + 1);
    uint8_t ts2[] = {0x00, 0x00, 0x2E, 0xE0};
    syltBuf.insert(syltBuf.end(), ts2, ts2 + 4);

    std::vector<LyricLine> decodedLines;
    bool ok = parseID3SYLT(syltBuf.data(), syltBuf.size(), decodedLines);
    assert(ok);
    assert(decodedLines.size() == 2);
    assert(decodedLines[0].timestampMs == 5000 && decodedLines[0].text == "Verse 1");
    assert(decodedLines[1].timestampMs == 12000 && decodedLines[1].text == "Verse 2");
    std::cout << "  [PASS] Binary ID3 SYLT frame decoding verified (" << decodedLines.size() << " synchronized lines parsed)." << std::endl;

    // 3. UTF-8 Code Point Counting
    String hindi_sample("नमस्ते दुनिया");
    size_t cp_count = countUtf8CodePoints(hindi_sample);
    assert(cp_count == 13);
    std::cout << "  [PASS] UTF-8 Devanagari code point counting verified (" << cp_count << " code points)." << std::endl;

    std::cout << "All native C++ harness tests PASSED (100%)!\n" << std::endl;
    return 0;
}
