/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file M5FacesBase.hpp
 * @brief M5Faces Bottom3 系列基类
 *        Base class for M5Faces Bottom3 modules
 *
 * 继承体系（扁平化）/ Inheritance (flat):
 *   M5FacesBase
 *     ├── M5Faces_Calculator3  (MODEL_ID = 0x01)
 *     ├── M5Faces_Keyboard3      (MODEL_ID = 0x02)
 *     └── M5Faces_Gamepad3     (MODEL_ID = 0x03)
 *
 * 使用方法 (ESP-IDF + M5Unified) / Usage (ESP-IDF + M5Unified):
 * @code
 *   M5Faces_Calculator3 calc;
 *   calc.begin(&M5.In_I2C);   // CoreS3 内部 I2C / CoreS3 internal I2C
 *   while (true) {
 *       if (calc.update()) {
 *           printf("key: %c\n", calc.getChar());
 *       }
 *   }
 * @endcode
 */

#ifndef __M5_FACES_BASE_H__
#define __M5_FACES_BASE_H__

#include "M5FacesI2C.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "hal/gpio_types.h"  // gpio_num_t for setInterruptPin()
#include "esp_attr.h"
#endif

// ============================
// 日志宏 / Logging Macros
// ============================
// 统一日志接口：ESP-IDF 使用 esp_log, Arduino 使用 Serial.printf
// Unified logging: ESP-IDF uses esp_log, Arduino uses Serial.printf
#ifndef M5FACES_LOG_TAG
#define M5FACES_LOG_TAG "M5Faces"
#endif

#ifdef ARDUINO
#define M5FACES_LOG_E(fmt, ...) Serial.printf("[E][" M5FACES_LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
#define M5FACES_LOG_W(fmt, ...) Serial.printf("[W][" M5FACES_LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
#define M5FACES_LOG_I(fmt, ...) Serial.printf("[I][" M5FACES_LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
#define M5FACES_LOG_D(fmt, ...) Serial.printf("[D][" M5FACES_LOG_TAG "] " fmt "\n", ##__VA_ARGS__)
#else
#include "esp_log.h"
#define M5FACES_LOG_E(fmt, ...) ESP_LOGE(M5FACES_LOG_TAG, fmt, ##__VA_ARGS__)
#define M5FACES_LOG_W(fmt, ...) ESP_LOGW(M5FACES_LOG_TAG, fmt, ##__VA_ARGS__)
#define M5FACES_LOG_I(fmt, ...) ESP_LOGI(M5FACES_LOG_TAG, fmt, ##__VA_ARGS__)
#define M5FACES_LOG_D(fmt, ...) ESP_LOGD(M5FACES_LOG_TAG, fmt, ##__VA_ARGS__)
#endif

// ============================
// 错误码 / Error Codes
// ============================
typedef enum {
    M5FACES_NO_DATA      = 1,   // 无待读事件（轮询模式：全零读取）/ No pending event (polling: all-zero read)
    M5FACES_OK           = 0,   // 成功 / Success
    M5FACES_FAIL         = -1,  // 通用失败 / General failure
    M5FACES_ERR_INVALID  = -2,  // 无效参数 / Invalid argument
    M5FACES_ERR_I2C_COMM = -3,  // I2C 通信错误 / I2C communication error
    M5FACES_ERR_NOT_INIT = -4,  // 未初始化 / Not initialized
    M5FACES_ERR_MISMATCH = -5,  // 型号不匹配 / Model ID mismatch
} m5faces_err_t;

// ============================
// Direct 模式轮询事件 / Direct Mode Polling Events
// ============================
/**
 * @brief pollDirect() 返回的事件类型
 *        Event types returned by pollDirect()
 *
 * PY32 固件是事件驱动的：每个按键状态变化（按下/释放）只生成一帧数据，
 * 被 I2C 读取后即清空，后续读取返回全零直到下一个事件。
 * PY32 firmware is event-driven: each key state change (press/release) generates
 * one data frame, cleared after I2C read. Subsequent reads return all-zero until
 * next event.
 *
 * 注意：以下事件模型是基于当前项目的“纯寄存器轮询模式”验证得到的实际行为。
 * 现场固件中的 `changed` 位并不总是可靠，因此 `RELEASED` 不能只靠
 * `changed=1` 判定，而要结合“当前全释放 + 相对上一活动帧 raw 有变化”判断。
 * The firmware `changed` bit is not always reliable. RELEASED therefore also
 * depends on an all-released state whose raw frame differs from the last active frame.
 *
 * 轮询路径事件模型 / Polling path event model:
 *   IDLE     → PRESSED（0x0A 帧，有键按下）
 *   PRESSED  → HELD（全零读取 = 无新事件，键仍被按住）
 *   HELD     → HELD（连续全零）
 *   HELD     → CHANGED（新 0x0A 帧，键组合变化）
 *   HELD     → RELEASED（当前全释放，且相对上一活动帧 raw 有变化）
 *   RELEASED → IDLE
 */
typedef enum {
    M5FACES_DIRECT_IDLE     = 0,  // 无事件（空闲期）/ No event (idle period)
    M5FACES_DIRECT_PRESSED  = 1,  // 新按键按下 / New key press detected
    M5FACES_DIRECT_RELEASED = 2,  // 全键释放 / All keys released
    M5FACES_DIRECT_CHANGED  = 3,  // 按键组合变化（按住期间）/ Key combo changed while held
    M5FACES_DIRECT_HELD     = 4,  // 键仍被按住（无新事件）/ Key still held (no new event)
} m5faces_direct_event_t;

// ============================
// 设备常量 / Device Constants
// ============================
#define M5FACES_BOTTOM3_ADDR 0x08U  // 默认 I2C 地址 / Default I2C address

#define M5FACES_REG_KEY         0x00U  // 按键值寄存器 / Key value register
#define M5FACES_REG_MODEL_ID    0xD0U  // 型号标识寄存器 / Model ID register
#define M5FACES_REG_DEVICE_UID  0xE0U  // 设备 UID 起始寄存器 / Device UID start register
#define M5FACES_DEVICE_UID_SIZE 12U    // 设备 UID 长度（0xE0~0xEB）/ Device UID length
#define M5FACES_REG_MODE        0xF0U  // 操作模式寄存器 / Operation mode register
#define M5FACES_REG_LED         0xF1U  // LED 灯效寄存器 / LED mode register
#define M5FACES_REG_FW_VERSION  0xFEU  // 固件版本寄存器（只读）/ Firmware version register (read-only)
#define M5FACES_REG_I2C_ADDR    0xFFU  // I2C 地址寄存器 / I2C address register

#define M5FACES_MODEL_CALCULATOR3 0x01U  // Faces Calculator3
#define M5FACES_MODEL_KEYBOARD3   0x02U  // Faces KEYBOARD3
#define M5FACES_MODEL_GAMEPAD3    0x03U  // Faces Gamepad3

#define M5FACES_I2C_FREQ_DEFAULT  100000U  // 100 kHz 标准模式 / Standard mode
#define M5FACES_I2C_FREQ_STANDARD 100000U  // 100 kHz 标准模式（同 DEFAULT）/ Standard mode (alias)
#define M5FACES_I2C_FREQ_FAST     400000U  // 400 kHz 快速模式 / Fast mode

// ============================
// 操作模式 / Operation Mode
// ============================
typedef enum {
    M5FACES_MODE_NORMAL = 0x00,  // Normal: 固件按键映射，1 字节 ASCII / Firmware key mapping, 1-byte ASCII
    M5FACES_MODE_DIRECT = 0x01,  // Direct: 原始矩阵数据，10 字节 / Raw matrix data, 10 bytes
} m5faces_mode_t;

// ============================
// LED 灯效模式 / LED Mode
// ============================
// 预设模式 0x00–0x08 / Preset modes 0x00–0x08
#define M5FACES_LED_OFF        0x00U  // 全灭 / All off
#define M5FACES_LED_AA_SINGLE  0x01U  // aA 单击：左灯常亮 / aA tap: left LED on
#define M5FACES_LED_AA_LOCK    0x02U  // aA 锁定：左灯慢闪 500ms / aA lock: left slow blink 500ms
#define M5FACES_LED_ALT_ACTIVE 0x03U  // ALT：左灯快闪 150ms / ALT: left fast blink 150ms
#define M5FACES_LED_FN_SINGLE  0x04U  // FN 单击：右灯常亮 / FN tap: right LED on
#define M5FACES_LED_FN_LOCK    0x05U  // FN 锁定：右灯慢闪 500ms / FN lock: right slow blink 500ms
#define M5FACES_LED_SYM_LOCK   0x06U  // SYM 锁定：右灯快闪 150ms / SYM lock: right fast blink 150ms
#define M5FACES_LED_SYM_SINGLE 0x07U  // SYM 单击：左右交替慢闪 500ms / SYM tap: alternating slow 500ms
#define M5FACES_LED_ALT_FAST   0x08U  // 外部设置：左右交替快闪 200ms / External: alternating fast 200ms
// 手动位控模式 (Bit7=1) / Manual bit-control mode (Bit7=1)
#define M5FACES_LED_MANUAL_NONE  0x80U  // 手动全灭 / Manual: all off
#define M5FACES_LED_MANUAL_LEFT  0x90U  // 手动左亮 / Manual: left on
#define M5FACES_LED_MANUAL_RIGHT 0xA0U  // 手动右亮 / Manual: right on
#define M5FACES_LED_MANUAL_BOTH  0xB0U  // 手动全亮 / Manual: both on

// ============================
// Direct 模式数据包 / Direct Mode Data Packet
// ============================
/**
 * @brief Direct 模式 10 字节数据包解析结构 (§7)
 *        Direct mode 10-byte packet parsed structure (§7)
 *
 * @note  电平规则 (active-low)：key_bits 中 0 = 按下, 1 = 未按下。
 *        modifier 布尔字段已做极性反转：true = 按下。
 *        Level rule (active-low): key_bits 0 = pressed, 1 = not-pressed.
 *        modifier booleans are inverted for convenience: true = pressed.
 */
typedef struct {
    uint8_t raw[10];  // 原始 10 字节 / Raw 10 bytes

    /// 矩阵行 (3 行, Bytes 1-6) / Matrix rows (3 rows, Bytes 1-6)
    struct {
        bool changed;       // bit7: 本次与上次不同 / Change flag
        uint8_t row_index;  // bit6-4: 行索引 (0-3) / Row index
        uint16_t key_bits;  // 10-bit: ((hi&0x03)<<8)|lo, 0=pressed
    } rows[3];

    /// 修饰键 (Bytes 7-8) / Modifier keys
    struct {
        bool changed;  // bit7: 变化标志 / Change flag
        bool aA;       // aA key pressed (active-low inverted)
        bool alt;      // ALT key pressed
        bool enter;    // Enter key pressed
        bool sym;      // SYM key pressed
        bool fn;       // FN key pressed
    } modifier;

    uint8_t checksum;  // Byte 9 二补数校验和 / Two's-complement checksum byte
    bool valid;        // 10 字节累加和为零 / All 10 bytes sum to zero
} m5faces_direct_data_t;

// ============================
// 基类 / Base Class
// ============================
class M5FacesBase {
public:
    /** @brief 构造基础驱动对象并初始化内部状态 / Construct the base driver and initialize internal state. */
    M5FacesBase()
    {
#ifndef ARDUINO
        _led_strip = nullptr;
#endif
    }

    /** @brief 释放驱动内部持有的设备与灯带资源 / Release device and LED-strip resources owned by the driver. */
    virtual ~M5FacesBase();

    // ── begin() 重载 / Overloads ────────────────────────────

#ifdef ARDUINO
#if M5FACES_HAS_M5UNIFIED_I2C
    /**
     * @brief 使用 M5Unified I2C_Class 初始化 (Arduino)
     *        Initialize using M5Unified I2C_Class (Arduino)
     * @param i2c   M5Unified I2C_Class 指针，如 &M5.In_I2C
     * @param addr  I2C 地址（默认 0x08）
     * @param freq  I2C 频率 Hz（默认 100 kHz）
     */
    m5faces_err_t begin(m5::I2C_Class *i2c, uint8_t addr = M5FACES_BOTTOM3_ADDR,
                        uint32_t freq = M5FACES_I2C_FREQ_DEFAULT);
#endif

    /**
     * @brief 使用 Wire (TwoWire) 初始化 (Arduino)
     *        Initialize using Wire (Arduino)
     */
    m5faces_err_t begin(TwoWire *wire = &Wire, uint8_t addr = M5FACES_BOTTOM3_ADDR,
                        uint32_t freq = M5FACES_I2C_FREQ_DEFAULT);

#else  // ESP-IDF

#if M5FACES_HAS_M5UNIFIED_I2C
    /**
     * @brief 使用 M5Unified I2C_Class 初始化 (ESP-IDF)
     *        Initialize using M5Unified I2C_Class (ESP-IDF)
     * @note  项目中存在 M5GFX/M5Unified 时推荐此方式以共享 I2C 总线
     *        Recommended when M5GFX/M5Unified is present — shares the I2C bus safely.
     * @param i2c   M5Unified I2C_Class 指针，如 &M5.In_I2C
     * @param addr  I2C 地址（默认 0x08）
     * @param freq  I2C 频率 Hz（默认 100 kHz）
     */
    m5faces_err_t begin(m5::I2C_Class *i2c, uint8_t addr = M5FACES_BOTTOM3_ADDR,
                        uint32_t freq = M5FACES_I2C_FREQ_DEFAULT);
#endif

    /**
     * @brief 使用 ESP-IDF 新 I2C Master 设备句柄初始化 (ESP-IDF, 独立模式)
     *        Initialize with ESP-IDF new I2C master device handle (standalone mode)
     * @note  调用方负责创建 i2c_master_bus 并调用 i2c_master_bus_add_device。
     *        此重载不支持运行时切换频率；如需切频请改用 begin(bus, addr, freq)。
     *        Caller must create i2c_master_bus and call i2c_master_bus_add_device first.
     *        Runtime frequency change is NOT supported with this overload;
     *        use begin(bus, addr, freq) instead if you need setI2CFreq().
     * @param dev_handle  已注册的设备句柄 / Pre-registered device handle
     */
    m5faces_err_t begin(i2c_master_dev_handle_t dev_handle);

    /**
     * @brief 使用 ESP-IDF I2C Master 总线句柄初始化（推荐：支持运行时切换频率）
     *        Initialize with ESP-IDF I2C master bus handle (recommended: supports runtime freq change)
     * @note  组件内部自动完成 i2c_master_bus_add_device，并在析构或 setI2CFreq() 时
     *        自动管理设备句柄的生命周期。
     *        The component manages device handle lifecycle automatically:
     *        creates device on begin(), recreates it in setI2CFreq(), removes it in destructor.
     * @param bus   已初始化的 I2C Master 总线句柄 / Initialized I2C master bus handle
     * @param addr  I2C 地址（默认 0x08）/ I2C address (default 0x08)
     * @param freq  I2C 频率 Hz（默认 100 kHz）/ I2C frequency in Hz (default 100 kHz)
     */
    m5faces_err_t begin(i2c_master_bus_handle_t bus, uint8_t addr = M5FACES_BOTTOM3_ADDR,
                        uint32_t freq = M5FACES_I2C_FREQ_DEFAULT);

#endif  // ARDUINO

    // ── I2C 频率切换 / I2C Frequency Switch ─────────────────

    /**
     * @brief  运行时切换 I2C 总线频率（100 kHz ↔ 400 kHz）
     *         Switch I2C bus frequency at runtime (100 kHz ↔ 400 kHz)
     *
     * 支持情况 / Supported modes:
     *   - M5Unified I2C_Class：直接更新内部频率，下次事务生效。
     *     M5Unified I2C_Class: updates internal freq; takes effect on next transaction.
     *   - begin(bus, addr, freq)：注销旧设备并以新频率重新注册。
     *     begin(bus, addr, freq): removes old device and re-registers with new freq.
     *   - begin(dev_handle)：不支持，返回 M5FACES_FAIL。
     *     begin(dev_handle): unsupported, returns M5FACES_FAIL.
     *   - Arduino Wire：调用 Wire::setClock(freq) 并更新内部值。
     *     Arduino Wire: calls Wire::setClock(freq) and updates internal value.
     *
     * @param  freq  目标频率 Hz；建议使用 M5FACES_I2C_FREQ_STANDARD (100 kHz)
     *               或 M5FACES_I2C_FREQ_FAST (400 kHz)
     *               Target frequency in Hz; use M5FACES_I2C_FREQ_STANDARD (100 kHz)
     *               or M5FACES_I2C_FREQ_FAST (400 kHz)
     * @return M5FACES_OK 成功 / M5FACES_ERR_NOT_INIT 未初始化 / M5FACES_FAIL 当前模式不支持
     *         M5FACES_OK on success / M5FACES_ERR_NOT_INIT / M5FACES_FAIL if unsupported mode
     */
    m5faces_err_t setI2CFreq(uint32_t freq);

    /** @brief 获取当前 I2C 频率（Hz）/ Get current I2C frequency (Hz) */
    uint32_t getI2CFreq() const
    {
        return _freq;
    }

    // ── 更新与查询 / Update & Query ─────────────────────────

    /**
     * @brief  轮询按键状态（应在主循环中调用）
     *         Poll key state (call in main loop)
     * @return true 若按键值有变化 / true if key value changed
     */
    virtual bool update();

    /**
     * @brief  读取 Direct 模式 10 字节数据包 (§7)
     *         Read Direct mode 10-byte data packet (§7)
     * @note   需先通过 setMode(M5FACES_MODE_DIRECT) 进入 Direct 模式。
     *         Must enter Direct mode first via setMode(M5FACES_MODE_DIRECT).
     * @note   `data->valid` 表示完整 10 字节的低 8 位累加和是否为零；校验失败时
     *         仍返回解析结果供诊断，但调用方应将该帧视为无效数据。
     *         `data->valid` reports whether the 8-bit sum of all 10 bytes is zero.
     *         A failed frame remains parsed for diagnostics but should be treated as invalid.
     * @param[out] data  解析后的数据包结构 / Parsed packet structure
     */
    m5faces_err_t updateDirect(m5faces_direct_data_t *data);

    /**
     * @brief  Direct 模式轮询专用接口（内部带 FIFO）
     *         Polling-specific Direct mode interface (with internal FIFO)
     *
     * 内部会连续读取并分类多个 Direct 帧，将事件按 FIFO 顺序缓存，
     * 每次调用仅弹出一个事件返回，避免快按快放或组合键变化被合并。
     * Internally reads and classifies multiple Direct frames into a FIFO.
     * Each call pops at most one event, preserving quick press/release order.
     *
     * @param[out] data  当前弹出的事件帧；HELD/IDLE 时为全零
     *                   Frame for the popped event; all-zero for HELD/IDLE
     * @return 事件类型 / Event type
     *
     * @note  RELEASED 后调用 getDirectPrevData() 可获取该次释放前最后的按下帧。
     *        After RELEASED, getDirectPrevData() returns the last pressed frame
     *        associated with that release event.
     * @note  调用 getDirectHoldCount() 可获取当前弹出事件对应的 hold 计数。
     *        getDirectHoldCount() returns the hold counter for the popped event.
     */
    m5faces_direct_event_t pollDirect(m5faces_direct_data_t *data);

    /** @brief 获取最近一次弹出事件对应的按住计数 / Get hold count for last popped event */
    uint32_t getDirectHoldCount() const
    {
        return _direct_poll_last_event_hold_cnt;
    }

    /** @brief 获取最近一次弹出事件关联的上一个 Direct 数据 / Get previous Direct data for last popped event */
    const m5faces_direct_data_t &getDirectPrevData() const
    {
        return _direct_poll_last_event_prev_data;
    }

    /** @brief 获取原始按键值（0 = 无按键） / Get raw key value (0 = no key) */
    uint8_t getKey() const
    {
        return _key_raw;
    }

    /** @brief 与上次 update() 相比，按键值是否变化 / Key changed since last update() */
    bool keyChanged() const
    {
        return _key_changed;
    }

    /** @brief 设备是否已初始化 / Is device initialized */
    bool isInitialized() const
    {
        return _initialized;
    }

    /**
     * @brief  当前型号是否支持 Direct 模式
     *         Whether this model supports Direct mode
     * @note   默认 false；支持 Direct 的子类（如 M5Faces_Keyboard3）重写为 true
     *         Default false; subclasses supporting Direct override to return true.
     */
    virtual bool supportsDirectMode() const
    {
        return false;
    }

    // ── 型号识别 / Model Identification ────────────────────

    /**
     * @brief  读取 0xD0 型号标识寄存器
     *         Read model ID register 0xD0
     * @param[out] model_id  读出的型号值 / Read-back model ID
     */
    m5faces_err_t getModelID(uint8_t *model_id);

    /**
     * @brief  读取设备 UID（寄存器 0xE0~0xEB，共 12 字节）
     *         Read the 12-byte device UID from registers 0xE0~0xEB
     * @param[out] uid  至少可容纳 M5FACES_DEVICE_UID_SIZE 字节的缓冲区
     *                  Buffer with room for at least M5FACES_DEVICE_UID_SIZE bytes
     */
    m5faces_err_t getDeviceUID(uint8_t *uid);

    /**
     * @brief  检查当前连接设备的型号是否与预期一致
     *         Check if the connected device model matches the expected value
     */
    bool isModelMatch(uint8_t expected_id);

    // ── 工作模式 / Operation Mode ───────────────────────────

    /**
     * @brief  设置工作模式 (reg 0xF0)
     *         Set operation mode (reg 0xF0)
     * @param  mode  M5FACES_MODE_NORMAL (0) 或 M5FACES_MODE_DIRECT (1)
     * @note   切换模式会清空固件内部矩阵历史状态缓存
     *         Switching mode clears the firmware's internal matrix history cache.
     */
    m5faces_err_t setMode(m5faces_mode_t mode);

    /**
     * @brief  读取当前工作模式 (reg 0xF0)
     *         Get current operation mode (reg 0xF0)
     * @param[out] mode  当前模式 / Current mode
     */
    m5faces_err_t getMode(m5faces_mode_t *mode);

    // ── LED 灯效 / LED Control ──────────────────────────────

    /**
     * @brief  设置 LED 灯效 (reg 0xF1)
     *         Set LED effect mode (reg 0xF1)
     * @param  led_mode  预设 0x00-0x08 或手动位控 (bit7=1, bit4=左灯, bit5=右灯)
     *                   Preset 0x00-0x08 or manual bit-control (bit7=1, bit4=left, bit5=right)
     */
    m5faces_err_t setLED(uint8_t led_mode);

    /**
     * @brief  读取当前 LED 灯效设置 (reg 0xF1)
     *         Get current LED effect setting (reg 0xF1)
     * @param[out] led_mode  当前 LED 模式值 / Current LED mode value
     */
    m5faces_err_t getLED(uint8_t *led_mode);

    // ── 中间转接板功能 (Bottom3 Base Board Features) ─────────

    /**
     * @brief 初始化中间转接板的 SK6812 RGB 灯带
     *        Initialize SK6812 RGB LED strip on the intermediate adapter board
     * @param pin RGB 数据引脚 / RGB data pin (指定引脚，不同底座不同)
     *            CoreS3 通常是引脚 5 / For CoreS3 it's usually pin 5
     * @param num_pixels 灯珠数量 / Number of pixels (默认 8 / Default 8)
     */
    void initRGB(int pin, size_t num_pixels = 8);

    /**
     * @brief 设置单个 RGB 灯珠颜色
     *        Set the color of a single pixel
     * @param index 灯珠索引 (0 开始) / Pixel index (0-based)
     * @param r 红色分量 / Red (0-255)
     * @param g 绿色分量 / Green (0-255)
     * @param b 蓝色分量 / Blue (0-255)
     */
    void setPixelColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief 一次性设置多个灯珠颜色
     *        Set all pixels' colors at once using an array
     * @param colors RGB 数据数组 (每个灯珠 3 字节: R, G, B) / Array of RGB values
     * @param count 要设置的灯珠数量 / Number of pixels to set
     */
    void setPixels(const uint8_t *colors, size_t count);

    /**
     * @brief 一次性设置 8 个灯珠的颜色
     *        Set 8 pixels' colors from an array
     * @param colors 颜色数组 (24 字节长，8 个 RGB) / Array of 24 bytes (8x RGB)
     */
    void setPixels8(const uint8_t colors[24]);

    /**
     * @brief 更新显示
     *        Show the current pixel data
     */
    void showRGB();

    // ── 固件信息 / Firmware Info ─────────────────────────────

    /**
     * @brief  读取固件版本号 (reg 0xFE, 只读)
     *         Read firmware version (reg 0xFE, read-only)
     * @param[out] version  固件版本值 / Firmware version value
     */
    m5faces_err_t getFirmwareVersion(uint8_t *version);

    // ── I2C 地址修改 / I2C Address ──────────────────────────

    /**
     * @brief  修改模块 I2C 地址 (reg 0xFF)
     *         Change module I2C address (reg 0xFF)
     * @param  new_addr  新地址（有效范围 0x08-0x77）。写入后立即生效，掉电不丢失。
     *                   New address (valid: 0x08-0x77). Takes effect immediately, survives power cycle.
     * @note   写入后需 ~20ms 等待模块完成地址切换。
     *         After writing, allow ~20ms for the module to complete the address switch.
     *         调用成功后本实例的内部地址 _addr 会同步更新。
     *         On success the internal _addr of this instance is also updated.
     */
    m5faces_err_t setI2CAddress(uint8_t new_addr);

    /**
     * @brief  读取模块当前存储的 I2C 地址 (reg 0xFF)
     *         Read the module's stored I2C address (reg 0xFF)
     * @param[out] addr  当前地址 / Current address
     */
    m5faces_err_t getI2CAddress(uint8_t *addr);

    // ── 批量寄存器读取 / Batch Register Read (§5.2) ────────────

    /**
     * @brief  批量读取操作模式 + LED 灯效 (段A: reg 0xF0, 2 bytes)
     *         Batch read operation mode + LED mode (Segment A: reg 0xF0, 2 bytes)
     * @param[out] mode      当前操作模式 / Current operation mode
     * @param[out] led_mode  当前 LED 灯效 / Current LED mode
     * @note   单次 I2C 事务读取 2 字节，比分别调用 getMode()+getLED() 效率更高。
     *         Single I2C transaction reads 2 bytes; more efficient than separate getMode()+getLED().
     */
    m5faces_err_t getModeLED(m5faces_mode_t *mode, uint8_t *led_mode);

    /**
     * @brief  批量读取固件版本 + I2C 地址 (段B: reg 0xFE, 2 bytes)
     *         Batch read firmware version + I2C address (Segment B: reg 0xFE, 2 bytes)
     * @param[out] version  固件版本号 / Firmware version
     * @param[out] addr     当前 I2C 地址 / Current I2C address
     * @note   单次 I2C 事务读取 2 字节，比分别调用 getFirmwareVersion()+getI2CAddress() 效率更高。
     *         Single I2C transaction reads 2 bytes; more efficient than separate calls.
     */
    m5faces_err_t getVersionAddr(uint8_t *version, uint8_t *addr);

    // ── 静态辅助 / Static Helpers ───────────────────────────

    /**
     * @brief  根据 0xD0 型号 ID 返回设备名称字符串
     *         Return device name string from model ID
     */
    static const char *modelName(uint8_t model_id);

    // ── 中断引脚 / Interrupt Pin ─────────────────────────────

    /**
     * @brief  设置 INT/IRQ 中断引脚（低电平有效），并选择工作模式
     *         Set INT/IRQ pin (active-low) and choose working mode
     *
     * @param  pin            GPIO 编号 / GPIO number
     *                        Arduino: uint8_t pin number
     *                        ESP-IDF: gpio_num_t (e.g. GPIO_NUM_10)
     * @param  use_interrupt  true  = 真实 GPIO 边沿中断（默认/推荐）
     *                               Real GPIO edge interrupt (default/recommended)
     *                               ISR 在下降沿触发，设置内部标志，update() 据此决定是否读 I2C。
     *                               ISR fires on falling edge, sets internal flag;
     *                               update() checks the flag before I2C read.
     *                        false = 电平轮询：每次 update() 主动读取引脚电平
     *                               Level polling: update() samples pin level each call.
     * @return M5FACES_OK 成功 / M5FACES_ERR_INVALID 无效引脚或 ISR 安装失败
     */
#ifdef ARDUINO
    m5faces_err_t setInterruptPin(uint8_t pin, bool use_interrupt = true);
#else
    m5faces_err_t setInterruptPin(gpio_num_t pin, bool use_interrupt = true);
#endif

    /**
     * @brief  查询 INT 引脚是否已配置
     *         Check if INT pin has been configured
     */
    bool hasInterruptPin() const
    {
        return _int_pin >= 0;
    }

    /**
     * @brief  查询当前是否处于真实 GPIO 中断模式
     *         Check if real GPIO interrupt mode is active
     * @return true = 中断模式 / interrupt mode
     *         false = 轮询模式或未配置 / polling mode or not configured
     */
    bool isInterruptMode() const
    {
        return _int_mode;
    }

    /**
     * @brief  查询 INT 引脚是否为低电平（有新数据）
     *         Check if INT pin is LOW (new data available)
     * @return true = INT 为低电平 / INT is LOW
     * @note   若未配置 INT 引脚，始终返回 true（退化为盲轮询）
     *         If INT pin is not configured, always returns true (falls back to blind polling).
     */
    bool isInterruptLow() const;

    // ── 底层 I2C 访问 / Low-level I2C ───────────────────────

    /**
     * @brief  读取寄存器（多字节）/ Read register(s)
     * @param  reg   起始寄存器地址 / Start register address
     * @param  data  读取缓冲区 / Read buffer
     * @param  len   字节数 / Byte count
     */
    m5faces_err_t readReg(uint8_t reg, uint8_t *data, size_t len = 1);

    /**
     * @brief  写入寄存器（多字节）/ Write register(s)
     * @param  reg   起始寄存器地址 / Start register address
     * @param  data  写入数据 / Write data
     * @param  len   字节数 / Byte count
     */
    m5faces_err_t writeReg(uint8_t reg, const uint8_t *data, size_t len = 1);

protected:
    /**
     * @brief  begin() 公共收尾：执行型号验证（若 _expected_model_id != 0）
     *         Common post-init step: verify model ID if _expected_model_id is set.
     * @note   子类构造函数中设置 _expected_model_id，begin() 会自动调用本函数
     *         Subclass constructors set _expected_model_id; begin() calls this automatically.
     */
    m5faces_err_t _afterBegin();

    // ── 状态 / State ─────────────────────────────────────────
    bool _initialized = false;
    uint8_t _addr     = M5FACES_BOTTOM3_ADDR;
    uint32_t _freq    = M5FACES_I2C_FREQ_DEFAULT;
    uint8_t _key_raw  = 0;
    uint8_t _key_prev = 0;
    bool _key_changed = false;

    int _int_pin            = -1;     // INT/IRQ GPIO (-1 = 未配置 / not configured)
    bool _int_mode          = false;  // true = 真实 GPIO 中断, false = 电平轮询
    volatile bool _int_flag = false;  // ISR 置位的中断到达标志 / ISR-set arrival flag

    // ── pollDirect() 状态 / pollDirect() state ───────────────
    static constexpr uint8_t M5FACES_DIRECT_QUEUE_CAP = 8;

    struct direct_queue_item_t {
        m5faces_direct_event_t evt      = M5FACES_DIRECT_IDLE;
        m5faces_direct_data_t data      = {};
        m5faces_direct_data_t prev_data = {};
        uint32_t hold_cnt               = 0;
    };

    bool _direct_poll_active                     = false;  // 当前是否存在按住状态 / Whether any key is currently held
    m5faces_direct_data_t _direct_poll_prev_data = {};     // 当前活动状态对应的最近一帧 / Latest frame of active state
    uint32_t _direct_poll_hold_cnt               = 0;      // 当前活动状态持续计数 / Poll count in current active state
    m5faces_direct_data_t _direct_poll_last_event_prev_data =
        {};                                         // 最近弹出事件关联的前一帧 / Previous frame for last popped event
    uint32_t _direct_poll_last_event_hold_cnt = 0;  // 最近弹出事件关联的 hold 计数 / Hold count for last popped event
    direct_queue_item_t _direct_queue[M5FACES_DIRECT_QUEUE_CAP] = {};
    uint8_t _direct_queue_head                                  = 0;
    uint8_t _direct_queue_count                                 = 0;

    /** @brief 判断 Direct 帧中是否存在按下的按键 / Check whether a Direct frame contains pressed keys. */
    bool _directHasKeys(const m5faces_direct_data_t &data) const;

    /** @brief 判断 Direct 帧是否设置了任一变化标志 / Check whether any change flag is set in a Direct frame. */
    bool _directHasChangedBits(const m5faces_direct_data_t &data) const;

    /** @brief 重置 Direct 轮询状态与事件队列 / Reset Direct polling state and its event queue. */
    void _directResetState();

    /** @brief 将 Direct 事件压入内部 FIFO / Push a Direct event into the internal FIFO. */
    bool _directQueuePush(m5faces_direct_event_t evt, const m5faces_direct_data_t &data, uint32_t hold_cnt,
                          const m5faces_direct_data_t &prev_data);

    /** @brief 从内部 FIFO 弹出一个 Direct 事件 / Pop one Direct event from the internal FIFO. */
    bool _directQueuePop(m5faces_direct_event_t *evt, m5faces_direct_data_t *data);

    /** @brief 对 Direct 帧分类并按需生成队列事件 / Classify a Direct frame and enqueue an event when needed. */
    void _directClassifyAndQueue(const m5faces_direct_data_t &data);

    /** @brief 连续读取 Direct 帧以填充内部事件队列 / Read Direct frames repeatedly to fill the event queue. */
    void _directFillQueue(uint8_t max_reads = 4);

    /**
     * @brief  子类在构造函数中设置此值以启用自动型号验证（0 = 不验证）
     *         Subclass sets this in constructor to enable auto model verification (0 = skip).
     */
    uint8_t _expected_model_id = 0;

    // ── I2C 后端 / I2C Backend ───────────────────────────────
    enum class I2CMode : uint8_t {
        NONE = 0,
#ifdef ARDUINO
        WIRE,  // Arduino TwoWire (Wire)
#endif
#if M5FACES_HAS_M5UNIFIED_I2C
        M5UNIFIED,  // M5Unified I2C_Class（支持运行时切频 / supports runtime freq change）
#endif
#ifndef ARDUINO
        IDF_MASTER,      // 外部提供 dev_handle（不支持运行时切频 / no runtime freq change）
        IDF_MASTER_BUS,  // 内部管理 dev_handle（支持运行时切频 / supports runtime freq change）
#endif
    };

    I2CMode _i2c_mode = I2CMode::NONE;

#if M5FACES_HAS_M5UNIFIED_I2C
    m5::I2C_Class *_m5i2c = nullptr;
#endif

#ifdef ARDUINO
    TwoWire *_wire = nullptr;
    /** @brief Arduino ISR 回调，仅置位中断标志 / Arduino ISR callback that only sets the interrupt flag. */
    static void IRAM_ATTR _arduino_isr(void *arg);
#else
    i2c_master_dev_handle_t _idf_dev = nullptr;  // 设备句柄（两种 IDF 模式共用）
    i2c_master_bus_handle_t _idf_bus = nullptr;  // 总线句柄（仅 IDF_MASTER_BUS 模式）
    /** @brief ESP-IDF GPIO ISR 回调，仅置位中断标志 / ESP-IDF GPIO ISR callback that only sets the interrupt flag. */
    static void IRAM_ATTR _gpio_isr_handler(void *arg);

    // Neopixel 指针对外部隐藏依赖 / Opaque pointer to espp::Neopixel
    void *_led_strip = nullptr;
#endif
};

#endif  // __M5_FACES_BASE_H__
