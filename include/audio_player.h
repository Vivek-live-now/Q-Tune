#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <vector>
#include "hw_config.h"
#include "audio_decoder.h"

enum AudioOutputMode {
    OUTPUT_MODE_SPEAKER_I2S = 0,
    OUTPUT_MODE_FIIO_USB_DAC = 1
};

struct WAVHeader {
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

class AudioPlayer {
public:
    AudioPlayer();
    bool begin();
    bool playFile(const String &path);
    void playTestTone(uint16_t frequencyHz, uint16_t durationMs);
    void update();
    void pause();
    void resume();
    void stop();
    bool isPlaying() const;
    bool isPaused() const;
    uint32_t getPositionMs() const;
    uint32_t getDurationMs() const;

    // Digital Volume Control (0-100%)
    void setVolume(uint8_t volume);
    uint8_t getVolume() const;
    void volumeUp(uint8_t step = 5);
    void volumeDown(uint8_t step = 5);

    // Track status & completion
    bool hasFinished() const;
    void clearFinished();

    // Stream Audio Playback (Wi-Fi streaming / DLNA / AirMusic)
    void prepareForStream(uint32_t sampleRate = 44100);
    void playStreamChunk(const int16_t *stereoSamples, size_t frameCount);

    // Audio Output Mode Selection (MAX98357A I2S Speaker vs FiiO KA11 USB DAC)
    void setOutputMode(AudioOutputMode mode);
    AudioOutputMode getOutputMode() const;
    const char* getOutputModeName() const;
    const char* getOutputModeShortName() const;

    // FreeRTOS Dedicated Audio Task Management
    void startAudioTask();
    void stopAudioTask();
    bool isAudioTaskRunning() const;

    // Track metadata
    String getCurrentTrackPath() const;
    String getCurrentTrackName() const;
    bool isFLAC() const;
    bool isMP3() const;
    bool isM4A() const;
    const char* getFormatName() const;
    uint32_t getSampleRate() const;
    uint16_t getChannels() const;
    uint16_t getBitsPerSample() const;
    uint32_t getTotalBytes() const;
    uint32_t getBitrateKbps() const;
    void closeFiles();

private:
    bool initialized;
    volatile bool playing;
    volatile bool paused;
    volatile bool trackFinished;
    File wavFile;
    WAVHeader currentWavHeader;
    uint32_t bytesPlayed;
    uint32_t totalDataBytes;
    uint32_t dataOffset;
    String currentTrackPath;

    // Multi-format audio engine state (WAV, FLAC, MP3, M4A)
    uint8_t currentAudioType; // 0: None, 1: WAV, 2: FLAC, 3: MP3, 4: M4A
    uint8_t consecutiveReadErrors;
    uint32_t currentSampleRate;
    uint16_t currentChannels;
    uint16_t currentBitsPerSample;
    FLACDecoder flacDecoder;
    WAVDecoder wavDecoder;
    MP3Decoder mp3Decoder;
    M4ADecoder m4aDecoder;

    // Digital Volume Scaling (Quadratic perceptual curve)
    uint8_t currentVolume;
    uint32_t volumeScale; // Fixed-point factor (0 to 256)

    // Audio Output Mode
    AudioOutputMode outputMode;

    // FreeRTOS Task state
    TaskHandle_t audioTaskHandle;
    volatile bool taskRunning;

    bool parseWAVHeader(File &file, WAVHeader &header);
    void setupI2S(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample);
    void routeAudioOutput(const void *stereoData, size_t sampleCount, size_t byteCount);
    static void audioTaskFunction(void *param);
};

extern AudioPlayer audioPlayer;

#endif // AUDIO_PLAYER_H
