/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Espressif USB Audio Class (UAC) 1.0/2.0 Host Driver Implementation
 * Hardware USB-OTG Host controller interface for FiiO KA11 (Cirrus Logic CS43131 DAC).
 * Reference: https://github.com/espressif/esp-usb/tree/master/host/class/uac
 */

#include "uac_host.h"
#include "uac.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

#if !defined(HOST_TEST)
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

static const char *TAG = "uac_host";
#else
// ----------------------------------------------------------------------------
// Host-Test Hardware Emulation Layer for ESP-IDF USB Host Stack
// ----------------------------------------------------------------------------
typedef void* usb_device_handle_t;
typedef void* usb_host_client_handle_t;

typedef enum {
    USB_TRANSFER_STATUS_COMPLETED = 0,
    USB_TRANSFER_STATUS_ERROR,
    USB_TRANSFER_STATUS_TIMED_OUT,
    USB_TRANSFER_STATUS_CANCELED,
    USB_TRANSFER_STATUS_NO_DEVICE,
} usb_transfer_status_t;

struct usb_transfer_t;
typedef void (*usb_transfer_cb_t)(struct usb_transfer_t *transfer);

typedef struct {
    int num_bytes;
    int actual_num_bytes;
    usb_transfer_status_t status;
} usb_isoc_packet_desc_t;

struct usb_transfer_t {
    uint8_t *data_buffer;
    size_t data_buffer_size;
    int num_bytes;
    int actual_num_bytes;
    uint32_t flags;
    usb_device_handle_t device_handle;
    uint8_t bEndpointAddress;
    usb_transfer_status_t status;
    uint32_t timeout_ms;
    usb_transfer_cb_t callback;
    void *context;
    int num_isoc_packets;
    usb_isoc_packet_desc_t isoc_packet_desc[16];
};

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

struct usb_device_desc_t {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed));

struct usb_config_desc_t {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed));


typedef enum {
    USB_HOST_CLIENT_EVENT_NEW_DEV = 0,
    USB_HOST_CLIENT_EVENT_DEV_GONE,
} usb_host_client_event_t;

typedef struct {
    usb_host_client_event_t event;
    union {
        struct {
            uint8_t address;
        } new_dev;
        struct {
            usb_device_handle_t dev_hdl;
        } dev_gone;
    };
} usb_host_client_event_msg_t;

typedef void (*usb_host_client_event_cb_t)(const usb_host_client_event_msg_t *event_msg, void *arg);

typedef struct {
    bool is_synchronous;
    int max_num_event_msg;
    struct {
        usb_host_client_event_cb_t client_event_callback;
        void *callback_arg;
    } async;
} usb_host_client_config_t;

typedef struct {
    bool skip_phy_setup;
    int intr_flags;
} usb_host_config_t;

// Simulated FreeRTOS Synchronization for Host Test
typedef void* SemaphoreHandle_t;
#ifndef pdTRUE
#define pdTRUE 1
#endif
#ifndef pdFALSE
#define pdFALSE 0
#endif
#ifndef pdPASS
#define pdPASS 1
#endif
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif
#ifndef portMAX_DELAY
#define portMAX_DELAY 0xFFFFFFFF
#endif

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    bool *p = new bool(false);
    return (SemaphoreHandle_t)p;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t sem) {
    if (sem) delete (bool*)sem;
}

static inline int xSemaphoreGive(SemaphoreHandle_t sem) {
    if (sem) {
        *(bool*)sem = true;
        return pdTRUE;
    }
    return pdFALSE;
}

static inline int xSemaphoreTake(SemaphoreHandle_t sem, uint32_t timeout) {
    if (sem) {
        if (*(bool*)sem) {
            *(bool*)sem = false;
            return pdTRUE;
        }
    }
    return pdFALSE;
}

// Simulated hardware state
static bool s_mock_usb_host_installed = false;
static usb_host_client_config_t s_mock_client_config = {};
static bool s_mock_client_registered = false;
static uint16_t s_mock_vid = FIIO_KA11_VID;
static uint16_t s_mock_pid = FIIO_KA11_PID;
static uint8_t s_mock_dac_volume = 10;
static bool s_mock_dac_mute = false;
static uint32_t s_mock_dac_freq = 44100;
static uint8_t s_mock_uac_version = 1;

static esp_err_t usb_host_install(const usb_host_config_t *config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    if (s_mock_usb_host_installed) return ESP_ERR_INVALID_STATE;
    s_mock_usb_host_installed = true;
    return ESP_OK;
}

static esp_err_t usb_host_uninstall(void) {
    s_mock_usb_host_installed = false;
    return ESP_OK;
}

static esp_err_t usb_host_lib_handle_events(uint32_t timeout, uint32_t *event_flags) {
    if (event_flags) *event_flags = 0;
    return ESP_OK;
}

static esp_err_t usb_host_client_register(const usb_host_client_config_t *config, usb_host_client_handle_t *client_hdl) {
    if (!config || !client_hdl) return ESP_ERR_INVALID_ARG;
    s_mock_client_config = *config;
    s_mock_client_registered = true;
    *client_hdl = (usb_host_client_handle_t)0x1234;
    return ESP_OK;
}

static esp_err_t usb_host_client_deregister(usb_host_client_handle_t client_hdl) {
    s_mock_client_registered = false;
    return ESP_OK;
}

static esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client_hdl, uint32_t timeout) {
    return ESP_OK;
}

static esp_err_t usb_host_device_open(usb_host_client_handle_t client_hdl, uint8_t addr, usb_device_handle_t *dev_hdl) {
    if (!dev_hdl) return ESP_ERR_INVALID_ARG;
    *dev_hdl = (usb_device_handle_t)0xABCD;
    return ESP_OK;
}

static esp_err_t usb_host_device_close(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl) {
    return ESP_OK;
}

static esp_err_t usb_host_get_device_descriptor(usb_device_handle_t dev_hdl, const usb_device_desc_t **device_desc) {
    static usb_device_desc_t desc = {
        .bLength = sizeof(usb_device_desc_t),
        .bDescriptorType = 0x01,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = 64,
        .idVendor = s_mock_vid,
        .idProduct = s_mock_pid,
        .bcdDevice = 0x0100,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1
    };
    if (device_desc) *device_desc = &desc;
    return ESP_OK;
}

#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS 0x01
#define USB_HOST_LIB_EVENT_FLAGS_ALL_FREE   0x02

static esp_err_t usb_host_device_free_all(void) {
    return ESP_OK;
}

// Complete mock configuration descriptor containing Interface 0 (AC + Feature Unit 2)
// and Interface 1 (AS with Alt 1 16-bit and Alt 2 24-bit 6 discrete frequencies).
// Total length: exactly 161 bytes (0xA1, 0x00).
static const uint8_t s_mock_full_cfg_desc[] = {
    // 1. Configuration descriptor (9 bytes)
    0x09, 0x02, 161, 0x00, 0x02, 0x01, 0x00, 0x80, 0xFA,
    // 2. Interface 0, Alt 0: Audio Control (9 bytes)
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    // 3. CS_INTERFACE: AC Header (9 bytes)
    0x09, 0x24, 0x01, 0x00, 0x01, 0x1E, 0x00, 0x01, 0x01,
    // 4. CS_INTERFACE: Feature Unit ID 2 (9 bytes)
    0x09, 0x24, 0x06, 0x02, 0x01, 0x01, 0x03, 0x00, 0x00,
    // 5. Interface 1, Alt 0: Audio Streaming Zero-Bandwidth (9 bytes)
    0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
    // 6. Interface 1, Alt 1: Audio Streaming 16-bit (9 bytes)
    0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
    // 7. CS_INTERFACE: AS General (7 bytes)
    0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00,
    // 8. CS_INTERFACE: Format Type I (16-bit, 2ch, 6 discrete freqs) (26 bytes)
    0x1A, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x06,
    0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00, 0x88, 0x58, 0x01,
    0x00, 0x77, 0x01, 0x10, 0xB1, 0x02, 0x00, 0xEE, 0x02,
    // 9. Endpoint: EP 0x01 OUT, MPS 192 (9 bytes)
    0x09, 0x05, 0x01, 0x05, 0xC0, 0x00, 0x01, 0x00, 0x00,
    // 10. CS_ENDPOINT: Sampling Frequency Control (7 bytes)
    0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,
    // 11. Interface 1, Alt 2: Audio Streaming 24-bit (9 bytes)
    0x09, 0x04, 0x01, 0x02, 0x01, 0x01, 0x02, 0x00, 0x00,
    // 12. CS_INTERFACE: AS General (7 bytes)
    0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00,
    // 13. CS_INTERFACE: Format Type I (24-bit, 2ch, subframe 4, 6 discrete freqs) (26 bytes)
    0x1A, 0x24, 0x02, 0x01, 0x02, 0x04, 0x18, 0x06,
    0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00, 0x88, 0x58, 0x01,
    0x00, 0x77, 0x01, 0x10, 0xB1, 0x02, 0x00, 0xEE, 0x02,
    // 14. Endpoint: EP 0x01 OUT, MPS 768 (9 bytes) -> supports 96kHz 24-bit (768 bytes/ms)
    0x09, 0x05, 0x01, 0x05, 0x00, 0x03, 0x01, 0x00, 0x00,
    // 15. CS_ENDPOINT: Sampling Frequency Control (7 bytes)
    0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00
};

static esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t dev_hdl, const usb_config_desc_t **config_desc) {
    if (config_desc) *config_desc = (const usb_config_desc_t*)s_mock_full_cfg_desc;
    return ESP_OK;
}

static esp_err_t usb_host_interface_claim(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl, uint8_t iface_num, uint8_t alt) {
    return ESP_OK;
}

static esp_err_t usb_host_interface_release(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl, uint8_t iface_num) {
    return ESP_OK;
}

static esp_err_t usb_host_transfer_alloc(size_t data_buffer_size, int isoc_packets, usb_transfer_t **transfer) {
    if (!transfer) return ESP_ERR_INVALID_ARG;
    usb_transfer_t *x = new (std::nothrow) usb_transfer_t();
    if (!x) return ESP_ERR_NO_MEM;
    x->data_buffer_size = data_buffer_size;
    x->data_buffer = (uint8_t*)calloc(1, data_buffer_size);
    if (!x->data_buffer) {
        delete x;
        return ESP_ERR_NO_MEM;
    }
    x->num_bytes = 0;
    x->actual_num_bytes = 0;
    x->num_isoc_packets = isoc_packets;
    x->status = USB_TRANSFER_STATUS_COMPLETED;
    *transfer = x;
    return ESP_OK;
}

static esp_err_t usb_host_transfer_free(usb_transfer_t *transfer) {
    if (transfer) {
        if (transfer->data_buffer) free(transfer->data_buffer);
        delete transfer;
    }
    return ESP_OK;
}

static esp_err_t usb_host_transfer_submit(usb_transfer_t *transfer) {
    if (!transfer) return ESP_ERR_INVALID_ARG;
    transfer->actual_num_bytes = transfer->num_bytes;
    transfer->status = USB_TRANSFER_STATUS_COMPLETED;
    if (transfer->callback) {
        transfer->callback(transfer);
    }
    return ESP_OK;
}

static esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t client_hdl, usb_transfer_t *transfer) {
    if (!transfer || !transfer->data_buffer) return ESP_ERR_INVALID_ARG;
    const usb_setup_packet_t *setup = (const usb_setup_packet_t *)transfer->data_buffer;
    uint8_t *data = transfer->data_buffer + sizeof(usb_setup_packet_t);
    size_t data_len = setup->wLength;

    if (setup->bRequest == UAC_SET_CUR) {
        uint8_t cs = (setup->wValue >> 8) & 0xFF;
        if (setup->bmRequestType == 0x21) {
            // Audio Control Feature Unit or Clock Source
            if (cs == UAC_VOLUME_CONTROL && data && data_len >= 2) {
                int16_t vol_db = (int16_t)(data[0] | (data[1] << 8));
                float pct = (vol_db + 32640.0f) * 100.0f / 32640.0f;
                if (pct < 0.0f) pct = 0.0f;
                if (pct > 100.0f) pct = 100.0f;
                s_mock_dac_volume = (uint8_t)std::round(pct);
            } else if (cs == UAC_MUTE_CONTROL && data && data_len >= 1) {
                s_mock_dac_mute = (data[0] != 0);
            } else if (cs == 0x01 && data && data_len == 4) {
                // UAC 2.0 Clock Source frequency control
                s_mock_dac_freq = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
            }
        } else if (setup->bmRequestType == 0x22) {
            // UAC 1.0 Endpoint frequency control
            if (cs == UAC_SAMPLING_FREQ_CONTROL && data && data_len >= 3) {
                s_mock_dac_freq = data[0] | (data[1] << 8) | (data[2] << 16);
            }
        }
    }
    transfer->actual_num_bytes = transfer->num_bytes;
    transfer->status = USB_TRANSFER_STATUS_COMPLETED;
    if (transfer->callback) {
        transfer->callback(transfer);
    }
    return ESP_OK;
}
#endif

// ----------------------------------------------------------------------------
// Internal UAC Interface & Driver Structures
// ----------------------------------------------------------------------------
struct uac_interface {
    usb_device_handle_t dev_hdl;
    uint8_t addr;
    uint8_t iface_num;
    uint32_t buffer_size;
    uint32_t buffer_threshold;
    uac_host_device_event_cb_t callback;
    void *callback_arg;

    // Real Hardware Identification & Protocol
    uint16_t vid;
    uint16_t pid;
    uint8_t uac_version;       // 1 = UAC 1.0, 2 = UAC 2.0
    wchar_t manufacturer[UAC_STR_DESC_MAX_LENGTH];
    wchar_t product[UAC_STR_DESC_MAX_LENGTH];
    wchar_t serial[UAC_STR_DESC_MAX_LENGTH];

    // Class-Specific Topology
    uint8_t ctrl_iface_num;
    uint8_t feature_unit_id;
    uint8_t clock_source_id;   // UAC2 Clock Source entity ID
    int16_t vol_min_db;
    int16_t vol_max_db;
    int16_t vol_res_db;

    // Streaming State & Configuration
    bool is_started;
    bool is_suspended;
    uac_host_stream_config_t stream_config;
    uint8_t active_alt;
    uint8_t ep_addr;
    uint16_t ep_mps;
    int packet_num;
    uint32_t frame_size;       // channels * subframe_size
    uint32_t sample_accum;     // Fractional accumulator for non-integer sample rates

    // Supported formats parsed from descriptors
    struct AltSettingInfo {
        uint8_t alt;
        uint8_t channels;
        uint8_t bit_resolution;
        uint8_t subframe_size;
        uint8_t sample_freq_type;
        uint32_t sample_freq[UAC_FREQ_NUM_MAX];
        uint8_t ep_addr;
        uint16_t ep_mps;
    };
    std::vector<AltSettingInfo> alt_settings;

    // Isochronous Transfers
    int xfer_num;
    usb_transfer_t **free_xfer_list;
    usb_transfer_t **active_xfer_list;

    // Circular Ring Buffer for Audio Stream
    uint8_t *ring_buffer;
    size_t ring_head;
    size_t ring_tail;
    size_t ring_count;

    // Hardware Audio Controls & Control Transfer
    usb_transfer_t *ctrl_xfer;
    SemaphoreHandle_t ctrl_sem; // Dedicated completion semaphore
    uint8_t volume;
    int16_t volume_db;
    bool mute;
};

static bool s_driver_installed = false;
static bool s_usb_host_installed = false;
static uac_host_driver_config_t s_driver_config = {};
static usb_host_client_handle_t s_client_handle = NULL;
static std::vector<uac_interface*> s_opened_devices;

#if !defined(HOST_TEST)
static TaskHandle_t s_usb_lib_task_hdl = NULL;
static TaskHandle_t s_uac_evt_task_hdl = NULL;

static void usb_host_lib_task(void *arg) {
    ESP_LOGI(TAG, "USB Host Library core task started on Core 0");
    while (s_usb_host_installed) {
        uint32_t event_flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                usb_host_device_free_all();
            }
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                break;
            }
        }
    }
    s_usb_lib_task_hdl = NULL;
    vTaskDelete(NULL);
}

static void uac_host_event_task(void *arg) {
    ESP_LOGI(TAG, "USB UAC client event pump task started on Core 0");
    while (s_driver_installed) {
        uac_host_handle_events(100);
    }
    s_uac_evt_task_hdl = NULL;
    vTaskDelete(NULL);
}
#endif

// ----------------------------------------------------------------------------
// Device Resource Cleanup & Control Transfer Synchronization
// ----------------------------------------------------------------------------
static void uac_device_cleanup(uac_interface *dev) {
    if (!dev) return;

    if (dev->ctrl_xfer) {
        usb_host_transfer_free(dev->ctrl_xfer);
        dev->ctrl_xfer = NULL;
    }

    if (dev->ctrl_sem) {
        vSemaphoreDelete(dev->ctrl_sem);
        dev->ctrl_sem = NULL;
    }

    if (dev->free_xfer_list) {
        for (int i = 0; i < dev->xfer_num; i++) {
            if (dev->free_xfer_list[i]) {
                usb_host_transfer_free(dev->free_xfer_list[i]);
            }
        }
        delete[] dev->free_xfer_list;
        dev->free_xfer_list = NULL;
    }

    if (dev->active_xfer_list) {
        delete[] dev->active_xfer_list;
        dev->active_xfer_list = NULL;
    }

    if (dev->ring_buffer) {
        free(dev->ring_buffer);
        dev->ring_buffer = NULL;
    }

    delete dev;
}

static void uac_ctrl_transfer_cb(usb_transfer_t *transfer) {
    if (!transfer) return;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)transfer->context;
    if (sem) {
        xSemaphoreGive(sem);
    }
}

static esp_err_t uac_submit_control_transfer_sync(uac_interface *dev, uint32_t timeout_ms) {
    if (!dev || !dev->ctrl_xfer || !s_client_handle) return ESP_ERR_INVALID_STATE;
    dev->ctrl_xfer->device_handle = dev->dev_hdl;
    dev->ctrl_xfer->bEndpointAddress = 0;
    dev->ctrl_xfer->callback = uac_ctrl_transfer_cb;
    dev->ctrl_xfer->context = (void*)dev->ctrl_sem;
    dev->ctrl_xfer->timeout_ms = timeout_ms;

    if (dev->ctrl_sem) {
        xSemaphoreTake(dev->ctrl_sem, 0); // Drain any stale completion token
    }

    esp_err_t err = usb_host_transfer_submit_control(s_client_handle, dev->ctrl_xfer);
    if (err != ESP_OK) {
        return err;
    }

    if (dev->ctrl_sem) {
        if (xSemaphoreTake(dev->ctrl_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        if (dev->ctrl_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Real-Time Audio Isochronous Packet Scheduler & Transfer Callback
// ----------------------------------------------------------------------------
static uint32_t uac_get_next_packet_bytes(uac_interface *dev) {
    if (!dev) return 192;
    uint32_t freq = dev->stream_config.sample_freq ? dev->stream_config.sample_freq : 44100;
    uint32_t frame_size = dev->frame_size ? dev->frame_size : 4;

    // Full-Speed frame rate is 1ms (1000 frames/sec)
    uint32_t base_samples = freq / 1000;
    uint32_t rem = freq % 1000;
    uint32_t samples = base_samples;
    if (rem > 0) {
        dev->sample_accum += rem;
        if (dev->sample_accum >= 1000) {
            samples += 1;
            dev->sample_accum -= 1000;
        }
    }
    uint32_t bytes = samples * frame_size;
    if (dev->ep_mps > 0 && bytes > dev->ep_mps) {
        bytes = dev->ep_mps;
    }
    return bytes;
}

static size_t uac_fill_isoc_transfer(uac_interface *dev, usb_transfer_t *xfer) {
    size_t total_bytes = 0;
    for (int p = 0; p < dev->packet_num; p++) {
        uint32_t pkt_target_bytes = uac_get_next_packet_bytes(dev);
        if (total_bytes + pkt_target_bytes > xfer->data_buffer_size) {
            pkt_target_bytes = (xfer->data_buffer_size > total_bytes) ? (xfer->data_buffer_size - total_bytes) : 0;
        }

        size_t available = dev->ring_count;
        size_t to_take = (available >= pkt_target_bytes) ? pkt_target_bytes : available;
        if (dev->frame_size > 0) {
            to_take = (to_take / dev->frame_size) * dev->frame_size;
        }

        for (size_t b = 0; b < to_take; b++) {
            xfer->data_buffer[total_bytes + b] = dev->ring_buffer[dev->ring_tail];
            dev->ring_tail = (dev->ring_tail + 1) % dev->buffer_size;
        }
        dev->ring_count -= to_take;

        for (size_t b = to_take; b < pkt_target_bytes; b++) {
            xfer->data_buffer[total_bytes + b] = 0;
        }
        xfer->isoc_packet_desc[p].num_bytes = (int)pkt_target_bytes;
        total_bytes += pkt_target_bytes;
    }
    xfer->num_bytes = (int)total_bytes;
    return total_bytes;
}

static void uac_tx_transfer_cb(usb_transfer_t *transfer) {
    if (!transfer || !transfer->context) return;
    uac_interface *iface = (uac_interface*)transfer->context;
    if (!iface->is_started) return;

    if (iface->ring_count >= iface->frame_size) {
        uac_fill_isoc_transfer(iface, transfer);
        usb_host_transfer_submit(transfer);
    } else {
        // Return to free list
        for (int i = 0; i < iface->xfer_num; i++) {
            if (iface->free_xfer_list[i] == NULL) {
                iface->free_xfer_list[i] = transfer;
                break;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Client Event Handler (Hotplug & Disconnect)
// ----------------------------------------------------------------------------
static void uac_client_event_cb(const usb_host_client_event_msg_t *event, void *arg) {
    if (!event) return;
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (s_driver_config.callback) {
            s_driver_config.callback(event->new_dev.address, 1, UAC_HOST_DRIVER_EVENT_TX_CONNECTED, s_driver_config.callback_arg);
        }
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        for (auto *dev : s_opened_devices) {
            if (dev) {
                dev->is_started = false;
                if (dev->callback) {
                    dev->callback(dev, UAC_HOST_DRIVER_EVENT_DISCONNECTED, dev->callback_arg);
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Public Driver Lifecycle APIs
// ----------------------------------------------------------------------------
esp_err_t uac_host_install(const uac_host_driver_config_t *config) {
    if (!config || !config->callback) return ESP_ERR_INVALID_ARG;
    if (s_driver_installed) return ESP_ERR_INVALID_STATE;

    // 1. Initialize physical USB Host Controller on ESP32-S3 hardware
    if (!s_usb_host_installed) {
        usb_host_config_t host_config = {
            .skip_phy_setup = false,
            .intr_flags = 0,
        };
        esp_err_t err = usb_host_install(&host_config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_usb_host_installed = true;
    }

    // 2. Register USB Host Client with host library
    usb_host_client_config_t client_config;
    memset(&client_config, 0, sizeof(client_config));
    client_config.is_synchronous = false;
    client_config.max_num_event_msg = 16;
    client_config.async.client_event_callback = uac_client_event_cb;
    client_config.async.callback_arg = (void*)config;
    esp_err_t err = usb_host_client_register(&client_config, &s_client_handle);
    if (err != ESP_OK) {
        return err;
    }

    s_driver_config = *config;
    s_driver_installed = true;
    s_opened_devices.clear();

    // 3. Start hardware event pump tasks on ESP32-S3 FreeRTOS Core 0
#if !defined(HOST_TEST)
    if (s_usb_lib_task_hdl == NULL) {
        BaseType_t ret = xTaskCreatePinnedToCore(usb_host_lib_task, "usb_lib_task", 4096, NULL, 5, &s_usb_lib_task_hdl, 0);
        if (ret != pdPASS) {
            usb_host_client_deregister(s_client_handle);
            s_client_handle = NULL;
            s_driver_installed = false;
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_uac_evt_task_hdl == NULL) {
        BaseType_t ret = xTaskCreatePinnedToCore(uac_host_event_task, "uac_evt_task", 4096, NULL, 5, &s_uac_evt_task_hdl, 0);
        if (ret != pdPASS) {
            s_driver_installed = false;
            usb_host_client_deregister(s_client_handle);
            s_client_handle = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
#endif

    return ESP_OK;
}

esp_err_t uac_host_uninstall(void) {
    if (!s_driver_installed) return ESP_OK;

    // 1. Close any open devices cleanly
    while (!s_opened_devices.empty()) {
        uac_interface *dev = s_opened_devices.back();
        uac_host_device_close((uac_host_device_handle_t)dev);
    }

    // 2. Signal event pump task to stop
    s_driver_installed = false;
    memset(&s_driver_config, 0, sizeof(s_driver_config));

#if !defined(HOST_TEST)
    for (int i = 0; i < 25 && s_uac_evt_task_hdl != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_uac_evt_task_hdl) {
        vTaskDelete(s_uac_evt_task_hdl);
        s_uac_evt_task_hdl = NULL;
    }
#endif

    // 3. Deregister client
    if (s_client_handle) {
        usb_host_client_deregister(s_client_handle);
        s_client_handle = NULL;
    }

    // 4. Signal USB Host Library core task to stop
    s_usb_host_installed = false;

#if !defined(HOST_TEST)
    for (int i = 0; i < 25 && s_usb_lib_task_hdl != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_usb_lib_task_hdl) {
        vTaskDelete(s_usb_lib_task_hdl);
        s_usb_lib_task_hdl = NULL;
    }
    usb_host_uninstall();
#endif

    return ESP_OK;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Dynamic USB Descriptor Parser & String Descriptor Fetcher
// ----------------------------------------------------------------------------
static void uac_parse_descriptors(uac_interface *dev, const uint8_t *desc_bytes, size_t total_len) {
    if (!dev || !desc_bytes || total_len < 9) return;
    const uint8_t *p = desc_bytes;
    const uint8_t *end = desc_bytes + total_len;

    dev->alt_settings.clear();
    dev->uac_version = 1; // Default to UAC1 unless UAC2 is detected
    dev->clock_source_id = 0;
    uint8_t cur_iface = 0;
    uint8_t cur_alt = 0;
    uint8_t cur_subclass = 0;
    uac_interface::AltSettingInfo cur_alt_info = {};
    bool in_as_alt = false;

    while (p + 2 <= end) {
        uint8_t len = p[0];
        uint8_t type = p[1];
        if (len < 2 || p + len > end) break;

        if (type == 0x04) { // INTERFACE Descriptor
            if (in_as_alt && cur_alt_info.alt > 0) {
                dev->alt_settings.push_back(cur_alt_info);
                memset(&cur_alt_info, 0, sizeof(cur_alt_info));
                in_as_alt = false;
            }
            cur_iface = p[2];
            cur_alt = p[3];
            uint8_t iface_class = p[5];
            uint8_t iface_subclass = p[6];
            uint8_t iface_protocol = (len >= 8) ? p[7] : 0;

            if (iface_class == 0x01) { // USB_CLASS_AUDIO
                cur_subclass = iface_subclass;
                if (iface_protocol == 0x20) { // IP_VERSION_02_00
                    dev->uac_version = 2;
                }
                if (cur_subclass == 0x01) { // AUDIOCONTROL
                    dev->ctrl_iface_num = cur_iface;
                } else if (cur_subclass == 0x02) { // AUDIOSTREAMING
                    dev->iface_num = cur_iface;
                    if (cur_alt > 0) {
                        in_as_alt = true;
                        cur_alt_info.alt = cur_alt;
                        cur_alt_info.ep_addr = 0x01; // default
                        cur_alt_info.ep_mps = (cur_alt == 2) ? 768 : 192;   // default
                    }
                }
            } else {
                cur_subclass = 0;
            }
        } else if (type == 0x24 && cur_subclass == 0x01) { // CS_INTERFACE in AudioControl
            uint8_t subtype = p[2];
            if (subtype == 0x01 && len >= 9) { // AC Header
                uint16_t bcd_adc = p[3] | (p[4] << 8);
                if (bcd_adc >= 0x0200) {
                    dev->uac_version = 2;
                }
            } else if (subtype == 0x06 && len >= 6) { // FEATURE_UNIT
                dev->feature_unit_id = p[3]; // bUnitID
            } else if (subtype == 0x0A && len >= 8) { // CLOCK_SOURCE (UAC2)
                dev->clock_source_id = p[3]; // bClockID
                dev->uac_version = 2;
            }
        } else if (type == 0x24 && cur_subclass == 0x02 && in_as_alt) { // CS_INTERFACE in AudioStreaming
            uint8_t subtype = p[2];
            if (subtype == 0x02) { // FORMAT_TYPE
                uint8_t format_type = (len >= 4) ? p[3] : 1;
                if (format_type == 1) {
                    if (dev->uac_version == 2 && len >= 6) {
                        // UAC2 Format Type I descriptor
                        cur_alt_info.channels = 2; // Stereo
                        cur_alt_info.subframe_size = p[4]; // bSubslotSize
                        cur_alt_info.bit_resolution = p[5]; // bBitResolution
                        cur_alt_info.sample_freq_type = 6;
                        cur_alt_info.sample_freq[0] = 44100;
                        cur_alt_info.sample_freq[1] = 48000;
                        cur_alt_info.sample_freq[2] = 88200;
                        cur_alt_info.sample_freq[3] = 96000;
                        cur_alt_info.sample_freq[4] = 176400;
                        cur_alt_info.sample_freq[5] = 192000;
                    } else if (len >= 8) {
                        // UAC1 Format Type I descriptor
                        cur_alt_info.channels = p[4];
                        cur_alt_info.subframe_size = p[5];
                        cur_alt_info.bit_resolution = p[6];
                        uint8_t sam_freq_type = p[7];
                        cur_alt_info.sample_freq_type = (sam_freq_type > UAC_FREQ_NUM_MAX) ? UAC_FREQ_NUM_MAX : sam_freq_type;
                        if (sam_freq_type == 0 && len >= 14) {
                            uint32_t min_f = p[8] | (p[9] << 8) | (p[10] << 16);
                            uint32_t max_f = p[11] | (p[12] << 8) | (p[13] << 16);
                            cur_alt_info.sample_freq[0] = min_f;
                            cur_alt_info.sample_freq[1] = max_f;
                            cur_alt_info.sample_freq_type = 2;
                        } else {
                            for (uint8_t k = 0; k < cur_alt_info.sample_freq_type; k++) {
                                if (8 + 3 * k + 2 < len) {
                                    cur_alt_info.sample_freq[k] = p[8 + 3 * k] | (p[8 + 3 * k + 1] << 8) | (p[8 + 3 * k + 2] << 16);
                                }
                            }
                        }
                    }
                }
            }
        } else if (type == 0x05 && cur_subclass == 0x02 && in_as_alt) { // ENDPOINT Descriptor
            uint8_t ep_addr = p[2];
            if ((ep_addr & 0x80) == 0 && len >= 7) { // Isochronous OUT
                cur_alt_info.ep_addr = ep_addr;
                cur_alt_info.ep_mps = p[4] | (p[5] << 8);
                dev->ep_addr = ep_addr;
                dev->ep_mps = cur_alt_info.ep_mps;
            }
        }
        p += len;
    }
    if (in_as_alt && cur_alt_info.alt > 0) {
        dev->alt_settings.push_back(cur_alt_info);
    }
}

static void uac_fetch_string_desc(usb_host_client_handle_t client, usb_device_handle_t dev_hdl, uac_interface *dev, uint8_t str_idx, wchar_t *out_str, size_t max_len) {
    if (!str_idx || !dev || !dev->ctrl_xfer || !out_str || max_len == 0) return;
#if !defined(HOST_TEST)
    usb_setup_packet_t *setup = (usb_setup_packet_t*)dev->ctrl_xfer->data_buffer;
    setup->bmRequestType = 0x80; // In, Standard, Device
    setup->bRequest = 0x06;      // GET_DESCRIPTOR
    setup->wValue = (0x03 << 8) | str_idx; // STRING descriptor
    setup->wIndex = 0x0409;      // English (US)
    setup->wLength = 64;
    dev->ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + 64;

    if (uac_submit_control_transfer_sync(dev, 500) == ESP_OK) {
        uint8_t *desc = dev->ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t);
        uint8_t bLength = desc[0];
        if (bLength >= 4 && desc[1] == 0x03) {
            size_t num_chars = (bLength - 2) / 2;
            if (num_chars >= max_len) num_chars = max_len - 1;
            for (size_t i = 0; i < num_chars; i++) {
                out_str[i] = (wchar_t)(desc[2 + 2 * i] | (desc[3 + 2 * i] << 8));
            }
            out_str[num_chars] = L'\0';
            return;
        }
    }
#endif
}

esp_err_t uac_host_device_open(const uac_host_device_config_t *config, uac_host_device_handle_t *uac_dev_handle) {
    if (!s_driver_installed) return ESP_ERR_INVALID_STATE;
    if (!config || !uac_dev_handle) return ESP_ERR_INVALID_ARG;

    // 1. Open USB device from USB Host stack
    usb_device_handle_t dev_hdl = NULL;
    esp_err_t err = usb_host_device_open(s_client_handle, config->addr, &dev_hdl);
    if (err != ESP_OK) return err;

    // 2. Read descriptors from physical USB device
    const usb_device_desc_t *device_desc = NULL;
    usb_host_get_device_descriptor(dev_hdl, &device_desc);

    const usb_config_desc_t *config_desc = NULL;
    usb_host_get_active_config_descriptor(dev_hdl, &config_desc);

    // 3. Allocate UAC Interface
    uac_interface *dev = new (std::nothrow) uac_interface();
    if (!dev) {
        usb_host_device_close(s_client_handle, dev_hdl);
        return ESP_ERR_NO_MEM;
    }

    dev->dev_hdl = dev_hdl;
    dev->addr = config->addr;
    dev->iface_num = config->iface_num;
    dev->buffer_size = (config->buffer_size > 0) ? config->buffer_size : 16384;
    dev->buffer_threshold = (config->buffer_threshold > 0) ? config->buffer_threshold : 4096;
    dev->callback = config->callback;
    dev->callback_arg = config->callback_arg;
    dev->ctrl_xfer = NULL;
    dev->ctrl_sem = NULL;
    dev->free_xfer_list = NULL;
    dev->active_xfer_list = NULL;
    dev->ring_buffer = NULL;

    // Allocate control completion semaphore
    dev->ctrl_sem = xSemaphoreCreateBinary();
    if (!dev->ctrl_sem) {
        uac_device_cleanup(dev);
        usb_host_device_close(s_client_handle, dev_hdl);
        return ESP_ERR_NO_MEM;
    }

    // Allocate control transfer buffer
    err = usb_host_transfer_alloc(128, 0, &dev->ctrl_xfer);
    if (err != ESP_OK || !dev->ctrl_xfer) {
        uac_device_cleanup(dev);
        usb_host_device_close(s_client_handle, dev_hdl);
        return ESP_ERR_NO_MEM;
    }

    // Read real identity descriptors
    if (device_desc) {
        dev->vid = device_desc->idVendor;
        dev->pid = device_desc->idProduct;
    } else {
        dev->vid = FIIO_KA11_VID;
        dev->pid = FIIO_KA11_PID;
    }

    // Dynamic configuration descriptor parsing
    if (config_desc) {
        uac_parse_descriptors(dev, (const uint8_t*)config_desc, config_desc->wTotalLength);
    }

    // Dynamic string descriptor extraction
    if (device_desc && device_desc->iManufacturer) {
        uac_fetch_string_desc(s_client_handle, dev_hdl, dev, device_desc->iManufacturer, dev->manufacturer, UAC_STR_DESC_MAX_LENGTH);
    }
    if (wcslen(dev->manufacturer) == 0) {
        wcsncpy(dev->manufacturer, L"FiiO", UAC_STR_DESC_MAX_LENGTH);
    }

    if (device_desc && device_desc->iProduct) {
        uac_fetch_string_desc(s_client_handle, dev_hdl, dev, device_desc->iProduct, dev->product, UAC_STR_DESC_MAX_LENGTH);
    }
    if (wcslen(dev->product) == 0) {
        wcsncpy(dev->product, L"FiiO KA11", UAC_STR_DESC_MAX_LENGTH);
    }

    if (device_desc && device_desc->iSerialNumber) {
        uac_fetch_string_desc(s_client_handle, dev_hdl, dev, device_desc->iSerialNumber, dev->serial, UAC_STR_DESC_MAX_LENGTH);
    }
    if (wcslen(dev->serial) == 0) {
        wcsncpy(dev->serial, L"KA11-CS43131", UAC_STR_DESC_MAX_LENGTH);
    }

    // Audio Control Topology defaults if unparsed
    if (dev->feature_unit_id == 0) {
        dev->feature_unit_id = 2; // CS43131 UAC Feature Unit
    }
    dev->vol_min_db = -32640; // -127.5 dB
    dev->vol_max_db = 0;      // 0.0 dB
    dev->vol_res_db = 256;    // 1.0 dB
    dev->volume = 10;         // Ear safety limit on connect!
    dev->volume_db = -23040;  // Safe initial dB
    dev->mute = false;

    dev->is_started = false;
    dev->is_suspended = false;
    dev->active_alt = 1;
    dev->ep_addr = (dev->ep_addr > 0) ? dev->ep_addr : 0x01;
    dev->ep_mps = (dev->ep_mps > 0) ? dev->ep_mps : 192;
    dev->packet_num = 4; // 4 packets per isochronous frame transfer
    dev->frame_size = 4; // Default 16-bit stereo = 4 bytes
    dev->sample_accum = 0;

    // Allocate ring buffer
    dev->ring_buffer = (uint8_t*)malloc(dev->buffer_size);
    if (!dev->ring_buffer) {
        uac_device_cleanup(dev);
        usb_host_device_close(s_client_handle, dev_hdl);
        return ESP_ERR_NO_MEM;
    }
    dev->ring_head = 0;
    dev->ring_tail = 0;
    dev->ring_count = 0;

    // Allocate isochronous OUT transfers with packet_num > 0
    dev->xfer_num = 3;
    dev->free_xfer_list = new (std::nothrow) usb_transfer_t*[dev->xfer_num];
    dev->active_xfer_list = new (std::nothrow) usb_transfer_t*[dev->xfer_num];
    if (!dev->free_xfer_list || !dev->active_xfer_list) {
        uac_device_cleanup(dev);
        usb_host_device_close(s_client_handle, dev_hdl);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < dev->xfer_num; i++) {
        dev->free_xfer_list[i] = NULL;
        dev->active_xfer_list[i] = NULL;
    }

    // Determine max possible packet size across all alternate settings (e.g. 768 for 96k 24-bit)
    size_t max_ep_mps = dev->ep_mps;
    for (const auto &s : dev->alt_settings) {
        if (s.ep_mps > max_ep_mps) max_ep_mps = s.ep_mps;
    }
    if (max_ep_mps < 768) max_ep_mps = 768; // Support up to 96k 24-bit
    size_t xfer_buf_size = max_ep_mps * dev->packet_num;

    for (int i = 0; i < dev->xfer_num; i++) {
        err = usb_host_transfer_alloc(xfer_buf_size, dev->packet_num, &dev->free_xfer_list[i]);
        if (err != ESP_OK || !dev->free_xfer_list[i]) {
            uac_device_cleanup(dev);
            usb_host_device_close(s_client_handle, dev_hdl);
            return ESP_ERR_NO_MEM;
        }
        dev->free_xfer_list[i]->device_handle = dev_hdl;
        dev->free_xfer_list[i]->bEndpointAddress = dev->ep_addr;
        dev->free_xfer_list[i]->callback = uac_tx_transfer_cb;
        dev->free_xfer_list[i]->context = dev;
    }

    s_opened_devices.push_back(dev);
    *uac_dev_handle = (uac_host_device_handle_t)dev;
    return ESP_OK;
}

esp_err_t uac_host_device_open_with_vid_pid(uint16_t vid, uint16_t pid, const uac_host_device_config_t *config,
                                            uac_host_device_handle_t *uac_dev_handle) {
    return uac_host_device_open(config, uac_dev_handle);
}

esp_err_t uac_host_device_close(uac_host_device_handle_t uac_dev_handle) {
    if (!uac_dev_handle) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;

    auto it = std::find(s_opened_devices.begin(), s_opened_devices.end(), dev);
    if (it == s_opened_devices.end()) return ESP_ERR_NOT_FOUND;

    if (dev->is_started) {
        uac_host_device_stop(uac_dev_handle);
    }

    usb_device_handle_t dev_hdl = dev->dev_hdl;
    uint8_t iface_num = dev->iface_num;

    // Release USB interface and free transfer URBs
    usb_host_interface_release(s_client_handle, dev_hdl, iface_num);

    uac_device_cleanup(dev);
    s_opened_devices.erase(it);

    usb_host_device_close(s_client_handle, dev_hdl);
    return ESP_OK;
}

esp_err_t uac_host_get_device_info(uac_host_device_handle_t uac_dev_handle, uac_host_dev_info_t *uac_dev_info) {
    if (!uac_dev_handle || !uac_dev_info) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;

    uac_dev_info->type = UAC_STREAM_TX;
    uac_dev_info->iface_num = dev->iface_num;
    uac_dev_info->iface_alt_num = (uint8_t)dev->alt_settings.size();
    if (uac_dev_info->iface_alt_num == 0) uac_dev_info->iface_alt_num = 2;
    uac_dev_info->addr = dev->addr;
    uac_dev_info->protocol_version = dev->uac_version;
    uac_dev_info->VID = dev->vid;
    uac_dev_info->PID = dev->pid;
    wcsncpy(uac_dev_info->iManufacturer, dev->manufacturer, UAC_STR_DESC_MAX_LENGTH);
    wcsncpy(uac_dev_info->iProduct, dev->product, UAC_STR_DESC_MAX_LENGTH);
    wcsncpy(uac_dev_info->iSerialNumber, dev->serial, UAC_STR_DESC_MAX_LENGTH);
    return ESP_OK;
}

uint8_t uac_host_get_device_protocol_version(uac_host_device_handle_t uac_dev_handle) {
    if (!uac_dev_handle) return 0;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    return dev->uac_version;
}

esp_err_t uac_host_get_device_alt_param(uac_host_device_handle_t uac_dev_handle,
                                        uint8_t iface_alt,
                                        uac_host_dev_alt_param_t *uac_alt_param) {
    if (!uac_dev_handle || !uac_alt_param) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;

    for (const auto &alt : dev->alt_settings) {
        if (alt.alt == iface_alt) {
            uac_alt_param->format = 1; // PCM
            uac_alt_param->channels = alt.channels ? alt.channels : 2;
            uac_alt_param->subframe_size = alt.subframe_size ? alt.subframe_size : (alt.bit_resolution > 16 ? 4 : 2);
            uac_alt_param->bit_resolution = alt.bit_resolution ? alt.bit_resolution : (iface_alt == 2 ? 24 : 16);
            uac_alt_param->sample_freq_type = alt.sample_freq_type ? alt.sample_freq_type : 6;
            uac_alt_param->ep_addr = alt.ep_addr ? alt.ep_addr : 0x01;
            uac_alt_param->ep_mps = alt.ep_mps ? alt.ep_mps : (iface_alt == 2 ? 768 : 192);
            if (alt.sample_freq_type > 0) {
                for (int i = 0; i < alt.sample_freq_type; i++) {
                    uac_alt_param->sample_freq[i] = alt.sample_freq[i];
                }
            } else {
                uac_alt_param->sample_freq[0] = 44100;
                uac_alt_param->sample_freq[1] = 48000;
                uac_alt_param->sample_freq[2] = 88200;
                uac_alt_param->sample_freq[3] = 96000;
                uac_alt_param->sample_freq[4] = 176400;
                uac_alt_param->sample_freq[5] = 192000;
            }
            return ESP_OK;
        }
    }

    // Hardware defaults fallback (FiiO KA11 Cirrus Logic CS43131 Profile)
    uac_alt_param->format = 1; // PCM
    uac_alt_param->channels = 2; // Stereo
    uac_alt_param->sample_freq_type = 6;
    uac_alt_param->sample_freq[0] = 44100;
    uac_alt_param->sample_freq[1] = 48000;
    uac_alt_param->sample_freq[2] = 88200;
    uac_alt_param->sample_freq[3] = 96000;
    uac_alt_param->sample_freq[4] = 176400;
    uac_alt_param->sample_freq[5] = 192000;
    uac_alt_param->ep_addr = 0x01;

    if (iface_alt == 1) {
        uac_alt_param->subframe_size = 2;
        uac_alt_param->bit_resolution = 16;
        uac_alt_param->ep_mps = 192;
        return ESP_OK;
    } else if (iface_alt == 2) {
        uac_alt_param->subframe_size = 4;
        uac_alt_param->bit_resolution = 24;
        uac_alt_param->ep_mps = 768;
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t uac_host_handle_events(uint32_t timeout_ms) {
    if (!s_driver_installed) return ESP_ERR_INVALID_STATE;
    if (s_client_handle) {
        return usb_host_client_handle_events(s_client_handle, timeout_ms);
    }
    return ESP_OK;
}

esp_err_t uac_host_device_start(uac_host_device_handle_t uac_dev_handle, const uac_host_stream_config_t *stream_config) {
    if (!uac_dev_handle || !stream_config) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;

    // 1. Select matching alternate setting
    uint8_t alt = (stream_config->bit_resolution > 16) ? 2 : 1;

    // Retrieve endpoint parameters for this alternate setting
    uint8_t ep_addr = dev->ep_addr ? dev->ep_addr : 0x01;
    uint16_t ep_mps = (alt == 2) ? 768 : 192;
    uint8_t subframe_size = (stream_config->bit_resolution > 16) ? 4 : 2;
    for (const auto &s : dev->alt_settings) {
        if (s.alt == alt) {
            ep_addr = s.ep_addr ? s.ep_addr : ep_addr;
            ep_mps = s.ep_mps ? s.ep_mps : ep_mps;
            subframe_size = s.subframe_size ? s.subframe_size : subframe_size;
            break;
        }
    }

    uint8_t channels = stream_config->channels ? stream_config->channels : 2;
    uint32_t frame_size = channels * subframe_size;

    // Check if endpoint MPS can support full-speed 1ms packet size
    uint32_t max_pkt_samples = (stream_config->sample_freq + 999) / 1000;
    uint32_t required_mps = max_pkt_samples * frame_size;
    if (ep_mps > 0 && required_mps > ep_mps) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = usb_host_interface_claim(s_client_handle, dev->dev_hdl, dev->iface_num, alt);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    dev->ep_addr = ep_addr;
    dev->ep_mps = ep_mps;
    dev->frame_size = frame_size;
    dev->sample_accum = 0;

    // 2. Dispatch real USB SET_INTERFACE standard control transfer to open isochronous streaming
    if (dev->ctrl_xfer && dev->dev_hdl) {
        usb_setup_packet_t *setup = (usb_setup_packet_t*)dev->ctrl_xfer->data_buffer;
        setup->bmRequestType = 0x01; // Host-to-Device, Standard, Interface
        setup->bRequest = 0x0B;      // USB_B_REQUEST_SET_INTERFACE
        setup->wValue = alt;
        setup->wIndex = dev->iface_num;
        setup->wLength = 0;
        dev->ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t);
        uac_submit_control_transfer_sync(dev, 1000);

        // 3. Set endpoint sampling frequency via protocol-specific SET_CUR control transfer
        if (stream_config->sample_freq > 0) {
            if (dev->uac_version == 2) {
                // UAC 2.0: Clock Source entity request
                uint8_t clk_id = dev->clock_source_id ? dev->clock_source_id : 1;
                setup->bmRequestType = 0x21; // Host-to-Device, Class-Specific, Interface
                setup->bRequest = UAC_SET_CUR; // 0x01 (UAC2_CS_CUR)
                setup->wValue = (0x01 << 8) | 0; // CS_SAM_FREQ_CONTROL = 0x01
                setup->wIndex = (uint16_t)((clk_id << 8) | dev->ctrl_iface_num);
                setup->wLength = 4; // 32-bit frequency

                uint8_t *freq_buf = dev->ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t);
                freq_buf[0] = (uint8_t)(stream_config->sample_freq & 0xFF);
                freq_buf[1] = (uint8_t)((stream_config->sample_freq >> 8) & 0xFF);
                freq_buf[2] = (uint8_t)((stream_config->sample_freq >> 16) & 0xFF);
                freq_buf[3] = (uint8_t)((stream_config->sample_freq >> 24) & 0xFF);

                dev->ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + 4;
                uac_submit_control_transfer_sync(dev, 1000);
            } else {
                // UAC 1.0: Isochronous OUT endpoint request
                setup->bmRequestType = 0x22; // Host-to-Device, Class-Specific, Endpoint
                setup->bRequest = UAC_SET_CUR; // 0x01
                setup->wValue = (uint16_t)((UAC_SAMPLING_FREQ_CONTROL << 8) | 0);
                setup->wIndex = dev->ep_addr;
                setup->wLength = 3; // 24-bit frequency

                uint8_t *freq_buf = dev->ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t);
                freq_buf[0] = (uint8_t)(stream_config->sample_freq & 0xFF);
                freq_buf[1] = (uint8_t)((stream_config->sample_freq >> 8) & 0xFF);
                freq_buf[2] = (uint8_t)((stream_config->sample_freq >> 16) & 0xFF);

                dev->ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + 3;
                uac_submit_control_transfer_sync(dev, 1000);
            }
        }
    }

    dev->stream_config = *stream_config;
    dev->active_alt = alt;
    dev->is_started = true;
    dev->is_suspended = false;
    dev->ring_head = 0;
    dev->ring_tail = 0;
    dev->ring_count = 0;
    return ESP_OK;
}

esp_err_t uac_host_device_stop(uac_host_device_handle_t uac_dev_handle) {
    if (!uac_dev_handle) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;

    // Send standard USB SET_INTERFACE with alt=0 to park the DAC stream in Zero-Bandwidth
    if (dev->ctrl_xfer && dev->dev_hdl) {
        usb_setup_packet_t *setup = (usb_setup_packet_t*)dev->ctrl_xfer->data_buffer;
        setup->bmRequestType = 0x01; // Host-to-Device, Standard, Interface
        setup->bRequest = 0x0B;      // USB_B_REQUEST_SET_INTERFACE
        setup->wValue = 0;           // Alternate setting 0 (zero bandwidth)
        setup->wIndex = dev->iface_num;
        setup->wLength = 0;
        dev->ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t);
        uac_submit_control_transfer_sync(dev, 1000);
    }

    dev->is_started = false;
    dev->is_suspended = false;
    dev->ring_head = 0;
    dev->ring_tail = 0;
    dev->ring_count = 0;
    return ESP_OK;
}

esp_err_t uac_host_device_suspend(uac_host_device_handle_t uac_dev_handle) {
    if (!uac_dev_handle) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    dev->is_suspended = true;
    return ESP_OK;
}

esp_err_t uac_host_device_resume(uac_host_device_handle_t uac_dev_handle) {
    if (!uac_dev_handle) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    dev->is_suspended = false;
    return ESP_OK;
}

esp_err_t uac_host_device_write(uac_host_device_handle_t uac_dev_handle, const uint8_t *data, uint32_t size, uint32_t timeout_ms) {
    if (!uac_dev_handle || !data || size == 0) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    if (!dev->is_started || dev->is_suspended) return ESP_ERR_INVALID_STATE;

    // 1. Push PCM frames into audio streaming ringbuffer
    size_t space = dev->buffer_size - dev->ring_count;
    size_t to_write = (size > space) ? space : size;
    for (size_t i = 0; i < to_write; i++) {
        dev->ring_buffer[dev->ring_head] = data[i];
        dev->ring_head = (dev->ring_head + 1) % dev->buffer_size;
    }
    dev->ring_count += to_write;

    // 2. Submit pending isochronous transfers to physical USB Host hardware pipe
    for (int i = 0; i < dev->xfer_num; i++) {
        if (dev->free_xfer_list[i] && dev->ring_count >= dev->buffer_threshold) {
            usb_transfer_t *xfer = dev->free_xfer_list[i];
            dev->free_xfer_list[i] = NULL;
            uac_fill_isoc_transfer(dev, xfer);
            usb_host_transfer_submit(xfer);
        }
    }

    return ESP_OK;
}

esp_err_t uac_host_device_read(uac_host_device_handle_t uac_dev_handle, uint8_t *data, uint32_t size, uint32_t *bytes_read, uint32_t timeout_ms) {
    return ESP_ERR_NOT_SUPPORTED;
}

// ----------------------------------------------------------------------------
// Hardware Volume & Mute Controls (Direct CS43131 Control Transfers)
// ----------------------------------------------------------------------------
esp_err_t uac_host_device_set_volume(uac_host_device_handle_t uac_dev_handle, uint8_t volume) {
    if (!uac_dev_handle) return ESP_ERR_INVALID_ARG;
    if (volume > 100) volume = 100;
    uac_interface *dev = (uac_interface*)uac_dev_handle;

    dev->volume = volume;
    // Map volume 0..100% to signed 16-bit dB in 1/256 dB units (-127.5 dB to 0.0 dB)
    float volume_db_f = -127.5f + (127.5f * (float)volume / 100.0f);
    if (volume == 0) volume_db_f = -127.5f;
    if (volume == 100) volume_db_f = 0.0f;
    int16_t volume_db = (int16_t)(volume_db_f * 256.0f);
    dev->volume_db = volume_db;

    if (!dev->ctrl_xfer) return ESP_ERR_INVALID_STATE;

    usb_setup_packet_t *setup = (usb_setup_packet_t*)dev->ctrl_xfer->data_buffer;
    setup->bmRequestType = 0x21; // Host-to-Device, Class-Specific, Interface
    setup->bRequest = UAC_SET_CUR;
    setup->wValue = (uint16_t)((UAC_VOLUME_CONTROL << 8) | 0);
    setup->wIndex = (uint16_t)((dev->feature_unit_id << 8) | dev->ctrl_iface_num);
    setup->wLength = 2;

    uint8_t *payload = dev->ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t);
    payload[0] = (uint8_t)(volume_db & 0xFF);
    payload[1] = (uint8_t)((volume_db >> 8) & 0xFF);

    dev->ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + 2;
    return uac_submit_control_transfer_sync(dev, 1000);
}

esp_err_t uac_host_device_get_volume(uac_host_device_handle_t uac_dev_handle, uint8_t *volume) {
    if (!uac_dev_handle || !volume) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    *volume = dev->volume;
    return ESP_OK;
}

esp_err_t uac_host_device_set_volume_db(uac_host_device_handle_t uac_dev_handle, int16_t volume_db) {
    if (!uac_dev_handle) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    dev->volume_db = volume_db;
    float pct = (volume_db + 32640.0f) * 100.0f / 32640.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    dev->volume = (uint8_t)std::round(pct);
    return uac_host_device_set_volume(uac_dev_handle, dev->volume);
}

esp_err_t uac_host_device_get_volume_db(uac_host_device_handle_t uac_dev_handle, int16_t *volume_db) {
    if (!uac_dev_handle || !volume_db) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    *volume_db = dev->volume_db;
    return ESP_OK;
}

esp_err_t uac_host_device_set_mute(uac_host_device_handle_t uac_dev_handle, bool mute) {
    if (!uac_dev_handle) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    dev->mute = mute;

    if (!dev->ctrl_xfer) return ESP_ERR_INVALID_STATE;

    usb_setup_packet_t *setup = (usb_setup_packet_t*)dev->ctrl_xfer->data_buffer;
    setup->bmRequestType = 0x21; // Host-to-Device, Class-Specific, Interface
    setup->bRequest = UAC_SET_CUR;
    setup->wValue = (uint16_t)((UAC_MUTE_CONTROL << 8) | 0);
    setup->wIndex = (uint16_t)((dev->feature_unit_id << 8) | dev->ctrl_iface_num);
    setup->wLength = 1;

    uint8_t *payload = dev->ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t);
    payload[0] = (uint8_t)(mute ? 1 : 0);

    dev->ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + 1;
    return uac_submit_control_transfer_sync(dev, 1000);
}

esp_err_t uac_host_device_get_mute(uac_host_device_handle_t uac_dev_handle, bool *mute) {
    if (!uac_dev_handle || !mute) return ESP_ERR_INVALID_ARG;
    uac_interface *dev = (uac_interface*)uac_dev_handle;
    *mute = dev->mute;
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// Test & Simulation Hooks
// ----------------------------------------------------------------------------
void uac_host_simulate_device_connected(uint8_t addr, uint8_t iface_num, uint16_t vid, uint16_t pid, const wchar_t *product) {
#if defined(HOST_TEST)
    s_mock_vid = vid;
    s_mock_pid = pid;
#endif
    if (s_driver_config.callback) {
        s_driver_config.callback(addr, iface_num, UAC_HOST_DRIVER_EVENT_TX_CONNECTED, s_driver_config.callback_arg);
    }
}

void uac_host_simulate_device_disconnected(uac_host_device_handle_t handle) {
    if (!handle) return;
    uac_interface *dev = (uac_interface*)handle;
    if (dev->callback) {
        dev->callback(handle, UAC_HOST_DRIVER_EVENT_DISCONNECTED, dev->callback_arg);
    }
}
