#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <Arduino.h>
#include "hw_config.h"

// ============================================================================
// FiiO KA11 USB Audio Class (UAC) Subsystem & Safe Mount / Eject Manager
// Targets Cirrus Logic CS43131 High-Res DAC via native ESP32-S3 USB-OTG Host
// ============================================================================

enum class USBAudioState {
    DISCONNECTED,
    CONNECTING,
    MOUNTED_READY,
    STREAMING,
    SAFE_TO_EJECT,
    ERROR_STATE
};

class USBManager {
public:
    USBManager();
    bool begin();
    void loop();

    // Safe Mount & Safe Eject Protocol
    bool mount();
    void safeEject();
    bool isMounted() const;
    bool isSafeToEject() const;
    bool isConnected() const;
    USBAudioState getState() const;
    const char* getStateString() const;

    // Digital PCM Audio Stream Output Sink
    size_t writeSamples(const int16_t *samples, size_t sampleCount);

    // Auto-mount and detection settings
    void setAutoMount(bool enable);
    bool isAutoMount() const;
    uint32_t getSamplesTransferred() const { return samplesTransferred; }
    void resetTransferredCounter() { samplesTransferred = 0; }

private:
    USBAudioState state;
    bool autoMountEnabled;
    bool hardwareInitialized;
    unsigned long stateChangeTime;
    uint32_t samplesTransferred;
    bool dacDetected;

    void configureHostPins();
    void parkHostBus();
};

extern USBManager usbManager;

#endif // USB_MANAGER_H
