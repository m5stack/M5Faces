/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP.cpp — 公共 API 分发层（按设备分发）
 * Public API dispatch layer: firmware selection + per-device routing.
 *
 * 每个设备拥有独立的 IAP 实现，互不共享:
 *   Calculator3 → M5Faces_IAP_Calculator3.cpp  (参考 examples/Faces_Calculator3_iap/)
 *   KEYBOARD3    → M5Faces_IAP_Keyboard3.cpp      (参考 examples/faces-keyboard3-test/)
 *   Gamepad3   → M5Faces_IAP_Gamepad3.cpp      (参考 examples/faces-keyboard3-test/, 同一固件工程师)
 */

#include "M5Faces_IAP.hpp"
#include "M5Faces_IAP_Calculator3.hpp"
#include "M5Faces_IAP_Keyboard3.hpp"
#include "M5Faces_IAP_Gamepad3.hpp"

#include <stdint.h>
#include <string.h>

#include "../generated/M5Faces_IAP_FirmwareWrappers.inc"

/* ------------------------------------------------------------------ */
/* 固件描述表 / Firmware descriptor table                              */
/* 每个设备指定独立的 run 函数，不再按"协议"分类                         */
/* ------------------------------------------------------------------ */
typedef esp_err_t (*faces_iap_run_fn)(faces_iap_bus_handle_t, const FacesIapOptions &);

struct FacesFwDesc {
    faces_iap_firmware_info_t info;
    const unsigned char *(*get_data)(void);
    unsigned long (*get_size)(void);
    uint32_t base_address;
    uint32_t chunk_size;
    uint32_t chunk_delay_ms;
    faces_iap_run_fn run; /* 设备专用 IAP 入口 */
};

static const FacesFwDesc k_fw_table[] = {
#define FACES_IAP_FW_ITEM(symbol, model_id, model_name, variant_name, display_name, data_fn, size_fn, base_address, \
                          chunk_size, chunk_delay_ms, run_fn)                                                       \
    {{symbol, model_id, model_name, variant_name, display_name, (size_t)size_fn(), base_address, chunk_size,        \
      chunk_delay_ms},                                                                                              \
     data_fn,                                                                                                       \
     size_fn,                                                                                                       \
     base_address,                                                                                                  \
     chunk_size,                                                                                                    \
     chunk_delay_ms,                                                                                                \
     run_fn},
#include "../generated/M5Faces_IAP_FirmwareItems.inc"
#undef FACES_IAP_FW_ITEM
};

static const size_t k_fw_table_count = sizeof(k_fw_table) / sizeof(k_fw_table[0]);

/** @brief 升级后校验固件版本与设备型号 / Verify firmware version and device model after an upgrade. */
static esp_err_t verify_upgraded_device(faces_iap_bus_handle_t bus, const FacesFwDesc &desc, uint8_t app_addr)
{
    faces_iap_dev_handle_t dev = nullptr;
    esp_err_t ret              = iap_bus_add_device(bus, app_addr ? app_addr : FACES_IAP_APP_ADDR, &dev);
    if (ret != ESP_OK) return ret;

    uint8_t expected_version = 0;
    if (strcmp(desc.info.variant_name, "V03") == 0)
        expected_version = 0x03;
    else if (strcmp(desc.info.variant_name, "V04") == 0)
        expected_version = 0x04;
    else
        ret = ESP_ERR_NOT_SUPPORTED;

    uint8_t version = 0;
    if (ret == ESP_OK) {
        ret = iap_read_reg(dev, 0xFE, &version, 1);
        if (ret == ESP_OK && version != expected_version) {
            ESP_LOGE("M5Faces_IAP", "Post-upgrade version mismatch: expected 0x%02X, got 0x%02X", expected_version,
                     version);
            ret = ESP_ERR_INVALID_RESPONSE;
        }
    }

    uint8_t model_id = 0;
    if (ret == ESP_OK) {
        ret = iap_read_reg(dev, 0xD0, &model_id, 1);
        if (ret == ESP_OK && model_id != desc.info.model_id) {
            ESP_LOGE("M5Faces_IAP", "Post-upgrade model mismatch: expected 0x%02X, got 0x%02X", desc.info.model_id,
                     model_id);
            ret = ESP_ERR_INVALID_RESPONSE;
        }
    }

    if (ret == ESP_OK) {
        ESP_LOGI("M5Faces_IAP",
                 "Post-upgrade verification passed: expected_model=0x%02X reported_model=0x%02X fw=0x%02X",
                 desc.info.model_id, model_id, version);
    }

    iap_bus_remove_device(dev);
    return ret;
}

/** @brief 获取内置固件镜像总数 / Get the total number of built-in firmware images. */
extern "C" size_t faces_iap_get_firmware_count(void)
{
    return k_fw_table_count;
}

/** @brief 按固件枚举值查询镜像信息 / Get firmware image information by enum value. */
extern "C" const faces_iap_firmware_info_t *faces_iap_get_firmware_info(faces_iap_fw_t fw)
{
    if ((int)fw < 0 || (size_t)fw >= k_fw_table_count) return nullptr;
    return &k_fw_table[(size_t)fw].info;
}

/** @brief 获取指定型号的内置固件数量 / Get the number of built-in images for a model. */
extern "C" size_t faces_iap_get_model_firmware_count(uint8_t model_id)
{
    size_t count = 0;
    for (size_t i = 0; i < k_fw_table_count; ++i) {
        if (k_fw_table[i].info.model_id == model_id) {
            ++count;
        }
    }
    return count;
}

/** @brief 按型号内索引查询固件信息 / Get firmware information by model-local index. */
extern "C" const faces_iap_firmware_info_t *faces_iap_get_model_firmware_info(uint8_t model_id, size_t index)
{
    for (size_t i = 0; i < k_fw_table_count; ++i) {
        if (k_fw_table[i].info.model_id != model_id) continue;
        if (index == 0) return &k_fw_table[i].info;
        --index;
    }
    return nullptr;
}

/** @brief 按型号和版本名称查找固件 / Find firmware by model and variant name. */
extern "C" faces_iap_fw_t faces_iap_find_firmware(uint8_t model_id, const char *variant_name)
{
    if (!variant_name || !variant_name[0]) return FACES_IAP_FW_INVALID;

    for (size_t i = 0; i < k_fw_table_count; ++i) {
        const faces_iap_firmware_info_t &info = k_fw_table[i].info;
        if (info.model_id == model_id && strcmp(info.variant_name, variant_name) == 0) {
            return info.fw;
        }
    }
    return FACES_IAP_FW_INVALID;
}

/* ------------------------------------------------------------------ */
/* 公共升级入口 / Public: faces_iap_upgrade                            */
/* ------------------------------------------------------------------ */
extern "C" esp_err_t faces_iap_upgrade(faces_iap_bus_handle_t bus, faces_iap_fw_t fw, faces_iap_gpio_num_t sda_io_num,
                                       faces_iap_gpio_num_t scl_io_num, uint8_t app_addr,
                                       faces_iap_progress_cb_t progress_cb, void *ctx)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if ((size_t)fw >= k_fw_table_count) return ESP_ERR_INVALID_ARG;

    const FacesFwDesc &desc = k_fw_table[(int)fw];

    FacesIapOptions opts{};
    opts.fw_data        = desc.get_data();
    opts.fw_size        = (size_t)desc.get_size();
    opts.base_address   = desc.base_address;
    opts.chunk_size     = desc.chunk_size;
    opts.chunk_delay_ms = desc.chunk_delay_ms;
    opts.progress_cb    = progress_cb;
    opts.progress_ctx   = ctx;
    opts.sda_io_num     = sda_io_num;
    opts.scl_io_num     = scl_io_num;
    opts.app_addr       = app_addr; /* 0 = 回落到默认 0x08 / 0 falls back to default 0x08 */

    if (!opts.fw_data || opts.fw_size == 0) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = desc.run(bus, opts);
    if (ret != ESP_OK) return ret;

    ret = verify_upgraded_device(bus, desc, app_addr);
    if (ret == ESP_OK && progress_cb) progress_cb(100, "Upgrade verified", ctx);
    return ret;
}

/** @brief 按型号和版本名称选择镜像并升级 / Select an image by model and variant name, then upgrade. */
extern "C" esp_err_t faces_iap_upgrade_by_variant(faces_iap_bus_handle_t bus, uint8_t model_id,
                                                  const char *variant_name, faces_iap_gpio_num_t sda_io_num,
                                                  faces_iap_gpio_num_t scl_io_num, uint8_t app_addr,
                                                  faces_iap_progress_cb_t progress_cb, void *ctx)
{
    faces_iap_fw_t fw = faces_iap_find_firmware(model_id, variant_name);
    if (fw == FACES_IAP_FW_INVALID) return ESP_ERR_NOT_FOUND;
    return faces_iap_upgrade(bus, fw, sda_io_num, scl_io_num, app_addr, progress_cb, ctx);
}
