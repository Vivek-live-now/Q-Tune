#include "audio_player.h"

AudioPlayer::AudioPlayer() :
    initialized(false), playing(false), paused(false),
    bytesPlayed(0), totalDataBytes(0), dataOffset(44) {}

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
    if (!SD.exists(path)) return false;

    wavFile = SD.open(path, FILE_READ);
    if (!wavFile) return false;

    if (!parseWAVHeader(wavFile, currentWavHeader)) {
        wavFile.close();
        return false;
    }

    setupI2S(currentWavHeader.sampleRate, currentWavHeader.numChannels, currentWavHeader.bitsPerSample);

    totalDataBytes = currentWavHeader.dataSize;
    bytesPlayed = 0;
    wavFile.seek(dataOffset);
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
            return;
        }

        int bytesRead = wavFile.read((uint8_t*)monoBuf, bytesToRead);
        if (bytesRead > 0) {
            int samples = bytesRead / sizeof(int16_t);
            for (int i = 0; i < samples; i++) {
                stereoBuf[i * 2] = monoBuf[i];
                stereoBuf[i * 2 + 1] = monoBuf[i];
            }
            size_t bytesWritten = 0;
            i2s_write(I2S_NUM, stereoBuf, samples * 4, &bytesWritten, portMAX_DELAY);
            bytesPlayed += bytesRead;
        } else {
            stop();
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
            return;
        }

        int bytesRead = wavFile.read(buffer, bytesToRead);
        if (bytesRead > 0) {
            size_t bytesWritten = 0;
            i2s_write(I2S_NUM, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
            bytesPlayed += bytesWritten;
        } else {
            stop();
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
    if (wavFile) {
        wavFile.close();
    }
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

AudioPlayer audioPlayer;
