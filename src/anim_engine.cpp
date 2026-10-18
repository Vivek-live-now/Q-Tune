#include "anim_engine.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include "display.h"
#include "hw_config.h"
#include "sd_manager.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline uint32_t millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

AnimEngine animEngine;

AnimEngine::AnimEngine() :
    is_open(false),
    is_playing(false),
    is_paused(false),
    is_looping(true),
    speed_multiplier(1.0f),
    current_frame_idx(0),
    last_frame_time(0),
    psram_anim_buf(nullptr),
    psram_anim_size(0),
    using_psram(false)
#ifndef ARDUINO
    , host_file(nullptr)
#endif
{
    memset(&current_header, 0, sizeof(current_header));
    memset(current_filepath, 0, sizeof(current_filepath));
    memset(frame_buffer, 0, sizeof(frame_buffer));
    memset(last_error_msg, 0, sizeof(last_error_msg));
}

AnimEngine::~AnimEngine() {
    close();
}

void AnimEngine::setError(const char* err) {
    if (err) {
        strncpy(last_error_msg, err, sizeof(last_error_msg) - 1);
        last_error_msg[sizeof(last_error_msg) - 1] = '\0';
    } else {
        last_error_msg[0] = '\0';
    }
}

bool AnimEngine::begin() {
    setError("OK");
    return true;
}

void AnimEngine::close() {
    if (psram_anim_buf) {
        free(psram_anim_buf);
        psram_anim_buf = nullptr;
    }
    psram_anim_size = 0;
    using_psram = false;

#ifdef ARDUINO
    if (sd_file) {
        if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
        sd_file.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    }
#else
    if (host_file) {
        fclose(host_file);
        host_file = nullptr;
    }
#endif

    is_open = false;
    is_playing = false;
    is_paused = false;
    current_frame_idx = 0;
    memset(current_filepath, 0, sizeof(current_filepath));
    memset(&current_header, 0, sizeof(current_header));
}

bool AnimEngine::open(const char* filepath) {
    if (!filepath || strlen(filepath) == 0) {
        setError("INVALID PATH");
        return false;
    }
    close();

    char resolved[96];
    if (filepath[0] == '/' || strchr(filepath, '/') != nullptr) {
        strncpy(resolved, filepath, sizeof(resolved) - 1);
    } else {
        snprintf(resolved, sizeof(resolved), "/anim/%s", filepath);
    }
    resolved[sizeof(resolved) - 1] = '\0';

    // Auto-append .anim extension if missing
    size_t rlen = strlen(resolved);
    if (rlen < 5 || strcasecmp(resolved + rlen - 5, ".anim") != 0) {
        strncat(resolved, ".anim", sizeof(resolved) - strlen(resolved) - 1);
    }

#ifdef ARDUINO
    if (!sdManager.isMounted()) {
        setError("SD NOT MOUNTED");
        return false;
    }

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    if (!SD.exists(resolved)) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        setError("FILE NOT FOUND");
        return false;
    }

    sd_file = SD.open(resolved, FILE_READ);
    if (!sd_file) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        setError("OPEN FAILED");
        return false;
    }

    size_t fileSize = sd_file.size();
    if (fileSize < sizeof(AnimHeader)) {
        sd_file.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        setError("ANIM HEADER TRUNCATED");
        return false;
    }

    // Attempt to preload entire .anim file into PSRAM for zero-SPI glitch audio playback
    uint8_t* pBuf = (uint8_t*)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pBuf) {
        pBuf = (uint8_t*)malloc(fileSize);
    }

    if (pBuf) {
        size_t bytesRead = sd_file.read(pBuf, fileSize);
        sd_file.close(); // SD file handle freed immediately!
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);

        if (bytesRead != fileSize) {
            free(pBuf);
            setError("PRELOAD FAILED");
            return false;
        }

        memcpy(&current_header, pBuf, sizeof(AnimHeader));
        psram_anim_buf = pBuf;
        psram_anim_size = fileSize;
        using_psram = true;
    } else {
        // Fallback: Read header from SD
        if (sd_file.read((uint8_t*)&current_header, sizeof(AnimHeader)) != sizeof(AnimHeader)) {
            sd_file.close();
            if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
            setError("ANIM HEADER TRUNCATED");
            return false;
        }
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        using_psram = false;
    }
#else
    host_file = fopen(resolved, "rb");
    if (!host_file) {
        // Try without leading slash for local test directories
        const char* rel = (resolved[0] == '/') ? resolved + 1 : resolved;
        host_file = fopen(rel, "rb");
        if (!host_file) {
            setError("FILE NOT FOUND");
            return false;
        }
    }

    fseek(host_file, 0, SEEK_END);
    size_t fileSize = ftell(host_file);
    fseek(host_file, 0, SEEK_SET);

    if (fileSize < sizeof(AnimHeader)) {
        fclose(host_file);
        host_file = nullptr;
        setError("ANIM HEADER TRUNCATED");
        return false;
    }

    uint8_t* pBuf = (uint8_t*)malloc(fileSize);
    if (pBuf && fread(pBuf, 1, fileSize, host_file) == fileSize) {
        memcpy(&current_header, pBuf, sizeof(AnimHeader));
        psram_anim_buf = pBuf;
        psram_anim_size = fileSize;
        using_psram = true;
        fclose(host_file);
        host_file = nullptr;
    } else {
        if (pBuf) free(pBuf);
        fseek(host_file, 0, SEEK_SET);
        if (fread(&current_header, 1, sizeof(AnimHeader), host_file) != sizeof(AnimHeader)) {
            fclose(host_file);
            host_file = nullptr;
            setError("ANIM HEADER TRUNCATED");
            return false;
        }
        using_psram = false;
    }
#endif

    // Validate QANM Header
    if (current_header.magic != ANIM_MAGIC) {
        setError("INVALID ANIM MAGIC");
        close();
        return false;
    }
    if (current_header.version != ANIM_VERSION) {
        setError("INVALID ANIM VERSION");
        close();
        return false;
    }
    if (current_header.width != ANIM_WIDTH || current_header.height != ANIM_HEIGHT) {
        setError("INVALID ANIM DIMENSIONS");
        close();
        return false;
    }
    if (current_header.frame_count == 0) {
        setError("ZERO ANIM FRAMES");
        close();
        return false;
    }

    strncpy(current_filepath, resolved, sizeof(current_filepath) - 1);
    current_filepath[sizeof(current_filepath) - 1] = '\0';

    is_looping = (current_header.flags & ANIM_FLAG_LOOP) != 0;
    if (!readFrameData(0)) {
        setError("CORRUPT FRAME DATA");
        close();
        return false;
    }

    is_open = true;
    is_playing = true;
    is_paused = false;
    current_frame_idx = 0;
    speed_multiplier = 1.0f;
    last_frame_time = millis();
    setError("OK");
    return true;
}

bool AnimEngine::readFrameData(uint16_t frame_idx) {
    if (frame_idx >= current_header.frame_count) return false;

    size_t offset = sizeof(AnimHeader) + (size_t)frame_idx * ANIM_FRAME_BYTES;

    if (using_psram && psram_anim_buf) {
        if (offset + ANIM_FRAME_BYTES > psram_anim_size) return false;
        memcpy(frame_buffer, psram_anim_buf + offset, ANIM_FRAME_BYTES);
        return true;
    }

#ifdef ARDUINO
    if (!sd_file) return false;
    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    if (!sd_file.seek(offset)) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return false;
    }
    size_t bytes_read = sd_file.read(frame_buffer, ANIM_FRAME_BYTES);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return (bytes_read == ANIM_FRAME_BYTES);
#else
    if (!host_file) return false;
    if (fseek(host_file, (long)offset, SEEK_SET) != 0) return false;
    size_t bytes_read = fread(frame_buffer, 1, ANIM_FRAME_BYTES, host_file);
    return (bytes_read == ANIM_FRAME_BYTES);
#endif
}

void AnimEngine::play() {
    if (is_open) {
        is_playing = true;
        is_paused = false;
        last_frame_time = millis();
    }
}

void AnimEngine::pause() {
    is_paused = true;
}

void AnimEngine::resume() {
    if (is_open) {
        is_paused = false;
        last_frame_time = millis();
    }
}

void AnimEngine::stop() {
    is_playing = false;
    is_paused = false;
    seekFrame(0);
}

void AnimEngine::togglePlayPause() {
    if (!is_playing) {
        play();
    } else if (is_paused) {
        resume();
    } else {
        pause();
    }
}

void AnimEngine::setSpeedMultiplier(float mult) {
    if (mult >= 0.25f && mult <= 4.0f) {
        speed_multiplier = mult;
    }
}

uint16_t AnimEngine::getEffectiveDelayMs() const {
    uint16_t base_delay = current_header.frame_delay_ms;
    if (base_delay == 0) base_delay = 50; // 20 FPS default
    float effective = (float)base_delay / speed_multiplier;
    if (effective < 10.0f) effective = 10.0f;
    return (uint16_t)effective;
}

void AnimEngine::stepForward() {
    if (!is_open || current_header.frame_count == 0) return;
    uint16_t next = current_frame_idx + 1;
    if (next >= current_header.frame_count) {
        next = is_looping ? 0 : (current_header.frame_count - 1);
    }
    seekFrame(next);
}

void AnimEngine::stepBackward() {
    if (!is_open || current_header.frame_count == 0) return;
    uint16_t prev = (current_frame_idx == 0) ? (current_header.frame_count - 1) : (current_frame_idx - 1);
    seekFrame(prev);
}

bool AnimEngine::seekFrame(uint16_t frame_idx) {
    if (!is_open || frame_idx >= current_header.frame_count) return false;
    if (readFrameData(frame_idx)) {
        current_frame_idx = frame_idx;
        return true;
    }
    return false;
}

bool AnimEngine::update() {
    if (!is_open || !is_playing || is_paused) return false;

    uint32_t now = millis();
    uint16_t delay_ms = getEffectiveDelayMs();

    if (now - last_frame_time < delay_ms) {
        return false;
    }

    last_frame_time = now;
    uint16_t next_frame = current_frame_idx + 1;

    if (next_frame >= current_header.frame_count) {
        if (is_looping) {
            next_frame = 0;
        } else {
            is_playing = false;
            return false;
        }
    }

    return seekFrame(next_frame);
}

void AnimEngine::drawFrame(int16_t x, int16_t y) {
#ifdef ARDUINO
    display.getU8g2().setDrawColor(1);
    display.getU8g2().drawXBMP(x, y, ANIM_WIDTH, ANIM_HEIGHT, frame_buffer);

    if (current_header.flags & ANIM_FLAG_INVERT) {
        display.getU8g2().setDrawColor(2); // XOR Invert mode
        display.getU8g2().drawBox(x, y, ANIM_WIDTH, ANIM_HEIGHT);
        display.getU8g2().setDrawColor(1);
    }
#else
    (void)x; (void)y;
#endif
}

#ifdef ARDUINO
std::vector<String> AnimEngine::listAnimFiles() {
    std::vector<String> list;
    if (!sdManager.isMounted()) return list;

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    if (!SD.exists("/anim")) {
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return list;
    }

    File dir = SD.open("/anim");
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
        return list;
    }

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".anim")) {
                list.push_back(name);
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return list;
}

bool AnimEngine::animExists(const char* name) {
    if (!name || strlen(name) == 0) return false;
    char path[96];
    if (name[0] == '/') {
        strncpy(path, name, sizeof(path) - 1);
    } else {
        snprintf(path, sizeof(path), "/anim/%s", name);
    }
    path[sizeof(path) - 1] = '\0';
    if (!strstr(path, ".anim")) {
        strncat(path, ".anim", sizeof(path) - strlen(path) - 1);
    }

    if (spiBusMutex != NULL) xSemaphoreTake(spiBusMutex, portMAX_DELAY);
    bool exists = SD.exists(path);
    if (spiBusMutex != NULL) xSemaphoreGive(spiBusMutex);
    return exists;
}
#endif
