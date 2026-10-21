#include "metadata_parser.h"
#include "lyrics_id3_codec.h"
#include <cstring>
#include <cstdlib>

MetadataParser metadataParser;

#ifdef ARDUINO
extern SemaphoreHandle_t spiBusMutex;
#endif

MetadataParser::MetadataParser() : loadedAudioPath("") {
    metadata.clear();
}

void MetadataParser::clear() {
    metadata.clear();
    loadedAudioPath = "";
}

String MetadataParser::cleanString(const String &str) {
    String res = str;
    res.trim();
    while (res.length() > 0 && (res[res.length() - 1] == '\0' || (uint8_t)res[res.length() - 1] < 32)) {
        res = res.substring(0, res.length() - 1);
        res.trim();
    }
    return res;
}

const char* MetadataParser::getID3v1Genre(uint8_t id) {
    static const char* const GENRES[] = {
        "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop",
        "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B", "Rap",
        "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska", "Death Metal", "Pranks",
        "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance",
        "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
        "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock",
        "Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream",
        "Southern Rock", "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle",
        "Native American", "Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes", "Trailer", "Lo-Fi",
        "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock",
        "Folk", "Folk-Rock", "National Folk", "Swing", "Fast Fusion", "Bebob", "Latin", "Revival",
        "Celtic", "Bluegrass", "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock",
        "Big Band", "Chorus", "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson", "Opera",
        "Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire", "Slow Jam",
        "Club", "Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle",
        "Duet", "Punk Rock", "Drum Solo", "A capella", "Euro-House", "Dance Hall", "Goa", "Drum & Bass",
        "Club-House", "Hardcore", "Terror", "Indie", "BritPop", "Negerpunk", "Polsk Punk", "Beat",
        "Christian Gangsta Rap", "Heavy Metal", "Black Metal", "Crossover", "Contemporary Christian", "Christian Rock",
        "Merengue", "Salsa", "Thrash Metal", "Anime", "JPop", "Synthpop"
    };
    constexpr size_t NUM_GENRES = sizeof(GENRES) / sizeof(GENRES[0]);
    if (id < NUM_GENRES) return GENRES[id];
    return "";
}

String MetadataParser::normalizeGenre(const String &genre) {
    String g = cleanString(genre);
    if (g.length() == 0) return "";

    // Check for ID3 numeric style: "(17)" or "(17)Rock"
    if (g.startsWith("(") && g.indexOf(')') > 1) {
        int closeParen = g.indexOf(')');
        String numPart = g.substring(1, closeParen);
        int gId = numPart.toInt();
        const char* lookedUp = getID3v1Genre((uint8_t)gId);
        String rem = cleanString(g.substring(closeParen + 1));
        if (rem.length() > 0) return rem;
        if (lookedUp && lookedUp[0] != '\0') return String(lookedUp);
    }

    // Check if purely numeric e.g. "17"
    bool allDigits = true;
    for (size_t i = 0; i < g.length(); i++) {
        if (g[i] < '0' || g[i] > '9') { allDigits = false; break; }
    }
    if (allDigits && g.length() <= 3) {
        int gId = g.toInt();
        const char* lookedUp = getID3v1Genre((uint8_t)gId);
        if (lookedUp && lookedUp[0] != '\0') return String(lookedUp);
    }

    return g;
}

bool MetadataParser::loadForTrack(const String &audioPath) {
    if (audioPath.length() == 0) {
        clear();
        return false;
    }
    if (loadedAudioPath == audioPath) {
        return metadata.hasTags;
    }

    clear();
    loadedAudioPath = audioPath;

#ifdef ARDUINO
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    File audioFile = SD.open(audioPath, FILE_READ);
    if (!audioFile) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    ArduinoFileStream stream(audioFile);
    String lower = audioPath;
    lower.toLowerCase();
    bool parsed = false;

    if (lower.endsWith(".mp3")) {
        parsed = parseMP3(stream, metadata);
    } else if (lower.endsWith(".flac")) {
        parsed = parseFLAC(stream, metadata);
    } else if (lower.endsWith(".m4a") || lower.endsWith(".aac")) {
        parsed = parseM4A(stream, metadata);
    } else if (lower.endsWith(".wav")) {
        parsed = parseWAV(stream, metadata);
    }

    audioFile.close();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    metadata.hasTags = parsed && (metadata.title.length() > 0 || metadata.artist.length() > 0 || metadata.album.length() > 0);
    return metadata.hasTags;
#else
    return false;
#endif
}

// ==============================================================================
// 1. ID3v2 Parsing (MP3 and WAV chunks)
// ==============================================================================
bool MetadataParser::parseID3v2(MetadataStream &stream, uint32_t tagOffset, uint32_t tagSize, uint8_t ver, TrackMetadata &meta) {
    if (tagSize < 10 || tagOffset + tagSize > stream.size()) return false;

    if (!stream.seek(tagOffset)) return false;

    uint32_t tagEnd = tagOffset + tagSize;
    uint8_t headerLen = (ver == 2) ? 6 : 10;
    bool foundAny = false;

    while (stream.position() + headerLen < tagEnd && stream.available() >= headerLen) {
        uint8_t fHdr[10];
        if (stream.read(fHdr, headerLen) != headerLen) break;

        char fId[5] = {0};
        memcpy(fId, fHdr, (ver == 2) ? 3 : 4);
        if (fId[0] == 0) break; // Reached zero padding

        uint32_t fSize = 0;
        if (ver == 2) {
            fSize = ((uint32_t)fHdr[3] << 16) | ((uint32_t)fHdr[4] << 8) | (uint32_t)fHdr[5];
        } else if (ver == 4) {
            // ID3v2.4 specifies synchsafe integer for frame size
            fSize = ((uint32_t)(fHdr[4] & 0x7F) << 21) |
                    ((uint32_t)(fHdr[5] & 0x7F) << 14) |
                    ((uint32_t)(fHdr[6] & 0x7F) << 7)  |
                    (uint32_t)(fHdr[7] & 0x7F);
        } else { // ID3v2.3
            fSize = ((uint32_t)fHdr[4] << 24) | ((uint32_t)fHdr[5] << 16) | ((uint32_t)fHdr[6] << 8) | (uint32_t)fHdr[7];
        }

        if (fSize == 0 || stream.position() + fSize > tagEnd || fSize > 16384) {
            break;
        }

        uint32_t frameStart = stream.position();

        // Check target frame IDs
        bool isTitle = (strcmp(fId, "TIT2") == 0 || strcmp(fId, "TT2") == 0);
        bool isArtist = (strcmp(fId, "TPE1") == 0 || strcmp(fId, "TP1") == 0);
        bool isAlbumArtist = (strcmp(fId, "TPE2") == 0);
        bool isAlbum = (strcmp(fId, "TALB") == 0 || strcmp(fId, "TAL") == 0);
        bool isYear = (strcmp(fId, "TYER") == 0 || strcmp(fId, "TDRC") == 0 || strcmp(fId, "TYE") == 0);
        bool isGenre = (strcmp(fId, "TCON") == 0 || strcmp(fId, "TCO") == 0);
        bool isTrack = (strcmp(fId, "TRCK") == 0 || strcmp(fId, "TRK") == 0);
        bool isComment = (strcmp(fId, "COMM") == 0 || strcmp(fId, "COM") == 0);

        if (isTitle || isArtist || isAlbumArtist || isAlbum || isYear || isGenre || isTrack) {
            if (fSize > 1) {
                std::vector<uint8_t> payload(fSize);
                if (stream.read(payload.data(), fSize) == fSize) {
                    uint8_t encoding = payload[0];
                    std::string decoded = LyricsID3Codec::decodeString(payload.data() + 1, fSize - 1, encoding);
                    String val = cleanString(String(decoded.c_str()));

                    if (val.length() > 0) {
                        if (isTitle && meta.title.length() == 0) { meta.title = val; foundAny = true; }
                        else if (isArtist && meta.artist.length() == 0) { meta.artist = val; foundAny = true; }
                        else if (isAlbumArtist && meta.artist.length() == 0) { meta.artist = val; foundAny = true; }
                        else if (isAlbum && meta.album.length() == 0) { meta.album = val; foundAny = true; }
                        else if (isYear && meta.year.length() == 0) {
                            if (val.length() >= 4 && val[0] >= '0' && val[0] <= '9') {
                                meta.year = val.substring(0, 4);
                            } else {
                                meta.year = val;
                            }
                            foundAny = true;
                        }
                        else if (isGenre && meta.genre.length() == 0) { meta.genre = normalizeGenre(val); foundAny = true; }
                        else if (isTrack && meta.trackNumber.length() == 0) { meta.trackNumber = val; foundAny = true; }
                    }
                }
            }
        } else if (isComment && meta.comment.length() == 0 && fSize > 4) {
            std::vector<uint8_t> payload(fSize);
            if (stream.read(payload.data(), fSize) == fSize) {
                uint8_t encoding = payload[0];
                size_t offset = 4; // Skip encoding (1B) + language (3B)
                // Find termination of description
                if (encoding == 1 || encoding == 2) {
                    while (offset + 1 < fSize && !(payload[offset] == 0 && payload[offset + 1] == 0)) {
                        offset += 2;
                    }
                    offset += 2;
                } else {
                    while (offset < fSize && payload[offset] != 0) {
                        offset++;
                    }
                    offset++;
                }
                if (offset < fSize) {
                    std::string decoded = LyricsID3Codec::decodeString(payload.data() + offset, fSize - offset, encoding);
                    String val = cleanString(String(decoded.c_str()));
                    if (val.length() > 0) {
                        meta.comment = val;
                        foundAny = true;
                    }
                }
            }
        }

        stream.seek(frameStart + fSize);
    }

    return foundAny;
}

// ==============================================================================
// 2. ID3v1 Parsing (Fallback for MP3)
// ==============================================================================
bool MetadataParser::parseID3v1(MetadataStream &stream, TrackMetadata &meta) {
    if (stream.size() < 128) return false;

    if (!stream.seek(stream.size() - 128)) return false;

    uint8_t buf[128];
    if (stream.read(buf, 128) != 128) return false;

    if (buf[0] != 'T' || buf[1] != 'A' || buf[2] != 'G') return false;

    char titleBuf[31] = {0};
    char artistBuf[31] = {0};
    char albumBuf[31] = {0};
    char yearBuf[5] = {0};
    char commentBuf[31] = {0};

    memcpy(titleBuf, buf + 3, 30);
    memcpy(artistBuf, buf + 33, 30);
    memcpy(albumBuf, buf + 63, 30);
    memcpy(yearBuf, buf + 93, 4);
    memcpy(commentBuf, buf + 97, 28);

    String t = cleanString(String(titleBuf));
    String a = cleanString(String(artistBuf));
    String al = cleanString(String(albumBuf));
    String y = cleanString(String(yearBuf));
    String c = cleanString(String(commentBuf));

    if (meta.title.length() == 0 && t.length() > 0) meta.title = t;
    if (meta.artist.length() == 0 && a.length() > 0) meta.artist = a;
    if (meta.album.length() == 0 && al.length() > 0) meta.album = al;
    if (meta.year.length() == 0 && y.length() > 0) meta.year = y;
    if (meta.comment.length() == 0 && c.length() > 0) meta.comment = c;

    // ID3v1.1 Track Number detection
    if (meta.trackNumber.length() == 0 && buf[125] == 0 && buf[126] != 0) {
        meta.trackNumber = String((int)buf[126]);
    }

    // Genre
    if (meta.genre.length() == 0 && buf[127] < 148) {
        const char* lookedUp = getID3v1Genre(buf[127]);
        if (lookedUp && lookedUp[0] != '\0') meta.genre = lookedUp;
    }

    return true;
}

// ==============================================================================
// 3. MP3 Format Parser (ID3v2 with ID3v1 Fallback)
// ==============================================================================
bool MetadataParser::parseMP3(MetadataStream &stream, TrackMetadata &meta) {
    if (stream.size() < 128) return false;

    stream.seek(0);
    uint8_t hdr[10];
    bool hasV2 = false;

    if (stream.read(hdr, 10) == 10 && hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
        uint8_t ver = hdr[3];
        uint32_t tagSize = ((uint32_t)(hdr[6] & 0x7F) << 21) |
                           ((uint32_t)(hdr[7] & 0x7F) << 14) |
                           ((uint32_t)(hdr[8] & 0x7F) << 7)  |
                           (uint32_t)(hdr[9] & 0x7F);

        uint32_t tagOffset = 10;
        // Skip extended header if present
        if (hdr[5] & 0x40) {
            uint8_t extHdr[4];
            if (stream.read(extHdr, 4) == 4) {
                uint32_t extSize = (ver == 4) ?
                    (((uint32_t)(extHdr[0] & 0x7F) << 21) | ((uint32_t)(extHdr[1] & 0x7F) << 14) |
                     ((uint32_t)(extHdr[2] & 0x7F) << 7)  | (uint32_t)(extHdr[3] & 0x7F)) :
                    (((uint32_t)extHdr[0] << 24) | ((uint32_t)extHdr[1] << 16) |
                     ((uint32_t)extHdr[2] << 8)  | (uint32_t)extHdr[3]);
                tagOffset += extSize;
            }
        }

        hasV2 = parseID3v2(stream, tagOffset, tagSize, ver, meta);
    }

    // Fallback or fill gaps from ID3v1
    if (meta.title.length() == 0 || meta.artist.length() == 0) {
        parseID3v1(stream, meta);
    }

    meta.hasTags = (meta.title.length() > 0 || meta.artist.length() > 0 || meta.album.length() > 0);
    return meta.hasTags;
}

// ==============================================================================
// 4. FLAC Format Parser (Vorbis Comments Block 4)
// ==============================================================================
bool MetadataParser::parseFLAC(MetadataStream &stream, TrackMetadata &meta) {
    if (stream.size() < 128) return false;

    stream.seek(0);
    uint8_t magic[4];
    if (stream.read(magic, 4) != 4) return false;
    if (memcmp(magic, "fLaC", 4) != 0) return false;

    bool isLast = false;
    String trackTotal = "";

    while (!isLast && stream.available() >= 4) {
        uint8_t bHdr[4];
        if (stream.read(bHdr, 4) != 4) break;

        isLast = (bHdr[0] & 0x80) != 0;
        uint8_t blockType = bHdr[0] & 0x7F;
        uint32_t blockLen = ((uint32_t)bHdr[1] << 16) | ((uint32_t)bHdr[2] << 8) | (uint32_t)bHdr[3];
        uint32_t blockStart = stream.position();

        if (blockType == 4) { // METADATA_BLOCK_VORBIS_COMMENT
            uint8_t lenBuf[4];
            // Vendor length
            if (stream.read(lenBuf, 4) != 4) break;
            uint32_t vendorLen = lenBuf[0] | ((uint32_t)lenBuf[1] << 8) | ((uint32_t)lenBuf[2] << 16) | ((uint32_t)lenBuf[3] << 24);
            stream.seek(stream.position() + vendorLen);

            // User comment list length
            if (stream.read(lenBuf, 4) != 4) break;
            uint32_t commentCount = lenBuf[0] | ((uint32_t)lenBuf[1] << 8) | ((uint32_t)lenBuf[2] << 16) | ((uint32_t)lenBuf[3] << 24);
            if (commentCount > 1000) commentCount = 1000;

            for (uint32_t i = 0; i < commentCount && stream.position() < blockStart + blockLen; i++) {
                if (stream.read(lenBuf, 4) != 4) break;
                uint32_t cLen = lenBuf[0] | ((uint32_t)lenBuf[1] << 8) | ((uint32_t)lenBuf[2] << 16) | ((uint32_t)lenBuf[3] << 24);

                if (cLen > 0 && cLen < 2048) {
                    std::vector<char> commentBuf(cLen + 1, 0);
                    if (stream.read((uint8_t*)commentBuf.data(), cLen) == cLen) {
                        String commentStr = commentBuf.data();
                        int eq = commentStr.indexOf('=');
                        if (eq > 0) {
                            String key = commentStr.substring(0, eq);
                            key.toUpperCase();
                            String val = cleanString(commentStr.substring(eq + 1));

                            if (key == "TITLE" && meta.title.length() == 0) {
                                meta.title = val;
                            } else if (key == "ARTIST" && meta.artist.length() == 0) {
                                meta.artist = val;
                            } else if (key == "ALBUMARTIST" && meta.artist.length() == 0) {
                                meta.artist = val;
                            } else if (key == "ALBUM" && meta.album.length() == 0) {
                                meta.album = val;
                            } else if ((key == "DATE" || key == "YEAR") && meta.year.length() == 0) {
                                if (val.length() >= 4 && val[0] >= '0' && val[0] <= '9') {
                                    meta.year = val.substring(0, 4);
                                } else {
                                    meta.year = val;
                                }
                            } else if (key == "GENRE" && meta.genre.length() == 0) {
                                meta.genre = normalizeGenre(val);
                            } else if ((key == "TRACKNUMBER" || key == "TRACK") && meta.trackNumber.length() == 0) {
                                meta.trackNumber = val;
                            } else if (key == "TRACKTOTAL" || key == "TOTALTRACKS") {
                                trackTotal = val;
                            } else if ((key == "COMMENT" || key == "DESCRIPTION") && meta.comment.length() == 0) {
                                meta.comment = val;
                            }
                        }
                    }
                } else {
                    stream.seek(stream.position() + cLen);
                }
            }

            if (trackTotal.length() > 0 && meta.trackNumber.length() > 0 && meta.trackNumber.indexOf('/') < 0) {
                meta.trackNumber += "/" + trackTotal;
            }

            meta.hasTags = (meta.title.length() > 0 || meta.artist.length() > 0 || meta.album.length() > 0);
            return meta.hasTags;
        }

        stream.seek(blockStart + blockLen);
    }

    return false;
}

// ==============================================================================
// 5. M4A / AAC Format Parser (QuickTime / MP4 ISO BMFF ilst Atoms)
// ==============================================================================
static bool scanM4AMetadataAtoms(MetadataStream &stream, uint32_t startOffset, uint32_t endOffset, TrackMetadata &meta, const char* parentItemType = nullptr) {
    if (startOffset >= endOffset) return false;

    stream.seek(startOffset);

    while (stream.position() + 8 <= endOffset && stream.available() >= 8) {
        uint32_t curPos = stream.position();
        uint8_t aHdr[8];
        if (stream.read(aHdr, 8) != 8) break;

        uint32_t aSize = ((uint32_t)aHdr[0] << 24) | ((uint32_t)aHdr[1] << 16) | ((uint32_t)aHdr[2] << 8) | (uint32_t)aHdr[3];
        char aType[5] = {0};
        memcpy(aType, &aHdr[4], 4);

        uint32_t headerLen = 8;
        if (aSize == 1) { // 64-bit atom size
            uint8_t largeHdr[8];
            if (stream.read(largeHdr, 8) != 8) break;
            uint32_t highSize = ((uint32_t)largeHdr[0] << 24) | ((uint32_t)largeHdr[1] << 16) | ((uint32_t)largeHdr[2] << 8) | (uint32_t)largeHdr[3];
            uint32_t lowSize = ((uint32_t)largeHdr[4] << 24) | ((uint32_t)largeHdr[5] << 16) | ((uint32_t)largeHdr[6] << 8) | (uint32_t)largeHdr[7];
            if (highSize > 0) break;
            aSize = lowSize;
            headerLen = 16;
        } else if (aSize == 0) {
            aSize = endOffset - curPos;
        }

        if (aSize < headerLen || curPos > endOffset || aSize > endOffset - curPos) break;

        // Container atom traversal
        if (strcmp(aType, "moov") == 0 || strcmp(aType, "udta") == 0 ||
            strcmp(aType, "meta") == 0 || strcmp(aType, "ilst") == 0) {
            uint32_t subStart = curPos + headerLen + (strcmp(aType, "meta") == 0 ? 4 : 0);
            uint32_t subEnd = curPos + aSize;
            scanM4AMetadataAtoms(stream, subStart, subEnd, meta, nullptr);
        } else if (strcmp(aType, "\xA9" "nam") == 0 || strcmp(aType, "©nam") == 0 ||
                   strcmp(aType, "\xA9" "ART") == 0 || strcmp(aType, "©ART") == 0 ||
                   strcmp(aType, "aART") == 0 ||
                   strcmp(aType, "\xA9" "alb") == 0 || strcmp(aType, "©alb") == 0 ||
                   strcmp(aType, "\xA9" "day") == 0 || strcmp(aType, "©day") == 0 ||
                   strcmp(aType, "\xA9" "gen") == 0 || strcmp(aType, "©gen") == 0 ||
                   strcmp(aType, "gnre") == 0 ||
                   strcmp(aType, "trkn") == 0 ||
                   strcmp(aType, "\xA9" "cmt") == 0 || strcmp(aType, "©cmt") == 0) {
            // Recurse into item to find child 'data' atom
            uint32_t subStart = curPos + headerLen;
            uint32_t subEnd = curPos + aSize;
            scanM4AMetadataAtoms(stream, subStart, subEnd, meta, aType);
        } else if (strcmp(aType, "data") == 0 && parentItemType != nullptr) {
            // Data atom: 4B size, 4B 'data', 4B type/flags, 4B locale
            if (aSize >= 16) {
                uint8_t flagsLocale[8];
                if (stream.read(flagsLocale, 8) == 8) {
                    uint32_t dataType = ((uint32_t)flagsLocale[0] << 24) | ((uint32_t)flagsLocale[1] << 16) |
                                        ((uint32_t)flagsLocale[2] << 8)  | (uint32_t)flagsLocale[3];
                    size_t payloadLen = aSize - 16;

                    if (dataType == 1 && payloadLen > 0 && payloadLen < 2048) { // UTF-8 Text
                        std::vector<char> valBuf(payloadLen + 1, 0);
                        if (stream.read((uint8_t*)valBuf.data(), payloadLen) == payloadLen) {
                            String val = MetadataParser::cleanString(String(valBuf.data()));
                            if (strcmp(parentItemType, "\xA9" "nam") == 0 || strcmp(parentItemType, "©nam") == 0) {
                                if (meta.title.length() == 0) meta.title = val;
                            } else if (strcmp(parentItemType, "\xA9" "ART") == 0 || strcmp(parentItemType, "©ART") == 0) {
                                if (meta.artist.length() == 0) meta.artist = val;
                            } else if (strcmp(parentItemType, "aART") == 0) {
                                if (meta.artist.length() == 0) meta.artist = val;
                            } else if (strcmp(parentItemType, "\xA9" "alb") == 0 || strcmp(parentItemType, "©alb") == 0) {
                                if (meta.album.length() == 0) meta.album = val;
                            } else if (strcmp(parentItemType, "\xA9" "day") == 0 || strcmp(parentItemType, "©day") == 0) {
                                if (meta.year.length() == 0) {
                                    if (val.length() >= 4 && val[0] >= '0' && val[0] <= '9') {
                                        meta.year = val.substring(0, 4);
                                    } else {
                                        meta.year = val;
                                    }
                                }
                            } else if (strcmp(parentItemType, "\xA9" "gen") == 0 || strcmp(parentItemType, "©gen") == 0) {
                                if (meta.genre.length() == 0) meta.genre = MetadataParser::normalizeGenre(val);
                            } else if (strcmp(parentItemType, "\xA9" "cmt") == 0 || strcmp(parentItemType, "©cmt") == 0) {
                                if (meta.comment.length() == 0) meta.comment = val;
                            }
                        }
                    } else if (strcmp(parentItemType, "trkn") == 0 && payloadLen >= 6) {
                        uint8_t trkBuf[8];
                        if (stream.read(trkBuf, 8) == 8) {
                            uint16_t trkNum = ((uint16_t)trkBuf[2] << 8) | trkBuf[3];
                            uint16_t trkTot = ((uint16_t)trkBuf[4] << 8) | trkBuf[5];
                            if (trkNum > 0) {
                                if (trkTot > 0) {
                                    meta.trackNumber = String((int)trkNum) + "/" + String((int)trkTot);
                                } else {
                                    meta.trackNumber = String((int)trkNum);
                                }
                            }
                        }
                    } else if (strcmp(parentItemType, "gnre") == 0 && payloadLen >= 2) {
                        uint8_t gBuf[2];
                        if (stream.read(gBuf, 2) == 2) {
                            uint16_t gVal = ((uint16_t)gBuf[0] << 8) | gBuf[1];
                            if (gVal > 0 && gVal <= 148) {
                                const char* lookedUp = MetadataParser::getID3v1Genre(gVal - 1);
                                if (lookedUp && lookedUp[0] != '\0' && meta.genre.length() == 0) {
                                    meta.genre = lookedUp;
                                }
                            }
                        }
                    }
                }
            }
        }

        stream.seek(curPos + aSize);
    }
    return (meta.title.length() > 0 || meta.artist.length() > 0 || meta.album.length() > 0);
}

bool MetadataParser::parseM4A(MetadataStream &stream, TrackMetadata &meta) {
    if (stream.size() < 128) return false;
    bool res = scanM4AMetadataAtoms(stream, 0, stream.size(), meta, nullptr);
    meta.hasTags = (meta.title.length() > 0 || meta.artist.length() > 0 || meta.album.length() > 0);
    return meta.hasTags;
}

// ==============================================================================
// 6. WAV Format Parser (RIFF LIST INFO & ID3 Chunk)
// ==============================================================================
bool MetadataParser::parseWAV(MetadataStream &stream, TrackMetadata &meta) {
    if (stream.size() < 44) return false;

    stream.seek(0);
    uint8_t riffHdr[12];
    if (stream.read(riffHdr, 12) != 12) return false;

    if (memcmp(riffHdr, "RIFF", 4) != 0 || memcmp(riffHdr + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool foundAny = false;

    while (stream.position() + 8 <= stream.size() && stream.available() >= 8) {
        uint32_t chunkPos = stream.position();
        char chunkId[5] = {0};
        uint32_t chunkSize = 0;

        if (stream.read((uint8_t*)chunkId, 4) != 4) break;
        uint8_t szBuf[4];
        if (stream.read(szBuf, 4) != 4) break;
        chunkSize = szBuf[0] | ((uint32_t)szBuf[1] << 8) | ((uint32_t)szBuf[2] << 16) | ((uint32_t)szBuf[3] << 24);

        uint32_t pad = (chunkSize & 1);

        if (strncmp(chunkId, "id3 ", 4) == 0 || strncmp(chunkId, "ID3 ", 4) == 0) {
            uint8_t hdr[10];
            if (stream.read(hdr, 10) == 10 && hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
                uint32_t tagSize = ((uint32_t)(hdr[6] & 0x7F) << 21) |
                                   ((uint32_t)(hdr[7] & 0x7F) << 14) |
                                   ((uint32_t)(hdr[8] & 0x7F) << 7)  |
                                   (uint32_t)(hdr[9] & 0x7F);
                if (parseID3v2(stream, chunkPos + 8 + 10, tagSize, hdr[3], meta)) {
                    foundAny = true;
                }
            }
        } else if (strncmp(chunkId, "LIST", 4) == 0 && chunkSize >= 4) {
            char listType[5] = {0};
            if (stream.read((uint8_t*)listType, 4) == 4 && strncmp(listType, "INFO", 4) == 0) {
                uint32_t infoEnd = chunkPos + 8 + chunkSize;
                while (stream.position() + 8 < infoEnd && stream.available() >= 8) {
                    char subId[5] = {0};
                    if (stream.read((uint8_t*)subId, 4) != 4) break;
                    uint8_t subSzBuf[4];
                    if (stream.read(subSzBuf, 4) != 4) break;
                    uint32_t subSize = subSzBuf[0] | ((uint32_t)subSzBuf[1] << 8) | ((uint32_t)subSzBuf[2] << 16) | ((uint32_t)subSzBuf[3] << 24);
                    uint32_t subPad = (subSize & 1);
                    uint32_t subStart = stream.position();

                    if (subSize > 0 && subSize < 1024) {
                        std::vector<char> strBuf(subSize + 1, 0);
                        if (stream.read((uint8_t*)strBuf.data(), subSize) == subSize) {
                            String val = cleanString(String(strBuf.data()));
                            if (strcmp(subId, "INAM") == 0 && meta.title.length() == 0) { meta.title = val; foundAny = true; }
                            else if (strcmp(subId, "IART") == 0 && meta.artist.length() == 0) { meta.artist = val; foundAny = true; }
                            else if (strcmp(subId, "IPRD") == 0 && meta.album.length() == 0) { meta.album = val; foundAny = true; }
                            else if ((strcmp(subId, "ICRD") == 0 || strcmp(subId, "YEAR") == 0) && meta.year.length() == 0) {
                                if (val.length() >= 4 && val[0] >= '0' && val[0] <= '9') {
                                    meta.year = val.substring(0, 4);
                                } else {
                                    meta.year = val;
                                }
                                foundAny = true;
                            }
                            else if (strcmp(subId, "IGNR") == 0 && meta.genre.length() == 0) { meta.genre = normalizeGenre(val); foundAny = true; }
                            else if ((strcmp(subId, "ITRK") == 0 || strcmp(subId, "PRT1") == 0) && meta.trackNumber.length() == 0) { meta.trackNumber = val; foundAny = true; }
                            else if (strcmp(subId, "ICMT") == 0 && meta.comment.length() == 0) { meta.comment = val; foundAny = true; }
                        }
                    }

                    stream.seek(subStart + subSize + subPad);
                }
            }
        }

        stream.seek(chunkPos + 8 + chunkSize + pad);
    }

    meta.hasTags = foundAny && (meta.title.length() > 0 || meta.artist.length() > 0 || meta.album.length() > 0);
    return meta.hasTags;
}
