/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "M5FacesBase.hpp"

#include <algorithm>

#if !defined(ARDUINO) && __has_include("neopixel.hpp")
#include "neopixel.hpp"
#define M5FACES_HAS_NEOPIXEL 1
#else
#define M5FACES_HAS_NEOPIXEL 0
#endif

#ifdef ARDUINO
#include <Arduino.h>
#define M5FACES_DELAY_MS(ms) delay(ms)
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#define M5FACES_DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#endif

/* 调试开关：打印每一次 Direct 模式 reg 0x00 的真实 10 字节读取结果。
 * 这层日志早于队列分类、事件合并和 UI 显示，可用于判断是否在更底层就已丢帧。
 * 注意：当前问题定位与判定规则均基于“纯寄存器轮询模式”验证；
 * 后续若启用 GPIO/IRQ 中断路径，需要单独重新校验时序与分类逻辑。 */
#define M5FACES_DIRECT_TRACE_EVERY_READ 0

#if M5FACES_DIRECT_TRACE_EVERY_READ
static uint32_t s_direct_trace_seq = 0;

/** @brief 输出一次 Direct 原始帧调试日志 / Log one raw Direct frame for diagnostics. */
static void m5faces_direct_trace_log(const char *kind, const uint8_t raw[10])
{
    M5FACES_LOG_I("Direct read#%lu %-7s [%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X]",
                  (unsigned long)++s_direct_trace_seq, kind ? kind : "?", raw[0], raw[1], raw[2], raw[3], raw[4],
                  raw[5], raw[6], raw[7], raw[8], raw[9]);
}
#endif

// ============================================================
// begin() — Arduino
// ============================================================
#ifdef ARDUINO

#if M5FACES_HAS_M5UNIFIED_I2C
/** @brief 使用 M5Unified 内部 I2C 初始化 Arduino 驱动 / Initialize the Arduino driver with M5Unified internal I2C. */
m5faces_err_t M5FacesBase::begin(m5::I2C_Class *i2c, uint8_t addr, uint32_t freq)
{
    if (!i2c) return M5FACES_ERR_INVALID;
    _m5i2c       = i2c;
    _addr        = addr;
    _freq        = freq;
    _i2c_mode    = I2CMode::M5UNIFIED;
    _initialized = true;
    return _afterBegin();
}
#endif  // M5FACES_HAS_M5UNIFIED_I2C

/** @brief 使用 Arduino TwoWire 初始化驱动 / Initialize the driver with Arduino TwoWire. */
m5faces_err_t M5FacesBase::begin(TwoWire *wire, uint8_t addr, uint32_t freq)
{
    if (!wire) return M5FACES_ERR_INVALID;
    _wire = wire;
    _addr = addr;
    _freq = freq;
    wire->begin();
    wire->setClock(freq);
    _i2c_mode    = I2CMode::WIRE;
    _initialized = true;
    return _afterBegin();
}

// ============================================================
// begin() — ESP-IDF
// ============================================================
#else  // ESP-IDF

#if M5FACES_HAS_M5UNIFIED_I2C
/** @brief 使用 M5Unified 内部 I2C 初始化 ESP-IDF 驱动 / Initialize the ESP-IDF driver with M5Unified internal I2C. */
m5faces_err_t M5FacesBase::begin(m5::I2C_Class *i2c, uint8_t addr, uint32_t freq)
{
    if (!i2c) return M5FACES_ERR_INVALID;
    _m5i2c       = i2c;
    _addr        = addr;
    _freq        = freq;
    _i2c_mode    = I2CMode::M5UNIFIED;
    _initialized = true;
    return _afterBegin();
}
#endif  // M5FACES_HAS_M5UNIFIED_I2C

/** @brief 使用外部 ESP-IDF I2C 设备句柄初始化驱动 / Initialize with an externally owned ESP-IDF I2C device handle. */
m5faces_err_t M5FacesBase::begin(i2c_master_dev_handle_t dev_handle)
{
#if !M5FACES_HAS_IDF_I2C_MASTER
    (void)dev_handle;
    return M5FACES_FAIL;
#else
    if (!dev_handle) return M5FACES_ERR_INVALID;
    _idf_dev     = dev_handle;
    _i2c_mode    = I2CMode::IDF_MASTER;
    _initialized = true;
    return _afterBegin();
#endif
}

/** @brief 使用 ESP-IDF I2C 总线并创建内部设备句柄 / Initialize from an ESP-IDF I2C bus and create an owned device
 * handle. */
m5faces_err_t M5FacesBase::begin(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t freq)
{
#if !M5FACES_HAS_IDF_I2C_MASTER
    (void)bus;
    (void)addr;
    (void)freq;
    return M5FACES_FAIL;
#else
    if (!bus) return M5FACES_ERR_INVALID;

    // 如果已存在旧设备句柄，先移除 / Remove old device handle if present
    if (_idf_dev && _i2c_mode == I2CMode::IDF_MASTER_BUS) {
        i2c_master_bus_rm_device(_idf_dev);
        _idf_dev = nullptr;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address      = addr;
    dev_cfg.scl_speed_hz        = freq;

    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
        return M5FACES_ERR_I2C_COMM;
    }

    _idf_bus     = bus;
    _idf_dev     = dev;
    _addr        = addr;
    _freq        = freq;
    _i2c_mode    = I2CMode::IDF_MASTER_BUS;
    _initialized = true;
    return _afterBegin();
#endif
}

#endif  // ARDUINO

/** @brief 释放内部创建的 I2C 与 RGB 资源 / Release internally owned I2C and RGB resources. */
M5FacesBase::~M5FacesBase()
{
#ifndef ARDUINO
// IDF_MASTER_BUS 模式：释放内部创建的设备句柄
// IDF_MASTER_BUS mode: release internally created device handle
#if M5FACES_HAS_IDF_I2C_MASTER
    if (_i2c_mode == I2CMode::IDF_MASTER_BUS && _idf_dev) {
        i2c_master_bus_rm_device(_idf_dev);
        _idf_dev = nullptr;
    }
#endif
#if M5FACES_HAS_NEOPIXEL
    if (_led_strip != nullptr) {
        delete static_cast<espp::Neopixel *>(_led_strip);
        _led_strip = nullptr;
    }
#endif
#endif
}

// ============================================================
// _afterBegin — 型号验证（子类触发）/ Model ID verification (triggered by subclass)
// ============================================================
m5faces_err_t M5FacesBase::_afterBegin()
{
    if (_expected_model_id == 0) return M5FACES_OK;  // 跳过验证 / skip
    if (isModelMatch(_expected_model_id)) return M5FACES_OK;

#ifndef ARDUINO
    // 型号不匹配时释放内部注册的设备句柄；外部句柄仍由调用者管理。
    // Release internally registered device handles on mismatch; callers retain external handles.
    if (_i2c_mode == I2CMode::IDF_MASTER_BUS && _idf_dev) {
        i2c_master_bus_rm_device(_idf_dev);
    }
    _idf_dev = nullptr;
    _idf_bus = nullptr;
#endif
#ifdef ARDUINO
    _wire = nullptr;
#endif
#if M5FACES_HAS_M5UNIFIED_I2C
    _m5i2c = nullptr;
#endif
    _initialized = false;
    _i2c_mode    = I2CMode::NONE;
    return M5FACES_ERR_MISMATCH;
}

// ============================================================
// readReg — 读取一个或多个设备寄存器 / Read one or more device registers
// ============================================================
m5faces_err_t M5FacesBase::readReg(uint8_t reg, uint8_t *data, size_t len)
{
    if (!_initialized) return M5FACES_ERR_NOT_INIT;
    if (!data || len == 0) return M5FACES_ERR_INVALID;

    bool ok = false;
    switch (_i2c_mode) {
#ifdef ARDUINO
        case I2CMode::WIRE:
            ok = m5faces_wire_read_bytes(_wire, _addr, reg, data, len);
            break;
#endif
#if M5FACES_HAS_M5UNIFIED_I2C
        case I2CMode::M5UNIFIED:
            ok = m5faces_m5i2c_read_bytes(_m5i2c, _addr, reg, data, len, _freq);
            break;
#endif
#ifndef ARDUINO
        case I2CMode::IDF_MASTER:
        case I2CMode::IDF_MASTER_BUS:
            ok = m5faces_idf_read_bytes(_idf_dev, reg, data, len);
            break;
#endif
        default:
            return M5FACES_ERR_NOT_INIT;
    }
    return ok ? M5FACES_OK : M5FACES_ERR_I2C_COMM;
}

// ============================================================
// writeReg — 写入一个或多个设备寄存器 / Write one or more device registers
// ============================================================
m5faces_err_t M5FacesBase::writeReg(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!_initialized) return M5FACES_ERR_NOT_INIT;
    if (!data || len == 0) return M5FACES_ERR_INVALID;

    bool ok = false;
    switch (_i2c_mode) {
#ifdef ARDUINO
        case I2CMode::WIRE:
            ok = m5faces_wire_write_bytes(_wire, _addr, reg, data, len);
            break;
#endif
#if M5FACES_HAS_M5UNIFIED_I2C
        case I2CMode::M5UNIFIED:
            ok = m5faces_m5i2c_write_bytes(_m5i2c, _addr, reg, data, len, _freq);
            break;
#endif
#ifndef ARDUINO
        case I2CMode::IDF_MASTER:
        case I2CMode::IDF_MASTER_BUS:
            ok = m5faces_idf_write_bytes(_idf_dev, reg, data, len);
            break;
#endif
        default:
            return M5FACES_ERR_NOT_INIT;
    }
    return ok ? M5FACES_OK : M5FACES_ERR_I2C_COMM;
}

// ============================================================
// ISR 处理函数 / ISR handler
// ============================================================
#ifdef ARDUINO
void IRAM_ATTR M5FacesBase::_arduino_isr(void *arg)
{
    static_cast<M5FacesBase *>(arg)->_int_flag = true;
}
#else
/** @brief ESP-IDF GPIO 中断回调，仅置位内部标志 / ESP-IDF GPIO ISR callback that only sets the internal flag. */
void IRAM_ATTR M5FacesBase::_gpio_isr_handler(void *arg)
{
    static_cast<M5FacesBase *>(arg)->_int_flag = true;
}
#endif

// ============================================================
// setInterruptPin — 配置 INT/IRQ 中断引脚
//                   Configure INT/IRQ interrupt pin
// ============================================================
#ifdef ARDUINO
m5faces_err_t M5FacesBase::setInterruptPin(uint8_t pin, bool use_interrupt)
{
    // 如曾注册过中断，先注销旧 handler / Remove old ISR if previously installed
    if (_int_mode && _int_pin >= 0) {
        detachInterrupt(digitalPinToInterrupt((uint8_t)_int_pin));
    }
    pinMode(pin, INPUT_PULLUP);
    _int_pin  = (int)pin;
    _int_mode = use_interrupt;
    _int_flag = false;
    if (use_interrupt) {
        attachInterruptArg(digitalPinToInterrupt(pin), _arduino_isr, this, FALLING);
    }
    return M5FACES_OK;
}
#else
/** @brief 配置 ESP-IDF INT 引脚及可选边沿中断 / Configure the ESP-IDF INT pin and optional edge interrupt. */
m5faces_err_t M5FacesBase::setInterruptPin(gpio_num_t pin, bool use_interrupt)
{
    if (pin < GPIO_NUM_0 || pin >= GPIO_NUM_MAX) return M5FACES_ERR_INVALID;

    // 如曾安装中断，先注销旧 handler / Remove old ISR handler if previously set
    if (_int_mode && _int_pin >= 0) {
        gpio_isr_handler_remove((gpio_num_t)_int_pin);
    }

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask  = (1ULL << pin);
    io_conf.mode          = GPIO_MODE_INPUT;
    io_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    // 中断模式：下降沿触发；轮询模式：不配置 IRQ
    // Interrupt mode: falling edge; polling mode: no IRQ
    io_conf.intr_type = use_interrupt ? GPIO_INTR_NEGEDGE : GPIO_INTR_DISABLE;
    if (gpio_config(&io_conf) != ESP_OK) return M5FACES_ERR_INVALID;

    _int_pin  = (int)pin;
    _int_mode = use_interrupt;
    _int_flag = false;

    if (use_interrupt) {
        // 安装 GPIO ISR 服务（已安装返回 ESP_ERR_INVALID_STATE，可忽略）
        // Install GPIO ISR service (ESP_ERR_INVALID_STATE = already installed, safe to ignore)
        esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
            return M5FACES_ERR_INVALID;
        }
        if (gpio_isr_handler_add(pin, _gpio_isr_handler, this) != ESP_OK) {
            return M5FACES_ERR_INVALID;
        }
    }
    return M5FACES_OK;
}
#endif

// ============================================================
// isInterruptLow — INT 引脚是否为低电平（有新数据）
//                  Check if INT pin is LOW (new data available)
// ============================================================
bool M5FacesBase::isInterruptLow() const
{
    if (_int_pin < 0) return true;  // 未配置时退化为盲轮询 / fallback
#ifdef ARDUINO
    return digitalRead(_int_pin) == LOW;
#else
    return gpio_get_level((gpio_num_t)_int_pin) == 0;
#endif
}

// ============================================================
// update — Normal 模式按键轮询（支持中断模式 + 轮询模式 + Enter 键特殊处理 §6.2）
//          Normal mode key polling (interrupt/polling mode + Enter key handling §6.2)
// ============================================================
bool M5FacesBase::update()
{
    if (!_initialized) return false;
    _key_changed = false;

    if (_int_pin >= 0) {
        if (_int_mode) {
            // 中断模式：检查 ISR 标志 / Interrupt mode: check ISR flag
            if (!_int_flag) return false;
            _int_flag = false;  // 先清除标志再读 I2C，防止遗漏后续中断
                                // Clear flag before I2C read to avoid missing next IRQ
        } else {
            // 轮询模式：检查 INT 引脚电平 / Polling mode: check INT pin level
            if (!isInterruptLow()) return false;
        }
    }
    // _int_pin < 0: 盲轮询 / blind polling (no INT pin)

    uint8_t key = 0;
    if (readReg(M5FACES_REG_KEY, &key) != M5FACES_OK) return false;

    // Enter 键特殊处理 (§6.2)：按一次产生 1 次 IRQ，但固件输出 0x0D 后跟 0x0A，
    // 主机需连续读取两次，两次读取完成后 IRQ 才清除。
    // Enter key (§6.2): one press = 1 IRQ, firmware outputs 0x0D then 0x0A.
    // Host must read twice consecutively; IRQ clears only after both reads.
    if (key == 0x0D) {
        uint8_t lf = 0;
        readReg(M5FACES_REG_KEY, &lf);  // consume 0x0A to release IRQ
    }

    _key_prev    = _key_raw;
    _key_raw     = key;
    _key_changed = (_key_raw != _key_prev);
    return _key_changed;
}

// ============================================================
// updateDirect — Direct 模式 10 字节数据包读取与解析 (§7)
//                Direct mode 10-byte packet read & parse (§7)
// ============================================================
m5faces_err_t M5FacesBase::updateDirect(m5faces_direct_data_t *data)
{
    if (!data) return M5FACES_ERR_INVALID;
    if (!_initialized) return M5FACES_ERR_NOT_INIT;

    // 从 reg 0x00 连续读取 10 字节
    m5faces_err_t ret = readReg(M5FACES_REG_KEY, data->raw, 10);
    if (ret != M5FACES_OK) return ret;

    // Byte 0 应为 0x0A（固定长度 10）
    if (data->raw[0] != 0x0A) {
        data->valid = false;
        // 全零 = 无待读事件（轮询模式常态）；非零非 0x0A = 数据损坏
        // All-zero = no pending event (normal in polling mode); non-zero non-0x0A = corrupt
        bool all_zero = true;
        for (int i = 0; i < 10; i++) {
            if (data->raw[i] != 0) {
                all_zero = false;
                break;
            }
        }
#if M5FACES_DIRECT_TRACE_EVERY_READ
        m5faces_direct_trace_log(all_zero ? "NO_DATA" : "CORRUPT", data->raw);
#endif
        return all_zero ? M5FACES_NO_DATA : M5FACES_FAIL;
    }

    // 解析 3 组矩阵行 (Bytes 1-6)
    for (int i = 0; i < 3; i++) {
        uint8_t hi              = data->raw[1 + i * 2];
        uint8_t lo              = data->raw[2 + i * 2];
        data->rows[i].changed   = (hi & 0x80) != 0;
        data->rows[i].row_index = (hi >> 4) & 0x07;
        data->rows[i].key_bits  = (uint16_t)((hi & 0x03) << 8) | lo;
    }

    // 解析修饰键行 (Bytes 7-8)
    uint8_t mhi            = data->raw[7];
    uint8_t mlo            = data->raw[8];
    data->modifier.changed = (mhi & 0x80) != 0;
    uint16_t mod_raw       = (uint16_t)((mhi & 0x03) << 8) | mlo;
    data->modifier.aA      = ((mod_raw & 0x01) == 0);  // active-low: 0 = pressed
    data->modifier.alt     = ((mod_raw & 0x02) == 0);
    data->modifier.enter   = ((mod_raw & 0x04) == 0);
    data->modifier.sym     = ((mod_raw & 0x08) == 0);
    data->modifier.fn      = ((mod_raw & 0x10) == 0);

    // Byte 9 是前 9 字节累加和的二补数，因此完整 10 字节的低 8 位累加和应为 0。
    // Byte 9 is the two's complement of bytes 0-8, so the 8-bit sum of all 10 bytes must be zero.
    data->checksum = data->raw[9];
    uint8_t sum    = 0;
    for (int i = 0; i < 9; i++) sum += data->raw[i];
    data->valid = (uint8_t)(sum + data->checksum) == 0;

#if M5FACES_DIRECT_TRACE_EVERY_READ
    m5faces_direct_trace_log(data->valid ? "FRAME" : "CRC_NG", data->raw);
#endif

    return M5FACES_OK;
}

/* 判断 Direct 帧中是否有键按下。/ Check whether a Direct frame contains pressed keys. */
bool M5FacesBase::_directHasKeys(const m5faces_direct_data_t &data) const
{
    for (int i = 0; i < 3; i++) {
        if (data.rows[i].key_bits != 0x3FFU) return true;
    }
    return data.modifier.aA || data.modifier.alt || data.modifier.enter || data.modifier.sym || data.modifier.fn;
}

/* 检查任一矩阵行或修饰键行的变化标志。/ Check change flags in matrix and modifier rows. */
bool M5FacesBase::_directHasChangedBits(const m5faces_direct_data_t &data) const
{
    return data.rows[0].changed || data.rows[1].changed || data.rows[2].changed || data.modifier.changed;
}

/* 清空 Direct 状态机和 FIFO。/ Clear the Direct state machine and FIFO. */
void M5FacesBase::_directResetState()
{
    _direct_poll_active               = false;
    _direct_poll_prev_data            = {};
    _direct_poll_hold_cnt             = 0;
    _direct_poll_last_event_prev_data = {};
    _direct_poll_last_event_hold_cnt  = 0;
    _direct_queue_head                = 0;
    _direct_queue_count               = 0;
}

/* 将事件及其关联状态压入 FIFO。/ Push an event and its associated state into the FIFO. */
bool M5FacesBase::_directQueuePush(m5faces_direct_event_t evt, const m5faces_direct_data_t &data, uint32_t hold_cnt,
                                   const m5faces_direct_data_t &prev_data)
{
    if (_direct_queue_count >= M5FACES_DIRECT_QUEUE_CAP) {
        M5FACES_LOG_W("Direct queue full, drop evt=%d", (int)evt);
        return false;
    }

    uint8_t tail                  = (uint8_t)((_direct_queue_head + _direct_queue_count) % M5FACES_DIRECT_QUEUE_CAP);
    _direct_queue[tail].evt       = evt;
    _direct_queue[tail].data      = data;
    _direct_queue[tail].prev_data = prev_data;
    _direct_queue[tail].hold_cnt  = hold_cnt;
    _direct_queue_count++;
    return true;
}

/* 弹出最早的 Direct 事件。/ Pop the oldest Direct event. */
bool M5FacesBase::_directQueuePop(m5faces_direct_event_t *evt, m5faces_direct_data_t *data)
{
    if (!evt || !data || _direct_queue_count == 0) {
        return false;
    }

    const direct_queue_item_t &item   = _direct_queue[_direct_queue_head];
    *evt                              = item.evt;
    *data                             = item.data;
    _direct_poll_last_event_prev_data = item.prev_data;
    _direct_poll_last_event_hold_cnt  = item.hold_cnt;

    _direct_queue_head = (uint8_t)((_direct_queue_head + 1) % M5FACES_DIRECT_QUEUE_CAP);
    _direct_queue_count--;
    return true;
}

/* 根据当前帧和历史状态生成事件。/ Generate events from the current frame and previous state. */
void M5FacesBase::_directClassifyAndQueue(const m5faces_direct_data_t &data)
{
    /* 当前 Direct 帧分类策略基于纯寄存器轮询模式：
     * - 主循环固定频率直接读取 reg 0x00
     * - 固件返回的 changed 位并不总是可靠，因此以“当前按键状态 + raw 是否变化”为主
     * - 尤其是全释放帧，即便 changed 未置位，只要相对上一活动帧发生变化，仍视为 RELEASED
     *
     * 这套规则目前不自动外推到 GPIO/IRQ 中断路径；
     * 中断模式后续需要结合 IRQ 拉低/清除时机重新单独完善。 */
    const bool has_keys    = _directHasKeys(data);
    const bool any_changed = _directHasChangedBits(data);
    const bool raw_changed = (memcmp(data.raw, _direct_poll_prev_data.raw, sizeof(data.raw)) != 0);

    if (has_keys) {
        if (!_direct_poll_active) {
            _direct_poll_active    = true;
            _direct_poll_prev_data = data;
            _direct_poll_hold_cnt  = 1;
            (void)_directQueuePush(M5FACES_DIRECT_PRESSED, data, _direct_poll_hold_cnt, data);
        } else if (raw_changed) {
            m5faces_direct_data_t prev = _direct_poll_prev_data;
            _direct_poll_prev_data     = data;
            _direct_poll_hold_cnt      = 1;
            (void)_directQueuePush(M5FACES_DIRECT_CHANGED, data, _direct_poll_hold_cnt, prev);
        } else {
            _direct_poll_hold_cnt++;
        }
        return;
    }

    if (_direct_poll_active && (any_changed || raw_changed)) {
        const m5faces_direct_data_t prev = _direct_poll_prev_data;
        const uint32_t hold_cnt          = _direct_poll_hold_cnt;
        _direct_poll_active              = false;
        _direct_poll_hold_cnt            = 0;
        (void)_directQueuePush(M5FACES_DIRECT_RELEASED, data, hold_cnt, prev);
        return;
    }

    if (_direct_poll_active) {
        _direct_poll_hold_cnt++;
    }
}

/* 批量读取待处理帧并填充 FIFO。/ Read pending frames in a batch and fill the FIFO. */
void M5FacesBase::_directFillQueue(uint8_t max_reads)
{
    for (uint8_t reads = 0; reads < max_reads && _direct_queue_count < M5FACES_DIRECT_QUEUE_CAP; reads++) {
        m5faces_direct_data_t frame = {};
        m5faces_err_t ret           = updateDirect(&frame);
        if (ret == M5FACES_OK) {
            // 校验失败的帧仅保留给 updateDirect() 调用者诊断，不进入事件队列。
            // Keep a failed frame available to updateDirect() diagnostics, but never enqueue it as an input event.
            if (!frame.valid) {
                M5FACES_LOG_W("Direct frame checksum invalid, event dropped");
                continue;
            }
            _directClassifyAndQueue(frame);
            continue;
        }
        if (ret == M5FACES_NO_DATA) {
            break;
        }
        break;
    }
}

// ============================================================
// pollDirect — Direct 模式轮询专用接口（当前为纯寄存器轮询路径）
//              Polling-specific Direct mode interface (currently tuned for register-polling path)
//
// PY32 固件行为（事件驱动）：
//   每次按键状态变化（按下/释放）→ 生成 1 帧 0x0A 数据 → INT 拉低
//   主机 I2C 读取 → 帧被消耗 → 寄存器清零 → INT 释放
//   后续读取全部返回 0x00 直到下一个事件
//
// 帧类型区分 / Frame types:
//   按下帧: 0x0A, key_bits 有 0（按下的键位），至少 1 行 changed=1
//   释放帧: 0x0A, key_bits 全 0x3FF（无按键），至少 1 行 changed=1
//   空闲探测帧: 0x0A, key_bits 全 0x3FF，所有 changed=0（周期性心跳，非按键事件）
//   无事件: 全零 0x00（两个事件之间的常态读取）
//
// PY32 firmware behavior (event-driven):
//   Each key state change (press/release) → generates one 0x0A frame → INT LOW
//   Host I2C read → frame consumed → register cleared → INT released
//   Subsequent reads return 0x00 until next event
//
// Frame type discrimination:
//   Press frame: 0x0A, some key_bits=0 (pressed), at least 1 row changed=1
//   Release frame: 0x0A, all key_bits=0x3FF (idle), at least 1 row changed=1
//   Idle probe: 0x0A, all key_bits=0x3FF, all changed=0 (periodic heartbeat)
//   No event: all-zero (normal between events)
//
// 注意 / Note:
//   当前实现与现场验证结论仅针对“纯寄存器轮询模式”成立。
//   GPIO/IRQ 中断模式保留现有接口，但其时序和事件判定后续需要继续完善。
// ============================================================
m5faces_direct_event_t M5FacesBase::pollDirect(m5faces_direct_data_t *data)
{
    if (!data) return M5FACES_DIRECT_IDLE;

    m5faces_direct_event_t evt = M5FACES_DIRECT_IDLE;

    if (_directQueuePop(&evt, data)) {
        return evt;
    }

    _directFillQueue();

    if (_directQueuePop(&evt, data)) {
        return evt;
    }

    *data                             = {};
    _direct_poll_last_event_prev_data = _direct_poll_prev_data;
    if (_direct_poll_active) {
        _direct_poll_last_event_hold_cnt = ++_direct_poll_hold_cnt;
        return M5FACES_DIRECT_HELD;
    }

    _direct_poll_last_event_hold_cnt = 0;
    return M5FACES_DIRECT_IDLE;
}

// ============================================================
// getModelID — 读取设备型号标识 / Read the device model identifier
// ============================================================
m5faces_err_t M5FacesBase::getModelID(uint8_t *model_id)
{
    if (!model_id) return M5FACES_ERR_INVALID;
    return readReg(M5FACES_REG_MODEL_ID, model_id);
}

// ============================================================
// getDeviceUID — 读取 12 字节设备 UID / Read the 12-byte device UID
// ============================================================
m5faces_err_t M5FacesBase::getDeviceUID(uint8_t *uid)
{
    if (!uid) return M5FACES_ERR_INVALID;
    return readReg(M5FACES_REG_DEVICE_UID, uid, M5FACES_DEVICE_UID_SIZE);
}

// ============================================================
// isModelMatch — 校验设备型号是否符合预期 / Verify that the device model matches the expectation
// ============================================================
bool M5FacesBase::isModelMatch(uint8_t expected_id)
{
    uint8_t id = 0;
    if (getModelID(&id) != M5FACES_OK) return false;
    return id == expected_id;
}

// ============================================================
// setMode — 写入 0xF0 切换 Normal / Direct
//           Write 0xF0 to switch Normal / Direct mode
// ============================================================
m5faces_err_t M5FacesBase::setMode(m5faces_mode_t mode)
{
    uint8_t val       = static_cast<uint8_t>(mode);
    m5faces_err_t ret = writeReg(M5FACES_REG_MODE, &val, 1);
    if (ret == M5FACES_OK) {
        _directResetState();
    }
    return ret;
}

// ============================================================
// getMode — 读取 0xF0 当前工作模式
//           Read 0xF0 for current operation mode
// ============================================================
m5faces_err_t M5FacesBase::getMode(m5faces_mode_t *mode)
{
    if (!mode) return M5FACES_ERR_INVALID;
    uint8_t val       = 0;
    m5faces_err_t ret = readReg(M5FACES_REG_MODE, &val, 1);
    if (ret == M5FACES_OK) {
        *mode = static_cast<m5faces_mode_t>(val);
    }
    return ret;
}

// ============================================================
// setLED — 写入 0xF1 设置 LED 灯效
//          Write 0xF1 to set LED effect
// ============================================================
m5faces_err_t M5FacesBase::setLED(uint8_t led_mode)
{
    return writeReg(M5FACES_REG_LED, &led_mode, 1);
}

// ============================================================
// getLED — 读取 0xF1 当前 LED 灯效设置
//          Read 0xF1 for current LED effect
// ============================================================
m5faces_err_t M5FacesBase::getLED(uint8_t *led_mode)
{
    if (!led_mode) return M5FACES_ERR_INVALID;
    return readReg(M5FACES_REG_LED, led_mode, 1);
}

// ============================================================
// 中间转接板功能 / Bottom3 Base Board Features
// ============================================================
/** @brief 初始化主机侧 RGB 灯带对象 / Initialize the host-side RGB strip object. */
void M5FacesBase::initRGB(int pin, size_t num_pixels)
{
    M5FACES_LOG_I("Initializing SK6812 RGB LED strip on GPIO %d, %u pixels", pin, (unsigned int)num_pixels);

#if !defined(ARDUINO) && M5FACES_HAS_NEOPIXEL
    // 使用 espp::Neopixel 进行初始化
    if (_led_strip != nullptr) {
        delete static_cast<espp::Neopixel *>(_led_strip);
    }

    espp::Neopixel::Config config;
    config.data_gpio = pin;
    config.num_leds  = num_pixels;
    config.log_level = espp::Logger::Verbosity::WARN;
    _led_strip       = new espp::Neopixel(config);
    M5FACES_LOG_I("_led_strip = %p (Neopixel created)", _led_strip);
#else
    (void)pin;
    (void)num_pixels;
    M5FACES_LOG_W("initRGB skipped: neopixel support is not enabled in this build");
#endif
}

/** @brief 更新一个 RGB 灯珠的缓存颜色 / Update the buffered color of one RGB pixel. */
void M5FacesBase::setPixelColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
#if !defined(ARDUINO) && M5FACES_HAS_NEOPIXEL
    if (_led_strip) {
        auto *strip = static_cast<espp::Neopixel *>(_led_strip);
        if (index < strip->num_leds()) {
            strip->set_color(espp::Rgb((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f), index);
        }
    }
#else
    (void)index;
    (void)r;
    (void)g;
    (void)b;
#endif
}

/** @brief 批量更新 RGB 灯珠缓存 / Update multiple buffered RGB pixel colors. */
void M5FacesBase::setPixels(const uint8_t *colors, size_t count)
{
#if !defined(ARDUINO) && M5FACES_HAS_NEOPIXEL
    if (_led_strip && colors) {
        auto *strip      = static_cast<espp::Neopixel *>(_led_strip);
        size_t max_count = std::min(count, strip->num_leds());
        for (size_t i = 0; i < max_count; i++) {
            uint8_t r = colors[i * 3 + 0];
            uint8_t g = colors[i * 3 + 1];
            uint8_t b = colors[i * 3 + 2];
            strip->set_color(espp::Rgb((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f), i);
        }
    }
#else
    (void)colors;
    (void)count;
#endif
}

/** @brief 更新固定八颗 RGB 灯珠的缓存 / Update the buffered colors of exactly eight RGB pixels. */
void M5FacesBase::setPixels8(const uint8_t colors[24])
{
    setPixels(colors, 8);
}

/** @brief 将缓存颜色发送到 RGB 灯带 / Send buffered colors to the RGB strip. */
void M5FacesBase::showRGB()
{
#if !defined(ARDUINO) && M5FACES_HAS_NEOPIXEL
    if (_led_strip) {
        static_cast<espp::Neopixel *>(_led_strip)->show();
    } else {
        static bool warned = false;
        if (!warned) {
            M5FACES_LOG_W("showRGB: _led_strip is NULL!");
            warned = true;
        }
    }
#endif
}

// ============================================================
// getFirmwareVersion — 读取 0xFE 固件版本号（只读）
//                      Read 0xFE firmware version (read-only)
// ============================================================
m5faces_err_t M5FacesBase::getFirmwareVersion(uint8_t *version)
{
    if (!version) return M5FACES_ERR_INVALID;
    return readReg(M5FACES_REG_FW_VERSION, version, 1);
}

// ============================================================
// setI2CAddress — 写入 0xFF 修改 I2C 地址
//                 Write 0xFF to change I2C address
// ============================================================
m5faces_err_t M5FacesBase::setI2CAddress(uint8_t new_addr)
{
    if (new_addr < 0x08 || new_addr > 0x77) return M5FACES_ERR_INVALID;
#ifndef ARDUINO
    if (_i2c_mode == I2CMode::IDF_MASTER) {
        M5FACES_LOG_W("setI2CAddress: unsupported with an externally owned dev_handle");
        return M5FACES_FAIL;
    }
#endif
    m5faces_err_t ret = writeReg(M5FACES_REG_I2C_ADDR, &new_addr, 1);
    if (ret == M5FACES_OK) {
        M5FACES_DELAY_MS(20);  // 等待模块完成地址切换
#ifndef ARDUINO
        if (_i2c_mode == I2CMode::IDF_MASTER_BUS) {
            if (_idf_dev) {
                i2c_master_bus_rm_device(_idf_dev);
                _idf_dev = nullptr;
            }
            i2c_device_config_t dev_cfg = {};
            dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
            dev_cfg.device_address      = new_addr;
            dev_cfg.scl_speed_hz        = _freq;
            if (i2c_master_bus_add_device(_idf_bus, &dev_cfg, &_idf_dev) != ESP_OK) {
                _initialized = false;
                M5FACES_LOG_E("setI2CAddress: cannot register new address 0x%02X", new_addr);
                return M5FACES_ERR_I2C_COMM;
            }
        }
#endif
        _addr = new_addr;  // 同步内部地址
    }
    return ret;
}

// ============================================================
// getI2CAddress — 读取 0xFF 当前 I2C 地址
//                 Read 0xFF for current I2C address
// ============================================================
m5faces_err_t M5FacesBase::getI2CAddress(uint8_t *addr)
{
    if (!addr) return M5FACES_ERR_INVALID;
    return readReg(M5FACES_REG_I2C_ADDR, addr, 1);
}

// ============================================================
// getModeLED — 批量读取 0xF0 + 0xF1 (§5.2 段A, 2 bytes)
//              Batch read 0xF0 + 0xF1 (§5.2 Segment A, 2 bytes)
// ============================================================
m5faces_err_t M5FacesBase::getModeLED(m5faces_mode_t *mode, uint8_t *led_mode)
{
    if (!mode || !led_mode) return M5FACES_ERR_INVALID;
    uint8_t buf[2]    = {0};
    m5faces_err_t ret = readReg(M5FACES_REG_MODE, buf, 2);
    if (ret == M5FACES_OK) {
        *mode     = static_cast<m5faces_mode_t>(buf[0]);
        *led_mode = buf[1];
    }
    return ret;
}

// ============================================================
// getVersionAddr — 批量读取 0xFE + 0xFF (§5.2 段B, 2 bytes)
//                  Batch read 0xFE + 0xFF (§5.2 Segment B, 2 bytes)
// ============================================================
m5faces_err_t M5FacesBase::getVersionAddr(uint8_t *version, uint8_t *addr)
{
    if (!version || !addr) return M5FACES_ERR_INVALID;
    uint8_t buf[2]    = {0};
    m5faces_err_t ret = readReg(M5FACES_REG_FW_VERSION, buf, 2);
    if (ret == M5FACES_OK) {
        *version = buf[0];
        *addr    = buf[1];
    }
    return ret;
}

// ============================================================
// setI2CFreq — 运行时切换 I2C 总线频率（100 kHz ↔ 400 kHz）
//              Switch I2C bus frequency at runtime (100 kHz ↔ 400 kHz)
// ============================================================
m5faces_err_t M5FacesBase::setI2CFreq(uint32_t freq)
{
    if (!_initialized) return M5FACES_ERR_NOT_INIT;

    switch (_i2c_mode) {
#ifdef ARDUINO
        case I2CMode::WIRE:
            _wire->setClock(freq);
            _freq = freq;
            return M5FACES_OK;
#endif
#if M5FACES_HAS_M5UNIFIED_I2C
        case I2CMode::M5UNIFIED:
            // M5Unified 每次事务都传入 _freq，直接更新即可
            // M5Unified passes _freq on every transaction; just update it.
            _freq = freq;
            return M5FACES_OK;
#endif
#ifndef ARDUINO
        case I2CMode::IDF_MASTER_BUS: {
#if !M5FACES_HAS_IDF_I2C_MASTER
            return M5FACES_FAIL;
#else
            // 注销旧设备，以新频率重新注册
            // Remove old device and re-register with new frequency.
            if (_idf_dev) {
                i2c_master_bus_rm_device(_idf_dev);
                _idf_dev = nullptr;
            }
            i2c_device_config_t dev_cfg = {};
            dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
            dev_cfg.device_address      = _addr;
            dev_cfg.scl_speed_hz        = freq;
            i2c_master_dev_handle_t dev = nullptr;
            if (i2c_master_bus_add_device(_idf_bus, &dev_cfg, &dev) != ESP_OK) {
                M5FACES_LOG_E("setI2CFreq: i2c_master_bus_add_device failed (freq=%lu)", (unsigned long)freq);
                return M5FACES_ERR_I2C_COMM;
            }
            _idf_dev = dev;
            _freq    = freq;
            M5FACES_LOG_I("setI2CFreq: switched to %lu Hz", (unsigned long)freq);
            return M5FACES_OK;
#endif
        }
        case I2CMode::IDF_MASTER:
            // 外部提供 dev_handle，频率已固化，不支持运行时切换
            // External dev_handle has fixed freq; runtime change not supported.
            M5FACES_LOG_W("setI2CFreq: not supported in IDF_MASTER mode (use begin(bus,addr,freq))");
            return M5FACES_FAIL;
#endif
        default:
            return M5FACES_ERR_NOT_INIT;
    }
}

// ============================================================
// modelName — 根据寄存器 0xD0 的型号 ID 返回设备名称
//             Return the device name for the model ID in register 0xD0
// ============================================================
const char *M5FacesBase::modelName(uint8_t model_id)
{
    switch (model_id) {
        case M5FACES_MODEL_CALCULATOR3:
            return "Calculator3";
        case M5FACES_MODEL_KEYBOARD3:
            return "Keyboard3";
        case M5FACES_MODEL_GAMEPAD3:
            return "Gamepad3";
        default:
            return "Unknown";
    }
}
