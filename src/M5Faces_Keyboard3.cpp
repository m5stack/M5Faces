/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "M5Faces_Keyboard3.hpp"

#include <stdio.h>

// ============================================================
// getChar — 将当前键码转换为字符 / Convert the current key code to a character
// ============================================================
char M5Faces_Keyboard3::getChar() const
{
    if (_key_raw == KEYBOARD3_KEY_NONE) return '\0';

    // 可打印 ASCII 字符直接返回 / Directly return printable ASCII
    if (_key_raw >= 0x20 && _key_raw < 0x7F) return static_cast<char>(_key_raw);

    // 控制字符映射 / Control character mapping
    switch (_key_raw) {
        case KEYBOARD3_KEY_BS:
            return '\b';
        case KEYBOARD3_KEY_TAB:
            return '\t';
        case KEYBOARD3_KEY_ENTER:
            return '\n';
        case KEYBOARD3_KEY_ESC:
            return '\x1B';
        default:
            return '\0';
    }
}

// ============================================================
// keyboard3_code_parse — 控制键 / 扩展功能键 / FN导航键 / ALT键 名称映射
//                       Map control/extension/FN-nav/ALT key code to display name
// ============================================================
const char* M5Faces_Keyboard3::keyboard3_code_parse(uint8_t raw)
{
    switch (raw) {
        /* 标准 ASCII 控制字符 / Standard ASCII control characters */
        case 0x00:
            return "NUL";
        case 0x01:
            return "SOH";
        case 0x02:
            return "STX";
        case 0x03:
            return "ETX";
        case 0x04:
            return "EOT";
        case 0x05:
            return "ENQ";
        case 0x06:
            return "ACK";
        case 0x07:
            return "BEL";
        case 0x08:
            return "BS";
        case 0x09:
            return "Tab";
        case 0x0A:
            return "LF";
        case 0x0B:
            return "VT";
        case 0x0C:
            return "FF";
        case 0x0D:
            return "Enter";
        case 0x0E:
            return "SO";
        case 0x0F:
            return "SI";
        case 0x1B:
            return "ESC";
        case 0x7F:
            return "DEL";
        case 0xFF:
            return "NOMAP"; /* 固件无映射哨兵值 / firmware no-mapping sentinel */
        /* KEYBOARD3 修饰键自身 (0x80+) / KEYBOARD3 modifier key self-codes (0x80+) */
        case KEYBOARD3_EXT_FN:
            return "Fn";
        case KEYBOARD3_EXT_ALT:
            return "Alt";
        case KEYBOARD3_EXT_SYM:
            return "Sym";
        case KEYBOARD3_EXT_SHIFT:
            return "Shift";
        case KEYBOARD3_EXT_CAPSLOCK:
            return "CapsLock";
        /* FN 功能键 (§10, 180-193) — 导航/编辑键 */
        /* FN function keys (§10, 180-193) — navigation/editing keys */
        case KEYBOARD3_FN_180:
            return "FN+G";
        case KEYBOARD3_FN_181:
            return "FN+H";
        case KEYBOARD3_FN_182:
            return "FN+J";
        case KEYBOARD3_FN_UP:
            return "FN+UP";
        case KEYBOARD3_FN_INSERT:
            return "FN+INS";
        case KEYBOARD3_FN_TAB:
            return "FN+TAB";
        case KEYBOARD3_FN_HOME:
            return "FN+HOME";
        case KEYBOARD3_FN_END:
            return "FN+END";
        case KEYBOARD3_FN_PAGE_UP:
            return "FN+PGUP";
        case KEYBOARD3_FN_PAGE_DOWN:
            return "FN+PGDN";
        case KEYBOARD3_FN_LEFT:
            return "FN+LEFT";
        case KEYBOARD3_FN_DOWN:
            return "FN+DOWN";
        case KEYBOARD3_FN_RIGHT:
            return "FN+RIGHT";
        /* ALT 组合键 (§10, 144-176) / ALT combo keys (§10, 144-176) */
        case KEYBOARD3_ALT_Q:
            return "ALT+Q";
        case KEYBOARD3_ALT_W:
            return "ALT+W";
        case KEYBOARD3_ALT_E:
            return "ALT+E";
        case KEYBOARD3_ALT_R:
            return "ALT+R";
        case KEYBOARD3_ALT_T:
            return "ALT+T";
        case KEYBOARD3_ALT_Y:
            return "ALT+Y";
        case KEYBOARD3_ALT_U:
            return "ALT+U";
        case KEYBOARD3_ALT_I:
            return "ALT+I";
        case KEYBOARD3_ALT_O:
            return "ALT+O";
        case KEYBOARD3_ALT_P:
            return "ALT+P";
        case KEYBOARD3_ALT_A:
            return "ALT+A";
        case KEYBOARD3_ALT_S:
            return "ALT+S";
        case KEYBOARD3_ALT_D:
            return "ALT+D";
        case KEYBOARD3_ALT_F:
            return "ALT+F";
        case KEYBOARD3_ALT_G:
            return "ALT+G";
        case KEYBOARD3_ALT_H:
            return "ALT+H";
        case KEYBOARD3_ALT_J:
            return "ALT+J";
        case KEYBOARD3_ALT_K:
            return "ALT+K";
        case KEYBOARD3_ALT_L:
            return "ALT+L";
        case KEYBOARD3_ALT_DEL:
            return "ALT+DEL";
        case KEYBOARD3_ALT_Z:
            return "ALT+Z";
        case KEYBOARD3_ALT_X:
            return "ALT+X";
        case KEYBOARD3_ALT_C:
            return "ALT+C";
        case KEYBOARD3_ALT_V:
            return "ALT+V";
        case KEYBOARD3_ALT_B:
            return "ALT+B";
        case KEYBOARD3_ALT_N:
            return "ALT+N";
        case KEYBOARD3_ALT_M:
            return "ALT+M";
        case KEYBOARD3_ALT_DOLLAR:
            return "ALT+$";
        case KEYBOARD3_ALT_0:
            return "ALT+0";
        case KEYBOARD3_ALT_SPACE:
            return "ALT+SPC";
        default:
            return NULL;
    }
}

// ============================================================
// keyboard3_key_map — Direct 模式矩阵位查表: (row, bit) → 按键名称
//                   Direct mode matrix lookup: (row, bit) → key name
// ============================================================
const char* M5Faces_Keyboard3::keyboard3_key_map(int row, int bit)
{
    /* OUTPUT_MODE_1 (row=0): 物理行1 Q..P, b0=P .. b9=Q */
    static const char* const row0[10] = {"P", "O", "I", "U", "Y", "T", "R", "E", "W", "Q"};
    /* OUTPUT_MODE_2 (row=1): 物理行2 A..DEL, b0=DEL .. b9=A */
    static const char* const row1[10] = {"DEL", "L", "K", "J", "H", "G", "F", "D", "S", "A"};
    /* OUTPUT_MODE_3 (row=2): 物理行3+行4，跳过 alt/ok/aA/SYM/FN
     * b0=SPC, b1=$, b2=M, b3=N, b4=B, b5=V, b6=C, b7=X, b8=Z, b9=0
     * (硬件 bit 顺序: 按下 Z 触发 b8, 按下 0 触发 b9) */
    static const char* const row2[10] = {"SPC", "$", "M", "N", "B", "V", "C", "X", "Z", "0"};

    if (bit < 0 || bit > 9) return "?";
    switch (row) {
        case 0:
            return row0[bit];
        case 1:
            return row1[bit];
        case 2:
            return row2[bit];
        default:
            return "?";
    }
}

// ============================================================
// keyboard3_direct_parse — Direct 模式 10 字节数据包解析
//                        Parse full 10-byte Direct mode packet
//
// 输出示例: "P,Q | A,S [aA,SYM]"  (row 用 '|' 分隔, 修饰键用 [] 包裹)
// 无任何键按下时返回空字符串
// Example: "P,Q | A,S [aA,SYM]"; returns an empty string when no key is pressed.
// ============================================================
int M5Faces_Keyboard3::keyboard3_direct_parse(const uint8_t raw10[10], char* buf, size_t bufsz)
{
    if (!buf || bufsz == 0) return 0;
    buf[0] = '\0';
    if (!raw10 || raw10[0] != 0x0A) return 0;

    size_t pos          = 0;
    bool any_key        = false;
    bool row_written[3] = {};
    auto append         = [&](const char* text) {
        if (pos >= bufsz - 1) return false;
        const size_t remaining = bufsz - pos;
        const int written      = snprintf(buf + pos, remaining, "%s", text);
        if (written < 0) return false;
        if (static_cast<size_t>(written) >= remaining) {
            pos      = bufsz - 1;
            buf[pos] = '\0';
            return false;
        }
        pos += static_cast<size_t>(written);
        return true;
    };

    /* 矩阵行 0-2 (Bytes 1-6): active-low, 0=按下 */
    for (int ri = 0; ri < 3; ri++) {
        uint8_t hi        = raw10[1 + ri * 2];
        uint8_t lo        = raw10[2 + ri * 2];
        uint16_t key_bits = (uint16_t)((hi & 0x03) << 8) | lo;

        bool first_in_row = true;
        for (int b = 0; b < 10; b++) {
            if (key_bits & (1 << b)) continue; /* 1 = 未按下 */
            /* 0 = 按下 */
            if (!row_written[ri] && any_key) {
                if (!append(" | ")) goto done;
            }
            row_written[ri] = true;
            if (!first_in_row && !append(",")) goto done;
            if (!append(keyboard3_key_map(ri, b))) goto done;
            first_in_row = false;
            any_key      = true;
        }
    }

    /* 修饰键行 (Bytes 7-8): active-low, bit0=aA, bit1=ALT, bit2=ENT, bit3=SYM, bit4=FN */
    {
        uint8_t mhi      = raw10[7];
        uint8_t mlo      = raw10[8];
        uint16_t mod_raw = (uint16_t)((mhi & 0x03) << 8) | mlo;
        static const struct {
            uint16_t mask;
            const char* name;
        } mods[] = {
            {0x01, "aA"}, {0x02, "ALT"}, {0x04, "ENT"}, {0x08, "SYM"}, {0x10, "FN"},
        };
        bool any_mod = false;
        for (int m = 0; m < 5; m++) {
            if (mod_raw & mods[m].mask) continue; /* 1 = 未按下 */
            if (!any_mod) {
                if (!append(any_key ? " [" : "[")) goto done;
            } else if (!append(",")) {
                goto done;
            }
            if (!append(mods[m].name)) goto done;
            any_mod = true;
        }
        if (any_mod) append("]");
    }

done:
    return static_cast<int>(pos);
}

// ============================================================
// KEYMAP — 完整按键映射表 (§10)
//          Full key mapping table (§10)
//
// 35 键 × 5 列: [默认, aA, SYM, FN, ALT]
// 35 keys × 5 cols: [default, aA, SYM, FN, ALT]
// 255 = 无效值 / 255 = invalid (host should ignore)
// ============================================================
const uint8_t M5Faces_Keyboard3::KEYMAP[KEYMAP_ROWS][KEYMAP_COLS] = {
    /*  0 */ {'q', 'Q', '#', '~', 144},
    /*  1 */ {'w', 'W', '1', '^', 145},
    /*  2 */ {'e', 'E', '2', '&', 146},
    /*  3 */ {'r', 'R', '3', '`', 147},
    /*  4 */ {'t', 'T', '(', '<', 148},
    /*  5 */ {'y', 'Y', ')', '>', 149},
    /*  6 */ {'u', 'U', '_', '{', 150},
    /*  7 */ {'i', 'I', '-', '}', 151},
    /*  8 */ {'o', 'O', '+', '[', 152},
    /*  9 */ {'p', 'P', '@', ']', 153},
    /* 10 */ {'a', 'A', '*', '|', 154},
    /* 11 */ {'s', 'S', '4', '=', 155},
    /* 12 */ {'d', 'D', '5', '\\', 156},
    /* 13 */ {'f', 'F', '6', '%', 157},
    /* 14 */ {'g', 'G', '/', 180, 158},
    /* 15 */ {'h', 'H', ':', 181, 159},
    /* 16 */ {'j', 'J', ';', 182, 160},
    /* 17 */ {'k', 'K', '\'', 183, 161},
    /* 18 */ {'l', 'L', '"', 184, 162},
    /* 19 */ {8, 8, 127, 8, 163},        // del: BS(8), SYM=DEL(127)
    /* 20 */ {255, 255, 255, 255, 255},  // aA 修饰键 / aA modifier
    /* 21 */ {'z', 'Z', '7', 186, 165},
    /* 22 */ {'x', 'X', '8', 187, 166},
    /* 23 */ {'c', 'C', '9', 188, 167},
    /* 24 */ {'v', 'V', '?', 189, 168},
    /* 25 */ {'b', 'B', '!', 190, 169},
    /* 26 */ {'n', 'N', ',', 191, 170},
    /* 27 */ {'m', 'M', '.', 192, 171},
    /* 28 */ {'$', '$', 255, 193, 172},
    /* 29 */ {13, 13, 13, 13, 13},                     // Enter (0x0D)
    /* 30 */ {255, 255, 255, 255, 255},                // 特殊键 / special
    /* 31 */ {'0', '0', KEYBOARD3_KEY_ESC, '0', 175},  // SYM+0 = ESC (0x1B)
    /* 32 */ {' ', ' ', ' ', ' ', 176},                // SPACE
    /* 33 */ {255, 255, 255, 255, 255},                // 特殊键 / special
    /* 34 */ {255, 255, 255, 255, 255},                // 特殊键 / special
};
