#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <Arduino.h>
#include <SD.h>

enum AudioFormat {
    AUDIO_FORMAT_UNKNOWN,
    AUDIO_FORMAT_WAV,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_FLAC
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
    bool open(File &file) override;
    int readSamples(uint8_t *buffer, size_t maxBytes) override;
    uint32_t getSampleRate() const override;
    uint16_t getChannels() const override;
    uint16_t getBitsPerSample() const override;
    uint32_t getTotalBytes() const override;

private:
    File srcFile;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint32_t dataBytes;
    uint32_t bytesReadSoFar;
};

class MP3Decoder : public AudioDecoder {
public:
    MP3Decoder();
    bool open(File &file) override;
    int readSamples(uint8_t *buffer, size_t maxBytes) override;
    uint32_t getSampleRate() const override;
    uint16_t getChannels() const override;
    uint16_t getBitsPerSample() const override;
    uint32_t getTotalBytes() const override;

private:
    File srcFile;
};

class FLACDecoder : public AudioDecoder {
public:
    FLACDecoder();
    bool open(File &file) override;
    int readSamples(uint8_t *buffer, size_t maxBytes) override;
    uint32_t getSampleRate() const override;
    uint16_t getChannels() const override;
    uint16_t getBitsPerSample() const override;
    uint32_t getTotalBytes() const override;

private:
    File srcFile;
};

class DecoderFactory {
public:
    static AudioFormat detectFormat(const String &filename);
    static AudioDecoder* createDecoder(AudioFormat format);
};

#endif // AUDIO_DECODER_H
