/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Espressif USB Audio Class (UAC) 1.0/2.0 Host Driver Interface
 * Reference: https://github.com/espressif/esp-usb/tree/master/host/class/uac
 */

#ifndef UAC_HOST_H
#define UAC_HOST_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

// Standard ESP error codes
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL -1
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE 0x104
#endif
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif
#ifndef ESP_ERR_TIMEOUT
#define ESP_ERR_TIMEOUT 0x107
#endif

typedef int esp_err_t;

#define UAC_STR_DESC_MAX_LENGTH              (32)
#define UAC_FREQ_NUM_MAX                     (16)
#define FLAG_STREAM_SUSPEND_AFTER_START      (1 << 0)

// FiiO KA11 Hardware Profile Constants (Cirrus Logic CS43131 High-Res DAC)
#define FIIO_KA11_VID                        (0x2972)
#define FIIO_KA11_PID                        (0x0053)

typedef struct uac_interface *uac_host_device_handle_t;

// ------------------------ USB UAC Host events --------------------------------
typedef enum {
    UAC_HOST_DRIVER_EVENT_RX_CONNECTED = 0x00,           /*!< UAC RX Device (e.g. microphone) found */
    UAC_HOST_DRIVER_EVENT_TX_CONNECTED,                  /*!< UAC TX Device (e.g. speaker/DAC) found */
} uac_host_driver_event_t;

typedef enum {
    UAC_HOST_DEVICE_EVENT_RX_DONE = 0x00,                /*!< RX buffer data size threshold exceeded */
    UAC_HOST_DEVICE_EVENT_TX_DONE,                       /*!< TX buffer data fell below threshold */
    UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR,                /*!< UAC transfer error */
    UAC_HOST_DRIVER_EVENT_DISCONNECTED,                  /*!< UAC Device has been disconnected / unplugged */
} uac_host_device_event_t;

typedef void (*uac_host_driver_event_cb_t)(uint8_t addr, uint8_t iface_num,
                                           const uac_host_driver_event_t event, void *arg);

typedef void (*uac_host_device_event_cb_t)(uac_host_device_handle_t uac_device_handle,
                                           const uac_host_device_event_t event, void *arg);

typedef enum {
    UAC_STREAM_TX = 0,     /*!< USB Audio TX (Host to DAC playback) */
    UAC_STREAM_RX,         /*!< USB Audio RX (Microphone capture to Host) */
    UAC_STREAM_MAX,
} uac_host_stream_t;

typedef struct {
    uac_host_stream_t type;                             /*!< Stream type */
    uint8_t iface_num;                                  /*!< UAC Interface Number */
    uint8_t iface_alt_num;                              /*!< UAC Interface Alternate Setting Total Number */
    uint8_t addr;                                       /*!< USB Address of connected UAC device */
    uint8_t protocol_version;                           /*!< UAC Protocol Version (1 = UAC 1.0, 2 = UAC 2.0) */
    uint16_t VID;                                       /*!< Vendor ID (0x2972 for FiiO) */
    uint16_t PID;                                       /*!< Product ID (0x0053 for KA11) */
    wchar_t iManufacturer[UAC_STR_DESC_MAX_LENGTH];     /*!< Manufacturer string (L"FiiO") */
    wchar_t iProduct[UAC_STR_DESC_MAX_LENGTH];          /*!< Product string (L"FiiO KA11") */
    wchar_t iSerialNumber[UAC_STR_DESC_MAX_LENGTH];     /*!< Serial Number string */
} uac_host_dev_info_t;

typedef struct {
    uint8_t format;                                     /*!< Audio stream format (1 = PCM) */
    uint8_t channels;                                   /*!< Channels (2 = Stereo) */
    uint8_t subframe_size;                              /*!< Bytes per subframe (2 for 16-bit, 4 for 24/32-bit) */
    uint8_t bit_resolution;                             /*!< Audio bit resolution (16, 24, 32) */
    uint8_t sample_freq_type;                           /*!< 0: continuous range, non-zero: discrete count */
    uint32_t sample_freq[UAC_FREQ_NUM_MAX];             /*!< Supported discrete sample frequencies */
    uint8_t ep_addr;                                    /*!< Isochronous endpoint address (e.g. 0x01) */
    uint16_t ep_mps;                                    /*!< Endpoint maximum packet size */
} uac_host_dev_alt_param_t;

typedef struct {
    bool create_background_task;
    size_t task_priority;
    size_t stack_size;
    int core_id;
    uac_host_driver_event_cb_t callback;
    void *callback_arg;
} uac_host_driver_config_t;

typedef struct {
    uint8_t addr;
    uint8_t iface_num;
    uint32_t buffer_size;
    uint32_t buffer_threshold;
    uac_host_device_event_cb_t callback;
    void *callback_arg;
} uac_host_device_config_t;

typedef struct {
    uint8_t channels;                                    /*!< 1: Mono, 2: Stereo */
    uint8_t bit_resolution;                              /*!< 16, 24, 32 bit */
    uint32_t sample_freq;                                /*!< e.g. 44100, 48000, 96000 */
    uint16_t flags;
} uac_host_stream_config_t;

// ----------------------------- Public APIs -----------------------------------
esp_err_t uac_host_install(const uac_host_driver_config_t *config);
esp_err_t uac_host_uninstall(void);

esp_err_t uac_host_device_open(const uac_host_device_config_t *config, uac_host_device_handle_t *uac_dev_handle);
esp_err_t uac_host_device_open_with_vid_pid(uint16_t vid, uint16_t pid, const uac_host_device_config_t *config,
                                            uac_host_device_handle_t *uac_dev_handle);
esp_err_t uac_host_device_close(uac_host_device_handle_t uac_dev_handle);

esp_err_t uac_host_get_device_info(uac_host_device_handle_t uac_dev_handle, uac_host_dev_info_t *uac_dev_info);
uint8_t uac_host_get_device_protocol_version(uac_host_device_handle_t uac_dev_handle);
esp_err_t uac_host_get_device_alt_param(uac_host_device_handle_t uac_dev_handle,
                                        uint8_t iface_alt,
                                        uac_host_dev_alt_param_t *uac_alt_param);
esp_err_t uac_host_handle_events(uint32_t timeout_ms);

esp_err_t uac_host_device_start(uac_host_device_handle_t uac_dev_handle, const uac_host_stream_config_t *stream_config);
esp_err_t uac_host_device_stop(uac_host_device_handle_t uac_dev_handle);
esp_err_t uac_host_device_suspend(uac_host_device_handle_t uac_dev_handle);
esp_err_t uac_host_device_resume(uac_host_device_handle_t uac_dev_handle);

esp_err_t uac_host_device_write(uac_host_device_handle_t uac_dev_handle, const uint8_t *data, uint32_t size, uint32_t timeout_ms);
esp_err_t uac_host_device_read(uac_host_device_handle_t uac_dev_handle, uint8_t *data, uint32_t size, uint32_t *bytes_read, uint32_t timeout_ms);

esp_err_t uac_host_device_set_mute(uac_host_device_handle_t uac_dev_handle, bool mute);
esp_err_t uac_host_device_get_mute(uac_host_device_handle_t uac_dev_handle, bool *mute);
esp_err_t uac_host_device_set_volume(uac_host_device_handle_t uac_dev_handle, uint8_t volume);
esp_err_t uac_host_device_get_volume(uac_host_device_handle_t uac_dev_handle, uint8_t *volume);
esp_err_t uac_host_device_set_volume_db(uac_host_device_handle_t uac_dev_handle, int16_t volume_db);
esp_err_t uac_host_device_get_volume_db(uac_host_device_handle_t uac_dev_handle, int16_t *volume_db);

// ------------------------ Test & Simulation Hooks ----------------------------
void uac_host_simulate_device_connected(uint8_t addr, uint8_t iface_num, uint16_t vid, uint16_t pid, const wchar_t *product);
void uac_host_simulate_device_disconnected(uac_host_device_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // UAC_HOST_H
