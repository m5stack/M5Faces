/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP_Keyboard3.hpp — KEYBOARD3 专用 IAP 接口
 * Device-specific IAP for M5Faces_Keyboard3.
 *
 * 参考 / Reference:
 *   examples/faces-keyboard3-test/src/BOOTLOADER_UPGRADE_F0.cpp
 *   examples/faces-keyboard3-test/src/iap_test.cpp
 *
 * Keyboard3 V03 固件 / Keyboard3 V03 firmware:
 *   - trigger: write 0x01 to reg 0xFD on device 0x08
 *   - base 0x08001000, 1024-byte chunks, 200 ms delay
 *   - FW_SIZE = 0xD000
 */
#pragma once

#include "M5Faces_IAP_Common.hpp"

/** @brief 执行 Keyboard3 专用 IAP 流程 / Run the Keyboard3-specific IAP procedure. */
esp_err_t m5faces_iap_keyboard3_run(faces_iap_bus_handle_t bus, const FacesIapOptions &opts);
