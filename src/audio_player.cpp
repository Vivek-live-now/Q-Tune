#include "audio_player.h"

AudioPlayer::AudioPlayer() :
    initialized(false), playing(false), paused(false), trackFinished(false),
    bytesPlayed(0), totalDataBytes(0), dataOffset(44),
    currentVolume(80), volumeScale(163),
    audioTaskHandle(NULL), taskRunning(false) {}

bool AudioPlayer::begin() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRCK,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_uninstall(I2S_NUM);
    if (i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL) != ESP_OK) {
        return false;
    }
    if (i2s_set_pin(I2S_NUM, &pin_config) != ESP_OK) {
        return false;
    }

    setVolume(currentVolume);
    startAudioTask();

    initialized = true;
    return true;
}

void AudioPlayer::setupI2S(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample) {
    i2s_set_sample_rates(I2S_NUM, sampleRate);
}

bool AudioPlayer::parseWAVHeader(File &file, WAVHeader &header) {
    if (file.size() < 44) return false;
    file.seek(0);

    char riff[4];
    uint32_t fileSize = 0;
    char wave[4];

    if (file.read((uint8_t*)riff, 4) != 4 || strncmp(riff, "RIFF", 4) != 0) return false;
    if (file.read((uint8_t*)&fileSize, 4) != 4) return false;
    if (file.read((uint8_t*)wave, 4) != 4 || strncmp(wave, "WAVE", 4) != 0) return false;

    memcpy(header.riff, riff, 4);
    header.chunkSize = fileSize;
    memcpy(header.wave, wave, 4);

    bool foundFmt = false;
    bool foundData = false;
    dataOffset = 44;

    // Scan chunks to support standard DAWs and metadata chunks (LIST, INFO, JUNK)
    while (file.available() >= 8 && (!foundFmt || !foundData)) {
        char chunkId[4];
        uint32_t chunkSize = 0;
        if (file.read((uint8_t*)chunkId, 4) != 4) break;
        if (file.read((uint8_t*)&chunkSize, 4) != 4) break;

        if (strncmp(chunkId, "fmt ", 4) == 0) {
            uint32_t fmtPos = file.position();
            if (chunkSize >= 16) {
                file.read((uint8_t*)&header.audioFormat, 2);
                file.read((uint8_t*)&header.numChannels, 2);
                file.read((uint8_t*)&header.sampleRate, 4);
                file.read((uint8_t*)&header.byteRate, 4);
                file.read((uint8_t*)&header.blockAlign, 2);
                file.read((uint8_t*)&header.bitsPerSample, 2);
                memcpy(header.fmt, chunkId, 4);
                header.subchunk1Size = chunkSize;
                foundFmt = true;
            }
            file.seek(fmtPos + chunkSize);
        } else if (strncmp(chunkId, "data", 4) == 0) {
            memcpy(header.data, chunkId, 4);
            header.dataSize = chunkSize;
            dataOffset = file.position();
            foundData = true;
            break;
        } else {
            file.seek(file.position() + chunkSize);
        }
    }

    if (!foundFmt || !foundData) {
        file.seek(0);
        if (file.read((uint8_t*)&header, sizeof(WAVHeader)) == sizeof(WAVHeader)) {
            if (strncmp(header.riff, "RIFF", 4) == 0 && strncmp(header.wave, "WAVE", 4) == 0) {
                dataOffset = 44;
                return (header.audioFormat == 1);
            }
        }
        return false;
    }

    return (header.audioFormat == 1);
}

bool AudioPlayer::playFile(const String &path) {
    stop();
    clearFinished();

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    bool exists = SD.exists(path);
    if (!exists) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    wavFile = SD.open(path, FILE_READ);
    if (!wavFile) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    if (!parseWAVHeader(wavFile, currentWavHeader)) {
        wavFile.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }

    totalDataBytes = currentWavHeader.dataSize;
    bytesPlayed = 0;
    wavFile.seek(dataOffset);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    setupI2S(currentWavHeader.sampleRate, currentWavHeader.numChannels, currentWavHeader.bitsPerSample);

    playing = true;
    paused = false;
    return true;
}

void AudioPlayer::update() {
    if (!playing || paused || !wavFile) return;

    if (currentWavHeader.numChannels == 1) {
        // Expand Mono PCM to Stereo frames for MAX98357A I2S
        int16_t monoBuf[256];
        int16_t stereoBuf[512];
        int bytesToRead = sizeof(monoBuf);
        if (bytesPlayed + bytesToRead > totalDataBytes) {
            bytesToRead = totalDataBytes - bytesPlayed;
        }

        if (bytesToRead <= 0) {
            stop();
            trackFinished = true;
            return;
        }

        int bytesRead = 0;
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        if (wavFile) {
            bytesRead = wavFile.read((uint8_t*)monoBuf, bytesToRead);
        }
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

        if (bytesRead > 0) {
            int samples = bytesRead / sizeof(int16_t);
            for (int i = 0; i < samples; i++) {
                int16_t sample = monoBuf[i];
                if (volumeScale < 256) {
                    sample = (int16_t)(((int32_t)sample * volumeScale) >> 8);
                }
                stereoBuf[i * 2] = sample;
                stereoBuf[i * 2 + 1] = sample;
            }
            size_t bytesWritten = 0;
            i2s_write(I2S_NUM, stereoBuf, samples * 4, &bytesWritten, portMAX_DELAY);
            bytesPlayed += bytesRead;
        } else {
            stop();
            trackFinished = true;
        }
    } else {
        // Direct Stereo PCM streaming
        uint8_t buffer[1024];
        int bytesToRead = sizeof(buffer);
        if (bytesPlayed + bytesToRead > totalDataBytes) {
            bytesToRead = totalDataBytes - bytesPlayed;
        }

        if (bytesToRead <= 0) {
            stop();
            trackFinished = true;
            return;
        }

        int bytesRead = 0;
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        if (wavFile) {
            bytesRead = wavFile.read(buffer, bytesToRead);
        }
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

        if (bytesRead > 0) {
            if (volumeScale < 256) {
                int16_t *samples = (int16_t*)buffer;
                int sampleCount = bytesRead / sizeof(int16_t);
                for (int i = 0; i < sampleCount; i++) {
                    samples[i] = (int16_t)(((int32_t)samples[i] * volumeScale) >> 8);
                }
            }
            size_t bytesWritten = 0;
            i2s_write(I2S_NUM, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
            bytesPlayed += bytesWritten;
        } else {
            stop();
            trackFinished = true;
        }
    }
}

void AudioPlayer::pause() {
    if (playing) paused = true;
}

void AudioPlayer::resume() {
    if (playing) paused = false;
}

void AudioPlayer::stop() {
    playing = false;
    paused = false;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    if (wavFile) {
        wavFile.close();
    }
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    bytesPlayed = 0;
    i2s_zero_dma_buffer(I2S_NUM);
}

bool AudioPlayer::isPlaying() const {
    return playing && !paused;
}

bool AudioPlayer::isPaused() const {
    return paused;
}

uint32_t AudioPlayer::getPositionMs() const {
    if (currentWavHeader.byteRate == 0) return 0;
    return (bytesPlayed * 1000ULL) / currentWavHeader.byteRate;
}

uint32_t AudioPlayer::getDurationMs() const {
    if (currentWavHeader.byteRate == 0) return 0;
    return (totalDataBytes * 1000ULL) / currentWavHeader.byteRate;
}

void AudioPlayer::playTestTone(uint16_t frequencyHz, uint16_t durationMs) {
    stop();
    setupI2S(44100, 2, 16);

    uint32_t sampleCount = (44100 * durationMs) / 1000;
    int16_t toneBuffer[512];

    uint32_t samplesGenerated = 0;
    while (samplesGenerated < sampleCount) {
        int chunkSamples = min((uint32_t)128, sampleCount - samplesGenerated);
        for (int i = 0; i < chunkSamples; i++) {
            double t = (double)(samplesGenerated + i) / 44100.0;
            int16_t val = (int16_t)(sin(2.0 * M_PI * frequencyHz * t) * 10000.0);
            toneBuffer[2 * i] = val;
            toneBuffer[2 * i + 1] = val;
        }
        size_t bytesWritten = 0;
        i2s_write(I2S_NUM, toneBuffer, chunkSamples * 4, &bytesWritten, portMAX_DELAY);
        samplesGenerated += chunkSamples;
    }
    i2s_zero_dma_buffer(I2S_NUM);
}

void AudioPlayer::setVolume(uint8_t volume) {
    if (volume > 100) volume = 100;
    currentVolume = volume;
    // Quadratic volume scaling for natural perceptual loudness:
    // (volume / 100)^2 * 256 = (volume * volume * 256) / 10000
    volumeScale = ((uint32_t)volume * volume * 256) / 10000;
}

uint8_t AudioPlayer::getVolume() const {
    return currentVolume;
}

void AudioPlayer::volumeUp(uint8_t step) {
    if (currentVolume + step > 100) setVolume(100);
    else setVolume(currentVolume + step);
}

void AudioPlayer::volumeDown(uint8_t step) {
    if (currentVolume < step) setVolume(0);
    else setVolume(currentVolume - step);
}

bool AudioPlayer::hasFinished() const {
    return trackFinished;
}

void AudioPlayer::clearFinished() {
    trackFinished = false;
}

void AudioPlayer::audioTaskFunction(void *param) {
    AudioPlayer *player = (AudioPlayer*)param;
    while (player->taskRunning) {
        if (player->playing && !player->paused) {
            player->update();
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
    vTaskDelete(NULL);
}

void AudioPlayer::startAudioTask() {
    if (audioTaskHandle == NULL) {
        taskRunning = true;
        xTaskCreatePinnedToCore(
            audioTaskFunction,
            "QAudioTask",
            4096,
            this,
            5,
            &audioTaskHandle,
            0 // Pin audio engine to Core 0
        );
    }
}

void AudioPlayer::stopAudioTask() {
    if (audioTaskHandle != NULL) {
        taskRunning = false;
        vTaskDelay(pdMS_TO_TICKS(25));
        audioTaskHandle = NULL;
    }
}

bool AudioPlayer::isAudioTaskRunning() const {
    return taskRunning && (audioTaskHandle != NULL);
}

AudioPlayer audioPlayer;
