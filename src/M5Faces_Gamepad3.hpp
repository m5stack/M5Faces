/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file M5Faces_Gamepad3.hpp
 * @brief M5Faces Gamepad3 驱动（MODEL_ID = 0x03）
 *        M5Faces Gamepad3 driver (MODEL_ID = 0x03)
 *
 * 寄存器协议（待硬件手册确认）/ Register protocol (pending hardware spec confirmation):
 *   0x00 寄存器返回 1 字节按键位掩码
 *   Register 0x00 returns a 1-byte button bitmask
 *
 *   位定义（active-low，0 表示按下）/ Bit definitions (active-low, 0 = pressed):
 *     Bit 0: UP    方向键上 / D-pad up
 *     Bit 1: DOWN  方向键下 / D-pad down
 *     Bit 2: LEFT  方向键左 / D-pad left
 *     Bit 3: RIGHT 方向键右 / D-pad right
 *     Bit 4: A     按键 A
 *     Bit 5: B     按键 B
 *     Bit 6: SELECT
 *     Bit 7: START
 *
 *   TODO: 根据实际硬件寄存器手册校验位定义与极性
 *         Verify bit definitions and polarity against actual hardware spec.
 */

#ifndef __M5_FACES_GAMEPAD3_H__
#define __M5_FACES_GAMEPAD3_H__

#include "M5FacesBase.hpp"

// ============================
// 按键位掩码 / Button Bitmask
// ============================
typedef enum {
    GAMEPAD3_BTN_UP     = (1 << 0),  // 方向上 / Up
    GAMEPAD3_BTN_DOWN   = (1 << 1),  // 方向下 / Down
    GAMEPAD3_BTN_LEFT   = (1 << 2),  // 方向左 / Left
    GAMEPAD3_BTN_RIGHT  = (1 << 3),  // 方向右 / Right
    GAMEPAD3_BTN_A      = (1 << 4),  // A 键
    GAMEPAD3_BTN_B      = (1 << 5),  // B 键
    GAMEPAD3_BTN_SELECT = (1 << 6),  // SELECT
    GAMEPAD3_BTN_START  = (1 << 7),  // START
} gamepad3_btn_t;

// ============================
// 按键状态结构体 / Button State Struct
// ============================
/**
 * @brief  Gamepad3 当前按键状态（位段联合体）
 *         Gamepad3 current button state (bitfield union)
 *
 * @note   active-low 设备：按下时对应位为 0；isPressed() 已处理极性反转。
 *         Active-low device: bit = 0 when pressed; isPressed() handles polarity inversion.
 */
typedef union {
    uint8_t raw;  ///< 原始寄存器值 / Raw register value
    struct {
        uint8_t up : 1;  ///< 1 = not pressed (active-low)
        uint8_t down : 1;
        uint8_t left : 1;
        uint8_t right : 1;
        uint8_t a : 1;
        uint8_t b : 1;
        uint8_t select : 1;
        uint8_t start : 1;
    } bits;
} gamepad3_state_t;

// ============================
// M5Faces_Gamepad3
// ============================
/**
 * @brief Faces Gamepad3 驱动类
 *        Faces Gamepad3 driver class
 *
 * update() 后可通过 getState() 或 isButtonPressed() 查询按键状态。
 * After update(), query button state via getState() or isButtonPressed().
 */
class M5Faces_Gamepad3 : public M5FacesBase {
public:
    static constexpr uint8_t MODEL_ID = M5FACES_MODEL_GAMEPAD3;

    /**
     * @brief 构造函数：设置预期型号 ID，begin() 时自动校验
     *        Constructor: set expected model ID for automatic verification in begin()
     */
    M5Faces_Gamepad3()
    {
        _expected_model_id = MODEL_ID;
        _key_raw           = 0xFF;
        _key_prev          = 0xFF;
    }

    // 继承所有 begin() 重载（自动校验型号）
    // Inherit all begin() overloads (model ID verified automatically via _afterBegin)
    using M5FacesBase::begin;

    // ── Gamepad3 专用 API ────────────────────────────────────

    /**
     * @brief  获取当前按键状态（调用 update() 后有效）
     *         Get current button state (valid after update())
     */
    gamepad3_state_t getState() const
    {
        return _gp_state;
    }

    /**
     * @brief  查询指定按键是否当前按下（active-low 极性已处理）
     *         Check if specific button is currently pressed (active-low polarity handled)
     * @param  btn  按键掩码，见 gamepad3_btn_t / Button mask, see gamepad3_btn_t
     */
    bool isButtonPressed(gamepad3_btn_t btn) const;

    /**
     * @brief  查询指定按键是否在本次 update() 中刚刚按下（边沿检测）
     *         Check if button was just pressed in this update() call (rising-edge detection)
     */
    bool isButtonJustPressed(gamepad3_btn_t btn) const;

    /**
     * @brief  查询指定按键是否在本次 update() 中刚刚释放（边沿检测）
     *         Check if button was just released in this update() call (falling-edge detection)
     */
    bool isButtonJustReleased(gamepad3_btn_t btn) const;

    /**
     * @brief  重写 update()，同时更新 gamepad3_state_t 结构体
     *         Override update(); also refreshes gamepad3_state_t struct
     */
    bool update() override;

    // ── 显示辅助 / Display Helpers ───────────────────────────

    /**
     * @brief  将原始字节解析为按下的按键名称列表字符串
     *         Parse raw byte into a '+'-joined string of pressed button names
     * @param  raw    原始寄存器值（active-low，0 = 按下）
     *                Raw register value (active-low, 0 = pressed)
     * @param  buf    输出缓冲区 / Output buffer
     * @param  bufsz  缓冲区大小 / Buffer size
     * @return 按下的按键数量；0 表示无按键（全部释放）
     *         Number of pressed buttons; 0 means no button pressed (all released).
     */
    static int gamepad3_code_parse(uint8_t raw, char* buf, size_t bufsz);

private:
    gamepad3_state_t _gp_state      = {0xFF};  // active-low: 全 1 = 全部未按下
    gamepad3_state_t _gp_state_prev = {0xFF};
};

#endif  // __M5_FACES_GAMEPAD3_H__
