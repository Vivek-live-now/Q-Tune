#include "album_art.h"
#include "sd_manager.h"
#include "tjpgd.h"
#include <vector>

extern SemaphoreHandle_t spiBusMutex;

struct JpegStream {
    File *file;
    uint32_t startOffset;
    uint32_t maxBytes;
    uint32_t bytesRead;
};

struct DecodeContext {
    uint8_t *grayBuffer;
    int targetW;
    int targetH;
};

static size_t jd_input_func(JDEC *jdec, uint8_t *buf, size_t len) {
    JpegStream *s = (JpegStream*)jdec->device;
    if (!s || !s->file || s->bytesRead >= s->maxBytes) return 0;

    size_t toRead = len;
    if (s->bytesRead + toRead > s->maxBytes) {
        toRead = s->maxBytes - s->bytesRead;
    }

    if (buf) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        size_t n = s->file->read(buf, toRead);
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        s->bytesRead += n;
        return n;
    } else {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        s->file->seek(s->file->position() + toRead);
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        s->bytesRead += toRead;
        return toRead;
    }
}

static int jd_output_func(JDEC *jdec, void *bitmap, JRECT *rect) {
    DecodeContext *ctx = (DecodeContext*)jdec->device;
    const uint8_t *src = (const uint8_t*)bitmap;
    int rw = rect->right - rect->left + 1;
    int rh = rect->bottom - rect->top + 1;

    for (int y = 0; y < rh; y++) {
        int dstY = rect->top + y;
        if (dstY >= ctx->targetH) break;
        for (int x = 0; x < rw; x++) {
            int dstX = rect->left + x;
            if (dstX >= ctx->targetW) break;
            ctx->grayBuffer[dstY * ctx->targetW + dstX] = src[y * rw + x];
        }
    }
    return 1;
}

AlbumArtManager::AlbumArtManager() :
    artWidth(56),
    artHeight(56),
    artLoaded(false),
    loadedTrackPath("") {
    memset(bitmap, 0, sizeof(bitmap));
}

void AlbumArtManager::begin() {
    clear();
}

void AlbumArtManager::clear() {
    artLoaded = false;
    loadedTrackPath = "";
    memset(bitmap, 0, sizeof(bitmap));
}

bool AlbumArtManager::hasArt() const {
    return artLoaded;
}

int AlbumArtManager::getWidth() const {
    return artWidth;
}

int AlbumArtManager::getHeight() const {
    return artHeight;
}

void AlbumArtManager::atkinsonDither(uint8_t *gray, int w, int h, uint8_t *out1Bit) {
    int totalPixels = w * h;
    int16_t *err = (int16_t*)malloc(totalPixels * sizeof(int16_t));
    if (!err) return;

    for (int i = 0; i < totalPixels; i++) err[i] = gray[i];
    memset(out1Bit, 0, (totalPixels + 7) / 8);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            int16_t oldVal = err[idx];
            if (oldVal < 0) oldVal = 0;
            if (oldVal > 255) oldVal = 255;

            uint8_t bit = (oldVal >= 128) ? 1 : 0;
            if (bit) {
                out1Bit[idx / 8] |= (1 << (7 - (idx % 8)));
            }

            int16_t error = oldVal - (bit ? 255 : 0);
            int16_t e8 = error >> 3; // Atkinson uses 1/8 factor

            if (x + 1 < w) err[idx + 1] += e8;
            if (x + 2 < w) err[idx + 2] += e8;
            if (y + 1 < h) {
                if (x - 1 >= 0) err[(y + 1) * w + (x - 1)] += e8;
                err[(y + 1) * w + x] += e8;
                if (x + 1 < w) err[(y + 1) * w + (x + 1)] += e8;
            }
            if (y + 2 < h) {
                err[(y + 2) * w + x] += e8;
            }
        }
    }
    free(err);
}

void AlbumArtManager::floydSteinbergDither(uint8_t *gray, int w, int h, uint8_t *out1Bit) {
    int totalPixels = w * h;
    int16_t *err = (int16_t*)malloc(totalPixels * sizeof(int16_t));
    if (!err) return;

    for (int i = 0; i < totalPixels; i++) err[i] = gray[i];
    memset(out1Bit, 0, (totalPixels + 7) / 8);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            int16_t oldVal = err[idx];
            if (oldVal < 0) oldVal = 0;
            if (oldVal > 255) oldVal = 255;

            uint8_t bit = (oldVal >= 128) ? 1 : 0;
            if (bit) {
                out1Bit[idx / 8] |= (1 << (7 - (idx % 8)));
            }

            int16_t error = oldVal - (bit ? 255 : 0);
            if (x + 1 < w) err[idx + 1] += (error * 7) / 16;
            if (y + 1 < h) {
                if (x - 1 >= 0) err[(y + 1) * w + (x - 1)] += (error * 3) / 16;
                err[(y + 1) * w + x] += (error * 5) / 16;
                if (x + 1 < w) err[(y + 1) * w + (x + 1)] += (error * 1) / 16;
            }
        }
    }
    free(err);
}

void AlbumArtManager::thresholdDither(const uint8_t *gray, int w, int h, uint8_t *out1Bit) {
    int totalPixels = w * h;
    memset(out1Bit, 0, (totalPixels + 7) / 8);
    for (int i = 0; i < totalPixels; i++) {
        if (gray[i] >= 128) {
            out1Bit[i / 8] |= (1 << (7 - (i % 8)));
        }
    }
}

bool AlbumArtManager::findMP3Art(File &file, uint32_t &offset, uint32_t &size) {
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
        if (fId[0] == 0) break; // Padding reached

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

        if (strcmp(fId, "APIC") == 0) {
            // Found Attached Picture!
            // Skip encoding (1 byte)
            file.read();
            // Read MIME type
            while (file.available() && file.position() < frameStart + fSize) {
                if (file.read() == 0) break;
            }
            // Skip picture type (1 byte)
            file.read();
            // Skip description string
            while (file.available() && file.position() < frameStart + fSize) {
                if (file.read() == 0) break;
            }

            offset = file.position();
            if (frameStart + fSize > offset) {
                size = (frameStart + fSize) - offset;
                return true;
            }
            return false;
        }

        file.seek(frameStart + fSize);
    }
    return false;
}

bool AlbumArtManager::findFLACArt(File &file, uint32_t &offset, uint32_t &size) {
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
        if (blockType == 6) { // METADATA_BLOCK_PICTURE
            // Skip picture type (4 bytes)
            file.seek(blockStart + 4);

            // Read MIME length
            uint8_t buf4[4];
            if (file.read(buf4, 4) != 4) break;
            uint32_t mimeLen = ((uint32_t)buf4[0] << 24) | ((uint32_t)buf4[1] << 16) | ((uint32_t)buf4[2] << 8) | (uint32_t)buf4[3];
            file.seek(file.position() + mimeLen);

            // Read description length
            if (file.read(buf4, 4) != 4) break;
            uint32_t descLen = ((uint32_t)buf4[0] << 24) | ((uint32_t)buf4[1] << 16) | ((uint32_t)buf4[2] << 8) | (uint32_t)buf4[3];
            file.seek(file.position() + descLen);

            // Skip width(4), height(4), depth(4), colors(4) = 16 bytes
            file.seek(file.position() + 16);

            // Read picture data length
            if (file.read(buf4, 4) != 4) break;
            uint32_t picLen = ((uint32_t)buf4[0] << 24) | ((uint32_t)buf4[1] << 16) | ((uint32_t)buf4[2] << 8) | (uint32_t)buf4[3];

            offset = file.position();
            size = picLen;
            return true;
        }

        file.seek(blockStart + blockLen);
    }
    return false;
}

bool AlbumArtManager::findM4AArt(File &file, uint32_t &offset, uint32_t &size) {
    if (!file || file.size() < 128) return false;
    file.seek(0);

    // Scan atoms looking for covr -> data
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
                // meta atom has 4 bytes flags/version before children
                file.seek(curPos + 12);
            }
            continue;
        } else if (strcmp(aType, "covr") == 0) {
            // Inside covr atom, find 'data' atom
            uint32_t covrEnd = curPos + aSize;
            while (file.position() + 8 <= covrEnd) {
                uint32_t subPos = file.position();
                uint8_t subHdr[8];
                if (file.read(subHdr, 8) != 8) break;
                uint32_t subSize = ((uint32_t)subHdr[0] << 24) | ((uint32_t)subHdr[1] << 16) | ((uint32_t)subHdr[2] << 8) | (uint32_t)subHdr[3];
                char subType[5] = {0};
                memcpy(subType, &subHdr[4], 4);

                if (strcmp(subType, "data") == 0) {
                    // data atom has 8 bytes header + 8 bytes type/flags = 16 bytes
                    offset = subPos + 16;
                    if (subSize > 16) {
                        size = subSize - 16;
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

bool AlbumArtManager::findFolderArt(const String &audioPath, String &artFilePath) {
    int lastSlash = audioPath.lastIndexOf('/');
    if (lastSlash <= 0) return false;
    String dir = audioPath.substring(0, lastSlash);

    // Candidates in priority order
    String candidates[4];
    candidates[0] = dir + "/cover.jpg";
    candidates[1] = dir + "/folder.jpg";
    candidates[2] = dir + "/album.jpg";

    int dot = audioPath.lastIndexOf('.');
    if (dot > lastSlash) {
        candidates[3] = audioPath.substring(0, dot) + ".jpg";
    }

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    for (int i = 0; i < 4; i++) {
        if (candidates[i].length() > 0 && SD.exists(candidates[i])) {
            artFilePath = candidates[i];
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
            return true;
        }
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return false;
}

bool AlbumArtManager::decodeJPEGFromStream(File &file, uint32_t offset, uint32_t size, int targetDim, DitherMode dither) {
    if (!file || size < 32) return false;

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    file.seek(offset);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    JpegStream stream;
    stream.file = &file;
    stream.startOffset = offset;
    stream.maxBytes = size;
    stream.bytesRead = 0;

    void *pool = malloc(TJPGD_WORKSPACE_SIZE);
    if (!pool) return false;

    JDEC jdec;
    jdec.device = &stream;

    JRESULT res = jd_prepare(&jdec, jd_input_func, pool, TJPGD_WORKSPACE_SIZE, &stream);
    if (res != JDR_OK) {
        free(pool);
        return false;
    }

    // Determine hardware downscale factor (0: 1/1, 1: 1/2, 2: 1/4, 3: 1/8)
    uint8_t scale = 0;
    if (jdec.width >= targetDim * 8) scale = 3;
    else if (jdec.width >= targetDim * 4) scale = 2;
    else if (jdec.width >= targetDim * 2) scale = 1;

    int scaledW = jdec.width >> scale;
    int scaledH = jdec.height >> scale;

    int targetW = targetDim;
    int targetH = targetDim;
    if (scaledW < targetW) targetW = scaledW;
    if (scaledH < targetH) targetH = scaledH;

    uint8_t *grayBuf = (uint8_t*)calloc(targetW * targetH, sizeof(uint8_t));
    if (!grayBuf) {
        free(pool);
        return false;
    }

    DecodeContext ctx;
    ctx.grayBuffer = grayBuf;
    ctx.targetW = targetW;
    ctx.targetH = targetH;
    jdec.device = &ctx;

    res = jd_decomp(&jdec, jd_output_func, scale);
    free(pool);

    if (res == JDR_OK) {
        artWidth = targetW;
        artHeight = targetH;

        if (dither == DITHER_ATKINSON) {
            atkinsonDither(grayBuf, targetW, targetH, bitmap);
        } else if (dither == DITHER_FLOYD_STEINBERG) {
            floydSteinbergDither(grayBuf, targetW, targetH, bitmap);
        } else {
            thresholdDither(grayBuf, targetW, targetH, bitmap);
        }
        artLoaded = true;
    }

    free(grayBuf);
    return artLoaded;
}

bool AlbumArtManager::loadForTrack(const String &audioPath) {
    if (audioPath.length() == 0) {
        clear();
        return false;
    }

    if (loadedTrackPath == audioPath && artLoaded) {
        return true; // Already cached in memory!
    }

    clear();
    loadedTrackPath = audioPath;

    if (playerConfig.getArtSource() == ART_SRC_DISABLED) {
        return false;
    }

    if (!sdManager.isMounted()) return false;

    DitherMode dither = playerConfig.getDitherMode();
    PlayerLayout layout = playerConfig.getLayout();
    int targetDim = (layout == LAYOUT_COVER_HERO) ? 64 : 56;

    uint32_t artOffset = 0;
    uint32_t artSize = 0;
    bool foundArt = false;

    // Check embedded art first if configured
    if (playerConfig.getArtSource() == ART_SRC_EMBEDDED_FIRST) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        File audioFile = SD.open(audioPath, FILE_READ);
        if (audioFile) {
            String lower = audioPath;
            lower.toLowerCase();
            if (lower.endsWith(".mp3")) {
                foundArt = findMP3Art(audioFile, artOffset, artSize);
            } else if (lower.endsWith(".flac")) {
                foundArt = findFLACArt(audioFile, artOffset, artSize);
            } else if (lower.endsWith(".m4a") || lower.endsWith(".aac")) {
                foundArt = findM4AArt(audioFile, artOffset, artSize);
            }

            if (foundArt) {
                decodeJPEGFromStream(audioFile, artOffset, artSize, targetDim, dither);
            }
            audioFile.close();
        }
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    }

    // If not found yet, check external folder art
    if (!artLoaded) {
        String folderArtPath = "";
        if (findFolderArt(audioPath, folderArtPath)) {
            if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
            File imgFile = SD.open(folderArtPath, FILE_READ);
            if (imgFile) {
                decodeJPEGFromStream(imgFile, 0, imgFile.size(), targetDim, dither);
                imgFile.close();
            }
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        }
    }

    return artLoaded;
}

void AlbumArtManager::draw(U8G2 &u8g2, int x, int y) {
    if (artLoaded) {
        // Draw 1-bit monochrome dithered album art bitmap
        u8g2.drawBitmap(x, y, (artWidth + 7) / 8, artHeight, bitmap);
        // Draw outer 1px frame
        u8g2.drawFrame(x - 1, y - 1, artWidth + 2, artHeight + 2);
    } else {
        drawDefaultIcon(u8g2, x, y);
    }
}

void AlbumArtManager::drawDefaultIcon(U8G2 &u8g2, int x, int y) {
    // Elegant Vinyl Record / Musical Note icon for tracks without cover art
    int w = 56;
    int h = 56;
    u8g2.drawFrame(x, y, w, h);
    int cx = x + w / 2;
    int cy = y + h / 2;
    u8g2.drawCircle(cx, cy, 22);
    u8g2.drawCircle(cx, cy, 14);
    u8g2.drawCircle(cx, cy, 6);
    u8g2.drawDisc(cx, cy, 3);
}

AlbumArtManager albumArtManager;
