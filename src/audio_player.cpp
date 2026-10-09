#include "audio_player.h"
#include "spectrum_analyzer.h"
#include "sd_manager.h"

AudioPlayer::AudioPlayer() :
    initialized(false), playing(false), paused(false), trackFinished(false),
    bytesPlayed(0), totalDataBytes(0), dataOffset(44), currentTrackPath(""),
    currentAudioType(0), consecutiveReadErrors(0), currentSampleRate(44100), currentChannels(2), currentBitsPerSample(16),
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
            uint32_t pad = (chunkSize & 1);
            file.seek(fmtPos + chunkSize + pad);
        } else if (strncmp(chunkId, "data", 4) == 0) {
            memcpy(header.data, chunkId, 4);
            header.dataSize = chunkSize;
            dataOffset = file.position();
            uint32_t remainingInFile = (file.size() > dataOffset) ? (file.size() - dataOffset) : 0;
            if (header.dataSize > remainingInFile) {
                header.dataSize = remainingInFile;
            }
            foundData = true;
            break;
        } else {
            uint32_t pad = (chunkSize & 1);
            file.seek(file.position() + chunkSize + pad);
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
    consecutiveReadErrors = 0;

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
    // Release spiBusMutex before opening decoders because decoder callbacks manage mutex with fine granularity
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    AudioFormat fmt = DecoderFactory::detectFormat(path);
    if (fmt == AUDIO_FORMAT_FLAC) {
        if (!flacDecoder.open(wavFile)) {
            closeFiles();
            return false;
        }
        currentAudioType = 2; // FLAC
        currentSampleRate = flacDecoder.getSampleRate();
        currentChannels = flacDecoder.getChannels();
        currentBitsPerSample = 16; // drflac_read_pcm_frames_s16 normalizes all output to 16-bit PCM
        totalDataBytes = flacDecoder.getTotalBytes();
        bytesPlayed = 0;
        dataOffset = 0;

        setupI2S(currentSampleRate, currentChannels, currentBitsPerSample);
        currentTrackPath = path;
        playing = true;
        paused = false;
        return true;
    }

    // Try high-performance dr_wav stream decoder first (handles all bit depths, extensible RIFF, metadata/tags)
    if (wavDecoder.open(wavFile)) {
        currentAudioType = 1; // WAV
        currentSampleRate = wavDecoder.getSampleRate();
        currentChannels = wavDecoder.getChannels();
        currentBitsPerSample = 16; // drwav_read_pcm_frames_s16 normalizes all bit depths to 16-bit PCM
        totalDataBytes = wavDecoder.getTotalBytes();
        bytesPlayed = 0;
        dataOffset = 0;

        setupI2S(currentSampleRate, currentChannels, currentBitsPerSample);
        currentTrackPath = path;
        playing = true;
        paused = false;
        return true;
    }

    // Fallback: raw WAV streaming only for valid 16-bit PCM
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    bool parsed = parseWAVHeader(wavFile, currentWavHeader);
    if (!parsed || currentWavHeader.bitsPerSample != 16 || currentWavHeader.audioFormat != 1) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        closeFiles();
        return false;
    }

    currentAudioType = 1; // WAV
    currentSampleRate = currentWavHeader.sampleRate;
    currentChannels = currentWavHeader.numChannels;
    currentBitsPerSample = currentWavHeader.bitsPerSample;
    uint32_t fileSize = wavFile.size();
    if (dataOffset < fileSize) {
        uint32_t maxAvail = fileSize - dataOffset;
        totalDataBytes = min(currentWavHeader.dataSize, maxAvail);
    } else {
        totalDataBytes = currentWavHeader.dataSize;
    }
    size_t frameAlign = currentChannels * (currentBitsPerSample / 8);
    if (frameAlign > 0) {
        totalDataBytes -= (totalDataBytes % frameAlign);
    }
    bytesPlayed = 0;
    wavFile.seek(dataOffset);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

    setupI2S(currentSampleRate, currentChannels, currentBitsPerSample);

    currentTrackPath = path;
    playing = true;
    paused = false;
    return true;
}

void AudioPlayer::update() {
    if (!playing || paused || !wavFile) return;

    if (currentAudioType == 2) {
        // FLAC Audio Stream Decoding
        if (currentChannels == 1) {
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

            int bytesRead = flacDecoder.readSamples((uint8_t*)monoBuf, bytesToRead);

            if (bytesRead > 0) {
                bytesRead &= ~1;
                consecutiveReadErrors = 0;
                int samples = bytesRead / sizeof(int16_t);
                spectrumAnalyzer.feedSamples(monoBuf, samples, 1);
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
                consecutiveReadErrors++;
                if (consecutiveReadErrors >= 5 || (bytesPlayed + bytesToRead >= totalDataBytes)) {
                    bool cardGone = false;
                    if (consecutiveReadErrors >= 10 && currentTrackPath.length() > 0) {
                        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                        cardGone = !SD.exists(currentTrackPath);
                        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
                    }
                    stop();
                    if (cardGone) {
                        sdManager.notifyCardRemoved();
                    } else {
                        trackFinished = true;
                    }
                    return;
                }
            }
        } else {
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

            int bytesRead = flacDecoder.readSamples(buffer, bytesToRead);

            if (bytesRead > 0) {
                bytesRead &= ~3;
                consecutiveReadErrors = 0;
                int frameCount = bytesRead / (sizeof(int16_t) * 2);
                spectrumAnalyzer.feedSamples((const int16_t*)buffer, frameCount, 2);

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
                consecutiveReadErrors++;
                if (consecutiveReadErrors >= 5 || (bytesPlayed + bytesToRead >= totalDataBytes)) {
                    bool cardGone = false;
                    if (consecutiveReadErrors >= 10 && currentTrackPath.length() > 0) {
                        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                        cardGone = !SD.exists(currentTrackPath);
                        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
                    }
                    stop();
                    if (cardGone) {
                        sdManager.notifyCardRemoved();
                    } else {
                        trackFinished = true;
                    }
                    return;
                }
            }
        }
        return;
    }

    if (currentChannels == 1) {
        // Expand Mono PCM to Stereo frames for MAX98357A I2S
        int16_t monoBuf[256];
        int16_t stereoBuf[512];
        int bytesToRead = sizeof(monoBuf);
        if (bytesPlayed + bytesToRead > totalDataBytes) {
            bytesToRead = totalDataBytes - bytesPlayed;
        }

        // Frame alignment: ensure bytesToRead is aligned to sample boundary (2 bytes)
        bytesToRead &= ~1;

        if (bytesToRead <= 0) {
            stop();
            trackFinished = true;
            return;
        }

        int bytesRead = 0;
        if (wavDecoder.isOpen()) {
            bytesRead = wavDecoder.readSamples((uint8_t*)monoBuf, bytesToRead);
        } else if (wavFile) {
            if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
            bytesRead = wavFile.read((uint8_t*)monoBuf, bytesToRead);
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
            bytesRead &= ~1; // Keep 16-bit word alignment
        }

        if (bytesRead > 0) {
            bytesRead &= ~1;
            consecutiveReadErrors = 0;
            int samples = bytesRead / sizeof(int16_t);
            spectrumAnalyzer.feedSamples(monoBuf, samples, 1);
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
            consecutiveReadErrors++;
            if (consecutiveReadErrors >= 5 || (bytesPlayed + bytesToRead >= totalDataBytes)) {
                bool cardGone = false;
                if (consecutiveReadErrors >= 10 && currentTrackPath.length() > 0) {
                    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                    cardGone = !SD.exists(currentTrackPath);
                    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
                }
                stop();
                if (cardGone) {
                    sdManager.notifyCardRemoved();
                } else {
                    trackFinished = true;
                }
                return;
            }
        }
    } else {
        // Direct Stereo PCM streaming
        uint8_t buffer[1024];
        int bytesToRead = sizeof(buffer);
        if (bytesPlayed + bytesToRead > totalDataBytes) {
            bytesToRead = totalDataBytes - bytesPlayed;
        }

        // Frame alignment: ensure bytesToRead is aligned to stereo frame boundary (4 bytes)
        bytesToRead &= ~3;

        if (bytesToRead <= 0) {
            stop();
            trackFinished = true;
            return;
        }

        int bytesRead = 0;
        if (wavDecoder.isOpen()) {
            bytesRead = wavDecoder.readSamples(buffer, bytesToRead);
        } else if (wavFile) {
            if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
            bytesRead = wavFile.read(buffer, bytesToRead);
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
            bytesRead &= ~3; // Keep 4-byte stereo frame alignment
        }

        if (bytesRead > 0) {
            bytesRead &= ~3;
            consecutiveReadErrors = 0;
            int frameCount = bytesRead / (sizeof(int16_t) * 2);
            spectrumAnalyzer.feedSamples((const int16_t*)buffer, frameCount, 2);

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
            consecutiveReadErrors++;
            if (consecutiveReadErrors >= 5 || (bytesPlayed + bytesToRead >= totalDataBytes)) {
                bool cardGone = false;
                if (consecutiveReadErrors >= 10 && currentTrackPath.length() > 0) {
                    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
                    cardGone = !SD.exists(currentTrackPath);
                    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
                }
                stop();
                if (cardGone) {
                    sdManager.notifyCardRemoved();
                } else {
                    trackFinished = true;
                }
                return;
            }
        }
    }
}

void AudioPlayer::pause() {
    if (playing) paused = true;
}

void AudioPlayer::resume() {
    if (playing) paused = false;
}

void AudioPlayer::closeFiles() {
    if (currentAudioType == 2) {
        flacDecoder.close();
    }
    wavDecoder.close();
    if (wavFile) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        wavFile.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    }
    currentAudioType = 0;
}

void AudioPlayer::stop() {
    playing = false;
    paused = false;
    currentTrackPath = "";
    closeFiles();
    bytesPlayed = 0;
    consecutiveReadErrors = 0;
    i2s_zero_dma_buffer(I2S_NUM);
    spectrumAnalyzer.clearSamples();
}

bool AudioPlayer::isPlaying() const {
    return playing && !paused;
}

bool AudioPlayer::isPaused() const {
    return paused;
}

uint32_t AudioPlayer::getPositionMs() const {
    uint32_t bps = (currentAudioType == 2 || wavDecoder.isOpen()) ? 16 : currentBitsPerSample;
    uint32_t byteRate = currentSampleRate * currentChannels * (bps / 8);
    if (byteRate == 0) return 0;
    return (bytesPlayed * 1000ULL) / byteRate;
}

uint32_t AudioPlayer::getDurationMs() const {
    uint32_t bps = (currentAudioType == 2 || wavDecoder.isOpen()) ? 16 : currentBitsPerSample;
    uint32_t byteRate = currentSampleRate * currentChannels * (bps / 8);
    if (byteRate == 0) return 0;
    return (totalDataBytes * 1000ULL) / byteRate;
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
        spectrumAnalyzer.feedSamples(toneBuffer, chunkSamples, 2);
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
            16384,
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

String AudioPlayer::getCurrentTrackPath() const {
    return currentTrackPath;
}

String AudioPlayer::getCurrentTrackName() const {
    if (currentTrackPath.length() == 0) return "";
    int lastSlash = currentTrackPath.lastIndexOf('/');
    if (lastSlash >= 0) {
        return currentTrackPath.substring(lastSlash + 1);
    }
    return currentTrackPath;
}

bool AudioPlayer::isFLAC() const {
    return (currentAudioType == 2);
}

AudioPlayer audioPlayer;
