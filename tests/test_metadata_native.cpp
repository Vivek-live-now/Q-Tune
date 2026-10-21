#include "../include/metadata_parser.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

static void appendBE32(std::vector<uint8_t> &out, uint32_t val) {
    out.push_back((val >> 24) & 0xFF);
    out.push_back((val >> 16) & 0xFF);
    out.push_back((val >> 8) & 0xFF);
    out.push_back(val & 0xFF);
}

static void appendLE32(std::vector<uint8_t> &out, uint32_t val) {
    out.push_back(val & 0xFF);
    out.push_back((val >> 8) & 0xFF);
    out.push_back((val >> 16) & 0xFF);
    out.push_back((val >> 24) & 0xFF);
}

static void appendSynchsafe32(std::vector<uint8_t> &out, uint32_t val) {
    out.push_back((val >> 21) & 0x7F);
    out.push_back((val >> 14) & 0x7F);
    out.push_back((val >> 7) & 0x7F);
    out.push_back(val & 0x7F);
}

// -----------------------------------------------------------------------------
// Test 1: MP3 ID3v2.3 Creation
// -----------------------------------------------------------------------------
static std::vector<uint8_t> createID3v23Frame(const char* id, const std::string &text, uint8_t encoding = 3) {
    std::vector<uint8_t> frame;
    for (int i = 0; i < 4; i++) frame.push_back(id[i]);
    uint32_t fSize = 1 + text.length();
    appendBE32(frame, fSize);
    frame.push_back(0); // Flag 1
    frame.push_back(0); // Flag 2
    frame.push_back(encoding);
    frame.insert(frame.end(), text.begin(), text.end());
    return frame;
}

static std::vector<uint8_t> createID3v23Tag(const std::vector<std::vector<uint8_t>> &frames) {
    std::vector<uint8_t> payload;
    for (const auto &f : frames) {
        payload.insert(payload.end(), f.begin(), f.end());
    }
    std::vector<uint8_t> tag;
    tag.push_back('I'); tag.push_back('D'); tag.push_back('3');
    tag.push_back(3); // v2.3
    tag.push_back(0); // revision
    tag.push_back(0); // flags
    appendSynchsafe32(tag, payload.size());
    tag.insert(tag.end(), payload.begin(), payload.end());
    // Pad to at least 128 bytes
    while (tag.size() < 140) tag.push_back(0);
    return tag;
}

// -----------------------------------------------------------------------------
// Test 2: MP3 ID3v1 Creation
// -----------------------------------------------------------------------------
static std::vector<uint8_t> createID3v1Tag(const char* title, const char* artist, const char* album, const char* year, uint8_t track, uint8_t genreId) {
    std::vector<uint8_t> tag(128, 0);
    tag[0] = 'T'; tag[1] = 'A'; tag[2] = 'G';
    size_t tLen = strlen(title); if (tLen > 30) tLen = 30;
    memcpy(&tag[3], title, tLen);
    size_t aLen = strlen(artist); if (aLen > 30) aLen = 30;
    memcpy(&tag[33], artist, aLen);
    size_t alLen = strlen(album); if (alLen > 30) alLen = 30;
    memcpy(&tag[63], album, alLen);
    size_t yLen = strlen(year); if (yLen > 4) yLen = 4;
    memcpy(&tag[93], year, yLen);
    tag[125] = 0;
    tag[126] = track;
    tag[127] = genreId;
    return tag;
}

// -----------------------------------------------------------------------------
// Test 3: FLAC Creation
// -----------------------------------------------------------------------------
static std::vector<uint8_t> createFLACTag(const std::vector<std::string> &comments) {
    std::vector<uint8_t> flac;
    flac.push_back('f'); flac.push_back('L'); flac.push_back('a'); flac.push_back('C');

    std::vector<uint8_t> block;
    // Vendor string
    std::string vendor = "reference libFLAC 1.3.3";
    appendLE32(block, vendor.length());
    block.insert(block.end(), vendor.begin(), vendor.end());
    // Comment count
    appendLE32(block, comments.size());
    for (const auto &c : comments) {
        appendLE32(block, c.length());
        block.insert(block.end(), c.begin(), c.end());
    }

    // Block header: isLast (0x80) | type 4 (Vorbis comment)
    flac.push_back(0x84);
    uint32_t bLen = block.size();
    flac.push_back((bLen >> 16) & 0xFF);
    flac.push_back((bLen >> 8) & 0xFF);
    flac.push_back(bLen & 0xFF);
    flac.insert(flac.end(), block.begin(), block.end());

    while (flac.size() < 140) flac.push_back(0);
    return flac;
}

// -----------------------------------------------------------------------------
// Test 4: M4A Atom Creation
// -----------------------------------------------------------------------------
static std::vector<uint8_t> makeAtom(const char* type, const std::vector<uint8_t> &payload) {
    std::vector<uint8_t> atom;
    uint32_t size = 8 + payload.size();
    appendBE32(atom, size);
    for (int i = 0; i < 4; i++) atom.push_back(type[i]);
    atom.insert(atom.end(), payload.begin(), payload.end());
    return atom;
}

static std::vector<uint8_t> makeTextDataAtom(const std::string &str) {
    std::vector<uint8_t> payload;
    appendBE32(payload, 1); // type 1 = UTF-8 text
    appendBE32(payload, 0); // locale 0
    payload.insert(payload.end(), str.begin(), str.end());
    return makeAtom("data", payload);
}

static std::vector<uint8_t> makeTrknDataAtom(uint16_t track, uint16_t total) {
    std::vector<uint8_t> payload;
    appendBE32(payload, 0); // type 0 = binary
    appendBE32(payload, 0); // locale 0
    payload.push_back(0); payload.push_back(0); // 2 bytes padding
    payload.push_back((track >> 8) & 0xFF); payload.push_back(track & 0xFF);
    payload.push_back((total >> 8) & 0xFF); payload.push_back(total & 0xFF);
    payload.push_back(0); payload.push_back(0); // 2 bytes padding
    return makeAtom("data", payload);
}

// -----------------------------------------------------------------------------
// Test 5: WAV RIFF Creation
// -----------------------------------------------------------------------------
static std::vector<uint8_t> createWAVWithListInfo(const std::vector<std::pair<std::string, std::string>> &infoItems) {
    std::vector<uint8_t> infoPayload;
    infoPayload.push_back('I'); infoPayload.push_back('N'); infoPayload.push_back('F'); infoPayload.push_back('O');

    for (const auto &item : infoItems) {
        for (int i = 0; i < 4; i++) infoPayload.push_back(item.first[i]);
        uint32_t strLen = item.second.length() + 1; // including null terminator
        appendLE32(infoPayload, strLen);
        infoPayload.insert(infoPayload.end(), item.second.begin(), item.second.end());
        infoPayload.push_back(0); // null terminator
        if (strLen & 1) infoPayload.push_back(0); // pad to word
    }

    std::vector<uint8_t> listChunk;
    listChunk.push_back('L'); listChunk.push_back('I'); listChunk.push_back('S'); listChunk.push_back('T');
    appendLE32(listChunk, infoPayload.size());
    listChunk.insert(listChunk.end(), infoPayload.begin(), infoPayload.end());

    std::vector<uint8_t> wav;
    wav.push_back('R'); wav.push_back('I'); wav.push_back('F'); wav.push_back('F');
    uint32_t totalRiffSize = 4 + listChunk.size() + 8;
    appendLE32(wav, totalRiffSize);
    wav.push_back('W'); wav.push_back('A'); wav.push_back('V'); wav.push_back('E');
    wav.insert(wav.end(), listChunk.begin(), listChunk.end());

    // Dummy data chunk
    wav.push_back('d'); wav.push_back('a'); wav.push_back('t'); wav.push_back('a');
    appendLE32(wav, 0);

    return wav;
}

int main() {
    std::cout << "=== Q-Tune Native Unit Tests (MetadataParser Multi-Format Engine) ===\n";

    // -------------------------------------------------------------------------
    // 1. MP3 ID3v2.3 Parser Test
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 1. MP3 ID3v2.3 tag decoding (Title, Artist, Album, Year, Genre, Track)...\n";
    {
        std::vector<std::vector<uint8_t>> frames = {
            createID3v23Frame("TIT2", "Hotel California"),
            createID3v23Frame("TPE1", "Eagles"),
            createID3v23Frame("TALB", "Hotel California"),
            createID3v23Frame("TYER", "1976"),
            createID3v23Frame("TCON", "Classic Rock"),
            createID3v23Frame("TRCK", "1/9")
        };
        auto mp3Data = createID3v23Tag(frames);
        MemoryStream stream(mp3Data.data(), mp3Data.size());
        TrackMetadata meta;
        bool ok = MetadataParser::parseMP3(stream, meta);
        assert(ok == true);
        assert(meta.hasTags == true);
        assert(meta.title == "Hotel California");
        assert(meta.artist == "Eagles");
        assert(meta.album == "Hotel California");
        assert(meta.year == "1976");
        assert(meta.genre == "Classic Rock");
        assert(meta.trackNumber == "1/9");
        std::cout << "  [PASS] MP3 ID3v2.3 parsed successfully: " << meta.title.c_str() << " by " << meta.artist.c_str() << "\n";
    }

    // -------------------------------------------------------------------------
    // 2. MP3 ID3v1 Fallback Test
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 2. MP3 ID3v1 legacy fallback decoding at EOF...\n";
    {
        auto tag = createID3v1Tag("Yesterday", "Beatles", "Help!", "1965", 11, 13); // 13 = Pop
        MemoryStream stream(tag.data(), tag.size());
        TrackMetadata meta;
        bool ok = MetadataParser::parseMP3(stream, meta);
        assert(ok == true);
        assert(meta.hasTags == true);
        assert(meta.title == "Yesterday");
        assert(meta.artist == "Beatles");
        assert(meta.album == "Help!");
        assert(meta.year == "1965");
        assert(meta.trackNumber == "11");
        assert(meta.genre == "Pop");
        std::cout << "  [PASS] MP3 ID3v1 fallback parsed successfully: " << meta.title.c_str() << " (" << meta.genre.c_str() << ")\n";
    }

    // -------------------------------------------------------------------------
    // 3. FLAC Vorbis Comment Test
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 3. FLAC Vorbis comment block decoding...\n";
    {
        std::vector<std::string> comments = {
            "TITLE=Comfortably Numb",
            "ARTIST=Pink Floyd",
            "ALBUM=The Wall",
            "DATE=1979",
            "GENRE=Progressive Rock",
            "TRACKNUMBER=6",
            "TRACKTOTAL=13"
        };
        auto flacData = createFLACTag(comments);
        MemoryStream stream(flacData.data(), flacData.size());
        TrackMetadata meta;
        bool ok = MetadataParser::parseFLAC(stream, meta);
        assert(ok == true);
        assert(meta.hasTags == true);
        assert(meta.title == "Comfortably Numb");
        assert(meta.artist == "Pink Floyd");
        assert(meta.album == "The Wall");
        assert(meta.year == "1979");
        assert(meta.genre == "Progressive Rock");
        assert(meta.trackNumber == "6/13");
        std::cout << "  [PASS] FLAC Vorbis comments parsed successfully: " << meta.title.c_str() << " (Track " << meta.trackNumber.c_str() << ")\n";
    }

    // -------------------------------------------------------------------------
    // 4. M4A / AAC QuickTime / MP4 ISO Atoms Test
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 4. M4A / AAC ilst metadata atoms decoding...\n";
    {
        std::vector<uint8_t> ilstPayload;
        auto titleAtom = makeAtom("\xA9" "nam", makeTextDataAtom("Bohemian Rhapsody"));
        auto artistAtom = makeAtom("\xA9" "ART", makeTextDataAtom("Queen"));
        auto albumAtom = makeAtom("\xA9" "alb", makeTextDataAtom("A Night at the Opera"));
        auto yearAtom = makeAtom("\xA9" "day", makeTextDataAtom("1975"));
        auto genreAtom = makeAtom("\xA9" "gen", makeTextDataAtom("Rock"));
        auto trackAtom = makeAtom("trkn", makeTrknDataAtom(4, 12));

        ilstPayload.insert(ilstPayload.end(), titleAtom.begin(), titleAtom.end());
        ilstPayload.insert(ilstPayload.end(), artistAtom.begin(), artistAtom.end());
        ilstPayload.insert(ilstPayload.end(), albumAtom.begin(), albumAtom.end());
        ilstPayload.insert(ilstPayload.end(), yearAtom.begin(), yearAtom.end());
        ilstPayload.insert(ilstPayload.end(), genreAtom.begin(), genreAtom.end());
        ilstPayload.insert(ilstPayload.end(), trackAtom.begin(), trackAtom.end());

        auto ilstAtom = makeAtom("ilst", ilstPayload);
        // meta atom needs 4 bytes version/flags (0,0,0,0) before child ilst
        std::vector<uint8_t> metaPayload = {0, 0, 0, 0};
        metaPayload.insert(metaPayload.end(), ilstAtom.begin(), ilstAtom.end());
        auto metaAtom = makeAtom("meta", metaPayload);
        auto udtaAtom = makeAtom("udta", metaAtom);
        auto moovAtom = makeAtom("moov", udtaAtom);

        // Prepend ftyp atom
        std::vector<uint8_t> ftypPayload = {'M', '4', 'A', ' ', 0, 0, 0, 0, 'M', '4', 'A', ' '};
        auto ftypAtom = makeAtom("ftyp", ftypPayload);

        std::vector<uint8_t> m4aData;
        m4aData.insert(m4aData.end(), ftypAtom.begin(), ftypAtom.end());
        m4aData.insert(m4aData.end(), moovAtom.begin(), moovAtom.end());

        MemoryStream stream(m4aData.data(), m4aData.size());
        TrackMetadata meta;
        bool ok = MetadataParser::parseM4A(stream, meta);
        assert(ok == true);
        assert(meta.hasTags == true);
        assert(meta.title == "Bohemian Rhapsody");
        assert(meta.artist == "Queen");
        assert(meta.album == "A Night at the Opera");
        assert(meta.year == "1975");
        assert(meta.genre == "Rock");
        assert(meta.trackNumber == "4/12");
        std::cout << "  [PASS] M4A atoms parsed successfully: " << meta.title.c_str() << " by " << meta.artist.c_str() << "\n";
    }

    // -------------------------------------------------------------------------
    // 5. WAV RIFF LIST INFO Test
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 5. WAV RIFF LIST INFO chunk decoding...\n";
    {
        std::vector<std::pair<std::string, std::string>> items = {
            {"INAM", "Imagine"},
            {"IART", "John Lennon"},
            {"IPRD", "Imagine Album"},
            {"ICRD", "1971"},
            {"IGNR", "Rock"},
            {"ITRK", "1"}
        };
        auto wavData = createWAVWithListInfo(items);
        MemoryStream stream(wavData.data(), wavData.size());
        TrackMetadata meta;
        bool ok = MetadataParser::parseWAV(stream, meta);
        assert(ok == true);
        assert(meta.hasTags == true);
        assert(meta.title == "Imagine");
        assert(meta.artist == "John Lennon");
        assert(meta.album == "Imagine Album");
        assert(meta.year == "1971");
        assert(meta.genre == "Rock");
        assert(meta.trackNumber == "1");
        std::cout << "  [PASS] WAV RIFF INFO parsed successfully: " << meta.title.c_str() << " by " << meta.artist.c_str() << "\n";
    }

    // -------------------------------------------------------------------------
    // 6. WAV Embedded ID3 Chunk Test
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 6. WAV Embedded 'id3 ' RIFF chunk decoding...\n";
    {
        std::vector<std::vector<uint8_t>> frames = {
            createID3v23Frame("TIT2", "Stairway to Heaven"),
            createID3v23Frame("TPE1", "Led Zeppelin"),
            createID3v23Frame("TALB", "Led Zeppelin IV"),
            createID3v23Frame("TYER", "1971")
        };
        auto id3Payload = createID3v23Tag(frames);

        std::vector<uint8_t> wav;
        wav.push_back('R'); wav.push_back('I'); wav.push_back('F'); wav.push_back('F');
        uint32_t totalRiffSize = 4 + 8 + id3Payload.size();
        appendLE32(wav, totalRiffSize);
        wav.push_back('W'); wav.push_back('A'); wav.push_back('V'); wav.push_back('E');

        wav.push_back('i'); wav.push_back('d'); wav.push_back('3'); wav.push_back(' ');
        appendLE32(wav, id3Payload.size());
        wav.insert(wav.end(), id3Payload.begin(), id3Payload.end());

        MemoryStream stream(wav.data(), wav.size());
        TrackMetadata meta;
        bool ok = MetadataParser::parseWAV(stream, meta);
        assert(ok == true);
        assert(meta.hasTags == true);
        assert(meta.title == "Stairway to Heaven");
        assert(meta.artist == "Led Zeppelin");
        assert(meta.album == "Led Zeppelin IV");
        assert(meta.year == "1971");
        std::cout << "  [PASS] WAV Embedded 'id3 ' chunk parsed successfully: " << meta.title.c_str() << " by " << meta.artist.c_str() << "\n";
    }

    // -------------------------------------------------------------------------
    // 7. Genre Normalization Test
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 7. Genre normalization (numeric, parentheses, ID3v1 table)...\n";
    {
        assert(MetadataParser::normalizeGenre("(17)") == "Rock");
        assert(MetadataParser::normalizeGenre("(17)Rock") == "Rock");
        assert(MetadataParser::normalizeGenre("17") == "Rock");
        assert(MetadataParser::normalizeGenre("Synthwave") == "Synthwave");
        assert(MetadataParser::normalizeGenre("(13)") == "Pop");
        assert(MetadataParser::normalizeGenre("(0)") == "Blues");
        assert(MetadataParser::normalizeGenre("(32)") == "Classical");
        std::cout << "  [PASS] Genre normalization verified across all ID3 syntax variants.\n";
    }

    // -------------------------------------------------------------------------
    // 8. Binary Persistent Metadata Index Specification Test ('QTMD' Spec v1)
    // -------------------------------------------------------------------------
    std::cout << "[TEST] 8. Binary Metadata Index Serialization (QTMD 256-byte stride)...\n";
    {
        static_assert(sizeof(MetadataIndexEntry) == 256, "MetadataIndexEntry must be 256 bytes");

        std::vector<uint8_t> indexData;
        // Header: Magic (4B), Version (2B), Count (4B)
        appendLE32(indexData, METADATA_INDEX_MAGIC);
        indexData.push_back(METADATA_INDEX_VERSION & 0xFF);
        indexData.push_back((METADATA_INDEX_VERSION >> 8) & 0xFF);
        appendLE32(indexData, 2); // 2 entries

        MetadataIndexEntry e1;
        memset(&e1, 0, sizeof(e1));
        strcpy(e1.audioPath, "/music/Queen/Bohemian.mp3");
        e1.audioFileSize = 5988220;
        e1.fileMTime = 1690000000;
        strcpy(e1.title, "Bohemian Rhapsody");
        strcpy(e1.artist, "Queen");
        strcpy(e1.album, "A Night at the Opera");
        strcpy(e1.genre, "Rock");
        strcpy(e1.year, "1975");
        strcpy(e1.trackNumber, "04");
        e1.format = 3;

        MetadataIndexEntry e2;
        memset(&e2, 0, sizeof(e2));
        strcpy(e2.audioPath, "/music/Eagles/Hotel.flac");
        e2.audioFileSize = 38440000;
        e2.fileMTime = 1690005000;
        strcpy(e2.title, "Hotel California");
        strcpy(e2.artist, "Eagles");
        strcpy(e2.album, "Hotel California");
        strcpy(e2.genre, "Classic Rock");
        strcpy(e2.year, "1976");
        strcpy(e2.trackNumber, "01");
        e2.format = 2;

        const uint8_t* p1 = (const uint8_t*)&e1;
        indexData.insert(indexData.end(), p1, p1 + sizeof(MetadataIndexEntry));
        const uint8_t* p2 = (const uint8_t*)&e2;
        indexData.insert(indexData.end(), p2, p2 + sizeof(MetadataIndexEntry));

        assert(indexData.size() == 10 + 2 * 256);

        // Deserialize and verify
        MemoryStream stream(indexData.data(), indexData.size());
        uint32_t magic = 0;
        uint16_t ver = 0;
        uint32_t count = 0;
        stream.read((uint8_t*)&magic, 4);
        stream.read((uint8_t*)&ver, 2);
        stream.read((uint8_t*)&count, 4);

        assert(magic == METADATA_INDEX_MAGIC);
        assert(ver == METADATA_INDEX_VERSION);
        assert(count == 2);

        MetadataIndexEntry out1, out2;
        stream.read((uint8_t*)&out1, sizeof(MetadataIndexEntry));
        stream.read((uint8_t*)&out2, sizeof(MetadataIndexEntry));

        assert(strcmp(out1.audioPath, "/music/Queen/Bohemian.mp3") == 0);
        assert(strcmp(out1.title, "Bohemian Rhapsody") == 0);
        assert(strcmp(out1.artist, "Queen") == 0);
        assert(strcmp(out1.album, "A Night at the Opera") == 0);
        assert(strcmp(out1.genre, "Rock") == 0);
        assert(strcmp(out1.year, "1975") == 0);
        assert(out1.format == 3);

        assert(strcmp(out2.audioPath, "/music/Eagles/Hotel.flac") == 0);
        assert(strcmp(out2.title, "Hotel California") == 0);
        assert(strcmp(out2.artist, "Eagles") == 0);
        assert(strcmp(out2.album, "Hotel California") == 0);
        assert(strcmp(out2.genre, "Classic Rock") == 0);
        assert(strcmp(out2.year, "1976") == 0);
        assert(out2.format == 2);

        std::cout << "  [PASS] MetadataIndexEntry 256-byte alignment and binary round-trip verified.\n";
    }

    std::cout << "\nAll MetadataParser Multi-Format Native Tests PASSED (100%)!\n";
    return 0;
}
