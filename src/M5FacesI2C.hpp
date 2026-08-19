/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file M5FacesI2C.hpp
 * @brief I2C 驱动抽象层（双平台：ESP-IDF 和 Arduino）
 *        I2C driver abstraction layer (Dual-Platform: ESP-IDF & Arduino)
 *
 * 平台优先级 / Platform priority:
 *   Arduino:
 *     1. M5Unified I2C_Class (若存在 / if available)
 *     2. Wire (TwoWire)
 *   ESP-IDF:
 *     1. M5Unified I2C_Class (若存在 / if available) — 与 M5GFX 共存的推荐方式
 *     2. ESP-IDF 新 I2C Master API (driver/i2c_master.h, IDF >= 5.0)
 *        — 独立使用时，由调用方提前创建 i2c_master_dev_handle_t
 */

#ifndef __M5_FACES_I2C_COMPAT_H__
#define __M5_FACES_I2C_COMPAT_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ============================================================
// Arduino
// ============================================================
#ifdef ARDUINO

#include "Wire.h"

// ── Wire inline helpers ──────────────────────────────────────

/** @brief 通过 Arduino TwoWire 读取寄存器 / Read registers through Arduino TwoWire. */
static inline bool m5faces_wire_read_bytes(TwoWire *wire, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    wire->beginTransmission(addr);
    wire->write(reg);
    if (wire->endTransmission(false) != 0) return false;
    if ((size_t)wire->requestFrom(addr, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) data[i] = wire->read();
    return true;
}

/** @brief 通过 Arduino TwoWire 写入寄存器 / Write registers through Arduino TwoWire. */
static inline bool m5faces_wire_write_bytes(TwoWire *wire, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{
    wire->beginTransmission(addr);
    wire->write(reg);
    for (size_t i = 0; i < len; i++) wire->write(data[i]);
    return wire->endTransmission() == 0;
}

// ── M5Unified I2C_Class 检测 (Arduino) ──────────────────────
#if defined(__cplusplus) && __has_include(<utility/I2C_Class.hpp>)
#define M5FACES_HAS_M5UNIFIED_I2C 1
#include <utility/I2C_Class.hpp>
#else
#define M5FACES_HAS_M5UNIFIED_I2C 0
#endif

#if M5FACES_HAS_M5UNIFIED_I2C
/** @brief 通过 M5Unified I2C_Class 读取寄存器 / Read registers through M5Unified I2C_Class. */
static inline bool m5faces_m5i2c_read_bytes(m5::I2C_Class *i2c, uint8_t addr, uint8_t reg, uint8_t *data, size_t len,
                                            uint32_t freq)
{
    return i2c->readRegister(addr, reg, data, len, freq);
}

/** @brief 通过 M5Unified I2C_Class 写入寄存器 / Write registers through M5Unified I2C_Class. */
static inline bool m5faces_m5i2c_write_bytes(m5::I2C_Class *i2c, uint8_t addr, uint8_t reg, const uint8_t *data,
                                             size_t len, uint32_t freq)
{
    return i2c->writeRegister(addr, reg, data, len, freq);
}
#endif  // M5FACES_HAS_M5UNIFIED_I2C

// ============================================================
// ESP-IDF
// ============================================================
#else

#include <esp_err.h>
#include <esp_idf_version.h>

// ── M5Unified I2C_Class 检测 (ESP-IDF) ───────────────────────
// 当项目中存在 M5GFX / M5Unified 时，推荐通过 M5.In_I2C / M5.Ex_I2C 传入
// When M5GFX/M5Unified is present, pass M5.In_I2C or M5.Ex_I2C for safe I2C sharing.
#if defined(__cplusplus) && __has_include(<utility/I2C_Class.hpp>)
#define M5FACES_HAS_M5UNIFIED_I2C 1
#include <utility/I2C_Class.hpp>
#else
#define M5FACES_HAS_M5UNIFIED_I2C 0
#endif

#if M5FACES_HAS_M5UNIFIED_I2C
/** @brief 通过 M5Unified I2C_Class 读取寄存器 / Read registers through M5Unified I2C_Class. */
static inline bool m5faces_m5i2c_read_bytes(m5::I2C_Class *i2c, uint8_t addr, uint8_t reg, uint8_t *data, size_t len,
                                            uint32_t freq)
{
    return i2c->readRegister(addr, reg, data, len, freq);
}

/** @brief 通过 M5Unified I2C_Class 写入寄存器 / Write registers through M5Unified I2C_Class. */
static inline bool m5faces_m5i2c_write_bytes(m5::I2C_Class *i2c, uint8_t addr, uint8_t reg, const uint8_t *data,
                                             size_t len, uint32_t freq)
{
    return i2c->writeRegister(addr, reg, data, len, freq);
}
#endif  // M5FACES_HAS_M5UNIFIED_I2C

// ── ESP-IDF 新 I2C Master API (IDF >= 5.0) ───────────────────
// 独立使用路径：调用方创建 i2c_master_bus + i2c_master_bus_add_device，
// 将 i2c_master_dev_handle_t 传给 M5FacesBase::begin()
// Standalone path: caller creates bus + device, passes i2c_master_dev_handle_t.
#if defined(__cplusplus) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0))
#define M5FACES_HAS_IDF_I2C_MASTER 1
#include "driver/i2c_master.h"

/** @brief 通过 ESP-IDF I2C 设备句柄读取寄存器 / Read registers through an ESP-IDF I2C device handle. */
static inline bool m5faces_idf_read_bytes(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, data, len, 50);
    return err == ESP_OK;
}

/** @brief 通过 ESP-IDF I2C 设备句柄写入寄存器 / Write registers through an ESP-IDF I2C device handle. */
static inline bool m5faces_idf_write_bytes(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *data, size_t len)
{
    // 栈上合并寄存器地址和数据（最大16字节写入）
    // Merge register address and data on stack (max 16-byte write)
    if (len > 16) return false;
    uint8_t buf[17];
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    esp_err_t err = i2c_master_transmit(dev, buf, len + 1, 50);
    return err == ESP_OK;
}
#else
#define M5FACES_HAS_IDF_I2C_MASTER 0

struct m5faces_dummy_i2c_master_dev_handle_t;
struct m5faces_dummy_i2c_master_bus_handle_t;
using i2c_master_dev_handle_t = m5faces_dummy_i2c_master_dev_handle_t *;
using i2c_master_bus_handle_t = m5faces_dummy_i2c_master_bus_handle_t *;

/** @brief 在不支持新 I2C API 时返回读取失败 / Return read failure when the new I2C API is unavailable. */
static inline bool m5faces_idf_read_bytes(i2c_master_dev_handle_t, uint8_t, uint8_t *, size_t)
{
    return false;
}

/** @brief 在不支持新 I2C API 时返回写入失败 / Return write failure when the new I2C API is unavailable. */
static inline bool m5faces_idf_write_bytes(i2c_master_dev_handle_t, uint8_t, const uint8_t *, size_t)
{
    return false;
}
#endif

#endif  // ARDUINO

#endif  // __M5_FACES_I2C_COMPAT_H__
