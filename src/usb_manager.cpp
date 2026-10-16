#include "usb_manager.h"
#if !defined(HOST_TEST)
#include "audio_player.h"
#include "player_config.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

USBManager usbManager;

static void uac_driver_event_callback(uint8_t addr, uint8_t iface_num, const uac_host_driver_event_t event, void *arg) {
    USBManager *mgr = (USBManager*)arg;
    if (mgr) {
        mgr->onDriverEvent(addr, iface_num, event);
    }
}

static void uac_device_event_callback(uac_host_device_handle_t uac_device_handle, const uac_host_device_event_t event, void *arg) {
    (void)uac_device_handle;
    USBManager *mgr = (USBManager*)arg;
    if (mgr) {
        mgr->onDeviceEvent(event);
    }
}

USBManager::USBManager() :
    state(USBAudioState::DISCONNECTED),
    autoMountEnabled(true),
    hardwareInitialized(false),
    stateChangeTime(0),
    samplesTransferred(0),
    bytesTransferred(0),
    underrunCount(0),
    dacDetected(false),
    uacDriverInstalled(false),
    uacDeviceHandle(NULL),
    deviceAddr(0),
    deviceIfaceNum(0),
    deviceVID(0),
    devicePID(0),
    currentSampleRate(44100),
    currentChannels(2),
    currentBitDepth(16),
    currentVolume(10), // Ear protection initial volume
    isMuted(false)
{
    strcpy(productName, "FiiO KA11");
    strcpy(manufacturerName, "FiiO");
}

USBManager::~USBManager() {
    safeEject();
    cleanupUACHost();
}

void USBManager::configureHostPins() {
    // USB Full-Speed Host Mode Pin Configuration on ESP32-S3:
    // GPIO 19: USB D- (DM)
    // GPIO 20: USB D+ (DP)
    // In USB Host mode, the host provides ~15k pull-downs on D+ and D- to detect device attachment.
    pinMode(USB_HOST_DM, INPUT_PULLDOWN);
    pinMode(USB_HOST_DP, INPUT_PULLDOWN);
}

void USBManager::parkHostBus() {
    // Safe Bus Park Protocol:
    // Disconnects active pull-ups/pull-downs, tristating lines to protect
    // the Cirrus Logic CS43131 DAC against inductive voltage spikes or popping when unplugged.
    pinMode(USB_HOST_DM, INPUT);
    pinMode(USB_HOST_DP, INPUT);
}

bool USBManager::begin() {
    // Note: Do NOT configure host pull-downs here at boot; GPIO 19 & 20 are used
    // by native USB Serial CDC. Host pins are configured only upon explicit mount().
    hardwareInitialized = true;
    state = USBAudioState::DISCONNECTED;
    stateChangeTime = millis();
    samplesTransferred = 0;
    bytesTransferred = 0;
    underrunCount = 0;
    return true;
}

bool USBManager::initializeUACHost() {
    if (uacDriverInstalled) return true;

    uac_host_driver_config_t driver_cfg = {
        .create_background_task = false,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = uac_driver_event_callback,
        .callback_arg = this
    };

    esp_err_t err = uac_host_install(&driver_cfg);
    if (err == ESP_OK) {
        uacDriverInstalled = true;
        return true;
    }
    return false;
}

void USBManager::cleanupUACHost() {
    if (uacDriverInstalled) {
        uac_host_uninstall();
        uacDriverInstalled = false;
    }
}

bool USBManager::mount() {
    if (!hardwareInitialized) {
        begin();
    }

    state = USBAudioState::CONNECTING;
    stateChangeTime = millis();

    // Re-engage Host bus termination
    configureHostPins();

    // Initialize UAC Host stack
    initializeUACHost();

    // Stabilization delay (100ms) for VBUS and inrush current settling
    delay(100);

    // Enumerate and open UAC device
    uac_host_device_config_t dev_cfg = {
        .addr = 1,
        .iface_num = 1,
        .buffer_size = 16384,
        .buffer_threshold = 4096,
        .callback = uac_device_event_callback,
        .callback_arg = this
    };

    esp_err_t err = uac_host_device_open(&dev_cfg, &uacDeviceHandle);
    if (err == ESP_OK && uacDeviceHandle != NULL) {
        deviceAddr = 1;
        deviceIfaceNum = 1;

        // Retrieve device descriptors & confirmation of FiiO KA11 enumeration
        uac_host_dev_info_t dev_info = {};
        if (uac_host_get_device_info(uacDeviceHandle, &dev_info) == ESP_OK) {
            deviceVID = dev_info.VID;
            devicePID = dev_info.PID;
            wcstombs(productName, dev_info.iProduct, sizeof(productName) - 1);
            wcstombs(manufacturerName, dev_info.iManufacturer, sizeof(manufacturerName) - 1);
        } else {
            deviceVID = FIIO_KA11_VID;
            devicePID = FIIO_KA11_PID;
            strcpy(productName, "FiiO KA11");
            strcpy(manufacturerName, "FiiO");
        }

        // Negotiate audio format (default 44.1kHz 16-bit Stereo PCM)
        configureFormat(currentSampleRate, currentChannels, currentBitDepth);

        // EAR SAFETY PROTECTION:
        // Automatically set initial volume to safe 10% on connect to protect listener's ears
        uint8_t safeVol = 10;
#if !defined(HOST_TEST)
        safeVol = playerConfig.getFiioVolume();
        if (safeVol == 0 || safeVol > 50) safeVol = 10;
        playerConfig.setFiioVolume(safeVol);
#endif
        setVolume(safeVol);
        audioPlayer.setVolume(safeVol);

        dacDetected = true;
        state = USBAudioState::MOUNTED_READY;
        stateChangeTime = millis();
        return true;
    }

    state = USBAudioState::ERROR_STATE;
    return false;
}

bool USBManager::configureFormat(uint32_t sampleRate, uint8_t channels, uint8_t bitDepth) {
    if (uacDeviceHandle == NULL) return false;

    // If currently streaming, stop stream first to re-negotiate
    if (state == USBAudioState::STREAMING) {
        uac_host_device_stop(uacDeviceHandle);
    }

    uac_host_stream_config_t stream_cfg = {
        .channels = channels,
        .bit_resolution = bitDepth,
        .sample_freq = sampleRate,
        .flags = 0
    };

    esp_err_t err = uac_host_device_start(uacDeviceHandle, &stream_cfg);
    if (err == ESP_OK) {
        currentSampleRate = sampleRate;
        currentChannels = channels;
        currentBitDepth = bitDepth;
        // Apply hardware volume
        uac_host_device_set_volume(uacDeviceHandle, currentVolume);
        uac_host_device_set_mute(uacDeviceHandle, isMuted);
        return true;
    }
    return false;
}

void USBManager::safeEject() {
    if (state == USBAudioState::SAFE_TO_EJECT || state == USBAudioState::DISCONNECTED) {
        return;
    }

    // 1. If currently set as active output, automatically route audio back to onboard speaker
    // to prevent audio thread starvation or hanging
    if (audioPlayer.getOutputMode() == OUTPUT_MODE_FIIO_USB_DAC) {
        audioPlayer.setOutputMode(OUTPUT_MODE_SPEAKER_I2S);
    }

    // Stop stream and close UAC device handle
    if (uacDeviceHandle != NULL) {
        uac_host_device_stop(uacDeviceHandle);
        uac_host_device_close(uacDeviceHandle);
        uacDeviceHandle = NULL;
    }

    // 2. Safe bus park protocol: tristate USB D+ / D- lines
    parkHostBus();

    dacDetected = false;
    state = USBAudioState::SAFE_TO_EJECT;
    stateChangeTime = millis();
}

void USBManager::handleHotUnplug() {
    if (state == USBAudioState::DISCONNECTED) return;

    // Auto-fallback to I2S speaker immediately so audio playback never stalls
    if (audioPlayer.getOutputMode() == OUTPUT_MODE_FIIO_USB_DAC) {
        audioPlayer.setOutputMode(OUTPUT_MODE_SPEAKER_I2S);
    }

    if (uacDeviceHandle != NULL) {
        uac_host_device_close(uacDeviceHandle);
        uacDeviceHandle = NULL;
    }

    parkHostBus();
    dacDetected = false;
    state = USBAudioState::DISCONNECTED;
    stateChangeTime = millis();
}

bool USBManager::isMounted() const {
    return (state == USBAudioState::MOUNTED_READY || state == USBAudioState::STREAMING);
}

bool USBManager::isSafeToEject() const {
    return (state == USBAudioState::SAFE_TO_EJECT || state == USBAudioState::DISCONNECTED);
}

bool USBManager::isConnected() const {
    return dacDetected && (state != USBAudioState::SAFE_TO_EJECT);
}

bool USBManager::isStreaming() const {
    return (state == USBAudioState::STREAMING);
}

USBAudioState USBManager::getState() const {
    return state;
}

const char* USBManager::getStateString() const {
    switch (state) {
        case USBAudioState::DISCONNECTED:   return "DISCONNECTED";
        case USBAudioState::CONNECTING:     return "CONNECTING...";
        case USBAudioState::MOUNTED_READY:  return "MOUNTED (READY)";
        case USBAudioState::STREAMING:      return "STREAMING (KA11)";
        case USBAudioState::SAFE_TO_EJECT:  return "SAFE TO UNPLUG";
        case USBAudioState::ERROR_STATE:    return "ERROR";
        default:                            return "UNKNOWN";
    }
}

size_t USBManager::writeSamples(const int16_t *samples, size_t sampleCount) {
    if (!isMounted() || !samples || sampleCount == 0) {
        return 0;
    }

    state = USBAudioState::STREAMING;
    size_t byteCount = sampleCount * sizeof(int16_t);

    if (uacDeviceHandle != NULL) {
        esp_err_t err = uac_host_device_write(uacDeviceHandle, (const uint8_t*)samples, (uint32_t)byteCount, 10);
        if (err == ESP_OK) {
            samplesTransferred += sampleCount;
            bytesTransferred += byteCount;
            return sampleCount;
        } else {
            underrunCount++;
            samplesTransferred += sampleCount;
            bytesTransferred += byteCount;
            return sampleCount;
        }
    }

    samplesTransferred += sampleCount;
    bytesTransferred += byteCount;
    return sampleCount;
}

bool USBManager::setVolume(uint8_t volume) {
    currentVolume = (volume > 100) ? 100 : volume;
    if (uacDeviceHandle != NULL) {
        return (uac_host_device_set_volume(uacDeviceHandle, currentVolume) == ESP_OK);
    }
    return true;
}

bool USBManager::setMute(bool mute) {
    isMuted = mute;
    if (uacDeviceHandle != NULL) {
        return (uac_host_device_set_mute(uacDeviceHandle, isMuted) == ESP_OK);
    }
    return true;
}

void USBManager::onDriverEvent(uint8_t addr, uint8_t iface_num, uac_host_driver_event_t event) {
    if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        deviceAddr = addr;
        deviceIfaceNum = iface_num;
        if (autoMountEnabled && state == USBAudioState::DISCONNECTED) {
            mount();
        }
    }
}

void USBManager::onDeviceEvent(uac_host_device_event_t event) {
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED || event == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        handleHotUnplug();
    }
}

void USBManager::setAutoMount(bool enable) {
    autoMountEnabled = enable;
}

bool USBManager::isAutoMount() const {
    return autoMountEnabled;
}

void USBManager::loop() {
    if (uacDriverInstalled) {
        uac_host_handle_events(0);
    }
    if (state == USBAudioState::STREAMING && !audioPlayer.isPlaying()) {
        state = USBAudioState::MOUNTED_READY;
    }
}
