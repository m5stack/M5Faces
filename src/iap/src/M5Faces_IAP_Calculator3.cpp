/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP_Calculator3.cpp — Calculator3 专用 IAP 实现
 * Device-specific IAP implementation for M5Faces_Calculator3.
 *
 * 参考 / Reference:
 *   examples/Faces_Calculator3_iap/BOOTLOADER_UPGRADE.cpp
 *   examples/Faces_Calculator3_iap/Faces_Calculator3_IAP.cpp
 *
 * Calculator3 V03：基地址 0x08002800，分块 2048 字节，块间延时 300 ms。
 * Calculator3 V03: base 0x08002800, 2048-byte chunks, 300 ms delay.
 * 触发方式：向地址 0x08 的设备寄存器 0xFD 写入 0x01。
 * Trigger: write 0x01 to reg 0xFD on device 0x08.
 */

#include "M5Faces_IAP_Calculator3.hpp"

static const char *TAG = "IAP_CALC3";

/* ------------------------------------------------------------------ */
/* 触发引导加载程序                                                      */
/* Trigger the bootloader.                                             */
/* 向应用设备 (addr 0x08) 的寄存器 0xFD 写入 0x01                       */
/* Ref: Faces_Calculator3_IAP.cpp                                      */
/*   M5.Ex_I2C.writeRegister(0x08, 0xFD, data1, 1, 400000U);          */
/* ------------------------------------------------------------------ */
static esp_err_t calc3_trigger(faces_iap_bus_handle_t bus, const FacesIapOptions &opts)
{
    faces_iap_dev_handle_t app_dev = nullptr;
    esp_err_t ret                  = iap_bus_add_device(bus, iap_eff_app_addr(opts), &app_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cannot add app device: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t buf[2] = {FACES_IAP_F0_JUMP_REG, FACES_IAP_F0_JUMP_VAL};
    ret            = iap_write(app_dev, buf, sizeof(buf));
    iap_bus_remove_device(app_dev);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Trigger write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(FACES_IAP_TRIGGER_DELAY_MS));
    iap_bus_reset(bus);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 流式传输固件                                                          */
/* Stream the firmware image.                                          */
/* chunk_size / chunk_delay_ms / base_address 由 opts 参数决定          */
/* Ref: BOOTLOADER_UPGRADE.cpp func_iap_upgrade()                      */
/*   - VE1: sendbuffer[2056], 2048-byte chunks, delay(300)             */
/*   - 内置 V03 镜像使用 2048 字节分块和 delay(300)。                */
/*   - The built-in V03 image uses 2048-byte chunks and delay(300).   */
/* ------------------------------------------------------------------ */
static esp_err_t calc3_stream(faces_iap_bus_handle_t bus, const FacesIapOptions &opts)
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
       Ref: Faces_Calculator3_IAP.cpp:
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

        /* 8-byte IAP header */
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
esp_err_t m5faces_iap_calculator3_run(faces_iap_bus_handle_t bus, const FacesIapOptions &opts)
{
    if (!bus || !opts.fw_data || opts.fw_size == 0) return ESP_ERR_INVALID_ARG;

    /* 检查设备是否已经处于 bootloader 模式 (0x54 可达) */
    esp_err_t ret;
    if (iap_probe(bus, FACES_IAP_BOOT_ADDR, 200) == ESP_OK) {
        ESP_LOGI(TAG, "Bootloader already present at 0x%02X, skipping trigger", FACES_IAP_BOOT_ADDR);
        if (opts.progress_cb) opts.progress_cb(0, "Calculator3: Bootloader already active", opts.progress_ctx);
    } else {
        if (opts.progress_cb) opts.progress_cb(0, "Calculator3: Triggering bootloader...", opts.progress_ctx);

        ESP_LOGI(TAG, "Trigger bootloader (reg 0xFD=1 on addr 0x%02X)", iap_eff_app_addr(opts));
        ret = calc3_trigger(bus, opts);
        if (ret != ESP_OK) return ret;

        if (opts.progress_cb) opts.progress_cb(0, "Calculator3: Waiting for bootloader...", opts.progress_ctx);

        ESP_LOGI(TAG, "Waiting for bootloader at 0x%02X (up to %d ms)", FACES_IAP_BOOT_ADDR, FACES_IAP_BOOT_TIMEOUT_MS);
        ret = iap_wait_bootloader(bus, FACES_IAP_BOOT_TIMEOUT_MS);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Bootloader did not appear (timeout)");
            return ret;
        }
    }

    ESP_LOGI(TAG, "Bootloader ready. Streaming %u bytes (chunk=%u, base=0x%08X)", (unsigned)opts.fw_size,
             (unsigned)opts.chunk_size, (unsigned)opts.base_address);

    ret = calc3_stream(bus, opts);
    if (ret != ESP_OK) return ret;

    if (opts.progress_cb) opts.progress_cb(99, "Calculator3: Waiting for app...", opts.progress_ctx);

    ESP_LOGI(TAG, "Waiting for application at 0x%02X", iap_eff_app_addr(opts));
    ret = iap_wait_app(bus, iap_eff_app_addr(opts), 10000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Application did not re-appear after upgrade (timeout)");
    }

    if (opts.progress_cb) opts.progress_cb(99, "Calculator3: Verifying...", opts.progress_ctx);

    ESP_LOGI(TAG, "Calculator3 IAP complete");
    return ESP_OK;
}
