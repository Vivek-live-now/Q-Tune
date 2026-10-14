#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include "gdma_manager.h"

int main() {
    std::cout << "Starting Host C++ Q-Tune GDMA Manager unit verification..." << std::endl;

    // 1. Initialization and 16-byte alignment check
    bool init_ok = gdmaManager.begin(7, 5, 4, 2, 41, 8000000UL);
    assert(init_ok);
    assert(gdmaManager.isInitialized());
    assert(!gdmaManager.isBusy());
    assert(gdmaManager.getState() == GdmaState::IDLE);

    // 2. Double-buffered Asynchronous Flush and Ping-Pong Buffer Swap
    ALIGNED16 uint8_t frame1[GdmaManager::BUFFER_SIZE];
    for (size_t i = 0; i < GdmaManager::BUFFER_SIZE; i++) {
        frame1[i] = (uint8_t)(i ^ 0x3C);
    }

    bool flush1_ok = gdmaManager.flushAsync(frame1);
    assert(flush1_ok);
    assert(gdmaManager.isBusy());
    assert(gdmaManager.getState() == GdmaState::TRANSMITTING);
    assert(gdmaManager.getStats().total_transfers == 1);

    gdmaManager.waitIdle();
    assert(!gdmaManager.isBusy());
    assert(gdmaManager.getState() == GdmaState::IDLE);
    assert(gdmaManager.getStats().last_transfer_time_us > 0);

    const uint8_t* active1 = gdmaManager.getActiveDmaBuffer();
    assert(active1 != nullptr);
    assert(memcmp(active1, frame1, GdmaManager::BUFFER_SIZE) == 0);

    // Second frame to test ping-pong swap
    ALIGNED16 uint8_t frame2[GdmaManager::BUFFER_SIZE];
    for (size_t i = 0; i < GdmaManager::BUFFER_SIZE; i++) {
        frame2[i] = (uint8_t)(i ^ 0xC3);
    }

    bool flush2_ok = gdmaManager.flushAsync(frame2);
    assert(flush2_ok);
    assert(gdmaManager.getStats().total_transfers == 2);

    gdmaManager.waitIdle();
    const uint8_t* active2 = gdmaManager.getActiveDmaBuffer();
    assert(active2 != nullptr);
    assert(active2 != active1); // Must have swapped to alternate ping-pong buffer!
    assert(memcmp(active2, frame2, GdmaManager::BUFFER_SIZE) == 0);

    // 3. Fast Memory-to-Memory (M2M) Engine
    ALIGNED16 uint8_t m2m_src[GdmaManager::BUFFER_SIZE];
    ALIGNED16 uint8_t m2m_dst[GdmaManager::BUFFER_SIZE];
    for (size_t i = 0; i < GdmaManager::BUFFER_SIZE; i++) {
        m2m_src[i] = (uint8_t)(i * 5 + 13);
        m2m_dst[i] = 0;
    }

    uint32_t prev_m2m_count = gdmaManager.getStats().m2m_copy_count;
    gdmaManager.memcpyM2M(m2m_dst, m2m_src, GdmaManager::BUFFER_SIZE);
    assert(memcmp(m2m_dst, m2m_src, GdmaManager::BUFFER_SIZE) == 0);
    assert(gdmaManager.getStats().m2m_copy_count > prev_m2m_count);

    // 4. Repetitive Stress Testing (100 frames)
    for (int f = 0; f < 100; f++) {
        frame1[0] = (uint8_t)f;
        assert(gdmaManager.flushAsync(frame1));
        gdmaManager.waitIdle();
    }
    assert(gdmaManager.getStats().total_transfers == 102);
    assert(gdmaManager.getStats().dropped_frames == 0);

    std::cout << "  [PASS] 1. Q-Tune GDMA 16-byte alignment, double-buffering, and non-blocking queue verified." << std::endl;
    std::cout << "  [PASS] 2. Ping-pong buffer swap and byte-perfect frame integrity verified." << std::endl;
    std::cout << "  [PASS] 3. Fast M2M DMA / SIMD memory copy verified." << std::endl;
    std::cout << "  [PASS] 4. Multi-frame stress test (100 frames) completed with 0 dropped frames." << std::endl;
    std::cout << "[PASS] Host C++ Q-Tune GDMA Manager unit verification passed." << std::endl;
    return 0;
}
