/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file M5Faces_Calculator3.hpp
 * @brief M5Faces Calculator3 驱动（MODEL_ID = 0x01）
 *        M5Faces Calculator3 driver (MODEL_ID = 0x01)
 *
 * 按键码说明 / Key code description:
 *   0x00        — 无按键 / No key pressed
 *   '0'~'9'     — 数字键 / Digit keys
 *   '+' '-' '*' '/' '=' '.' — 运算符 / Operators
 *   0x08 (BS)   — 退格/删除 / Backspace / Delete
 *   0x0D (CR)   — 确认/回车 / Enter / Confirm
 *   TODO: 根据实际硬件寄存器手册校验键码
 *         Verify against actual hardware register specification.
 */

#ifndef __M5_FACES_CALCULATOR3_H__
#define __M5_FACES_CALCULATOR3_H__

#include "M5FacesBase.hpp"

// ============================
// Calculator3 按键枚举 / Key Enum
// ============================
typedef enum {
    CALC3_KEY_NONE  = 0x00,
    CALC3_KEY_0     = '0',
    CALC3_KEY_1     = '1',
    CALC3_KEY_2     = '2',
    CALC3_KEY_3     = '3',
    CALC3_KEY_4     = '4',
    CALC3_KEY_5     = '5',
    CALC3_KEY_6     = '6',
    CALC3_KEY_7     = '7',
    CALC3_KEY_8     = '8',
    CALC3_KEY_9     = '9',
    CALC3_KEY_PLUS  = '+',
    CALC3_KEY_MINUS = '-',
    CALC3_KEY_MUL   = '*',
    CALC3_KEY_DIV   = '/',
    CALC3_KEY_EQ    = '=',
    CALC3_KEY_DOT   = '.',
    CALC3_KEY_BS    = 0x08,  // 退格 / Backspace
    CALC3_KEY_ENTER = 0x0D,  // 确认 / Enter
} calc3_key_t;

// ============================
// M5Faces_Calculator3
// ============================
/**
 * @brief Faces Calculator3 驱动类
 *        Faces Calculator3 driver class
 *
 * begin() 时自动验证 MODEL_ID，型号不匹配返回 M5FACES_ERR_MISMATCH。
 * Automatically verifies MODEL_ID in begin(); returns M5FACES_ERR_MISMATCH on mismatch.
 */
class M5Faces_Calculator3 : public M5FacesBase {
public:
    static constexpr uint8_t MODEL_ID = M5FACES_MODEL_CALCULATOR3;

    /**
     * @brief 构造函数：设置预期型号 ID，begin() 时自动校验
     *        Constructor: set expected model ID for automatic verification in begin()
     */
    M5Faces_Calculator3()
    {
        _expected_model_id = MODEL_ID;
    }

    // 继承所有 begin() 重载（自动校验型号）
    // Inherit all begin() overloads (model ID verified automatically via _afterBegin)
    using M5FacesBase::begin;

    // ── Calculator3 专用 API ─────────────────────────────────

    /**
     * @brief  将当前按键值转换为可打印字符（无效键返回 '\0'）
     *         Convert current key to printable char ('\0' if invalid / no key)
     */
    char getChar() const;

    /**
     * @brief  当前是否有按键按下（key != 0）
     *         Is any key currently pressed (key != 0)
     */
    bool isPressed() const
    {
        return _key_raw != CALC3_KEY_NONE;
    }

    // ── 显示辅助 / Display Helpers ───────────────────────────

    /**
     * @brief  将原始键码转换为可读名称字符串（仅用于控制字符 / 特殊键）
     *         Map raw key code to a readable name string (for control chars / special keys only)
     * @param  raw  原始键码 / Raw key code
     * @return 名称字符串（如 "BS"、"Enter"、"NUL"），
     *         若为可打印字符或未知码则返回 NULL
     *         Name string (e.g. "BS", "Enter", "NUL");
     *         returns NULL if printable or unknown code.
     */
    static const char* calc3_code_parse(uint8_t raw);
};

#endif  // __M5_FACES_CALCULATOR3_H__
