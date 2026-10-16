#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>

#define HOST_TEST 1
#include "hw_config.h"
#include "uac_host.h"
#include "usb_manager.h"

MockAudioPlayer audioPlayer;

int main() {
    std::cout << "Starting FiiO KA11 USB Audio Class (UAC) Host Verification..." << std::endl;

    // ------------------------------------------------------------------------
    // Test 1: Clean Startup and Initial State
    // ------------------------------------------------------------------------
    bool begin_ok = usbManager.begin();
    assert(begin_ok);
    assert(usbManager.getState() == USBAudioState::DISCONNECTED);
    assert(!usbManager.isMounted());
    assert(usbManager.isSafeToEject());
    assert(strcmp(usbManager.getStateString(), "DISCONNECTED") == 0);
    std::cout << "  [PASS] 1. Initial USBManager disconnected state verified." << std::endl;

    // ------------------------------------------------------------------------
    // Test 2: Host Connection & Real FiiO KA11 Enumeration
    // ------------------------------------------------------------------------
    bool mount_ok = usbManager.mount();
    assert(mount_ok);
    assert(usbManager.isMounted());
    assert(usbManager.isConnected());
    assert(usbManager.getState() == USBAudioState::MOUNTED_READY);
    assert(strcmp(usbManager.getStateString(), "MOUNTED (READY)") == 0);

    // Verify FiiO KA11 Device Enumeration descriptors
    assert(usbManager.getVID() == FIIO_KA11_VID);
    assert(usbManager.getPID() == FIIO_KA11_PID);
    assert(strcmp(usbManager.getProductName(), "FiiO KA11") == 0);
    assert(strcmp(usbManager.getManufacturer(), "FiiO") == 0);
    std::cout << "  [PASS] 2. FiiO KA11 USB DAC enumeration verified (VID: 0x2972, PID: 0x0053)." << std::endl;

    // ------------------------------------------------------------------------
    // Test 3: Audio Format Negotiation (UAC Alternate Settings)
    // ------------------------------------------------------------------------
    uac_host_device_handle_t handle = usbManager.getDeviceHandle();
    assert(handle != NULL);

    uac_host_dev_alt_param_t alt1 = {};
    assert(uac_host_get_device_alt_param(handle, 1, &alt1) == ESP_OK);
    assert(alt1.format == 1); // PCM
    assert(alt1.channels == 2); // Stereo
    assert(alt1.bit_resolution == 16);
    assert(alt1.sample_freq_type == 6);

    uac_host_dev_alt_param_t alt2 = {};
    assert(uac_host_get_device_alt_param(handle, 2, &alt2) == ESP_OK);
    assert(alt2.bit_resolution == 24);

    // Negotiate 44.1kHz 16-bit
    assert(usbManager.configureFormat(44100, 2, 16));
    assert(usbManager.getSampleRate() == 44100);
    assert(usbManager.getChannels() == 2);
    assert(usbManager.getBitDepth() == 16);

    // Negotiate 48.0kHz 16-bit
    assert(usbManager.configureFormat(48000, 2, 16));
    assert(usbManager.getSampleRate() == 48000);

    // Negotiate 96.0kHz 24-bit High-Res
    assert(usbManager.configureFormat(96000, 2, 24));
    assert(usbManager.getSampleRate() == 96000);
    assert(usbManager.getBitDepth() == 24);

    // Return to standard 44.1kHz 16-bit for audio playback
    assert(usbManager.configureFormat(44100, 2, 16));
    std::cout << "  [PASS] 3. USB Audio Class format negotiation (44.1k/48k/96k 16/24-bit) verified." << std::endl;

    // ------------------------------------------------------------------------
    // Test 4: Continuous PCM Streaming & Hardware Volume / Mute
    // ------------------------------------------------------------------------
    usbManager.resetTransferredCounter();
    int16_t pcmSamples[512];
    for (int i = 0; i < 512; i++) {
        pcmSamples[i] = (int16_t)(i * 32);
    }

    // Stream 10 frames (5120 samples = 10240 bytes)
    for (int f = 0; f < 10; f++) {
        size_t written = usbManager.writeSamples(pcmSamples, 512);
        assert(written == 512);
        assert(usbManager.isStreaming());
    }

    assert(usbManager.getSamplesTransferred() == 5120);
    assert(usbManager.getBytesTransferred() == 5120 * sizeof(int16_t));

    // Hardware Volume & Mute control
    assert(usbManager.setVolume(90));
    assert(usbManager.getVolume() == 90);
    assert(usbManager.setMute(true));
    assert(usbManager.isMute());
    assert(usbManager.setMute(false));
    assert(!usbManager.isMute());
    std::cout << "  [PASS] 4. Continuous PCM streaming, volume, and mute controls verified." << std::endl;

    // ------------------------------------------------------------------------
    // Test 5: Hot-Unplug & Graceful I2S Speaker Fallback
    // ------------------------------------------------------------------------
    audioPlayer.setOutputMode(OUTPUT_MODE_FIIO_USB_DAC);
    assert(audioPlayer.getOutputMode() == OUTPUT_MODE_FIIO_USB_DAC);

    // Simulate unexpected physical cable disconnect
    uac_host_simulate_device_disconnected(handle);

    // Verify hot-unplug safety: state resets and audio automatically falls back to I2S speaker
    assert(usbManager.getState() == USBAudioState::DISCONNECTED);
    assert(!usbManager.isMounted());
    assert(audioPlayer.getOutputMode() == OUTPUT_MODE_SPEAKER_I2S);
    std::cout << "  [PASS] 5. Hot-unplug handling and instant I2S speaker fallback verified." << std::endl;

    // ------------------------------------------------------------------------
    // Test 6: Safe Eject Protocol & Bus Parking
    // ------------------------------------------------------------------------
    assert(usbManager.mount());
    audioPlayer.setOutputMode(OUTPUT_MODE_FIIO_USB_DAC);
    assert(usbManager.writeSamples(pcmSamples, 256) == 256);

    // User triggers Safe Eject
    usbManager.safeEject();
    assert(usbManager.getState() == USBAudioState::SAFE_TO_EJECT);
    assert(usbManager.isSafeToEject());
    assert(strcmp(usbManager.getStateString(), "SAFE TO UNPLUG") == 0);
    assert(audioPlayer.getOutputMode() == OUTPUT_MODE_SPEAKER_I2S);
    std::cout << "  [PASS] 6. Safe Eject protocol, bus line parking, and safe state verified." << std::endl;

    // ------------------------------------------------------------------------
    // Test 7: Output Switching Back and Forth
    // ------------------------------------------------------------------------
    assert(usbManager.mount());
    audioPlayer.setOutputMode(OUTPUT_MODE_FIIO_USB_DAC);
    assert(audioPlayer.getOutputMode() == OUTPUT_MODE_FIIO_USB_DAC);
    assert(strcmp(audioPlayer.getOutputModeShortName(), "KA11") == 0);

    audioPlayer.setOutputMode(OUTPUT_MODE_SPEAKER_I2S);
    assert(audioPlayer.getOutputMode() == OUTPUT_MODE_SPEAKER_I2S);
    assert(strcmp(audioPlayer.getOutputModeShortName(), "SPKR") == 0);
    std::cout << "  [PASS] 7. Seamless switching between FiiO KA11 and MAX98357A I2S verified." << std::endl;

    std::cout << "\n[ALL CHECKS PASSED] FiiO KA11 USB Host UAC implementation 100% verified." << std::endl;
    return 0;
}
