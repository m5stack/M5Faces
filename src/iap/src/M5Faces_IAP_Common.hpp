/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * M5Faces_IAP_Common.hpp — 共享协议常量、跨框架 I2C 辅助函数和 IAP 选项结构体
 * Shared protocol constants, cross-framework I2C helpers, and IAP options struct.
 */
#pragma once

#include "M5Faces_IAP.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/i2c_periph.h"
#include "soc/soc_caps.h"
#endif

/* ------------------------------------------------------------------ */
/* I2C 地址 / I2C addresses                                            */
/* ------------------------------------------------------------------ */
#define FACES_IAP_APP_ADDR  0x08u /* 正常应用 / Normal application  */
#define FACES_IAP_BOOT_ADDR 0x54u /* 引导加载程序 / Bootloader       */

/* ------------------------------------------------------------------ */
/* IAP 操作码 / Opcodes                                                 */
/* ------------------------------------------------------------------ */
#define FACES_IAP_OPC_WREN  0x06u /* 写操作 / Write enable           */
#define FACES_IAP_OPC_USRCD 0x77u /* 跳转到用户代码 / Jump to app    */

/* ------------------------------------------------------------------ */
/* F0 协议触发常量 / F0 protocol trigger constants                      */
/* ------------------------------------------------------------------ */
#define FACES_IAP_F0_JUMP_REG 0xFDu
#define FACES_IAP_F0_JUMP_VAL 0x01u

/* ------------------------------------------------------------------ */
/* Calculator3 VE1 protocol trigger constants                          */
/* ------------------------------------------------------------------ */
#define FACES_IAP_CALC3VE_SCRATCH_REG 0x18u /* DMM_SCRATCH_PAD_REG1        */
#define FACES_IAP_CALC3VE_CMD_REG     0x14u /* DMM_EXE_CMD_REG             */
#define FACES_IAP_CALC3VE_CMD_JMP     0xDEu /* CMD_JMP_BOOTLOADER          */

/* ------------------------------------------------------------------ */
/* 时序 / Timing                                                        */
/* ------------------------------------------------------------------ */
#define FACES_IAP_I2C_FREQ_HZ      400000u
#define FACES_IAP_TRIGGER_DELAY_MS 200u
#define FACES_IAP_BOOT_TIMEOUT_MS  30000u
#define FACES_IAP_POLL_INTERVAL_MS 100u

/* ------------------------------------------------------------------ */
/* IAP 选项结构体 / IAP options struct                                   */
/* ------------------------------------------------------------------ */
struct FacesIapOptions {
    const uint8_t *fw_data;  /* 固件字节数组 / Firmware data     */
    size_t fw_size;          /* 固件字节数 / Firmware size       */
    uint32_t base_address;   /* Flash 起始地址 / Base address    */
    uint32_t chunk_size;     /* 每块字节数 1024 or 2048          */
    uint32_t chunk_delay_ms; /* 块间延迟 200 or 300 ms           */
    faces_iap_progress_cb_t progress_cb;
    void *progress_ctx;
    faces_iap_gpio_num_t sda_io_num; /* I2C SDA 引脚号 (Keyboard3/Gamepad3 需要) */
    faces_iap_gpio_num_t scl_io_num; /* I2C SCL 引脚号 (Keyboard3/Gamepad3 需要) */
    uint8_t app_addr;                /* 应用 I2C 地址，0 = 使用默认 0x08 */
                                     /* App I2C address; 0 = use default 0x08 */
};

#ifdef ARDUINO
struct FacesIapDevice {
    faces_iap_bus_handle_t bus;
    uint8_t addr;
};
typedef FacesIapDevice *faces_iap_dev_handle_t;
#else
typedef i2c_master_dev_handle_t faces_iap_dev_handle_t;
#endif

/**
 * @brief 返回有效的应用地址，未指定时使用 0x08。
 *        Return the effective app address, falling back to 0x08 when unspecified.
 */
static inline uint8_t iap_eff_app_addr(const FacesIapOptions &opts)
{
    return opts.app_addr ? opts.app_addr : FACES_IAP_APP_ADDR;
}

/* ------------------------------------------------------------------ */
/* 跨框架 I2C 辅助内联 / Cross-framework I2C inline helpers           */
/* ------------------------------------------------------------------ */
/** @brief 在 I2C 总线上注册指定地址的临时设备 / Add a temporary device at the given I2C address. */
static inline esp_err_t iap_bus_add_device(faces_iap_bus_handle_t bus, uint8_t addr, faces_iap_dev_handle_t *dev_out)
{
#ifdef ARDUINO
#if M5FACES_HAS_M5UNIFIED_I2C
    if (!bus || !dev_out) return ESP_ERR_INVALID_ARG;
    FacesIapDevice *dev = static_cast<FacesIapDevice *>(malloc(sizeof(FacesIapDevice)));
    if (!dev) return ESP_ERR_NO_MEM;
    dev->bus  = bus;
    dev->addr = addr;
    *dev_out  = dev;
    return ESP_OK;
#else
    (void)bus;
    (void)addr;
    (void)dev_out;
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
    cfg.device_address      = addr;
    cfg.scl_speed_hz        = FACES_IAP_I2C_FREQ_HZ;
    return i2c_master_bus_add_device(bus, &cfg, dev_out);
#endif
}

/** @brief 向 IAP 设备发送一段原始数据 / Transmit a raw data block to an IAP device. */
static inline esp_err_t iap_write(faces_iap_dev_handle_t dev, const uint8_t *buf, size_t len)
{
#ifdef ARDUINO
#if M5FACES_HAS_M5UNIFIED_I2C
    if (!dev || !dev->bus || (!buf && len)) return ESP_ERR_INVALID_ARG;
    if (!dev->bus->start(dev->addr, false, FACES_IAP_I2C_FREQ_HZ)) return ESP_FAIL;
    const bool wrote   = len == 0 || dev->bus->write(buf, len);
    const bool stopped = dev->bus->stop();
    return wrote && stopped ? ESP_OK : ESP_FAIL;
#else
    (void)dev;
    (void)buf;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    /* 足够长的超时以应对大块传输 / Generous timeout for large chunk transfer */
    return i2c_master_transmit(dev, buf, len, 2000);
#endif
}

/** @brief 注销并释放临时 I2C 设备 / Remove and release a temporary I2C device. */
static inline void iap_bus_remove_device(faces_iap_dev_handle_t dev)
{
#ifdef ARDUINO
    free(dev);
#else
    if (dev) i2c_master_bus_rm_device(dev);
#endif
}

/** @brief 探测指定 I2C 地址 / Probe a specified I2C address. */
static inline esp_err_t iap_probe(faces_iap_bus_handle_t bus, uint8_t addr, uint32_t timeout_ms)
{
#ifdef ARDUINO
#if M5FACES_HAS_M5UNIFIED_I2C
    (void)timeout_ms;
    return bus && bus->scanID(addr, FACES_IAP_I2C_FREQ_HZ) ? ESP_OK : ESP_FAIL;
#else
    (void)bus;
    (void)addr;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    return i2c_master_probe(bus, addr, timeout_ms);
#endif
}

/** @brief 从设备寄存器读取数据 / Read data from a device register. */
static inline esp_err_t iap_read_reg(faces_iap_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
#ifdef ARDUINO
#if M5FACES_HAS_M5UNIFIED_I2C
    if (!dev || !dev->bus || !data || len == 0) return ESP_ERR_INVALID_ARG;
    return dev->bus->readRegister(dev->addr, reg, data, len, FACES_IAP_I2C_FREQ_HZ) ? ESP_OK : ESP_FAIL;
#else
    (void)dev;
    (void)reg;
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 200);
#endif
}

/** @brief 重新初始化 I2C 总线 / Reset or reinitialize the I2C bus. */
static inline esp_err_t iap_bus_reset(faces_iap_bus_handle_t bus)
{
#ifdef ARDUINO
#if M5FACES_HAS_M5UNIFIED_I2C
    if (!bus) return ESP_ERR_INVALID_ARG;
    const i2c_port_t port = bus->getPort();
    const int sda         = bus->getSDA();
    const int scl         = bus->getSCL();
    bus->release();
    delay(20);
    return bus->begin(port, sda, scl) ? ESP_OK : ESP_FAIL;
#else
    (void)bus;
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    return i2c_master_bus_reset(bus);
#endif
}

/** @brief 轮询引导加载程序直到就绪或超时 / Poll the bootloader until it is ready or times out. */
static inline esp_err_t iap_wait_bootloader(faces_iap_bus_handle_t bus, int64_t timeout_ms)
{
    static const char *WTAG = "IAP_WAIT";
    int64_t deadline_us     = esp_timer_get_time() + timeout_ms * 1000LL;
    int64_t last_log_us     = 0;
    int probes              = 0;
    while (esp_timer_get_time() < deadline_us) {
        esp_err_t r = iap_probe(bus, FACES_IAP_BOOT_ADDR, 200);
        if (r == ESP_OK) {
            ESP_LOGI(WTAG, "Bootloader found after %d probes", probes + 1);
            return ESP_OK;
        }
        ++probes;
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_log_us >= 1000000LL) {
            ESP_LOGI(WTAG, "Waiting for bootloader... (%d probes)", probes);
            last_log_us = now_us;
        }
        vTaskDelay(pdMS_TO_TICKS(FACES_IAP_POLL_INTERVAL_MS));
    }
    ESP_LOGE(WTAG, "Bootloader not found after %d probes (timeout %lld ms)", probes, timeout_ms);
    return ESP_ERR_TIMEOUT;
}

/* ------------------------------------------------------------------ */
/* GPIO Matrix 辅助: 断开/重连 I2C 外设信号                              */
/* ESP-IDF 的 i2c_master 驱动通过 GPIO Matrix 将外设输出信号路由到引脚。 */
/* gpio_set_level() 只写 GPIO 输出寄存器，不影响物理引脚输出。必须先      */
/* 将 GPIO 寄存器路由到引脚输出（断开外设），才能用 GPIO 驱动线路。       */
/* ------------------------------------------------------------------ */
#ifndef ARDUINO
/** @brief 断开 I2C 外设输出并将引脚切回 GPIO 矩阵 / Disconnect I2C peripheral outputs and route pins to GPIO. */
static inline void iap_i2c_disconnect_gpio(gpio_num_t sda, gpio_num_t scl)
{
    esp_rom_gpio_connect_out_signal(sda, SIG_GPIO_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(scl, SIG_GPIO_OUT_IDX, false, false);
}

/** @brief 查找给定 I2C 总线句柄对应的控制器端口 / Find the controller port for an I2C bus handle. */
static inline int iap_i2c_bus_port(i2c_master_bus_handle_t bus)
{
    for (int port = 0; port < SOC_I2C_NUM; ++port) {
        i2c_master_bus_handle_t candidate = nullptr;
        if (i2c_master_get_bus_handle((i2c_port_num_t)port, &candidate) == ESP_OK && candidate == bus) {
            return port;
        }
    }
    return -1;
}

/** @brief 将 SDA/SCL 重新连接到对应的 I2C 外设信号 / Reconnect SDA/SCL to the matching I2C peripheral signals. */
static inline esp_err_t iap_i2c_reconnect_gpio(i2c_master_bus_handle_t bus, gpio_num_t sda, gpio_num_t scl)
{
    const int port = iap_i2c_bus_port(bus);
    if (port < 0) return ESP_ERR_NOT_FOUND;
    esp_rom_gpio_connect_out_signal(sda, i2c_periph_signal[port].sda_out_sig, 0, 0);
    esp_rom_gpio_connect_in_signal(sda, i2c_periph_signal[port].sda_in_sig, 0);
    esp_rom_gpio_connect_out_signal(scl, i2c_periph_signal[port].scl_out_sig, 0, 0);
    esp_rom_gpio_connect_in_signal(scl, i2c_periph_signal[port].scl_in_sig, 0);
    return ESP_OK;
}
#endif

/**
 * @brief 释放 I2C 外设并将 SDA/SCL 拉低 100 ms 后恢复。
 *        Release the I2C peripheral, hold SDA/SCL low for 100 ms, then restore it.
 */
static inline esp_err_t iap_pulse_i2c_lines(faces_iap_bus_handle_t bus, faces_iap_gpio_num_t sda,
                                            faces_iap_gpio_num_t scl)
{
#ifdef ARDUINO
#if M5FACES_HAS_M5UNIFIED_I2C
    if (!bus) return ESP_ERR_INVALID_ARG;
    const i2c_port_t port = bus->getPort();
    bus->release();
    delay(10);
    pinMode(sda, OUTPUT_OPEN_DRAIN);
    pinMode(scl, OUTPUT_OPEN_DRAIN);
    digitalWrite(sda, LOW);
    digitalWrite(scl, LOW);
    delay(100);
    digitalWrite(sda, HIGH);
    digitalWrite(scl, HIGH);
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);
    delay(50);
    return bus->begin(port, sda, scl) ? ESP_OK : ESP_FAIL;
#else
    (void)bus;
    (void)sda;
    (void)scl;
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    iap_i2c_disconnect_gpio(sda, scl);
    gpio_set_direction(sda, GPIO_MODE_OUTPUT);
    gpio_set_direction(scl, GPIO_MODE_OUTPUT);
    gpio_set_level(sda, 0);
    gpio_set_level(scl, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    gpio_set_direction(sda, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(scl, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(sda, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(scl, GPIO_PULLUP_ONLY);
    esp_err_t ret = iap_i2c_reconnect_gpio(bus, sda, scl);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(50));
    return iap_bus_reset(bus);
#endif
}

/* ------------------------------------------------------------------ */
/* 跳转到应用 (带重试)                                                  */
/* PY32 bootloader 可能需要多次 jump 命令才能真正跳转到应用。             */
/* Ref: m5_py32bin_tools/src/devices/pm1/py32_pm1_dev.cpp               */
/*      jump_to_application() — 发送 0x77 后循环重试直到 NACK            */
/* ------------------------------------------------------------------ */
/** @brief 重试发送跳转命令，直到引导加载程序释放总线 / Retry the jump command until the bootloader releases the bus. */
static inline esp_err_t iap_jump_with_retry(faces_iap_bus_handle_t bus, faces_iap_dev_handle_t boot_dev)
{
    static const char *JTAG = "IAP_JUMP";

    /* 等待 bootloader 就绪（最后一个 chunk 的 flash 写入可能仍在进行） */
    {
        const int64_t ready_deadline = esp_timer_get_time() + 5000LL * 1000;
        while (iap_probe(bus, FACES_IAP_BOOT_ADDR, 100) != ESP_OK) {
            if (esp_timer_get_time() > ready_deadline) {
                ESP_LOGW(JTAG, "Bootloader unresponsive before jump, sending anyway");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    /* 发送 jump 命令 */
    uint8_t jump_cmd = FACES_IAP_OPC_USRCD;
    ESP_LOGI(JTAG, "Sending jump-to-app command (0x%02X)", jump_cmd);
    esp_err_t ret = iap_write(boot_dev, &jump_cmd, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(JTAG, "Jump command failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 重试循环: PY32 bootloader 可能需要多次 jump 命令 */
    const int64_t deadline_us = esp_timer_get_time() + 5000LL * 1000;
    while (esp_timer_get_time() < deadline_us) {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (iap_probe(bus, FACES_IAP_BOOT_ADDR, 100) != ESP_OK) {
            ESP_LOGI(JTAG, "Bootloader released bus, application should be running");
            return ESP_OK;
        }
        ESP_LOGD(JTAG, "Bootloader still active, retrying jump command...");
        iap_write(boot_dev, &jump_cmd, 1);
    }

    ESP_LOGW(JTAG, "Bootloader still responding after jump retries");
    return ESP_ERR_TIMEOUT;
}

/** @brief 等待设备重启后重新出现在应用地址 / Wait for the device to reappear at its app address after reboot. */
static inline esp_err_t iap_wait_app(faces_iap_bus_handle_t bus, uint8_t app_addr, int64_t timeout_ms)
{
    int64_t deadline_us = esp_timer_get_time() + timeout_ms * 1000LL;
    while (esp_timer_get_time() < deadline_us) {
        esp_err_t r = iap_probe(bus, app_addr, 100);
        if (r == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(FACES_IAP_POLL_INTERVAL_MS));
    }
    return ESP_ERR_TIMEOUT;
}
