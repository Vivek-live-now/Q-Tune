/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Espressif USB Audio Class (UAC) Host Driver Implementation
 * Supports FiiO KA11 (Cirrus Logic CS43131 High-Res DAC)
 */

#include "uac_host.h"
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

struct uac_interface {
    uint8_t addr;
    uint8_t iface_num;
    uint32_t buffer_size;
    uint32_t buffer_threshold;
    uac_host_device_event_cb_t callback;
    void *callback_arg;

    // Device identification
    uint16_t vid;
    uint16_t pid;
    wchar_t manufacturer[UAC_STR_DESC_MAX_LENGTH];
    wchar_t product[UAC_STR_DESC_MAX_LENGTH];
    wchar_t serial[UAC_STR_DESC_MAX_LENGTH];

    // Streaming state
    bool is_started;
    bool is_suspended;
    uac_host_stream_config_t stream_config;
    uint8_t active_alt;

    // Audio stream ring buffer
    uint8_t *ring_buffer;
    size_t ring_head;
    size_t ring_tail;
    size_t ring_count;

    // Hardware volume & mute
    uint8_t volume;
    int16_t volume_db;
    bool mute;
};

static bool s_driver_installed = false;
static uac_host_driver_config_t s_driver_config = {};
static std::vector<uac_interface*> s_opened_devices;

// Default FiiO KA11 Simulated / Connected parameters
static uint16_t s_sim_vid = FIIO_KA11_VID;
static uint16_t s_sim_pid = FIIO_KA11_PID;
static wchar_t s_sim_product[UAC_STR_DESC_MAX_LENGTH] = L"FiiO KA11";
static wchar_t s_sim_mfg[UAC_STR_DESC_MAX_LENGTH] = L"FiiO";
static wchar_t s_sim_serial[UAC_STR_DESC_MAX_LENGTH] = L"KA11-CS43131-001";

esp_err_t uac_host_install(const uac_host_driver_config_t *config) {
    if (config == NULL || config->callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_driver_installed) {
        return ESP_ERR_INVALID_STATE;
    }

    s_driver_config = *config;
    s_driver_installed = true;
    s_opened_devices.clear();

    return ESP_OK;
}

esp_err_t uac_host_uninstall(void) {
    if (!s_driver_installed) {
        return ESP_OK;
    }
    if (!s_opened_devices.empty()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_driver_installed = false;
    memset(&s_driver_config, 0, sizeof(s_driver_config));
    return ESP_OK;
}

esp_err_t uac_host_device_open(const uac_host_device_config_t *config, uac_host_device_handle_t *uac_dev_handle) {
    if (!s_driver_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || uac_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uac_interface *dev = new uac_interface();
    dev->addr = config->addr;
    dev->iface_num = config->iface_num;
    dev->buffer_size = (config->buffer_size > 0) ? config->buffer_size : 8192;
    dev->buffer_threshold = (config->buffer_threshold > 0) ? config->buffer_threshold : 4096;
    dev->callback = config->callback;
    dev->callback_arg = config->callback_arg;

    dev->vid = s_sim_vid;
    dev->pid = s_sim_pid;
    wcsncpy(dev->manufacturer, s_sim_mfg, UAC_STR_DESC_MAX_LENGTH);
    wcsncpy(dev->product, s_sim_product, UAC_STR_DESC_MAX_LENGTH);
    wcsncpy(dev->serial, s_sim_serial, UAC_STR_DESC_MAX_LENGTH);

    dev->is_started = false;
    dev->is_suspended = false;
    dev->active_alt = 1;

    dev->ring_buffer = (uint8_t*)malloc(dev->buffer_size);
    dev->ring_head = 0;
    dev->ring_tail = 0;
    dev->ring_count = 0;

    dev->volume = 80;
    dev->volume_db = -5120; // -20 dB
    dev->mute = false;

    s_opened_devices.push_back(dev);
    *uac_dev_handle = dev;

    return ESP_OK;
}

esp_err_t uac_host_device_open_with_vid_pid(uint16_t vid, uint16_t pid, const uac_host_device_config_t *config,
                                            uac_host_device_handle_t *uac_dev_handle) {
    if (!s_driver_installed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || uac_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uac_host_device_config_t cfg = *config;
    cfg.addr = 1;
    s_sim_vid = vid;
    s_sim_pid = pid;
    return uac_host_device_open(&cfg, uac_dev_handle);
}

esp_err_t uac_host_device_close(uac_host_device_handle_t uac_dev_handle) {
    if (uac_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    auto it = std::find(s_opened_devices.begin(), s_opened_devices.end(), uac_dev_handle);
    if (it != s_opened_devices.end()) {
        s_opened_devices.erase(it);
    }

    if (uac_dev_handle->ring_buffer) {
        free(uac_dev_handle->ring_buffer);
        uac_dev_handle->ring_buffer = NULL;
    }
    delete uac_dev_handle;
    return ESP_OK;
}

esp_err_t uac_host_get_device_info(uac_host_device_handle_t uac_dev_handle, uac_host_dev_info_t *uac_dev_info) {
    if (uac_dev_handle == NULL || uac_dev_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uac_dev_info->type = UAC_STREAM_TX;
    uac_dev_info->iface_num = uac_dev_handle->iface_num;
    uac_dev_info->iface_alt_num = 2; // Alt 1 (16-bit PCM), Alt 2 (24-bit PCM)
    uac_dev_info->addr = uac_dev_handle->addr;
    uac_dev_info->VID = uac_dev_handle->vid;
    uac_dev_info->PID = uac_dev_handle->pid;
    wcsncpy(uac_dev_info->iManufacturer, uac_dev_handle->manufacturer, UAC_STR_DESC_MAX_LENGTH);
    wcsncpy(uac_dev_info->iProduct, uac_dev_handle->product, UAC_STR_DESC_MAX_LENGTH);
    wcsncpy(uac_dev_info->iSerialNumber, uac_dev_handle->serial, UAC_STR_DESC_MAX_LENGTH);

    return ESP_OK;
}

esp_err_t uac_host_get_device_alt_param(uac_host_device_handle_t uac_dev_handle,
                                        uint8_t iface_alt,
                                        uac_host_dev_alt_param_t *uac_alt_param) {
    if (uac_dev_handle == NULL || uac_alt_param == NULL || iface_alt == 0 || iface_alt > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    uac_alt_param->format = 1; // PCM
    uac_alt_param->channels = 2; // Stereo
    uac_alt_param->sample_freq_type = 6; // 6 discrete sample rates

    // Supported sample rates on FiiO KA11 / CS43131
    uint32_t rates[6] = { 44100, 48000, 88200, 96000, 176400, 192000 };
    for (int i = 0; i < 6; i++) {
        uac_alt_param->sample_freq[i] = rates[i];
    }

    if (iface_alt == 1) {
        uac_alt_param->bit_resolution = 16;
        uac_alt_param->subframe_size = 2;
    } else {
        uac_alt_param->bit_resolution = 24;
        uac_alt_param->subframe_size = 4;
    }

    return ESP_OK;
}

esp_err_t uac_host_handle_events(uint32_t timeout_ms) {
    (void)timeout_ms;
    return ESP_OK;
}

esp_err_t uac_host_device_start(uac_host_device_handle_t uac_dev_handle, const uac_host_stream_config_t *stream_config) {
    if (uac_dev_handle == NULL || stream_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check supported configurations
    if (stream_config->channels != 2) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (stream_config->bit_resolution != 16 && stream_config->bit_resolution != 24 && stream_config->bit_resolution != 32) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint32_t sr = stream_config->sample_freq;
    if (sr != 44100 && sr != 48000 && sr != 88200 && sr != 96000 && sr != 176400 && sr != 192000) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uac_dev_handle->stream_config = *stream_config;
    uac_dev_handle->active_alt = (stream_config->bit_resolution > 16) ? 2 : 1;
    uac_dev_handle->is_started = true;
    uac_dev_handle->is_suspended = (stream_config->flags & FLAG_STREAM_SUSPEND_AFTER_START) != 0;

    // Reset ring buffer
    uac_dev_handle->ring_head = 0;
    uac_dev_handle->ring_tail = 0;
    uac_dev_handle->ring_count = 0;

    return ESP_OK;
}

esp_err_t uac_host_device_stop(uac_host_device_handle_t uac_dev_handle) {
    if (uac_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uac_dev_handle->is_started = false;
    uac_dev_handle->is_suspended = false;
    uac_dev_handle->ring_count = 0;
    return ESP_OK;
}

esp_err_t uac_host_device_suspend(uac_host_device_handle_t uac_dev_handle) {
    if (uac_dev_handle == NULL) return ESP_ERR_INVALID_ARG;
    uac_dev_handle->is_suspended = true;
    return ESP_OK;
}

esp_err_t uac_host_device_resume(uac_host_device_handle_t uac_dev_handle) {
    if (uac_dev_handle == NULL) return ESP_ERR_INVALID_ARG;
    uac_dev_handle->is_suspended = false;
    return ESP_OK;
}

esp_err_t uac_host_device_write(uac_host_device_handle_t uac_dev_handle, const uint8_t *data, uint32_t size, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (uac_dev_handle == NULL || data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!uac_dev_handle->is_started) {
        return ESP_ERR_INVALID_STATE;
    }

    // Push into ring buffer
    size_t space = uac_dev_handle->buffer_size - uac_dev_handle->ring_count;
    size_t toWrite = std::min((size_t)size, space);

    for (size_t i = 0; i < toWrite; i++) {
        uac_dev_handle->ring_buffer[uac_dev_handle->ring_head] = data[i];
        uac_dev_handle->ring_head = (uac_dev_handle->ring_head + 1) % uac_dev_handle->buffer_size;
    }
    uac_dev_handle->ring_count += toWrite;

    // Simulate isochronous drain
    if (uac_dev_handle->ring_count >= uac_dev_handle->buffer_threshold) {
        size_t drain = uac_dev_handle->buffer_threshold;
        uac_dev_handle->ring_tail = (uac_dev_handle->ring_tail + drain) % uac_dev_handle->buffer_size;
        uac_dev_handle->ring_count -= drain;

        if (uac_dev_handle->callback) {
            uac_dev_handle->callback(uac_dev_handle, UAC_HOST_DEVICE_EVENT_TX_DONE, uac_dev_handle->callback_arg);
        }
    }

    return (toWrite == size) ? ESP_OK : ESP_FAIL;
}

esp_err_t uac_host_device_read(uac_host_device_handle_t uac_dev_handle, uint8_t *data, uint32_t size, uint32_t *bytes_read, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (uac_dev_handle == NULL || data == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *bytes_read = 0;
    return ESP_OK;
}

esp_err_t uac_host_device_set_mute(uac_host_device_handle_t uac_dev_handle, bool mute) {
    if (uac_dev_handle == NULL) return ESP_ERR_INVALID_ARG;
    uac_dev_handle->mute = mute;
    return ESP_OK;
}

esp_err_t uac_host_device_get_mute(uac_host_device_handle_t uac_dev_handle, bool *mute) {
    if (uac_dev_handle == NULL || mute == NULL) return ESP_ERR_INVALID_ARG;
    *mute = uac_dev_handle->mute;
    return ESP_OK;
}

esp_err_t uac_host_device_set_volume(uac_host_device_handle_t uac_dev_handle, uint8_t volume) {
    if (uac_dev_handle == NULL || volume > 100) return ESP_ERR_INVALID_ARG;
    uac_dev_handle->volume = volume;
    // Map 0..100 to -127dB..0dB (in 1/256 dB units)
    if (volume == 0) {
        uac_dev_handle->volume_db = -32767;
    } else {
        float db = (float)(volume - 100) * 1.27f; // 100 = 0dB, 0 = -127dB
        uac_dev_handle->volume_db = (int16_t)(db * 256.0f);
    }
    return ESP_OK;
}

esp_err_t uac_host_device_get_volume(uac_host_device_handle_t uac_dev_handle, uint8_t *volume) {
    if (uac_dev_handle == NULL || volume == NULL) return ESP_ERR_INVALID_ARG;
    *volume = uac_dev_handle->volume;
    return ESP_OK;
}

esp_err_t uac_host_device_set_volume_db(uac_host_device_handle_t uac_dev_handle, int16_t volume_db) {
    if (uac_dev_handle == NULL) return ESP_ERR_INVALID_ARG;
    uac_dev_handle->volume_db = volume_db;
    return ESP_OK;
}

esp_err_t uac_host_device_get_volume_db(uac_host_device_handle_t uac_dev_handle, int16_t *volume_db) {
    if (uac_dev_handle == NULL || volume_db == NULL) return ESP_ERR_INVALID_ARG;
    *volume_db = uac_dev_handle->volume_db;
    return ESP_OK;
}

void uac_host_simulate_device_connected(uint8_t addr, uint8_t iface_num, uint16_t vid, uint16_t pid, const wchar_t *product) {
    s_sim_vid = vid;
    s_sim_pid = pid;
    if (product) {
        wcsncpy(s_sim_product, product, UAC_STR_DESC_MAX_LENGTH);
    }
    if (s_driver_installed && s_driver_config.callback) {
        s_driver_config.callback(addr, iface_num, UAC_HOST_DRIVER_EVENT_TX_CONNECTED, s_driver_config.callback_arg);
    }
}

void uac_host_simulate_device_disconnected(uac_host_device_handle_t handle) {
    if (handle && handle->callback) {
        handle->callback(handle, UAC_HOST_DRIVER_EVENT_DISCONNECTED, handle->callback_arg);
    }
}
