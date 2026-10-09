#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>
#include <vector>
#include "hw_config.h"

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

    // FreeRTOS Dedicated Audio Task Management
    void startAudioTask();
    void stopAudioTask();
    bool isAudioTaskRunning() const;

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

    // Digital Volume Scaling (Quadratic perceptual curve)
    uint8_t currentVolume;
    uint32_t volumeScale; // Fixed-point factor (0 to 256)

    // FreeRTOS Task state
    TaskHandle_t audioTaskHandle;
    volatile bool taskRunning;

    bool parseWAVHeader(File &file, WAVHeader &header);
    void setupI2S(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample);
    static void audioTaskFunction(void *param);
};

extern AudioPlayer audioPlayer;

#endif // AUDIO_PLAYER_H
