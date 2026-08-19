/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file M5Faces_Keyboard3.hpp
 * @brief M5Faces KEYBOARD3 驱动（MODEL_ID = 0x02）
 *        M5Faces KEYBOARD3 driver (MODEL_ID = 0x02)
 *
 * 按键码说明 / Key code description:
 *   0x00        — 无按键 / No key pressed
 *   0x20~0x7E   — 标准 ASCII 可打印字符 / Standard ASCII printable characters
 *   0x08 (BS)   — 退格 / Backspace
 *   0x0D (CR)   — 回车 / Enter
 *   0x09 (TAB)  — Tab 键 / Tab key
 */

#ifndef __M5_FACES_KEYBOARD3_H__
#define __M5_FACES_KEYBOARD3_H__

#include "M5FacesBase.hpp"

// ============================
// KEYBOARD3 特殊键枚举（非 ASCII 区域）
// KEYBOARD3 Special Key Enum (non-ASCII range)
// ============================
typedef enum {
    KEYBOARD3_KEY_NONE  = 0x00,
    KEYBOARD3_KEY_BS    = 0x08,  // 退格 / Backspace
    KEYBOARD3_KEY_TAB   = 0x09,  // Tab
    KEYBOARD3_KEY_ENTER = 0x0D,  // 回车 / Enter
    KEYBOARD3_KEY_ESC   = 0x1B,  // ESC
    KEYBOARD3_KEY_DEL   = 0x7F,  // Delete
} keyboard3_key_t;

// ============================
// KEYBOARD3 扩展功能键（固件输出的非 ASCII 码）
// KEYBOARD3 Extension Keys (non-ASCII codes from firmware)
// ============================
// 修饰键自身的键码（0x80+） / Modifier key self-codes (0x80+)
#define KEYBOARD3_EXT_FN       0x80U  // Fn 键自身 / Fn key itself
#define KEYBOARD3_EXT_ALT      0x81U  // Alt 键自身 / Alt key itself
#define KEYBOARD3_EXT_SYM      0x82U  // Sym 键自身 / Sym key itself
#define KEYBOARD3_EXT_SHIFT    0x83U  // Shift 键自身 / Shift key itself
#define KEYBOARD3_EXT_CAPSLOCK 0x84U  // CapsLock / CapsLock

// ============================
// KEYBOARD3 FN 功能键编码 (§10, 180-193)
// KEYBOARD3 FN Function Key Codes (§10, 180-193)
// ============================
// 按下 FN + 字母键时，固件返回的 ASCII 范围外编码。
// Non-ASCII codes returned by firmware when FN + letter key is pressed.
#define KEYBOARD3_FN_180       180U  // G+FN (预留 / reserved)
#define KEYBOARD3_FN_181       181U  // H+FN (预留 / reserved)
#define KEYBOARD3_FN_182       182U  // J+FN (预留 / reserved)
#define KEYBOARD3_FN_UP        183U  // K+FN → ▲ 方向上 / Up arrow
#define KEYBOARD3_FN_INSERT    184U  // L+FN → Insert
#define KEYBOARD3_FN_TAB       186U  // Z+FN → Tab
#define KEYBOARD3_FN_HOME      187U  // X+FN → Home
#define KEYBOARD3_FN_END       188U  // C+FN → End
#define KEYBOARD3_FN_PAGE_UP   189U  // V+FN → Page Up
#define KEYBOARD3_FN_PAGE_DOWN 190U  // B+FN → Page Down
#define KEYBOARD3_FN_LEFT      191U  // N+FN → ◄ 方向左 / Left arrow
#define KEYBOARD3_FN_DOWN      192U  // M+FN → ▼ 方向下 / Down arrow
#define KEYBOARD3_FN_RIGHT     193U  // $+FN → ► 方向右 / Right arrow

// ============================
// KEYBOARD3 ALT 键编码范围 (§10, 144-176)
// KEYBOARD3 ALT Key Code Range (§10, 144-176)
// ============================
// 按下 ALT + 字母键时，固件返回的 ASCII 范围外编码。
// Non-ASCII codes returned by firmware when ALT + letter key is pressed.
// 有效范围 144-176（跳过 164=aA 键, 173-174=Enter/特殊键，这些键输出 255 或 13）。
// Valid range 144-176 (skips 164=aA key, 173-174=Enter/special keys; those output 255 or 13).
// 注意: 255 是固件的"无映射"哨兵值，主机应忽略，不属于通信错误。
// Note: 255 is the firmware's "no-mapping" sentinel — host must ignore, not an I2C error.
#define KEYBOARD3_ALT_Q      144U  // Q+ALT
#define KEYBOARD3_ALT_W      145U  // W+ALT
#define KEYBOARD3_ALT_E      146U  // E+ALT
#define KEYBOARD3_ALT_R      147U  // R+ALT
#define KEYBOARD3_ALT_T      148U  // T+ALT
#define KEYBOARD3_ALT_Y      149U  // Y+ALT
#define KEYBOARD3_ALT_U      150U  // U+ALT
#define KEYBOARD3_ALT_I      151U  // I+ALT
#define KEYBOARD3_ALT_O      152U  // O+ALT
#define KEYBOARD3_ALT_P      153U  // P+ALT
#define KEYBOARD3_ALT_A      154U  // A+ALT
#define KEYBOARD3_ALT_S      155U  // S+ALT
#define KEYBOARD3_ALT_D      156U  // D+ALT
#define KEYBOARD3_ALT_F      157U  // F+ALT
#define KEYBOARD3_ALT_G      158U  // G+ALT
#define KEYBOARD3_ALT_H      159U  // H+ALT
#define KEYBOARD3_ALT_J      160U  // J+ALT
#define KEYBOARD3_ALT_K      161U  // K+ALT
#define KEYBOARD3_ALT_L      162U  // L+ALT
#define KEYBOARD3_ALT_DEL    163U  // del+ALT
#define KEYBOARD3_ALT_Z      165U  // Z+ALT
#define KEYBOARD3_ALT_X      166U  // X+ALT
#define KEYBOARD3_ALT_C      167U  // C+ALT
#define KEYBOARD3_ALT_V      168U  // V+ALT
#define KEYBOARD3_ALT_B      169U  // B+ALT
#define KEYBOARD3_ALT_N      170U  // N+ALT
#define KEYBOARD3_ALT_M      171U  // M+ALT
#define KEYBOARD3_ALT_DOLLAR 172U  // $+ALT
#define KEYBOARD3_ALT_0      175U  // 0+ALT
#define KEYBOARD3_ALT_SPACE  176U  // SPACE+ALT

// ============================
// M5Faces_Keyboard3
// ============================
/**
 * @brief Faces KEYBOARD3 驱动类
 *        Faces KEYBOARD3 driver class
 *
 * 按键原始值大多为直接的 ASCII 值，可通过 getChar() 获取对应字符。
 * Key raw values are mostly direct ASCII; use getChar() to get the character.
 */
class M5Faces_Keyboard3 : public M5FacesBase {
public:
    static constexpr uint8_t MODEL_ID = M5FACES_MODEL_KEYBOARD3;

    /**
     * @brief 构造函数：设置预期型号 ID，begin() 时自动校验
     *        Constructor: set expected model ID for automatic verification in begin()
     */
    M5Faces_Keyboard3()
    {
        _expected_model_id = MODEL_ID;
    }

    /** @brief KEYBOARD3 支持 Direct 模式 / KEYBOARD3 supports Direct mode */
    bool supportsDirectMode() const override
    {
        return true;
    }

    // 继承所有 begin() 重载（自动校验型号）
    // Inherit all begin() overloads (model ID verified automatically via _afterBegin)
    using M5FacesBase::begin;

    // ── KEYBOARD3 专用 API ─────────────────────────────────────

    /**
     * @brief  将当前按键值转换为字符（无效键返回 '\0'）
     *         Convert current key to char ('\0' if none/invalid)
     */
    char getChar() const;

    /**
     * @brief  当前是否有按键按下
     *         Is any key currently pressed
     */
    bool isPressed() const
    {
        return _key_raw != KEYBOARD3_KEY_NONE;
    }

    /**
     * @brief  判断是否为可打印字符（空格~波浪号）
     *         Check if the key is a printable ASCII character (space ~ tilde)
     */
    bool isPrintable() const
    {
        return _key_raw >= 0x20 && _key_raw < 0x7F;
    }

    // ── 显示辅助 / Display Helpers ───────────────────────────

    /**
     * @brief  将原始键码转换为可读名称字符串（仅用于控制字符 / 扩展功能键）
     *         Map raw key code to a readable name string (for control/extension keys only)
     * @param  raw  原始键码 / Raw key code
     * @return 名称字符串（如 "BS"、"Tab"、"ESC"、"Fn"、"Alt"、"FN+UP"、"ALT+Q" 等），
     *         若为可打印字符或未知码则返回 NULL
     *         Name string (e.g. "BS", "Tab", "ESC", "Fn", "Alt", "FN+UP", "ALT+Q", ...);
     *         returns NULL if printable or unknown code.
     */
    static const char* keyboard3_code_parse(uint8_t raw);

    /**
     * @brief  Direct 模式矩阵位查表：row + bit 坐标 → 按键名称
     *         Direct mode matrix lookup: (row, bit) → key name
     *
     * OUTPUT_MODE_1 (row=0): Q..P, b0=P .. b9=Q
     * OUTPUT_MODE_2 (row=1): A..DEL, b0=DEL .. b9=A
     * OUTPUT_MODE_3 (row=2): Z..SPC (跳过 alt/ok/aA/SYM/FN),
     *                        b0=SPC, b1=$, b2=M, b3=N, b4=B, b5=V, b6=C, b7=X, b8=Z, b9=0
     *
     * @param  row  矩阵行索引 0-2 / Matrix row index 0-2
     * @param  bit  位索引 0-9 / Bit index 0-9
     * @return 按键名称字符串，越界返回 "?"
     */
    static const char* keyboard3_key_map(int row, int bit);

    /**
     * @brief  Direct 模式 10 字节数据包解析 → 当前按下的键名列表（当前帧状态）
     *         Parse Direct mode 10-byte packet → list of currently pressed key names
     *
     * 格式: "P,Q | A,S [aA,SYM]"  (row间用 '|' 分隔, 修饰键用 [] 包裹)
     * Format: "P,Q | A,S [aA,SYM]"  (rows separated by '|', modifiers in [])
     *
     * @param  raw10  10 字节原始数据 (Byte0=0x0A 长度字节)
     * @param  buf    输出缓冲区
     * @param  bufsz  缓冲区大小
     * @return 实际写入字节数（不含 '\0'），同 snprintf
     */
    static int keyboard3_direct_parse(const uint8_t raw10[10], char* buf, size_t bufsz);

    // ── 按键映射表 / Key Mapping Table (§10) ─────────────────

    /** 映射表列索引 / Keymap column indices */
    enum KeymapCol : uint8_t {
        COL_DEFAULT = 0,  // 默认（小写）/ Default (lowercase)
        COL_AA      = 1,  // aA（大写）/ aA (uppercase)
        COL_SYM     = 2,  // SYM（符号）/ SYM (symbol)
        COL_FN      = 3,  // FN（功能）/ FN (function)
        COL_ALT     = 4,  // ALT / ALT
    };

    static constexpr uint8_t KEYMAP_ROWS = 35;
    static constexpr uint8_t KEYMAP_COLS = 5;

    /**
     * @brief  完整按键映射表 (§10)，35 键 × 5 列（默认/aA/SYM/FN/ALT）
     *         Full key mapping table (§10), 35 keys × 5 columns (default/aA/SYM/FN/ALT)
     * @note   255 = 无效值，主机应忽略 / 255 = invalid, host should ignore
     */
    static const uint8_t KEYMAP[KEYMAP_ROWS][KEYMAP_COLS];
};

#endif  // __M5_FACES_KEYBOARD3_H__
