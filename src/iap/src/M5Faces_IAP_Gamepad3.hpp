/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP_Gamepad3.hpp — Gamepad3 专用 IAP 接口
 * Device-specific IAP for M5Faces_Gamepad3.
 *
 * 参考 / Reference:
 *   examples/faces-keyboard3-test/src/BOOTLOADER_UPGRADE_F0.cpp
 *   (与 KEYBOARD3 同一固件工程师，但此文件为 Gamepad3 的独立副本)
 *
 * Gamepad3 V04 固件 / Gamepad3 V04 firmware:
 *   - trigger: write 0x01 to reg 0xFD on device 0x08
 *   - base 0x08001000, 1024-byte chunks, 200 ms delay
 *   - FW_SIZE 较小 (0x2C00)
 */
#pragma once

#include "M5Faces_IAP_Common.hpp"

/** @brief 执行 Gamepad3 专用 IAP 流程 / Run the Gamepad3-specific IAP procedure. */
esp_err_t m5faces_iap_gamepad3_run(faces_iap_bus_handle_t bus, const FacesIapOptions &opts);
