#ifndef GDMA_MANAGER_H
#define GDMA_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if !defined(ALIGNED16)
  #if defined(__GNUC__) || defined(__clang__)
    #define ALIGNED16 __attribute__((aligned(16)))
  #else
    #define ALIGNED16
  #endif
#endif

// Performance & Telemetry Statistics for GDMA Operations
struct GdmaTransferStats {
    uint32_t total_transfers;       // Total number of asynchronous frames queued
    uint32_t dropped_frames;        // Number of dropped or busy frames (if queue full)
    uint32_t last_transfer_time_us; // Duration of the last DMA bus transfer in microseconds
    uint32_t peak_transfer_time_us; // Maximum duration recorded for a DMA transfer
    uint32_t m2m_copy_count;        // Total number of M2M DMA / vector copies performed
    bool dma_channel_active;        // True if hardware GDMA channel is active
};

enum class GdmaState {
    UNINITIALIZED = 0,
    IDLE = 1,
    TRANSMITTING = 2,
    ERROR = 3
};

class GdmaManager {
public:
    static const size_t BUFFER_SIZE = 1024; // 128x64 1-bit OLED framebuffer size
    static const size_t PAGE_COUNT = 8;     // SH1106 has 8 pages (128x8 bits each)
    static const size_t PAGE_SIZE = 128;    // 128 bytes per page

    GdmaManager();
    ~GdmaManager();

    // Hardware Bus Initialization
    bool begin(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, uint32_t bus_clock = 8000000UL);

    // Asynchronous non-blocking OLED framebuffer push
    bool flushAsync(const uint8_t* frame_buffer);

    // Block until in-flight DMA transmission completes
    void waitIdle();

    // Fast Memory-to-Memory (M2M) Hardware DMA / Aligned SIMD block copy
    void memcpyM2M(void* dst, const void* src, size_t len);

    // State & Diagnostics
    bool isBusy() const;
    bool isInitialized() const;
    GdmaState getState() const;
    const GdmaTransferStats& getStats() const;
    void resetStats();

    // Double buffer accessors
    const uint8_t* getActiveDmaBuffer() const;
    size_t getBufferSize() const { return BUFFER_SIZE; }

private:
    bool initialized;
    volatile GdmaState state;
    GdmaTransferStats stats;

    int8_t pin_sclk;
    int8_t pin_mosi;
    int8_t pin_cs;
    int8_t pin_dc;
    int8_t pin_rst;
    uint32_t clock_speed_hz;

    // Double-buffered ping-pong buffers (16-byte aligned for DMA & SIMD)
    uint8_t* dma_ping_buf;
    uint8_t* dma_pong_buf;
    uint8_t active_buf_idx;

    uint32_t transfer_start_micros;

    void allocateBuffers();
    void freeBuffers();

#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
    void* spi_handle; // spi_device_handle_t
#endif
};

extern GdmaManager gdmaManager;

#endif // GDMA_MANAGER_H
