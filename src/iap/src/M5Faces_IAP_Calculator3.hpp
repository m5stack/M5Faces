/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP_Calculator3.hpp — Calculator3 专用 IAP 接口
 * Device-specific IAP for M5Faces_Calculator3.
 *
 * 参考 / Reference:
 *   examples/Faces_Calculator3_iap/BOOTLOADER_UPGRADE.cpp
 *   examples/Faces_Calculator3_iap/Faces_Calculator3_IAP.cpp
 *
 * Calculator3 V03：
 * Calculator3 V03:
 *   - 基地址 0x08002800，分块 2048 字节，块间延时 300 ms
 *   - base 0x08002800, 2048-byte chunks, 300 ms delay
 *   - 触发方式：向地址 0x08 的设备寄存器 0xFD 写入 0x01
 *   - trigger: write 0x01 to reg 0xFD on device 0x08
 */
#pragma once

#include "M5Faces_IAP_Common.hpp"

/** @brief 执行 Calculator3 专用 IAP 流程 / Run the Calculator3-specific IAP procedure. */
esp_err_t m5faces_iap_calculator3_run(faces_iap_bus_handle_t bus, const FacesIapOptions &opts);
