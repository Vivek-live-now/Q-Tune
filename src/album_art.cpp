#include "album_art.h"
#include "sd_manager.h"
#include "tjpgd.h"
#include <vector>

extern SemaphoreHandle_t spiBusMutex;

struct JpegDecodeSession {
    File *file;
    uint32_t startOffset;
    uint32_t maxBytes;
    uint32_t bytesRead;
    uint8_t *grayBuffer;
    int targetW;
    int targetH;
    int cropX;
    int cropY;
};

static size_t jd_input_func(JDEC *jdec, uint8_t *buf, size_t len) {
    JpegDecodeSession *s = (JpegDecodeSession*)jdec->device;
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
    JpegDecodeSession *session = (JpegDecodeSession*)jdec->device;
    if (!session || !session->grayBuffer) return 0;

    const uint8_t *src = (const uint8_t*)bitmap;
    int rw = rect->right - rect->left + 1;
    int rh = rect->bottom - rect->top + 1;

    for (int y = 0; y < rh; y++) {
        int srcY = rect->top + y;
        int dstY = srcY - session->cropY;
        if (dstY < 0 || dstY >= session->targetH) continue;

        for (int x = 0; x < rw; x++) {
            int srcX = rect->left + x;
            int dstX = srcX - session->cropX;
            if (dstX < 0 || dstX >= session->targetW) continue;

            session->grayBuffer[dstY * session->targetW + dstX] = src[y * rw + x];
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

void AlbumArtManager::thresholdDither(const uint8_t *gray, int w, int h, uint8_t *out1Bit, uint8_t threshold) {
    int totalPixels = w * h;
    memset(out1Bit, 0, (totalPixels + 7) / 8);
    for (int i = 0; i < totalPixels; i++) {
        if (gray[i] >= threshold) {
            out1Bit[i / 8] |= (1 << (7 - (i % 8)));
        }
    }
}

void AlbumArtManager::bayer4Dither(const uint8_t *gray, int w, int h, uint8_t *out1Bit, uint8_t threshold) {
    // Bayer 4x4 ordered dithering matrix (values 0-15, normalized to 0-255 range)
    static const uint8_t bayer4[4][4] = {
        {  0, 136,  34, 170 },
        { 204,  68, 238, 102 },
        {  51, 187,  17, 153 },
        { 255, 119, 221,  85 }
    };
    int totalPixels = w * h;
    memset(out1Bit, 0, (totalPixels + 7) / 8);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            // Add Bayer matrix offset scaled around the threshold
            int16_t adjusted = (int16_t)gray[i] + ((int16_t)bayer4[y & 3][x & 3] - 128) / 2;
            if (adjusted < 0) adjusted = 0;
            if (adjusted > 255) adjusted = 255;
            if ((uint8_t)adjusted >= threshold) {
                out1Bit[i / 8] |= (1 << (7 - (i % 8)));
            }
        }
    }
}

void AlbumArtManager::sierraLiteDither(uint8_t *gray, int w, int h, uint8_t *out1Bit) {
    // Sierra Lite: fast 2-row error diffusion
    // Pattern:     X  2
    //           1  1       (out of 4)
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
            if (bit) out1Bit[idx / 8] |= (1 << (7 - (idx % 8)));

            int16_t error = oldVal - (bit ? 255 : 0);
            if (x + 1 < w)              err[idx + 1]             += (error * 2) / 4;
            if (y + 1 < h) {
                if (x - 1 >= 0)         err[(y+1)*w + (x-1)]    += (error * 1) / 4;
                                        err[(y+1)*w + x]         += (error * 1) / 4;
            }
        }
    }
    free(err);
}

void AlbumArtManager::stuckiDither(uint8_t *gray, int w, int h, uint8_t *out1Bit) {
    // Stucki error diffusion — very smooth gradients
    // Distribution (/ 42):
    //               X  8  4
    //     2  4  8  4  2
    //     1  2  4  2  1
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
            if (bit) out1Bit[idx / 8] |= (1 << (7 - (idx % 8)));

            int16_t e = oldVal - (bit ? 255 : 0);

#define STUCKI_ADD(dx, dy, num) \
            if ((x+(dx)) >= 0 && (x+(dx)) < w && (y+(dy)) >= 0 && (y+(dy)) < h) \
                err[(y+(dy))*w + (x+(dx))] += (e * (num)) / 42;

            STUCKI_ADD(+1, 0, 8)
            STUCKI_ADD(+2, 0, 4)
            STUCKI_ADD(-2, 1, 2)
            STUCKI_ADD(-1, 1, 4)
            STUCKI_ADD( 0, 1, 8)
            STUCKI_ADD(+1, 1, 4)
            STUCKI_ADD(+2, 1, 2)
            STUCKI_ADD(-2, 2, 1)
            STUCKI_ADD(-1, 2, 2)
            STUCKI_ADD( 0, 2, 4)
            STUCKI_ADD(+1, 2, 2)
            STUCKI_ADD(+2, 2, 1)
#undef STUCKI_ADD
        }
    }
    free(err);
}

void AlbumArtManager::applyBrightnessContrast(uint8_t *gray, int count, int8_t brightness, int8_t contrast) {
    if (brightness == 0 && contrast == 0) return;

    // Contrast: scale around midpoint 128.
    // contrast in -100..+100; map to factor 0.0..2.0 (0 = 1.0 = no change)
    // factor = 1.0 + contrast/100.0
    // Brightness: simple additive offset, scaled: brightness * 127 / 100
    int bOffset = (int)brightness * 127 / 100;
    // contrast factor as fixed-point *128: 128 + contrast * 128/100
    int cFactor128 = 128 + (int)contrast * 128 / 100;
    if (cFactor128 < 0) cFactor128 = 0;

    for (int i = 0; i < count; i++) {
        int32_t v = gray[i];
        // Apply contrast around midpoint
        v = 128 + ((v - 128) * cFactor128) / 128;
        // Apply brightness
        v += bOffset;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        gray[i] = (uint8_t)v;
    }
}


// Helper to scan for JPEG Start-Of-Image marker (0xFF, 0xD8) in a stream window
static bool scanForJPEGHeader(File &file, uint32_t frameStart, uint32_t frameSize, uint32_t &jpegOffset, uint32_t &jpegSize) {
    uint32_t scanLimit = frameStart + frameSize;
    if (scanLimit > file.size()) scanLimit = file.size();

    file.seek(frameStart);
    uint8_t buf[256];
    uint8_t prev = 0;
    uint32_t curPos = frameStart;

    while (curPos < scanLimit) {
        size_t toRead = sizeof(buf);
        if (curPos + toRead > scanLimit) {
            toRead = scanLimit - curPos;
        }
        size_t bytesRead = file.read(buf, toRead);
        if (bytesRead == 0) break;

        for (size_t i = 0; i < bytesRead; i++) {
            if (prev == 0xFF && buf[i] == 0xD8) {
                jpegOffset = curPos + i - 1;
                if (jpegOffset < frameStart + frameSize) {
                    jpegSize = (frameStart + frameSize) - jpegOffset;
                    return true;
                }
                return false;
            }
            prev = buf[i];
        }
        curPos += bytesRead;
    }
    return false;
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

    // Check for ID3v2 Extended Header
    if (hdr[5] & 0x40) {
        if (file.position() + 4 <= tagEnd) {
            uint8_t extHdr[4];
            if (file.read(extHdr, 4) == 4) {
                uint32_t extSize = ((uint32_t)(extHdr[0] & 0x7F) << 21) |
                                   ((uint32_t)(extHdr[1] & 0x7F) << 14) |
                                   ((uint32_t)(extHdr[2] & 0x7F) << 7)  |
                                   (uint32_t)(extHdr[3] & 0x7F);
                if (10 + extSize < tagEnd) {
                    file.seek(10 + extSize);
                }
            }
        }
    }

    uint8_t headerLen = (ver == 2) ? 6 : 10;

    while (file.position() + headerLen < tagEnd) {
        uint8_t fHdr[10];
        if (file.read(fHdr, headerLen) != headerLen) break;

        char fId[5] = {0};
        memcpy(fId, fHdr, (ver == 2) ? 3 : 4);
        if (fId[0] == 0) break; // Padding reached

        uint32_t fSize = 0;
        if (ver == 2) {
            fSize = ((uint32_t)fHdr[3] << 16) | ((uint32_t)fHdr[4] << 8) | (uint32_t)fHdr[5];
        } else if (ver == 4) {
            fSize = ((uint32_t)(fHdr[4] & 0x7F) << 21) |
                    ((uint32_t)(fHdr[5] & 0x7F) << 14) |
                    ((uint32_t)(fHdr[6] & 0x7F) << 7)  |
                    (uint32_t)(fHdr[7] & 0x7F);
        } else { // ID3v2.3
            fSize = ((uint32_t)fHdr[4] << 24) | ((uint32_t)fHdr[5] << 16) | ((uint32_t)fHdr[6] << 8) | (uint32_t)fHdr[7];
        }

        if (fSize == 0 || file.position() + fSize > tagEnd) break;
        uint32_t frameStart = file.position();

        if (strcmp(fId, "APIC") == 0 || strcmp(fId, "PIC") == 0) {
            if (scanForJPEGHeader(file, frameStart, fSize, offset, size)) {
                return true;
            }
        }

        file.seek(frameStart + fSize);
    }

    // Fallback: If APIC frame wasn't parsed (e.g. malformed frame header or syncsafe quirk),
    // scan the ID3 tag payload for embedded JPEG SOI marker
    if (tagEnd > 10) {
        if (scanForJPEGHeader(file, 10, tagEnd - 10, offset, size)) {
            return true;
        }
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
            if (scanForJPEGHeader(file, blockStart, blockLen, offset, size)) {
                return true;
            }
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
                    if (scanForJPEGHeader(file, subPos, subSize, offset, size)) {
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

    JpegDecodeSession session;
    session.file = &file;
    session.startOffset = offset;
    session.maxBytes = size;
    session.bytesRead = 0;
    session.grayBuffer = nullptr;
    session.targetW = 0;
    session.targetH = 0;
    session.cropX = 0;
    session.cropY = 0;

    void *pool = malloc(TJPGD_WORKSPACE_SIZE);
    if (!pool) return false;

    JDEC jdec;
    jdec.device = &session;

    JRESULT res = jd_prepare(&jdec, jd_input_func, pool, TJPGD_WORKSPACE_SIZE, &session);
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

    int cropX = 0;
    int cropY = 0;
    if (scaledW > targetW) cropX = (scaledW - targetW) / 2;
    if (scaledH > targetH) cropY = (scaledH - targetH) / 2;

    uint8_t *grayBuf = (uint8_t*)calloc(targetW * targetH, sizeof(uint8_t));
    if (!grayBuf) {
        free(pool);
        return false;
    }

    session.grayBuffer = grayBuf;
    session.targetW = targetW;
    session.targetH = targetH;
    session.cropX = cropX;
    session.cropY = cropY;

    // jdec.device remains &session throughout decompression
    res = jd_decomp(&jdec, jd_output_func, scale);
    free(pool);

    if (res == JDR_OK) {
        artWidth = targetW;
        artHeight = targetH;

        // Apply brightness/contrast preprocessing before dithering
        int8_t  bri = playerConfig.getArtBrightness();
        int8_t  con = playerConfig.getArtContrast();
        uint8_t thr = playerConfig.getArtThreshold();
        applyBrightnessContrast(grayBuf, targetW * targetH, bri, con);

        switch (dither) {
            case DITHER_ATKINSON:
                atkinsonDither(grayBuf, targetW, targetH, bitmap);
                break;
            case DITHER_FLOYD_STEINBERG:
                floydSteinbergDither(grayBuf, targetW, targetH, bitmap);
                break;
            case DITHER_BAYER4:
                bayer4Dither(grayBuf, targetW, targetH, bitmap, thr);
                break;
            case DITHER_SIERRA_LITE:
                sierraLiteDither(grayBuf, targetW, targetH, bitmap);
                break;
            case DITHER_STUCKI:
                stuckiDither(grayBuf, targetW, targetH, bitmap);
                break;
            case DITHER_THRESHOLD:
            default:
                thresholdDither(grayBuf, targetW, targetH, bitmap, thr);
                break;
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

    auto tryLoadEmbedded = [&]() -> bool {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        File audioFile = SD.open(audioPath, FILE_READ);
        if (audioFile) {
            String lower = audioPath;
            lower.toLowerCase();
            uint32_t artOffset = 0;
            uint32_t artSize = 0;
            bool foundArt = false;
            if (lower.endsWith(".mp3")) {
                foundArt = findMP3Art(audioFile, artOffset, artSize);
            } else if (lower.endsWith(".flac")) {
                foundArt = findFLACArt(audioFile, artOffset, artSize);
            } else if (lower.endsWith(".m4a") || lower.endsWith(".aac")) {
                foundArt = findM4AArt(audioFile, artOffset, artSize);
            }
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

            if (foundArt) {
                decodeJPEGFromStream(audioFile, artOffset, artSize, targetDim, dither);
            }
            if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
            audioFile.close();
        }
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return artLoaded;
    };

    auto tryLoadFolder = [&]() -> bool {
        String folderArtPath = "";
        if (findFolderArt(audioPath, folderArtPath)) {
            if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
            File imgFile = SD.open(folderArtPath, FILE_READ);
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
            if (imgFile) {
                decodeJPEGFromStream(imgFile, 0, imgFile.size(), targetDim, dither);
                if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                imgFile.close();
                if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
            }
        }
        return artLoaded;
    };

    if (playerConfig.getArtSource() == ART_SRC_EMBEDDED_FIRST) {
        if (!tryLoadEmbedded()) {
            tryLoadFolder();
        }
    } else if (playerConfig.getArtSource() == ART_SRC_FOLDER_FIRST) {
        if (!tryLoadFolder()) {
            tryLoadEmbedded();
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
