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

#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
    if (pin_dc >= 0) {
        gpio_set_direction((gpio_num_t)pin_dc, GPIO_MODE_OUTPUT);
    }

    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = pin_mosi;
    buscfg.sclk_io_num = pin_sclk;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 2048;

    // Initialize SPI bus with GDMA channel auto-allocation (SPI2_HOST = FSPI)
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE is normal if Arduino SPI.begin() already claimed the bus
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = (int)clock_speed_hz;
    devcfg.mode = 0; // SPI Mode 0
    devcfg.spics_io_num = pin_cs;
    devcfg.queue_size = 16; // 8 pages * 2 (command + data)

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, (spi_device_handle_t*)&spi_handle);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        state = GdmaState::ERROR;
        initialized = false;
        return false;
    }
#endif

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
    if (spi_handle && pin_dc >= 0) {
        transfer_start_micros = (uint32_t)esp_timer_get_time();
        state = GdmaState::TRANSMITTING;
        stats.dma_channel_active = true;

        static spi_transaction_t trans_data[PAGE_COUNT];
        memset(trans_data, 0, sizeof(trans_data));

        for (int page = 0; page < (int)PAGE_COUNT; page++) {
            // Set D/C low for SH1106 Page / Column Addressing Commands
            gpio_set_level((gpio_num_t)pin_dc, 0);
            uint8_t cmd[3] = {
                (uint8_t)(0xB0 | page), // Page address (0..7)
                0x02,                   // Column lower nibble (SH1106 offset 2)
                0x10                    // Column higher nibble
            };
            spi_transaction_t t_cmd = {};
            t_cmd.length = 24; // 3 bytes * 8 bits
            t_cmd.tx_buffer = cmd;
            spi_device_polling_transmit((spi_device_handle_t)spi_handle, &t_cmd);

            // Set D/C high for SH1106 Data DMA Streaming
            gpio_set_level((gpio_num_t)pin_dc, 1);
            trans_data[page].length = PAGE_SIZE * 8; // 128 bytes * 8 bits
            trans_data[page].tx_buffer = target + (page * PAGE_SIZE);
            spi_device_queue_trans((spi_device_handle_t)spi_handle, &trans_data[page], portMAX_DELAY);
        }
    }
#else
    // Host / Emulation Mode
    state = GdmaState::TRANSMITTING;
    stats.dma_channel_active = true;
    transfer_start_micros = 1000;
#endif

    stats.total_transfers++;
    return true;
}

void GdmaManager::waitIdle() {
    if (state != GdmaState::TRANSMITTING) {
        return;
    }

#if (defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)) && !defined(HOST_TEST)
    if (spi_handle) {
        spi_transaction_t* ret_trans = nullptr;
        for (int i = 0; i < (int)PAGE_COUNT; i++) {
            spi_device_get_trans_result((spi_device_handle_t)spi_handle, &ret_trans, portMAX_DELAY);
        }
        uint32_t elapsed = (uint32_t)esp_timer_get_time() - transfer_start_micros;
        stats.last_transfer_time_us = elapsed;
        if (elapsed > stats.peak_transfer_time_us) {
            stats.peak_transfer_time_us = elapsed;
        }
    }
#else
    // Host Emulated DMA Timing (~1.25 ms for 1024 bytes @ 8MHz SPI)
    stats.last_transfer_time_us = 1250;
    if (stats.last_transfer_time_us > stats.peak_transfer_time_us) {
        stats.peak_transfer_time_us = stats.last_transfer_time_us;
    }
#endif

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
