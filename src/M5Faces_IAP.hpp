/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP.hpp — 公共 IAP API
 * Public API for M5Faces Bottom3 in-application programming.
 *
 * 每个设备拥有独立的 IAP 实现（不同固件工程师）。
 * 固件版本列表由组件目录中的 *_app_without_flash.h 头文件自动生成。
 *   - M5Faces_Calculator3  (MODEL_ID 0x01)
 *       V03: base 0x08002800, 2048-byte chunks, 300 ms delay
 *   - M5Faces_Keyboard3      (MODEL_ID 0x02)
 *       V03: base 0x08001000, 1024-byte chunks, 200 ms delay
 *   - M5Faces_Gamepad3     (MODEL_ID 0x03)
 *       V04: base 0x08001000, 1024-byte chunks, 200 ms delay
 */
#pragma once

#include "M5FacesI2C.hpp"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef ARDUINO
/** Arduino/PIO 使用 M5Unified 内部 I2C 指针 / Arduino/PIO uses an M5Unified internal-I2C pointer. */
#if M5FACES_HAS_M5UNIFIED_I2C
typedef m5::I2C_Class *faces_iap_bus_handle_t;
#else
typedef void *faces_iap_bus_handle_t;
#endif
typedef int faces_iap_gpio_num_t;
#else
#include "driver/i2c_master.h"
#include "driver/gpio.h"
typedef i2c_master_bus_handle_t faces_iap_bus_handle_t;
typedef gpio_num_t faces_iap_gpio_num_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACES_IAP_FW_INVALID = -1,
#define FACES_IAP_FW_ITEM(symbol, model_id, model_name, variant_name, display_name, data_fn, size_fn, base_address, \
                          chunk_size, chunk_delay_ms, run_fn)                                                       \
    symbol,
#include "iap/generated/M5Faces_IAP_FirmwareItems.inc"
#undef FACES_IAP_FW_ITEM
    FACES_IAP_FW_COUNT
} faces_iap_fw_t;

typedef struct {
    faces_iap_fw_t fw;
    uint8_t model_id;
    const char *model_name;
    const char *variant_name;
    const char *display_name;
    size_t size;              ///< 固件镜像字节数 / Firmware image size in bytes
    uint32_t base_address;    ///< Flash 写入基地址 / Flash base address
    uint32_t chunk_size;      ///< 每次传输字节数 / Transfer chunk size
    uint32_t chunk_delay_ms;  ///< 块间等待时间 / Delay between chunks
} faces_iap_firmware_info_t;

/**
 * @brief 升级进度回调 / Progress callback
 *
 * @param percent  进度百分比 0-100
 * @param status   当前状态文字（UTF-8 字符串）
 * @param ctx      用户上下文指针 / User context pointer
 */
typedef void (*faces_iap_progress_cb_t)(int percent, const char *status, void *ctx);

/**
 * @brief 使用指定固件镜像执行 IAP 升级
 * Perform IAP upgrade with the specified built-in firmware image.
 *
 * This function is BLOCKING and may take 5-30 seconds depending on firmware size.
 * Run it from a dedicated FreeRTOS task.
 *
 * @param bus         Arduino/PIO 传 `&M5.In_I2C`；ESP-IDF 传 I2C master bus handle
 *                    Pass `&M5.In_I2C` on Arduino/PIO; pass an I2C master bus handle on ESP-IDF
 * @param fw          要烧录的固件镜像 / Firmware image to flash
 * @param sda_io_num  I2C SDA GPIO pin (Keyboard3/Gamepad3 bootloader entry)
 * @param scl_io_num  I2C SCL GPIO 引脚号
 * @param app_addr    应用设备当前 I2C 地址（不必为 0x08）；0 = 使用默认 0x08
 *                    Current I2C address of the app device (may not be 0x08); 0 = use default 0x08
 * @param progress_cb 进度回调（可为 NULL）/ Progress callback (can be NULL)
 * @param ctx         传递给回调的上下文 / Context passed to callback
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t faces_iap_upgrade(faces_iap_bus_handle_t bus, faces_iap_fw_t fw, faces_iap_gpio_num_t sda_io_num,
                            faces_iap_gpio_num_t scl_io_num, uint8_t app_addr, faces_iap_progress_cb_t progress_cb,
                            void *ctx);

/**
 * @brief 根据 model_id + variant 名称选择固件并执行 IAP
 *        Select firmware by model_id and variant name, then perform IAP.
 *
 * @param variant_name 受控版本名；当前支持 "V03" 和 "V04" / Controlled variant name; currently "V03" or "V04"
 * @return ESP_OK on success; ESP_ERR_NOT_FOUND if the variant is not registered
 */
esp_err_t faces_iap_upgrade_by_variant(faces_iap_bus_handle_t bus, uint8_t model_id, const char *variant_name,
                                       faces_iap_gpio_num_t sda_io_num, faces_iap_gpio_num_t scl_io_num,
                                       uint8_t app_addr, faces_iap_progress_cb_t progress_cb, void *ctx);

/** @brief 获取内置固件镜像总数 / Get the total number of built-in firmware images. */
size_t faces_iap_get_firmware_count(void);

/** @brief 按固件枚举值查询镜像信息 / Get firmware image information by firmware enum value. */
const faces_iap_firmware_info_t *faces_iap_get_firmware_info(faces_iap_fw_t fw);

/** @brief 获取指定型号的内置固件数量 / Get the number of built-in images for a model. */
size_t faces_iap_get_model_firmware_count(uint8_t model_id);

/** @brief 按型号内索引查询固件信息 / Get firmware information by model-local index. */
const faces_iap_firmware_info_t *faces_iap_get_model_firmware_info(uint8_t model_id, size_t index);

/** @brief 按型号和版本名称查找固件枚举值 / Find a firmware enum value by model and variant name. */
faces_iap_fw_t faces_iap_find_firmware(uint8_t model_id, const char *variant_name);

#ifdef __cplusplus
}
#endif
