#include "audio_decoder.h"
#include "hw_config.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DRWAV_ASSERT(x) ((void)0)
#include "dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DRFLAC_ASSERT(x) ((void)0)
#include "dr_flac.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#if __has_include(<libhelix-aac/aacdec.h>)
#include <libhelix-aac/aacdec.h>
#elif __has_include("libhelix-aac/aacdec.h")
#include "libhelix-aac/aacdec.h"
#elif __has_include("aacdec.h")
#include "aacdec.h"
#else
extern "C" {
typedef void *HAACDecoder;
typedef struct _AACFrameInfo {
    int bitRate;
    int nChans;
    int sampRateCore;
    int sampRateOut;
    int bitsPerSample;
    int outputSamps;
    int profile;
    int tnsUsed;
    int pnsUsed;
} AACFrameInfo;
HAACDecoder AACInitDecoder(void);
void AACFreeDecoder(HAACDecoder hAACDecoder);
int AACDecode(HAACDecoder hAACDecoder, unsigned char **inbuf, int *bytesLeft, short *outbuf);
int AACFindSyncWord(unsigned char *buf, int nBytes);
void AACGetLastFrameInfo(HAACDecoder hAACDecoder, AACFrameInfo *aacFrameInfo);
int AACSetRawBlockParams(HAACDecoder hAACDecoder, int copyLast, AACFrameInfo *aacFrameInfo);
int AACFlushCodec(HAACDecoder hAACDecoder);
}
#endif

#ifndef ERR_AAC_NONE
#define ERR_AAC_NONE 0
#endif
#ifndef ERR_AAC_INDATA_UNDERFLOW
#define ERR_AAC_INDATA_UNDERFLOW -1
#endif

extern SemaphoreHandle_t spiBusMutex;

static size_t wav_read_cb(void* pUserData, void* pBufferOut, size_t bytesToRead) {
    File* file = (File*)pUserData;
    if (!file || !(*file)) return 0;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    int n = file->read((uint8_t*)pBufferOut, bytesToRead);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    if (n <= 0) return 0;
    return (size_t)n;
}

static drwav_bool32 wav_seek_cb(void* pUserData, int offset, drwav_seek_origin origin) {
    File* file = (File*)pUserData;
    if (!file || !(*file)) return DRWAV_FALSE;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    drwav_bool32 res = DRWAV_FALSE;
    if (origin == DRWAV_SEEK_SET) {
        if (offset >= 0) {
            res = file->seek((uint32_t)offset) ? DRWAV_TRUE : DRWAV_FALSE;
        }
    } else if (origin == DRWAV_SEEK_CUR) {
        int64_t target = (int64_t)file->position() + offset;
        if (target >= 0 && target <= (int64_t)file->size()) {
            res = file->seek((uint32_t)target) ? DRWAV_TRUE : DRWAV_FALSE;
        }
    } else if (origin == DRWAV_SEEK_END) {
        int64_t target = (int64_t)file->size() + offset;
        if (target >= 0 && target <= (int64_t)file->size()) {
            res = file->seek((uint32_t)target) ? DRWAV_TRUE : DRWAV_FALSE;
        }
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return res;
}

static drwav_bool32 wav_tell_cb(void* pUserData, drwav_int64* pCursor) {
    File* file = (File*)pUserData;
    if (!file || !(*file) || !pCursor) return DRWAV_FALSE;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    *pCursor = (drwav_int64)file->position();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return DRWAV_TRUE;
}

WAVDecoder::WAVDecoder() :
    pWavHandle(nullptr), sampleRate(44100), channels(2), bitsPerSample(16),
    totalBytes(0), bytesReadSoFar(0) {}

WAVDecoder::~WAVDecoder() {
    close();
}

void WAVDecoder::close() {
    if (pWavHandle) {
        drwav_uninit((drwav*)pWavHandle);
        free(pWavHandle);
        pWavHandle = nullptr;
    }
    if (srcFile) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        srcFile.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    }
    sampleRate = 44100;
    channels = 2;
    bitsPerSample = 16;
    totalBytes = 0;
    bytesReadSoFar = 0;
}

bool WAVDecoder::isOpen() const {
    return (pWavHandle != nullptr);
}

bool WAVDecoder::open(File &file) {
    close();
    srcFile = file;
    if (!srcFile || srcFile.size() < 44) return false;

    drwav* pWav = (drwav*)malloc(sizeof(drwav));
    if (!pWav) return false;

    srcFile.seek(0);
    // DRWAV_SEQUENTIAL avoids backward seeks and stops immediately at the data chunk header
    if (!drwav_init_ex(pWav, wav_read_cb, wav_seek_cb, wav_tell_cb, NULL, &srcFile, NULL, DRWAV_SEQUENTIAL, NULL)) {
        free(pWav);
        srcFile = File();
        return false;
    }

    pWavHandle = (void*)pWav;
    sampleRate = pWav->sampleRate;
    channels = pWav->channels;
    bitsPerSample = 16; // drwav_read_pcm_frames_s16 normalizes all bit depths to 16-bit
    totalBytes = (uint32_t)(pWav->totalPCMFrameCount * channels * sizeof(int16_t));
    if (totalBytes == 0 && channels > 0) {
        uint32_t rawDataSize = (srcFile.size() > pWav->dataChunkDataPos) ? (srcFile.size() - (uint32_t)pWav->dataChunkDataPos) : 0;
        uint32_t rawBytesPerFrame = pWav->channels * (pWav->bitsPerSample / 8);
        if (rawBytesPerFrame > 0) {
            uint32_t frames = rawDataSize / rawBytesPerFrame;
            totalBytes = frames * channels * sizeof(int16_t);
        }
    }
    bytesReadSoFar = 0;
    return true;
}

int WAVDecoder::readSamples(uint8_t *buffer, size_t maxBytes) {
    if (!pWavHandle || !srcFile) return 0;
    drwav* pWav = (drwav*)pWavHandle;

    size_t frameSize = sizeof(int16_t) * channels;
    if (frameSize == 0) return 0;
    drwav_uint64 framesToRead = maxBytes / frameSize;
    if (framesToRead == 0) return 0;

    drwav_uint64 framesRead = drwav_read_pcm_frames_s16(pWav, framesToRead, (drwav_int16*)buffer);
    size_t bytesRead = (size_t)(framesRead * frameSize);
    bytesReadSoFar += bytesRead;
    return (int)bytesRead;
}

uint32_t WAVDecoder::getSampleRate() const { return sampleRate; }
uint16_t WAVDecoder::getChannels() const { return channels; }
uint16_t WAVDecoder::getBitsPerSample() const { return bitsPerSample; }
uint32_t WAVDecoder::getTotalBytes() const { return totalBytes; }


static size_t flac_read_cb(void* pUserData, void* pBufferOut, size_t bytesToRead) {
    File* file = (File*)pUserData;
    if (!file || !(*file)) return 0;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    int n = file->read((uint8_t*)pBufferOut, bytesToRead);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    if (n <= 0) return 0;
    return (size_t)n;
}

static drflac_bool32 flac_seek_cb(void* pUserData, int offset, drflac_seek_origin origin) {
    File* file = (File*)pUserData;
    if (!file || !(*file)) return DRFLAC_FALSE;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    drflac_bool32 res = DRFLAC_FALSE;
    if (origin == DRFLAC_SEEK_SET) {
        if (offset >= 0) {
            res = file->seek((uint32_t)offset) ? DRFLAC_TRUE : DRFLAC_FALSE;
        }
    } else if (origin == DRFLAC_SEEK_CUR) {
        int64_t target = (int64_t)file->position() + offset;
        if (target >= 0 && target <= (int64_t)file->size()) {
            res = file->seek((uint32_t)target) ? DRFLAC_TRUE : DRFLAC_FALSE;
        }
    } else if (origin == DRFLAC_SEEK_END) {
        int64_t target = (int64_t)file->size() + offset;
        if (target >= 0 && target <= (int64_t)file->size()) {
            res = file->seek((uint32_t)target) ? DRFLAC_TRUE : DRFLAC_FALSE;
        }
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return res;
}

static drflac_bool32 flac_tell_cb(void* pUserData, drflac_int64* pCursor) {
    File* file = (File*)pUserData;
    if (!file || !(*file) || !pCursor) return DRFLAC_FALSE;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    *pCursor = (drflac_int64)file->position();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return DRFLAC_TRUE;
}

static void* flac_malloc_cb(size_t sz, void* pUserData) {
    (void)pUserData;
    if (psramFound()) {
        void* p = ps_malloc(sz);
        if (p) return p;
    }
    return malloc(sz);
}

static void* flac_realloc_cb(void* p, size_t sz, void* pUserData) {
    (void)pUserData;
    if (!p) return flac_malloc_cb(sz, pUserData);
    return realloc(p, sz);
}

static void flac_free_cb(void* p, void* pUserData) {
    (void)pUserData;
    free(p);
}

FLACDecoder::FLACDecoder() :
    pFlacHandle(nullptr), sampleRate(44100), channels(2), bitsPerSample(16),
    totalBytes(0), bytesReadSoFar(0) {}

FLACDecoder::~FLACDecoder() {
    close();
}

void FLACDecoder::close() {
    if (pFlacHandle) {
        drflac_close((drflac*)pFlacHandle);
        pFlacHandle = nullptr;
    }
    if (srcFile) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        srcFile.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    }
    sampleRate = 44100;
    channels = 2;
    bitsPerSample = 16;
    totalBytes = 0;
    bytesReadSoFar = 0;
}

bool FLACDecoder::isOpen() const {
    return (pFlacHandle != nullptr);
}

bool FLACDecoder::open(File &file) {
    close();
    srcFile = file;
    if (!srcFile || srcFile.size() < 42) return false;

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    srcFile.seek(0);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    drflac_allocation_callbacks allocCallbacks;
    allocCallbacks.pUserData = nullptr;
    allocCallbacks.onMalloc = flac_malloc_cb;
    allocCallbacks.onRealloc = flac_realloc_cb;
    allocCallbacks.onFree = flac_free_cb;

    drflac* pFlac = drflac_open(flac_read_cb, flac_seek_cb, flac_tell_cb, &srcFile, &allocCallbacks);
    if (!pFlac) {
        srcFile = File();
        return false;
    }

    pFlacHandle = (void*)pFlac;
    sampleRate = pFlac->sampleRate;
    channels = pFlac->channels;
    bitsPerSample = pFlac->bitsPerSample;
    totalBytes = (uint32_t)(pFlac->totalPCMFrameCount * channels * sizeof(int16_t));
    bytesReadSoFar = 0;
    return true;
}

int FLACDecoder::readSamples(uint8_t *buffer, size_t maxBytes) {
    if (!pFlacHandle || !srcFile) return 0;
    drflac* pFlac = (drflac*)pFlacHandle;

    size_t frameSize = sizeof(int16_t) * channels;
    if (frameSize == 0) return 0;
    drflac_uint64 framesToRead = maxBytes / frameSize;
    if (framesToRead == 0) return 0;

    drflac_uint64 framesRead = drflac_read_pcm_frames_s16(pFlac, framesToRead, (drflac_int16*)buffer);
    size_t bytesRead = (size_t)(framesRead * frameSize);
    bytesReadSoFar += bytesRead;
    return (int)bytesRead;
}

uint32_t FLACDecoder::getSampleRate() const { return sampleRate; }
uint16_t FLACDecoder::getChannels() const { return channels; }
uint16_t FLACDecoder::getBitsPerSample() const { return bitsPerSample; }
uint32_t FLACDecoder::getTotalBytes() const { return totalBytes; }

// ============================================================================
// MP3 Decoder (Powered by minimp3 single-header engine)
// ============================================================================

MP3Decoder::MP3Decoder() :
    pMp3Handle(nullptr), pScratch(nullptr), sampleRate(44100), channels(2), bitsPerSample(16),
    totalBytes(0), bytesReadSoFar(0), dataStartOffset(0),
    inputBufferLen(0), inputBufferPos(0),
    pcmBufferLen(0), pcmBufferPos(0) {
    pMp3Handle = malloc(sizeof(mp3dec_t));
    if (pMp3Handle) {
        mp3dec_init((mp3dec_t*)pMp3Handle);
    }
    pScratch = malloc(sizeof(mp3dec_scratch_t));
}

MP3Decoder::~MP3Decoder() {
    close();
    if (pMp3Handle) {
        free(pMp3Handle);
        pMp3Handle = nullptr;
    }
    if (pScratch) {
        free(pScratch);
        pScratch = nullptr;
    }
}

void MP3Decoder::close() {
    if (srcFile) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        srcFile.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    }
    sampleRate = 44100;
    channels = 2;
    bitsPerSample = 16;
    totalBytes = 0;
    bytesReadSoFar = 0;
    dataStartOffset = 0;
    inputBufferLen = 0;
    inputBufferPos = 0;
    pcmBufferLen = 0;
    pcmBufferPos = 0;
}

bool MP3Decoder::isOpen() const {
    return (srcFile && (pMp3Handle != nullptr));
}

bool MP3Decoder::open(File &file) {
    close();
    srcFile = file;
    if (!srcFile || srcFile.size() < 128) return false;

    if (!pMp3Handle) {
        pMp3Handle = malloc(sizeof(mp3dec_t));
        if (!pMp3Handle) return false;
    }
    if (!pScratch) {
        pScratch = malloc(sizeof(mp3dec_scratch_t));
        if (!pScratch) return false;
    }
    mp3dec_init((mp3dec_t*)pMp3Handle);

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    srcFile.seek(0);
    uint8_t id3Hdr[10];
    if (srcFile.read(id3Hdr, 10) == 10) {
        if (id3Hdr[0] == 'I' && id3Hdr[1] == 'D' && id3Hdr[2] == '3') {
            uint32_t tagSize = ((uint32_t)(id3Hdr[6] & 0x7F) << 21) |
                               ((uint32_t)(id3Hdr[7] & 0x7F) << 14) |
                               ((uint32_t)(id3Hdr[8] & 0x7F) << 7)  |
                               ((uint32_t)(id3Hdr[9] & 0x7F));
            dataStartOffset = tagSize + 10;
        } else {
            dataStartOffset = 0;
        }
    }
    srcFile.seek(dataStartOffset);
    inputBufferLen = srcFile.read(inputBuffer, sizeof(inputBuffer));
    inputBufferPos = 0;
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    if (inputBufferLen < 4) return false;

    mp3dec_frame_info_t info;
    int samples = 0;
    // Scan frames with dynamic refilling to handle large ID3 tags, Xing/Info headers, and 320kbps frames
    for (int attempt = 0; attempt < 64; attempt++) {
        size_t unparsed = inputBufferLen - inputBufferPos;
        if (unparsed < 2304 && srcFile.available() > 0) {
            if (unparsed > 0) {
                memmove(inputBuffer, inputBuffer + inputBufferPos, unparsed);
            }
            if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
            int bytesRead = srcFile.read(inputBuffer + unparsed, sizeof(inputBuffer) - unparsed);
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
            inputBufferLen = unparsed + ((bytesRead > 0) ? bytesRead : 0);
            inputBufferPos = 0;
            unparsed = inputBufferLen;
        }

        if (unparsed < 4) break;

        samples = mp3dec_decode_frame_scratch((mp3dec_t*)pMp3Handle,
                                              inputBuffer + inputBufferPos,
                                              inputBufferLen - inputBufferPos,
                                              pcmBuffer,
                                              &info,
                                              pScratch);
        if (info.hz > 0 && samples > 0) {
            break;
        }
        inputBufferPos += (info.frame_bytes > 0) ? info.frame_bytes : 1;
    }

    if (info.hz > 0) {
        sampleRate = info.hz;
        channels = info.channels;
        bitsPerSample = 16;
        pcmBufferLen = samples * channels;
        pcmBufferPos = 0;
        inputBufferPos += info.frame_bytes;

        uint32_t audioFileSize = srcFile.size() - dataStartOffset;
        if (info.bitrate_kbps > 0) {
            uint32_t durationSec = (audioFileSize * 8) / (info.bitrate_kbps * 1000);
            totalBytes = durationSec * sampleRate * channels * sizeof(int16_t);
        } else {
            totalBytes = audioFileSize * 4;
        }
        bytesReadSoFar = 0;
        return true;
    }

    return false;
}

int MP3Decoder::readSamples(uint8_t *buffer, size_t maxBytes) {
    if (!srcFile || !pMp3Handle || !pScratch || buffer == nullptr || maxBytes == 0) return 0;

    size_t bytesWritten = 0;
    int16_t *outPtr = (int16_t*)buffer;
    size_t samplesNeeded = maxBytes / sizeof(int16_t);

    while (samplesNeeded > 0) {
        if (pcmBufferPos < pcmBufferLen) {
            size_t available = pcmBufferLen - pcmBufferPos;
            size_t toCopy = (samplesNeeded < available) ? samplesNeeded : available;
            memcpy(outPtr, &pcmBuffer[pcmBufferPos], toCopy * sizeof(int16_t));
            pcmBufferPos += toCopy;
            outPtr += toCopy;
            samplesNeeded -= toCopy;
            bytesWritten += toCopy * sizeof(int16_t);
        } else {
            // Need to decode next MP3 frame
            size_t unparsed = inputBufferLen - inputBufferPos;
            // Refill when buffer is below 2304 bytes (MAX_FREE_FORMAT_FRAME_SIZE) to guarantee
            // that 320 kbps frames (1045 bytes + bit reservoir 511 bytes + next header 4 bytes) never underflow
            if (unparsed < 2304 && srcFile.available() > 0) {
                if (unparsed > 0) {
                    memmove(inputBuffer, inputBuffer + inputBufferPos, unparsed);
                }
                if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                int bytesRead = srcFile.read(inputBuffer + unparsed, sizeof(inputBuffer) - unparsed);
                if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
                inputBufferLen = unparsed + ((bytesRead > 0) ? bytesRead : 0);
                inputBufferPos = 0;
            }

            if (inputBufferPos >= inputBufferLen) {
                break; // EOF
            }

            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame_scratch((mp3dec_t*)pMp3Handle,
                                                      inputBuffer + inputBufferPos,
                                                      inputBufferLen - inputBufferPos,
                                                      pcmBuffer,
                                                      &info,
                                                      pScratch);

            inputBufferPos += (info.frame_bytes > 0) ? info.frame_bytes : 1;

            if (samples > 0) {
                pcmBufferLen = samples * info.channels;
                pcmBufferPos = 0;
            }
        }
    }

    bytesReadSoFar += bytesWritten;
    return (int)bytesWritten;
}

uint32_t MP3Decoder::getSampleRate() const { return sampleRate; }
uint16_t MP3Decoder::getChannels() const { return channels; }
uint16_t MP3Decoder::getBitsPerSample() const { return bitsPerSample; }
uint32_t MP3Decoder::getTotalBytes() const { return totalBytes; }

// ============================================================================
// M4A & AAC Decoder (ISO Base Media Container Parser & Stream Demuxer)
// ============================================================================

M4ADecoder::M4ADecoder() :
    pAacHandle(nullptr), isRawADTS(false), sampleRate(44100), channels(2), bitsPerSample(16),
    totalBytes(0), bytesReadSoFar(0), mdatOffset(0), mdatSize(0), currentFilePos(0),
    inputBufferLen(0), inputBufferPos(0), pcmBufferLen(0), pcmBufferPos(0) {
    pAacHandle = AACInitDecoder();
}

M4ADecoder::~M4ADecoder() {
    close();
    if (pAacHandle) {
        AACFreeDecoder((HAACDecoder)pAacHandle);
        pAacHandle = nullptr;
    }
}

void M4ADecoder::close() {
    if (pAacHandle) {
        AACFlushCodec((HAACDecoder)pAacHandle);
        AACFreeDecoder((HAACDecoder)pAacHandle);
        pAacHandle = nullptr;
    }
    if (srcFile) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        srcFile.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    }
    sampleRate = 44100;
    channels = 2;
    bitsPerSample = 16;
    totalBytes = 0;
    bytesReadSoFar = 0;
    mdatOffset = 0;
    mdatSize = 0;
    currentFilePos = 0;
    inputBufferLen = 0;
    inputBufferPos = 0;
    pcmBufferLen = 0;
    pcmBufferPos = 0;
    isRawADTS = false;
}

bool M4ADecoder::isOpen() const {
    return (srcFile != false);
}

bool M4ADecoder::parseADTSHeader(const uint8_t *hdr, uint32_t &sRate, uint16_t &chans, uint32_t &frameLen) {
    if (!hdr) return false;
    if (hdr[0] != 0xFF || (hdr[1] & 0xF0) != 0xF0) return false;

    static const uint32_t sampleRates[16] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
        16000, 12000, 11025, 8000,  7350,  0,     0,     0
    };

    uint8_t freqIdx = (hdr[2] >> 2) & 0x0F;
    if (freqIdx >= 13) return false;
    sRate = sampleRates[freqIdx];

    chans = ((hdr[2] & 0x01) << 2) | ((hdr[3] >> 6) & 0x03);
    if (chans == 0) chans = 2;

    frameLen = ((uint32_t)(hdr[3] & 0x03) << 11) | ((uint32_t)hdr[4] << 3) | ((hdr[5] >> 5) & 0x07);
    return (frameLen >= 7);
}

bool M4ADecoder::parseM4AAtoms(File &file) {
    if (!file || file.size() < 32) return false;
    file.seek(0);

    bool foundFtyp = false;
    bool foundMoov = false;
    uint32_t durationMs = 0;

    while (file.available() >= 8) {
        uint32_t curPos = file.position();
        uint8_t hdr[8];
        if (file.read(hdr, 8) != 8) break;
        uint32_t atomSize = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3];
        char atomType[5] = {0};
        memcpy(atomType, &hdr[4], 4);
        atomType[4] = '\0';

        uint32_t headerLen = 8;
        if (atomSize == 0) {
            // Atom extends to end of file
            atomSize = file.size() - curPos;
        } else if (atomSize == 1) {
            // 64-bit large size
            uint8_t largeHdr[8];
            if (file.read(largeHdr, 8) != 8) break;
            atomSize = ((uint32_t)largeHdr[4] << 24) | ((uint32_t)largeHdr[5] << 16) | ((uint32_t)largeHdr[6] << 8) | (uint32_t)largeHdr[7];
            headerLen = 16;
        } else if (atomSize < 8) {
            break;
        }

        if (strcmp(atomType, "ftyp") == 0) {
            foundFtyp = true;
            file.seek(curPos + atomSize);
        } else if (strcmp(atomType, "mdat") == 0) {
            mdatOffset = curPos + headerLen;
            mdatSize = (atomSize > headerLen) ? (atomSize - headerLen) : 0;
            file.seek(curPos + atomSize);
        } else if (strcmp(atomType, "moov") == 0) {
            foundMoov = true;
            uint32_t moovEnd = curPos + atomSize;
            while (file.position() < moovEnd && file.available() >= 8) {
                uint32_t subPos = file.position();
                uint8_t subHdr[8];
                if (file.read(subHdr, 8) != 8) break;
                uint32_t subSize = ((uint32_t)subHdr[0] << 24) | ((uint32_t)subHdr[1] << 16) | ((uint32_t)subHdr[2] << 8) | (uint32_t)subHdr[3];
                char subType[5] = {0};
                memcpy(subType, &subHdr[4], 4);
                subType[4] = '\0';

                if (subSize < 8) { file.seek(subPos + 8); continue; }

                if (strcmp(subType, "mdhd") == 0) {
                    uint8_t mdhdBuf[24];
                    if (file.read(mdhdBuf, sizeof(mdhdBuf)) >= 20) {
                        uint8_t ver = mdhdBuf[0];
                        uint32_t timeScale = 0;
                        uint32_t duration = 0;
                        if (ver == 0) {
                            timeScale = ((uint32_t)mdhdBuf[12] << 24) | ((uint32_t)mdhdBuf[13] << 16) | ((uint32_t)mdhdBuf[14] << 8) | (uint32_t)mdhdBuf[15];
                            duration = ((uint32_t)mdhdBuf[16] << 24) | ((uint32_t)mdhdBuf[17] << 16) | ((uint32_t)mdhdBuf[18] << 8) | (uint32_t)mdhdBuf[19];
                        } else {
                            timeScale = ((uint32_t)mdhdBuf[20] << 24) | ((uint32_t)mdhdBuf[21] << 16) | ((uint32_t)mdhdBuf[22] << 8) | (uint32_t)mdhdBuf[23];
                        }
                        if (timeScale > 0) {
                            durationMs = (uint32_t)((duration * 1000ULL) / timeScale);
                        }
                    }
                    file.seek(subPos + subSize);
                } else if (strcmp(subType, "mp4a") == 0) {
                    uint8_t mp4aBuf[32];
                    if (file.read(mp4aBuf, sizeof(mp4aBuf)) >= 28) {
                        channels = ((uint16_t)mp4aBuf[16] << 8) | mp4aBuf[17];
                        sampleRate = ((uint32_t)mp4aBuf[24] << 8) | mp4aBuf[25];
                        if (sampleRate == 0) {
                            sampleRate = ((uint32_t)mp4aBuf[22] << 8) | mp4aBuf[23];
                        }
                    }
                    file.seek(subPos + subSize);
                } else if (strcmp(subType, "stsd") == 0) {
                    file.seek(subPos + 16);
                    continue;
                } else if (strcmp(subType, "trak") == 0 || strcmp(subType, "mdia") == 0 || strcmp(subType, "minf") == 0 || strcmp(subType, "stbl") == 0) {
                    continue;
                } else {
                    file.seek(subPos + subSize);
                }
            }
            file.seek(curPos + atomSize);
        } else {
            file.seek(curPos + atomSize);
        }
    }

    if (sampleRate == 0) sampleRate = 44100;
    if (channels == 0) channels = 2;
    bitsPerSample = 16;

    if (durationMs > 0) {
        totalBytes = (uint32_t)((durationMs * (uint64_t)sampleRate * channels * 2ULL) / 1000ULL);
    } else if (mdatSize > 0) {
        totalBytes = mdatSize * 6;
    } else {
        totalBytes = file.size() * 6;
    }

    return (foundFtyp || foundMoov);
}

bool M4ADecoder::open(File &file) {
    close();
    srcFile = file;
    if (!srcFile || srcFile.size() < 64) return false;

    if (!pAacHandle) {
        pAacHandle = AACInitDecoder();
        if (!pAacHandle) return false;
    }

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    srcFile.seek(0);
    uint8_t initHdr[8];
    if (srcFile.read(initHdr, 8) != 8) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    uint32_t adtsRate = 0;
    uint16_t adtsChans = 0;
    uint32_t adtsFrameLen = 0;
    if (parseADTSHeader(initHdr, adtsRate, adtsChans, adtsFrameLen)) {
        isRawADTS = true;
        sampleRate = adtsRate;
        channels = adtsChans;
        bitsPerSample = 16;
        totalBytes = srcFile.size() * 6;
        srcFile.seek(0);
        inputBufferLen = srcFile.read(inputBuffer, sizeof(inputBuffer));
        inputBufferPos = 0;
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return true;
    }

    bool ok = parseM4AAtoms(srcFile);
    if (ok) {
        if (mdatOffset > 0) {
            srcFile.seek(mdatOffset);
            currentFilePos = mdatOffset;
        } else {
            srcFile.seek(0);
        }
        inputBufferLen = srcFile.read(inputBuffer, sizeof(inputBuffer));
        inputBufferPos = 0;

        if (inputBufferLen >= 4 && inputBuffer[0] == 0xFF && (inputBuffer[1] & 0xF0) == 0xF0) {
            isRawADTS = true;
        } else {
            isRawADTS = false;
            AACFrameInfo info;
            memset(&info, 0, sizeof(info));
            info.nChans = channels;
            info.sampRateCore = sampleRate;
            info.profile = 1; // AAC_PROFILE_LC
            AACSetRawBlockParams((HAACDecoder)pAacHandle, 0, &info);
        }
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return ok;
}

int M4ADecoder::readSamples(uint8_t *buffer, size_t maxBytes) {
    if (!srcFile || !pAacHandle || buffer == nullptr || maxBytes == 0) return 0;

    size_t bytesWritten = 0;
    int16_t *outPtr = (int16_t*)buffer;
    size_t samplesNeeded = maxBytes / sizeof(int16_t);

    while (samplesNeeded > 0) {
        if (pcmBufferPos < pcmBufferLen) {
            size_t available = pcmBufferLen - pcmBufferPos;
            size_t toCopy = (samplesNeeded < available) ? samplesNeeded : available;
            memcpy(outPtr, &pcmBuffer[pcmBufferPos], toCopy * sizeof(int16_t));
            pcmBufferPos += toCopy;
            outPtr += toCopy;
            samplesNeeded -= toCopy;
            bytesWritten += toCopy * sizeof(int16_t);
        } else {
            // Refill input buffer from file if low
            size_t unparsed = inputBufferLen - inputBufferPos;
            if (unparsed < 2048 && srcFile.available() > 0) {
                if (unparsed > 0) {
                    memmove(inputBuffer, inputBuffer + inputBufferPos, unparsed);
                }
                if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                int bytesRead = srcFile.read(inputBuffer + unparsed, sizeof(inputBuffer) - unparsed);
                if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
                inputBufferLen = unparsed + ((bytesRead > 0) ? bytesRead : 0);
                inputBufferPos = 0;
            }

            if (inputBufferPos >= inputBufferLen) {
                break; // EOF
            }

            if (isRawADTS) {
                int syncOffset = AACFindSyncWord(inputBuffer + inputBufferPos, (int)(inputBufferLen - inputBufferPos));
                if (syncOffset < 0) {
                    inputBufferPos = inputBufferLen;
                    continue;
                }
                inputBufferPos += syncOffset;
            }

            unsigned char *inPtr = inputBuffer + inputBufferPos;
            int bytesLeft = (int)(inputBufferLen - inputBufferPos);
            if (bytesLeft < 7) break;

            int err = AACDecode((HAACDecoder)pAacHandle, &inPtr, &bytesLeft, (short*)pcmBuffer);
            if (err == 0) {
                inputBufferPos = inPtr - inputBuffer;
                AACFrameInfo frameInfo;
                AACGetLastFrameInfo((HAACDecoder)pAacHandle, &frameInfo);
                if (frameInfo.outputSamps > 0) {
                    pcmBufferLen = frameInfo.outputSamps;
                    pcmBufferPos = 0;
                    if (frameInfo.sampRateOut > 0) sampleRate = frameInfo.sampRateOut;
                    if (frameInfo.nChans > 0) channels = frameInfo.nChans;
                }
            } else if (err == ERR_AAC_INDATA_UNDERFLOW) {
                // Buffer needs more data to complete frame decoding
                if (srcFile.available() > 0) {
                    size_t rem = inputBufferLen - inputBufferPos;
                    if (rem > 0) {
                        memmove(inputBuffer, inputBuffer + inputBufferPos, rem);
                    }
                    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                    int bRead = srcFile.read(inputBuffer + rem, sizeof(inputBuffer) - rem);
                    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
                    inputBufferLen = rem + ((bRead > 0) ? bRead : 0);
                    inputBufferPos = 0;
                    if (bRead > 0) {
                        continue; // Retry with refilled buffer
                    }
                }
                break; // True EOF
            } else {
                if (isRawADTS) {
                    int nextSync = AACFindSyncWord(inputBuffer + inputBufferPos + 1, (int)(inputBufferLen - inputBufferPos - 1));
                    if (nextSync >= 0) {
                        inputBufferPos += 1 + nextSync;
                    } else {
                        inputBufferPos = inputBufferLen;
                    }
                } else {
                    int nextSync = AACFindSyncWord(inputBuffer + inputBufferPos, (int)(inputBufferLen - inputBufferPos));
                    if (nextSync == 0) {
                        isRawADTS = true;
                    } else if (nextSync > 0) {
                        inputBufferPos += nextSync;
                        isRawADTS = true;
                    } else {
                        inputBufferPos++;
                    }
                }
            }
        }
    }

    bytesReadSoFar += bytesWritten;
    return (int)bytesWritten;
}

uint32_t M4ADecoder::getSampleRate() const { return sampleRate; }
uint16_t M4ADecoder::getChannels() const { return channels; }
uint16_t M4ADecoder::getBitsPerSample() const { return bitsPerSample; }
uint32_t M4ADecoder::getTotalBytes() const { return totalBytes; }

// ============================================================================
// Decoder Factory
// ============================================================================

AudioFormat DecoderFactory::detectFormat(const String &filename) {
    String lower = filename;
    lower.toLowerCase();
    if (lower.endsWith(".wav")) return AUDIO_FORMAT_WAV;
    if (lower.endsWith(".mp3")) return AUDIO_FORMAT_MP3;
    if (lower.endsWith(".flac")) return AUDIO_FORMAT_FLAC;
    if (lower.endsWith(".m4a")) return AUDIO_FORMAT_M4A;
    if (lower.endsWith(".aac")) return AUDIO_FORMAT_AAC;
    return AUDIO_FORMAT_UNKNOWN;
}

AudioDecoder* DecoderFactory::createDecoder(AudioFormat format) {
    switch (format) {
        case AUDIO_FORMAT_WAV: return new WAVDecoder();
        case AUDIO_FORMAT_MP3: return new MP3Decoder();
        case AUDIO_FORMAT_FLAC: return new FLACDecoder();
        case AUDIO_FORMAT_M4A:
        case AUDIO_FORMAT_AAC: return new M4ADecoder();
        default: return nullptr;
    }
}
