#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <Arduino.h>
#include <SD.h>

enum AudioFormat {
    AUDIO_FORMAT_UNKNOWN = 0,
    AUDIO_FORMAT_WAV,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_FLAC,
    AUDIO_FORMAT_M4A,
    AUDIO_FORMAT_AAC
};

class AudioDecoder {
public:
    virtual ~AudioDecoder() {}
    virtual bool open(File &file) = 0;
    virtual int readSamples(uint8_t *buffer, size_t maxBytes) = 0;
    virtual uint32_t getSampleRate() const = 0;
    virtual uint16_t getChannels() const = 0;
    virtual uint16_t getBitsPerSample() const = 0;
    virtual uint32_t getTotalBytes() const = 0;
};

class WAVDecoder : public AudioDecoder {
public:
    WAVDecoder();
    ~WAVDecoder() override;
    bool open(File &file) override;
    int readSamples(uint8_t *buffer, size_t maxBytes) override;
    uint32_t getSampleRate() const override;
    uint16_t getChannels() const override;
    uint16_t getBitsPerSample() const override;
    uint32_t getTotalBytes() const override;
    void close();
    bool isOpen() const;

private:
    File srcFile;
    void* pWavHandle;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint32_t totalBytes;
    uint32_t bytesReadSoFar;
};

class MP3Decoder : public AudioDecoder {
public:
    MP3Decoder();
    ~MP3Decoder() override;
    bool open(File &file) override;
    int readSamples(uint8_t *buffer, size_t maxBytes) override;
    uint32_t getSampleRate() const override;
    uint16_t getChannels() const override;
    uint16_t getBitsPerSample() const override;
    uint32_t getTotalBytes() const override;
    void close();
    bool isOpen() const;

private:
    File srcFile;
    void* pMp3Handle;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint32_t totalBytes;
    uint32_t bytesReadSoFar;
    uint32_t dataStartOffset;

    uint8_t inputBuffer[4096];
    size_t inputBufferLen;
    size_t inputBufferPos;

    int16_t pcmBuffer[2304]; // MINIMP3_MAX_SAMPLES_PER_FRAME
    size_t pcmBufferLen;
    size_t pcmBufferPos;
};

class FLACDecoder : public AudioDecoder {
public:
    FLACDecoder();
    ~FLACDecoder() override;
    bool open(File &file) override;
    int readSamples(uint8_t *buffer, size_t maxBytes) override;
    uint32_t getSampleRate() const override;
    uint16_t getChannels() const override;
    uint16_t getBitsPerSample() const override;
    uint32_t getTotalBytes() const override;
    void close();
    bool isOpen() const;

private:
    File srcFile;
    void* pFlacHandle;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint32_t totalBytes;
    uint32_t bytesReadSoFar;
};

class M4ADecoder : public AudioDecoder {
public:
    M4ADecoder();
    ~M4ADecoder() override;
    bool open(File &file) override;
    int readSamples(uint8_t *buffer, size_t maxBytes) override;
    uint32_t getSampleRate() const override;
    uint16_t getChannels() const override;
    uint16_t getBitsPerSample() const override;
    uint32_t getTotalBytes() const override;
    void close();
    bool isOpen() const;

private:
    File srcFile;
    bool isRawADTS;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint32_t totalBytes;
    uint32_t bytesReadSoFar;

    // M4A MP4 container atom index
    uint32_t mdatOffset;
    uint32_t mdatSize;
    uint32_t currentFilePos;

    uint8_t inputBuffer[4096];
    size_t inputBufferLen;
    size_t inputBufferPos;

    int16_t pcmBuffer[2048 * 2];
    size_t pcmBufferLen;
    size_t pcmBufferPos;

    bool parseM4AAtoms(File &file);
    bool parseADTSHeader(const uint8_t *hdr, uint32_t &sRate, uint16_t &chans, uint32_t &frameLen);
};

class DecoderFactory {
public:
    static AudioFormat detectFormat(const String &filename);
    static AudioDecoder* createDecoder(AudioFormat format);
};

#endif // AUDIO_DECODER_H
