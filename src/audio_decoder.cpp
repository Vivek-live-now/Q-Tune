#include "audio_decoder.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

struct WAVHeaderRaw {
    char riff[4];
    uint32_t chunkSize;
    char wave[4];
    char fmt[4];
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];
    uint32_t dataSize;
};

WAVDecoder::WAVDecoder() : sampleRate(44100), channels(2), bitsPerSample(16), dataBytes(0), bytesReadSoFar(0) {}

bool WAVDecoder::open(File &file) {
    srcFile = file;
    if (!srcFile || srcFile.size() < sizeof(WAVHeaderRaw)) return false;

    WAVHeaderRaw header;
    srcFile.seek(0);
    if (srcFile.read((uint8_t*)&header, sizeof(WAVHeaderRaw)) != sizeof(WAVHeaderRaw)) return false;

    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
        return false;
    }
    sampleRate = header.sampleRate;
    channels = header.numChannels;
    bitsPerSample = header.bitsPerSample;
    dataBytes = header.dataSize;
    bytesReadSoFar = 0;
    return true;
}

int WAVDecoder::readSamples(uint8_t *buffer, size_t maxBytes) {
    if (!srcFile) return 0;
    size_t remaining = dataBytes - bytesReadSoFar;
    size_t toRead = min(maxBytes, remaining);
    if (toRead == 0) return 0;

    int readCount = srcFile.read(buffer, toRead);
    if (readCount > 0) bytesReadSoFar += readCount;
    return readCount;
}

uint32_t WAVDecoder::getSampleRate() const { return sampleRate; }
uint16_t WAVDecoder::getChannels() const { return channels; }
uint16_t WAVDecoder::getBitsPerSample() const { return bitsPerSample; }
uint32_t WAVDecoder::getTotalBytes() const { return dataBytes; }

MP3Decoder::MP3Decoder() {}
bool MP3Decoder::open(File &file) { srcFile = file; return false; }
int MP3Decoder::readSamples(uint8_t *buffer, size_t maxBytes) { return 0; }
uint32_t MP3Decoder::getSampleRate() const { return 44100; }
uint16_t MP3Decoder::getChannels() const { return 2; }
uint16_t MP3Decoder::getBitsPerSample() const { return 16; }
uint32_t MP3Decoder::getTotalBytes() const { return 0; }

static size_t flac_read_cb(void* pUserData, void* pBufferOut, size_t bytesToRead) {
    File* file = (File*)pUserData;
    if (!file || !(*file)) return 0;
    return file->read((uint8_t*)pBufferOut, bytesToRead);
}

static drflac_bool32 flac_seek_cb(void* pUserData, int offset, drflac_seek_origin origin) {
    File* file = (File*)pUserData;
    if (!file || !(*file)) return DRFLAC_FALSE;
    if (origin == drflac_seek_origin_start) {
        return file->seek((uint32_t)offset) ? DRFLAC_TRUE : DRFLAC_FALSE;
    } else if (origin == drflac_seek_origin_current) {
        uint32_t cur = file->position();
        return file->seek((uint32_t)(cur + offset)) ? DRFLAC_TRUE : DRFLAC_FALSE;
    }
    return DRFLAC_FALSE;
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

    // Verify 4-byte FLAC stream marker "fLaC"
    uint8_t marker[4];
    srcFile.seek(0);
    if (srcFile.read(marker, 4) != 4) return false;
    if (marker[0] != 0x66 || marker[1] != 0x4C || marker[2] != 0x61 || marker[3] != 0x43) {
        return false;
    }
    srcFile.seek(0);

    drflac* pFlac = drflac_open(flac_read_cb, flac_seek_cb, &srcFile, NULL);
    if (!pFlac) return false;

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

AudioFormat DecoderFactory::detectFormat(const String &filename) {
    String lower = filename;
    lower.toLowerCase();
    if (lower.endsWith(".wav")) return AUDIO_FORMAT_WAV;
    if (lower.endsWith(".mp3")) return AUDIO_FORMAT_MP3;
    if (lower.endsWith(".flac")) return AUDIO_FORMAT_FLAC;
    return AUDIO_FORMAT_UNKNOWN;
}

AudioDecoder* DecoderFactory::createDecoder(AudioFormat format) {
    switch (format) {
        case AUDIO_FORMAT_WAV: return new WAVDecoder();
        case AUDIO_FORMAT_MP3: return new MP3Decoder();
        case AUDIO_FORMAT_FLAC: return new FLACDecoder();
        default: return nullptr;
    }
}
