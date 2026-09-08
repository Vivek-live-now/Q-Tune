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

private:
    bool initialized;
    bool playing;
    bool paused;
    File wavFile;
    WAVHeader currentWavHeader;
    uint32_t bytesPlayed;
    uint32_t totalDataBytes;

    bool parseWAVHeader(File &file, WAVHeader &header);
    void setupI2S(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample);
};

extern AudioPlayer audioPlayer;

#endif // AUDIO_PLAYER_H
