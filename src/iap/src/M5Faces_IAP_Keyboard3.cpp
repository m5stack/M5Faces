/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP_Keyboard3.cpp — KEYBOARD3 专用 IAP 实现
 * Device-specific IAP implementation for M5Faces_Keyboard3.
 *
 * 参考 / Reference:
 *   examples/faces-keyboard3-test/src/BOOTLOADER_UPGRADE_F0.cpp
 *   examples/faces-keyboard3-test/src/iap_test.cpp
 *
 * KEYBOARD3 与 Gamepad3 由同一固件工程师开发，协议相似但本文件为 KEYBOARD3 独立副本，
 * 方便后续 KEYBOARD3 协议单独变化时无需顾虑 Gamepad3。
 *
 * 协议:
 *   1. 向 0x08 的 reg 0xFD 写入 0x01 触发 PY32 进 bootloader
 *   2. 等待 bootloader 地址 0x54 出现
 *   3. 以 1024-byte 块传输固件 (8 字节头 + 数据)
 *   4. 发送 0x77 跳转到新应用
 */

#include "M5Faces_IAP_Keyboard3.hpp"

static const char *TAG = "IAP_KEYBOARD3";

/* ------------------------------------------------------------------ */
/* 触发引导加载程序                                                      */
/* Trigger the bootloader.                                             */
/* Ref: iap_test.cpp                                                   */
/*   i2c_write_byte(0x08, 0xFD, 0x01);                                */
/*   pinMode(SDA, OUTPUT); pinMode(SCL, OUTPUT);                       */
/*   digitalWrite(SCL, LOW); digitalWrite(SDA, LOW);                   */
/*   delay(100);                                                       */
/*   gpio_reset_pin(SCL); gpio_reset_pin(SDA);                        */
/* ------------------------------------------------------------------ */
/** @brief 触发 Keyboard3 引导加载程序 / Trigger the Keyboard3 bootloader. */
static esp_err_t keyboard3_trigger(faces_iap_bus_handle_t bus, const FacesIapOptions &opts)
{
    faces_iap_dev_handle_t app_dev = nullptr;
    esp_err_t ret                  = iap_bus_add_device(bus, iap_eff_app_addr(opts), &app_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cannot add app device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Step 1: Write 0x01 to reg 0xFD — tell PY32 app to jump to bootloader */
    uint8_t buf[2] = {FACES_IAP_F0_JUMP_REG, FACES_IAP_F0_JUMP_VAL};
    ret            = iap_write(app_dev, buf, sizeof(buf));
    iap_bus_remove_device(app_dev);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Trigger write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Step 2: Pull SDA/SCL LOW for 100ms (from iap_test.cpp)
       PY32 需要在复位过程中检测到 I2C 线路被拉低才能进入 bootloader 模式。
       The PY32 checks the I2C line state during reset to enter bootloader mode.

       Critical: The ESP-IDF i2c_master driver routes peripheral output through
       the GPIO matrix (GPIO_FUNCx_OUT_SEL). gpio_set_level() only writes to the
       GPIO output register — it does NOT change the physical pin output because
       the I2C peripheral signal still drives it. We must first disconnect the
       I2C peripheral from the pins via the GPIO matrix, then pull LOW, then
       restore the I2C peripheral connections. */
    ESP_LOGI(TAG, "Pulling SDA(GPIO%d)/SCL(GPIO%d) LOW for 100ms", (int)opts.sda_io_num, (int)opts.scl_io_num);

    return iap_pulse_i2c_lines(bus, opts.sda_io_num, opts.scl_io_num);
}

/* ------------------------------------------------------------------ */
/* 流式传输固件                                                          */
/* Stream the firmware image.                                          */
/* Ref: BOOTLOADER_UPGRADE_F0.cpp func_iap_upgrade()                   */
/*   sendbuffer[1032], 1024-byte chunks, delay(200)                    */
/*   base = 0x08001000, FW_SIZE = 0xD000                               */
/* ------------------------------------------------------------------ */
static esp_err_t keyboard3_stream(faces_iap_bus_handle_t bus, const FacesIapOptions &opts)
{
    esp_err_t ret;

    const size_t buf_cap = 8u + opts.chunk_size;
    uint8_t *sendbuf     = (uint8_t *)malloc(buf_cap);
    if (!sendbuf) {
        ESP_LOGE(TAG, "OOM allocating %u-byte send buffer", (unsigned)buf_cap);
        return ESP_ERR_NO_MEM;
    }

    faces_iap_dev_handle_t boot_dev = nullptr;
    ret                             = iap_bus_add_device(bus, FACES_IAP_BOOT_ADDR, &boot_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cannot add boot device: %s", esp_err_to_name(ret));
        free(sendbuf);
        return ret;
    }

    /* Standalone WREN (write-enable) — required before streaming chunks.
       Ref: iap_test.cpp:
         btl_upgrade.writeBytes(0x54, 0x06, NULL, 0); */
    uint8_t wren_cmd = FACES_IAP_OPC_WREN;
    ret              = iap_write(boot_dev, &wren_cmd, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Standalone WREN failed: %s", esp_err_to_name(ret));
        iap_bus_remove_device(boot_dev);
        free(sendbuf);
        return ret;
    }
    ESP_LOGI(TAG, "Standalone WREN sent");

    uint32_t addr    = opts.base_address;
    size_t remaining = opts.fw_size;
    size_t offset    = 0;
    size_t total     = opts.fw_size;

    while (remaining > 0) {
        /* Poll bootloader readiness before each chunk */
        const int64_t deadline_us = esp_timer_get_time() + 5000LL * 1000;
        while (iap_probe(bus, FACES_IAP_BOOT_ADDR, 100) != ESP_OK) {
            if (esp_timer_get_time() > deadline_us) {
                ESP_LOGE(TAG, "Bootloader unresponsive before chunk @0x%08X", (unsigned)addr);
                iap_bus_remove_device(boot_dev);
                free(sendbuf);
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        size_t chunk = (remaining >= opts.chunk_size) ? opts.chunk_size : remaining;

        /* 8-byte IAP header (same format as BOOTLOADER_UPGRADE_F0.cpp) */
        sendbuf[0] = FACES_IAP_OPC_WREN;
        sendbuf[1] = (uint8_t)(addr >> 24);
        sendbuf[2] = (uint8_t)(addr >> 16);
        sendbuf[3] = (uint8_t)(addr >> 8);
        sendbuf[4] = (uint8_t)(addr >> 0);
        sendbuf[5] = (uint8_t)(chunk >> 8);
        sendbuf[6] = (uint8_t)(chunk >> 0);
        sendbuf[7] = 0xFF;

        memcpy(sendbuf + 8, opts.fw_data + offset, chunk);

        ret = iap_write(boot_dev, sendbuf, 8u + chunk);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Chunk write failed @0x%08X: %s", (unsigned)addr, esp_err_to_name(ret));
            iap_bus_remove_device(boot_dev);
            free(sendbuf);
            return ret;
        }

        remaining -= chunk;
        offset += chunk;
        addr += chunk;

        if (opts.progress_cb) {
            int pct             = (int)(((total - remaining) * 100u) / total);
            size_t total_chunks = (total + opts.chunk_size - 1) / opts.chunk_size;
            size_t cur_chunk    = offset / opts.chunk_size;
            char sbuf[48];
            snprintf(sbuf, sizeof(sbuf), "Chunk %u/%u  %d%%", (unsigned)cur_chunk, (unsigned)total_chunks, pct);
            opts.progress_cb(pct, sbuf, opts.progress_ctx);
        }

        vTaskDelay(pdMS_TO_TICKS(opts.chunk_delay_ms));
    }

    /* Send jump command with retry (PY32 may need multiple attempts)
       Ref: m5_py32bin_tools jump_to_application() */
    ret = iap_jump_with_retry(bus, boot_dev);
    iap_bus_remove_device(boot_dev);
    free(sendbuf);
    return ret;
}

/* ------------------------------------------------------------------ */
/* 公共入口 / Public entry                                              */
/* ------------------------------------------------------------------ */
esp_err_t m5faces_iap_keyboard3_run(faces_iap_bus_handle_t bus, const FacesIapOptions &opts)
{
    if (!bus || !opts.fw_data || opts.fw_size == 0) return ESP_ERR_INVALID_ARG;

    /* 检查设备是否已经处于 bootloader 模式 (0x54 可达) */
    esp_err_t ret;
    if (iap_probe(bus, FACES_IAP_BOOT_ADDR, 200) == ESP_OK) {
        ESP_LOGI(TAG, "Bootloader already present at 0x%02X, skipping trigger", FACES_IAP_BOOT_ADDR);
        if (opts.progress_cb) opts.progress_cb(0, "Keyboard3: Bootloader already active", opts.progress_ctx);
    } else {
        if (opts.progress_cb) opts.progress_cb(0, "Keyboard3: Triggering bootloader...", opts.progress_ctx);

        ESP_LOGI(TAG, "Trigger bootloader (reg 0xFD=1 on addr 0x%02X)", iap_eff_app_addr(opts));
        ret = keyboard3_trigger(bus, opts);
        if (ret != ESP_OK) return ret;

        if (opts.progress_cb) opts.progress_cb(0, "Keyboard3: Waiting for bootloader...", opts.progress_ctx);

        ESP_LOGI(TAG, "Waiting for bootloader at 0x%02X (up to %d ms)", FACES_IAP_BOOT_ADDR, FACES_IAP_BOOT_TIMEOUT_MS);
        ret = iap_wait_bootloader(bus, FACES_IAP_BOOT_TIMEOUT_MS);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Bootloader did not appear (timeout)");
            return ret;
        }
    }

    ESP_LOGI(TAG, "Bootloader ready. Streaming %u bytes (chunk=%u, base=0x%08X)", (unsigned)opts.fw_size,
             (unsigned)opts.chunk_size, (unsigned)opts.base_address);

    ret = keyboard3_stream(bus, opts);
    if (ret != ESP_OK) return ret;

    if (opts.progress_cb) opts.progress_cb(99, "Keyboard3: Waiting for app...", opts.progress_ctx);

    ESP_LOGI(TAG, "Waiting for application at 0x%02X", iap_eff_app_addr(opts));
    ret = iap_wait_app(bus, iap_eff_app_addr(opts), 10000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Application did not re-appear after upgrade (timeout)");
    }

    if (opts.progress_cb) opts.progress_cb(99, "Keyboard3: Verifying...", opts.progress_ctx);

    ESP_LOGI(TAG, "Keyboard3 IAP complete");
    return ESP_OK;
}
