/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "M5Faces_Gamepad3.hpp"
#include <stdio.h>

// ============================================================
// update — 重写以同时维护 gamepad3_state_t
//          Override to also maintain gamepad3_state_t
// ============================================================
bool M5Faces_Gamepad3::update()
{
    if (!_initialized) return false;
    _key_changed = false;

    // INT 引脚为高电平 = 无新数据，跳过 I2C 读取
    // INT pin HIGH = no new data, skip I2C read
    if (_int_pin >= 0) {
        if (_int_mode) {
            if (!_int_flag) return false;
            _int_flag = false;
        } else if (!isInterruptLow()) {
            return false;
        }
    }

    uint8_t raw = 0xFF;  // active-low 默认全部未按下 / active-low: all unpressed by default
    if (readReg(M5FACES_REG_KEY, &raw) != M5FACES_OK) return false;

    // Gamepad3 V03/V04 在无事件时可能保留 0x00 发送缓冲；盲轮询时将其视为无新样本。
    // Gamepad3 V03/V04 may expose a stale 0x00 TX buffer; ignore it during blind polling.
    if (_int_pin < 0 && raw == 0x00) return false;

    _gp_state_prev = _gp_state;
    _gp_state.raw  = raw;

    // 同步基类 _key_raw（供 keyChanged() / getKey() 使用）
    // Sync base _key_raw (for keyChanged() / getKey())
    _key_prev    = _key_raw;
    _key_raw     = raw;
    _key_changed = (_key_raw != _key_prev);
    return _key_changed;
}

// ============================================================
// isButtonPressed — active-low 极性处理（位为 0 = 按下）
//                   active-low polarity: bit value 0 means pressed
// ============================================================
bool M5Faces_Gamepad3::isButtonPressed(gamepad3_btn_t btn) const
{
    // active-low: 对应位为 0 表示按下
    // active-low: bit = 0 means pressed
    return (_gp_state.raw & static_cast<uint8_t>(btn)) == 0;
}

// ============================================================
// isButtonJustPressed — 上一状态未按下（位为1），当前已按下（位为0）
//                       Previous: bit=1 (not pressed), Current: bit=0 (pressed)
// ============================================================
bool M5Faces_Gamepad3::isButtonJustPressed(gamepad3_btn_t btn) const
{
    uint8_t mask = static_cast<uint8_t>(btn);
    return (_gp_state_prev.raw & mask) != 0  // 之前未按下 / was not pressed
           && (_gp_state.raw & mask) == 0;   // 现在按下   / now pressed
}

// ============================================================
// isButtonJustReleased — 上一状态按下（位为0），当前未按下（位为1）
//                        Previous: bit=0 (pressed), Current: bit=1 (not pressed)
// ============================================================
bool M5Faces_Gamepad3::isButtonJustReleased(gamepad3_btn_t btn) const
{
    uint8_t mask = static_cast<uint8_t>(btn);
    return (_gp_state_prev.raw & mask) == 0  // 之前按下   / was pressed
           && (_gp_state.raw & mask) != 0;   // 现在未按下 / now not pressed
}

// ============================================================
// gamepad3_code_parse — 将原始字节解析为按下按键名称列表
//                        Parse raw byte to pressed button name list
// ============================================================
int M5Faces_Gamepad3::gamepad3_code_parse(uint8_t raw, char* buf, size_t bufsz)
{
    if (!buf || bufsz == 0) return 0;
    buf[0] = '\0';

    // active-low 取反：pressed = 1 / active-low invert: pressed = 1
    uint8_t pressed = (uint8_t)(~raw);
    if (pressed == 0) {
        buf[0] = '\0';
        return 0;
    }

    static const char* btn_names[] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "SELECT", "START"};
    size_t pos                     = 0;
    int cnt                        = 0;
    auto append                    = [&](const char* text) {
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
    for (int i = 0; i < 8; i++) {
        if (pressed & (1 << i)) {
            cnt++;
            if (pos < bufsz - 1) {
                char token[16];
                snprintf(token, sizeof(token), "%s%s", cnt > 1 ? "+" : "", btn_names[i]);
                append(token);
            }
        }
    }
    return cnt;
}
