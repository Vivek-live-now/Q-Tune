#include "audio_player.h"
#include "spectrum_analyzer.h"
#include "sd_manager.h"
#include "usb_manager.h"

AudioPlayer::AudioPlayer() :
    initialized(false), playing(false), paused(false), trackFinished(false),
    bytesPlayed(0), totalDataBytes(0), dataOffset(44), currentTrackPath(""),
    currentAudioType(0), consecutiveReadErrors(0), currentSampleRate(44100), currentChannels(2), currentBitsPerSample(16),
    currentVolume(80), volumeScale(163), outputMode(OUTPUT_MODE_SPEAKER_I2S),
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
        sdManager.addRecentTrack(path);
        return true;
    } else if (fmt == AUDIO_FORMAT_MP3) {
        if (!mp3Decoder.open(wavFile)) {
            closeFiles();
            return false;
        }
        currentAudioType = 3; // MP3
        currentSampleRate = mp3Decoder.getSampleRate();
        currentChannels = mp3Decoder.getChannels();
        currentBitsPerSample = 16;
        totalDataBytes = mp3Decoder.getTotalBytes();
        bytesPlayed = 0;
        dataOffset = 0;

        setupI2S(currentSampleRate, currentChannels, currentBitsPerSample);
        currentTrackPath = path;
        playing = true;
        paused = false;
        sdManager.addRecentTrack(path);
        return true;
    } else if (fmt == AUDIO_FORMAT_M4A || fmt == AUDIO_FORMAT_AAC) {
        if (!m4aDecoder.open(wavFile)) {
            closeFiles();
            return false;
        }
        currentAudioType = 4; // M4A / AAC
        currentSampleRate = m4aDecoder.getSampleRate();
        currentChannels = m4aDecoder.getChannels();
        currentBitsPerSample = 16;
        totalDataBytes = m4aDecoder.getTotalBytes();
        bytesPlayed = 0;
        dataOffset = 0;

        setupI2S(currentSampleRate, currentChannels, currentBitsPerSample);
        currentTrackPath = path;
        playing = true;
        paused = false;
        sdManager.addRecentTrack(path);
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
        sdManager.addRecentTrack(path);
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
    sdManager.addRecentTrack(path);
    return true;
}

void AudioPlayer::update() {
    if (!playing || paused || !wavFile) return;

    if (currentAudioType == 2 || currentAudioType == 3 || currentAudioType == 4) {
        // Multi-format stream decoding (FLAC = 2, MP3 = 3, M4A = 4)
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

            int bytesRead = 0;
            if (currentAudioType == 2) {
                bytesRead = flacDecoder.readSamples((uint8_t*)monoBuf, bytesToRead);
            } else if (currentAudioType == 3) {
                bytesRead = mp3Decoder.readSamples((uint8_t*)monoBuf, bytesToRead);
            } else if (currentAudioType == 4) {
                bytesRead = m4aDecoder.readSamples((uint8_t*)monoBuf, bytesToRead);
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
                routeAudioOutput(stereoBuf, samples * 2, samples * 4);
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

            int bytesRead = 0;
            if (currentAudioType == 2) {
                bytesRead = flacDecoder.readSamples(buffer, bytesToRead);
            } else if (currentAudioType == 3) {
                bytesRead = mp3Decoder.readSamples(buffer, bytesToRead);
            } else if (currentAudioType == 4) {
                bytesRead = m4aDecoder.readSamples(buffer, bytesToRead);
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
                routeAudioOutput(buffer, bytesRead / sizeof(int16_t), bytesRead);
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
            routeAudioOutput(stereoBuf, samples * 2, samples * 4);
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
            routeAudioOutput(buffer, bytesRead / sizeof(int16_t), bytesRead);
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
    } else if (currentAudioType == 3) {
        mp3Decoder.close();
    } else if (currentAudioType == 4) {
        m4aDecoder.close();
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
    uint32_t bps = (currentAudioType == 2 || currentAudioType == 3 || currentAudioType == 4 || wavDecoder.isOpen()) ? 16 : currentBitsPerSample;
    uint32_t byteRate = currentSampleRate * currentChannels * (bps / 8);
    if (byteRate == 0) return 0;
    return (bytesPlayed * 1000ULL) / byteRate;
}

uint32_t AudioPlayer::getDurationMs() const {
    uint32_t bps = (currentAudioType == 2 || currentAudioType == 3 || currentAudioType == 4 || wavDecoder.isOpen()) ? 16 : currentBitsPerSample;
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

void AudioPlayer::setOutputMode(AudioOutputMode mode) {
    outputMode = mode;
}

AudioOutputMode AudioPlayer::getOutputMode() const {
    return outputMode;
}

const char* AudioPlayer::getOutputModeName() const {
    switch (outputMode) {
        case OUTPUT_MODE_FIIO_USB_DAC: return "FiiO KA11 USB";
        case OUTPUT_MODE_SPEAKER_I2S:
        default:                       return "I2S Speaker";
    }
}

const char* AudioPlayer::getOutputModeShortName() const {
    switch (outputMode) {
        case OUTPUT_MODE_FIIO_USB_DAC: return "KA11";
        case OUTPUT_MODE_SPEAKER_I2S:
        default:                       return "SPKR";
    }
}

void AudioPlayer::prepareForStream(uint32_t sampleRate) {
    stop();
    setupI2S(sampleRate, 2, 16);
    currentSampleRate = sampleRate;
    currentChannels = 2;
    currentBitsPerSample = 16;
}

void AudioPlayer::playStreamChunk(const int16_t *stereoSamples, size_t frameCount) {
    if (stereoSamples == NULL || frameCount == 0) return;

    int16_t scaledBuffer[256];
    size_t remaining = frameCount;
    const int16_t *ptr = stereoSamples;

    while (remaining > 0) {
        size_t framesToProcess = (remaining > 128) ? 128 : remaining;

        if (volumeScale < 256) {
            for (size_t i = 0; i < framesToProcess * 2; i++) {
                scaledBuffer[i] = (int16_t)(((int32_t)ptr[i] * volumeScale) >> 8);
            }
            routeAudioOutput(scaledBuffer, framesToProcess * 2, framesToProcess * 4);
            spectrumAnalyzer.feedSamples(scaledBuffer, framesToProcess, 2);
        } else {
            routeAudioOutput(ptr, framesToProcess * 2, framesToProcess * 4);
            spectrumAnalyzer.feedSamples(ptr, framesToProcess, 2);
        }

        ptr += framesToProcess * 2;
        remaining -= framesToProcess;
    }
}

void AudioPlayer::routeAudioOutput(const void *stereoData, size_t sampleCount, size_t byteCount) {
    if (outputMode == OUTPUT_MODE_FIIO_USB_DAC && usbManager.isMounted()) {
        usbManager.writeSamples((const int16_t*)stereoData, sampleCount);
    } else {
        size_t bytesWritten = 0;
        i2s_write(I2S_NUM, stereoData, byteCount, &bytesWritten, portMAX_DELAY);
    }
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
            24576,
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

bool AudioPlayer::isMP3() const {
    return (currentAudioType == 3);
}

bool AudioPlayer::isM4A() const {
    return (currentAudioType == 4);
}

const char* AudioPlayer::getFormatName() const {
    switch (currentAudioType) {
        case 1: return "WAV";
        case 2: return "FLAC";
        case 3: return "MP3";
        case 4: return "M4A";
        default: return "PCM";
    }
}

uint32_t AudioPlayer::getSampleRate() const {
    return currentSampleRate > 0 ? currentSampleRate : 44100;
}

uint16_t AudioPlayer::getChannels() const {
    return currentChannels > 0 ? currentChannels : 2;
}

uint16_t AudioPlayer::getBitsPerSample() const {
    return currentBitsPerSample > 0 ? currentBitsPerSample : 16;
}

uint32_t AudioPlayer::getTotalBytes() const {
    return totalDataBytes;
}

uint32_t AudioPlayer::getBitrateKbps() const {
    uint32_t durMs = getDurationMs();
    if (durMs > 0 && totalDataBytes > 0) {
        return (uint32_t)(((uint64_t)totalDataBytes * 8ULL) / durMs);
    }
    return (getSampleRate() * getChannels() * getBitsPerSample()) / 1000;
}

AudioPlayer audioPlayer;

