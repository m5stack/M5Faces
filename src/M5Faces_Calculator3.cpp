/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "M5Faces_Calculator3.hpp"

// ============================================================
// getChar — 将当前键码转换为字符 / Convert the current key code to a character
// ============================================================
char M5Faces_Calculator3::getChar() const
{
    if (_key_raw == CALC3_KEY_NONE) return '\0';

    // 可打印 ASCII 字符直接返回 / Directly return printable ASCII
    if (_key_raw >= 0x20 && _key_raw < 0x7F) return static_cast<char>(_key_raw);

    // 控制字符映射 / Control character mapping
    switch (_key_raw) {
        case CALC3_KEY_BS:
            return '\b';
        case CALC3_KEY_ENTER:
            return '\n';
        default:
            return '\0';
    }
}

// ============================================================
// calc3_code_parse — 将控制键码映射为显示名称
//                     Map control key code to display name
// ============================================================
const char* M5Faces_Calculator3::calc3_code_parse(uint8_t raw)
{
    switch (raw) {
        case 0x00:
            return "NUL";
        case 0x08:
            return "BS";
        case 0x0D:
            return "Enter";
        default:
            return NULL;
    }
}
