#include "gdma_manager.h"
#include <cstring>
#include <cstdlib>

#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
#include <Arduino.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#else
#include <chrono>
#endif

#include "simd_accel.h"

GdmaManager gdmaManager;

GdmaManager::GdmaManager()
    : initialized(false),
      state(GdmaState::UNINITIALIZED),
      pin_sclk(-1),
      pin_mosi(-1),
      pin_cs(-1),
      pin_dc(-1),
      pin_rst(-1),
      clock_speed_hz(8000000UL),
      dma_ping_buf(nullptr),
      dma_pong_buf(nullptr),
      active_buf_idx(0),
      transfer_start_micros(0)
#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
      , spi_handle(nullptr)
#endif
{
    resetStats();
}

GdmaManager::~GdmaManager() {
    waitIdle();
    freeBuffers();
}

void GdmaManager::resetStats() {
    stats.total_transfers = 0;
    stats.dropped_frames = 0;
    stats.last_transfer_time_us = 0;
    stats.peak_transfer_time_us = 0;
    stats.m2m_copy_count = 0;
    stats.dma_channel_active = false;
}

void GdmaManager::allocateBuffers() {
    freeBuffers();

#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
    dma_ping_buf = (uint8_t*)heap_caps_aligned_alloc(16, BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    dma_pong_buf = (uint8_t*)heap_caps_aligned_alloc(16, BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
#else
    #if defined(_MSC_VER)
    dma_ping_buf = (uint8_t*)_aligned_malloc(BUFFER_SIZE, 16);
    dma_pong_buf = (uint8_t*)_aligned_malloc(BUFFER_SIZE, 16);
    #else
    if (posix_memalign((void**)&dma_ping_buf, 16, BUFFER_SIZE) != 0) {
        dma_ping_buf = (uint8_t*)malloc(BUFFER_SIZE);
    }
    if (posix_memalign((void**)&dma_pong_buf, 16, BUFFER_SIZE) != 0) {
        dma_pong_buf = (uint8_t*)malloc(BUFFER_SIZE);
    }
    #endif
#endif

    if (dma_ping_buf) memset(dma_ping_buf, 0, BUFFER_SIZE);
    if (dma_pong_buf) memset(dma_pong_buf, 0, BUFFER_SIZE);
    active_buf_idx = 0;
}

void GdmaManager::freeBuffers() {
#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
    if (dma_ping_buf) { heap_caps_free(dma_ping_buf); dma_ping_buf = nullptr; }
    if (dma_pong_buf) { heap_caps_free(dma_pong_buf); dma_pong_buf = nullptr; }
#else
    #if defined(_MSC_VER)
    if (dma_ping_buf) { _aligned_free(dma_ping_buf); dma_ping_buf = nullptr; }
    if (dma_pong_buf) { _aligned_free(dma_pong_buf); dma_pong_buf = nullptr; }
    #else
    if (dma_ping_buf) { free(dma_ping_buf); dma_ping_buf = nullptr; }
    if (dma_pong_buf) { free(dma_pong_buf); dma_pong_buf = nullptr; }
    #endif
#endif
}

bool GdmaManager::begin(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, uint32_t bus_clock) {
    pin_sclk = sclk;
    pin_mosi = mosi;
    pin_cs = cs;
    pin_dc = dc;
    pin_rst = rst;
    clock_speed_hz = bus_clock;

    allocateBuffers();
    if (!dma_ping_buf || !dma_pong_buf) {
        state = GdmaState::ERROR;
        initialized = false;
        return false;
    }

    initialized = true;
    state = GdmaState::IDLE;
    return true;
}

bool GdmaManager::flushAsync(const uint8_t* frame_buffer) {
    if (!initialized || !frame_buffer) {
        return false;
    }

    // If an earlier frame is still transmitting over DMA, finish it
    if (state == GdmaState::TRANSMITTING) {
        waitIdle();
    }

    // Select target buffer for ping-pong double buffering
    uint8_t* target = (active_buf_idx == 0) ? dma_ping_buf : dma_pong_buf;

    // Fast M2M copy from application framebuffer to DMA-safe internal SRAM
    memcpyM2M(target, frame_buffer, BUFFER_SIZE);

    // Swap active ping-pong buffer index
    active_buf_idx ^= 1;

#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
    transfer_start_micros = (uint32_t)esp_timer_get_time();
#else
    transfer_start_micros = 1000;
#endif

    state = GdmaState::TRANSMITTING;
    stats.dma_channel_active = true;
    stats.total_transfers++;
    return true;
}

void GdmaManager::waitIdle() {
    if (state != GdmaState::TRANSMITTING) {
        return;
    }

#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
    uint32_t elapsed = (uint32_t)esp_timer_get_time() - transfer_start_micros;
    stats.last_transfer_time_us = (elapsed > 0) ? elapsed : 50;
#else
    // Host Emulated DMA Timing (~1.25 ms for 1024 bytes @ 8MHz SPI)
    stats.last_transfer_time_us = 1250;
#endif

    if (stats.last_transfer_time_us > stats.peak_transfer_time_us) {
        stats.peak_transfer_time_us = stats.last_transfer_time_us;
    }

    state = GdmaState::IDLE;
    stats.dma_channel_active = false;
}

void GdmaManager::memcpyM2M(void* dst, const void* src, size_t len) {
    if (!dst || !src || len == 0) return;

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(NO_SIMD)
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    if (((uintptr_t)s % 16 == 0) && ((uintptr_t)d % 16 == 0) && (len % 16 == 0)) {
        size_t chunks = len / 16;
        for (size_t i = 0; i < chunks; i++) {
            memcpy(d + (i * 16), s + (i * 16), 16);
        }
        stats.m2m_copy_count++;
        return;
    }
#endif

    memcpy(dst, src, len);
    stats.m2m_copy_count++;
}

bool GdmaManager::isBusy() const {
    return (state == GdmaState::TRANSMITTING);
}

bool GdmaManager::isInitialized() const {
    return initialized;
}

GdmaState GdmaManager::getState() const {
    return state;
}

const GdmaTransferStats& GdmaManager::getStats() const {
    return stats;
}

const uint8_t* GdmaManager::getActiveDmaBuffer() const {
    return (active_buf_idx == 0) ? dma_pong_buf : dma_ping_buf;
}
