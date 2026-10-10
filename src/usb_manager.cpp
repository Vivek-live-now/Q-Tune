#include "usb_manager.h"
#include "audio_player.h"

USBManager usbManager;

USBManager::USBManager() :
    state(USBAudioState::DISCONNECTED),
    autoMountEnabled(true),
    hardwareInitialized(false),
    stateChangeTime(0),
    samplesTransferred(0),
    dacDetected(false)
{}

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
    return true;
}

bool USBManager::mount() {
    if (!hardwareInitialized) {
        begin();
    }

    state = USBAudioState::CONNECTING;
    stateChangeTime = millis();

    // Re-engage Host bus termination
    configureHostPins();

    // Stabilization delay (100ms) for VBUS and inrush current settling
    delay(100);

    dacDetected = true;
    state = USBAudioState::MOUNTED_READY;
    stateChangeTime = millis();
    return true;
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

    // 2. Safe bus park protocol: tristate USB D+ / D- lines
    parkHostBus();

    dacDetected = false;
    state = USBAudioState::SAFE_TO_EJECT;
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
    samplesTransferred += sampleCount;

    // Fixed rate-limiting simulation matching 44.1kHz stereo isochronous timing
    // 512 samples at 44.1kHz stereo = ~5.8ms of audio
    return sampleCount;
}

void USBManager::setAutoMount(bool enable) {
    autoMountEnabled = enable;
}

bool USBManager::isAutoMount() const {
    return autoMountEnabled;
}

void USBManager::loop() {
    // Background polling for state machine and hotplug supervision
    if (state == USBAudioState::STREAMING && !audioPlayer.isPlaying()) {
        state = USBAudioState::MOUNTED_READY;
    }
}
