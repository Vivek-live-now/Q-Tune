#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#if defined(HOST_TEST)
#include <cstdint>
#include <cstddef>
#include <cstring>
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT_PULLDOWN
#define INPUT_PULLDOWN 2
#endif
inline void pinMode(int pin, int mode) {}
inline unsigned long millis() {
    static unsigned long ms = 1000;
    return ms += 10;
}
inline void delay(unsigned long ms) {}
inline void yield() {}
#else
#include <Arduino.h>
#endif

#include "hw_config.h"
#include "uac_host.h"

#ifndef AUDIO_OUTPUT_MODE_DEFINED
#define AUDIO_OUTPUT_MODE_DEFINED
enum AudioOutputMode {
    OUTPUT_MODE_SPEAKER_I2S = 0,
    OUTPUT_MODE_FIIO_USB_DAC = 1
};
#endif

#if defined(HOST_TEST)
class MockAudioPlayer {
public:
    AudioOutputMode mode = OUTPUT_MODE_SPEAKER_I2S;
    bool playing = false;
    uint8_t volume = 80;
    void setOutputMode(AudioOutputMode m) { mode = m; }
    AudioOutputMode getOutputMode() const { return mode; }
    const char* getOutputModeShortName() const {
        return (mode == OUTPUT_MODE_FIIO_USB_DAC) ? "KA11" : "SPKR";
    }
    bool isPlaying() const { return playing; }
    void setVolume(uint8_t v) { volume = v; }
    uint8_t getVolume() const { return volume; }
};
extern MockAudioPlayer audioPlayer;
#endif

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
    ~USBManager();
    bool begin();
    void loop();

    // Safe Mount & Safe Eject Protocol
    bool mount();
    void safeEject();
    void handleHotUnplug();
    bool isMounted() const;
    bool isSafeToEject() const;
    bool isConnected() const;
    bool isStreaming() const;
    USBAudioState getState() const;
    const char* getStateString() const;

    // Digital PCM Audio Stream Output Sink
    size_t writeSamples(const int16_t *samples, size_t sampleCount);

    // Audio Format Negotiation
    bool configureFormat(uint32_t sampleRate, uint8_t channels = 2, uint8_t bitDepth = 16);
    uint32_t getSampleRate() const { return currentSampleRate; }
    uint8_t getChannels() const { return currentChannels; }
    uint8_t getBitDepth() const { return currentBitDepth; }

    // Hardware Identification & Diagnostics
    uint16_t getVID() const { return deviceVID; }
    uint16_t getPID() const { return devicePID; }
    const char* getProductName() const { return productName; }
    const char* getManufacturer() const { return manufacturerName; }

    // Hardware Volume & Mute Controls (CS43131 Direct)
    bool setVolume(uint8_t volume);
    uint8_t getVolume() const { return currentVolume; }
    bool setMute(bool mute);
    bool isMute() const { return isMuted; }

    // Auto-mount and detection settings
    void setAutoMount(bool enable);
    bool isAutoMount() const;
    uint32_t getSamplesTransferred() const { return samplesTransferred; }
    uint64_t getBytesTransferred() const { return bytesTransferred; }
    uint32_t getUnderrunCount() const { return underrunCount; }
    void resetTransferredCounter() { samplesTransferred = 0; bytesTransferred = 0; underrunCount = 0; }

    // Device Handle
    uac_host_device_handle_t getDeviceHandle() const { return uacDeviceHandle; }

    // Driver Callbacks (Internal use)
    void onDriverEvent(uint8_t addr, uint8_t iface_num, uac_host_driver_event_t event);
    void onDeviceEvent(uac_host_device_event_t event);

private:
    USBAudioState state;
    bool autoMountEnabled;
    bool hardwareInitialized;
    unsigned long stateChangeTime;
    uint32_t samplesTransferred;
    uint64_t bytesTransferred;
    uint32_t underrunCount;
    bool dacDetected;

    // UAC Host Driver & Device Handle
    bool uacDriverInstalled;
    uac_host_device_handle_t uacDeviceHandle;
    uint8_t deviceAddr;
    uint8_t deviceIfaceNum;

    // Negotiated Device Attributes
    uint16_t deviceVID;
    uint16_t devicePID;
    char productName[64];
    char manufacturerName[32];
    uint32_t currentSampleRate;
    uint8_t currentChannels;
    uint8_t currentBitDepth;
    uint8_t currentVolume;
    bool isMuted;

    void configureHostPins();
    void parkHostBus();
    bool initializeUACHost();
    void cleanupUACHost();
};

extern USBManager usbManager;

#endif // USB_MANAGER_H
