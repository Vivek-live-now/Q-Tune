#include "audio_decoder.h"

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

FLACDecoder::FLACDecoder() {}
bool FLACDecoder::open(File &file) { srcFile = file; return false; }
int FLACDecoder::readSamples(uint8_t *buffer, size_t maxBytes) { return 0; }
uint32_t FLACDecoder::getSampleRate() const { return 44100; }
uint16_t FLACDecoder::getChannels() const { return 2; }
uint16_t FLACDecoder::getBitsPerSample() const { return 16; }
uint32_t FLACDecoder::getTotalBytes() const { return 0; }

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
