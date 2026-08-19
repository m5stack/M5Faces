/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "M5FacesPoller.hpp"
#include "M5Faces_Keyboard3.hpp"  // for keyboard3_direct_parse in log output
#include <string.h>

/* ---- 回调与模式设置 / Callback and mode setters ---- */

/** @brief 设置 Normal 模式回调及上下文 / Set the Normal-mode callback and context. */
void M5FacesPoller::setNormalKeyCb(NormalKeyCb cb, void *ctx)
{
    _normal_cb  = cb;
    _normal_ctx = ctx;
}

/** @brief 设置 Direct 模式回调及上下文 / Set the Direct-mode callback and context. */
void M5FacesPoller::setDirectEventCb(DirectEventCb cb, void *ctx)
{
    _direct_cb  = cb;
    _direct_ctx = ctx;
}

/** @brief 选择 Direct 模式的轮询或中断路径 / Select the polling or interrupt path for Direct mode. */
void M5FacesPoller::setPollingMode(bool use_polling)
{
    _polling = use_polling;
}

/* ---- 周期分发器 / Tick dispatcher ---- */

/** @brief 按当前设备模式执行一次处理周期 / Run one processing cycle for the current device mode. */
void M5FacesPoller::tick(M5FacesBase *faces, m5faces_mode_t mode, uint8_t model_id)
{
    if (!faces) return;

    if (mode == M5FACES_MODE_NORMAL) {
        _dir_irq_active    = false;
        _dir_irq_prev_data = {};
        _dir_irq_hold_cnt  = 0;
        _tick_normal(faces);
    } else if (mode == M5FACES_MODE_DIRECT) {
        if (_polling) {
            _dir_irq_active    = false;
            _dir_irq_prev_data = {};
            _dir_irq_hold_cnt  = 0;
            _tick_direct_poll(faces, model_id);
        } else {
            _tick_direct_int(faces, model_id);
        }
    }
}

/* ======================================================================
 * Normal 模式轮询 / Normal mode polling
 * ====================================================================== */

/** @brief 读取并分发一次 Normal 模式按键变化 / Read and dispatch one Normal-mode key change. */
void M5FacesPoller::_tick_normal(M5FacesBase *faces)
{
    faces->update();

    /* ---- I2C raw 字节变化追踪（仅变化时打印，附带上一状态的连续次数）
     * I2C raw byte change tracking (log only on change, include repeat count) ---- */
    {
        uint8_t cur = faces->getKey();
        if (!_norm_init) {
            _norm_prev = cur;
            _norm_init = true;
            _norm_cnt  = 1;
        } else if (cur != _norm_prev) {
            M5FACES_LOG_I("I2C raw: 0x%02X -> 0x%02X (x%lu)", _norm_prev, cur, (unsigned long)_norm_cnt);
            _norm_prev = cur;
            _norm_cnt  = 1;
        } else {
            _norm_cnt++;
        }
    }

    if (faces->keyChanged()) {
        uint8_t raw = faces->getKey();
        if (_normal_cb) {
            _normal_cb(faces, raw, _normal_ctx);
        }
    } else {
        /* 中断模式诊断：INT 引脚低电平但键值未变化
         * Interrupt mode diagnostic: INT pin is low but no key change detected */
        if (!_polling && faces->isInterruptLow()) {
            M5FACES_LOG_D(
                "update: no change (INT LOW, raw=0x%02X) — "
                "possible missed release / consecutive same-key press",
                (int)faces->getKey());
        }
    }
}

/** @brief 仅在原始 Direct 帧变化时输出诊断日志 / Emit diagnostics only when the raw Direct frame changes. */
void M5FacesPoller::_trace_direct_frame(const m5faces_direct_data_t &ddata, uint8_t model_id)
{
    /* ---- Direct raw 10 字节帧变化追踪（仅变化时打印）
     * Direct raw 10-byte frame change tracking (log only on change)
     *
     * 注意：这里的 "Direct evt raw" 是 pollDirect() 出队后的“事件级变化日志”，
     * 不是每一次底层 reg 0x00 真实读取的逐条镜像。
     * 若要观察每一次真实读取，请启用 M5FacesBase.cpp 中的
     * M5FACES_DIRECT_TRACE_EVERY_READ。 ---- */
    if (!_dir_init) {
        memcpy(_dir_prev, ddata.raw, 10);
        _dir_init = true;
        _dir_cnt  = 1;
        return;
    }

    if (memcmp(ddata.raw, _dir_prev, 10) != 0) {
        char keys[96] = {};
        if (model_id == M5FACES_MODEL_KEYBOARD3) {
            M5Faces_Keyboard3::keyboard3_direct_parse(ddata.raw, keys, sizeof(keys));
        }
        bool all_zero = true;
        for (uint8_t value : ddata.raw) {
            if (value != 0) {
                all_zero = false;
                break;
            }
        }
        // 全零表示轮询期间没有新事件，不是帧头错误。
        // An all-zero read means no pending polling event, not a bad header.
        const char *status =
            all_zero ? "NO_DATA" : ((ddata.raw[0] == 0x0A) ? (ddata.valid ? "OK" : "CRC_NG") : "HDR_NG");
        M5FACES_LOG_I(
            "Direct evt raw: "
            "[%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X] -> "
            "[%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X] "
            "%s%s%s%s (x%lu)",
            _dir_prev[0], _dir_prev[1], _dir_prev[2], _dir_prev[3], _dir_prev[4], _dir_prev[5], _dir_prev[6],
            _dir_prev[7], _dir_prev[8], _dir_prev[9], ddata.raw[0], ddata.raw[1], ddata.raw[2], ddata.raw[3],
            ddata.raw[4], ddata.raw[5], ddata.raw[6], ddata.raw[7], ddata.raw[8], ddata.raw[9], status,
            keys[0] ? " keys=[" : "", keys[0] ? keys : "", keys[0] ? "]" : "", (unsigned long)_dir_cnt);
        memcpy(_dir_prev, ddata.raw, 10);
        _dir_cnt = 1;
        return;
    }

    _dir_cnt++;
}

/** @brief 记录并分发一个有效 Direct 事件 / Log and dispatch one valid Direct event. */
bool M5FacesPoller::_dispatch_direct_event(M5FacesBase *faces, uint8_t model_id, m5faces_direct_event_t evt,
                                           const m5faces_direct_data_t &ddata)
{
    switch (evt) {
        case M5FACES_DIRECT_PRESSED: {
            char keys[96] = {};
            if (model_id == M5FACES_MODEL_KEYBOARD3)
                M5Faces_Keyboard3::keyboard3_direct_parse(ddata.raw, keys, sizeof(keys));
            M5FACES_LOG_I(
                "Direct evt pressed %s "
                "[%02X %02X %02X %02X %02X %02X %02X %02X %02X]%s%s%s",
                ddata.valid ? "OK" : "NG", ddata.raw[1], ddata.raw[2], ddata.raw[3], ddata.raw[4], ddata.raw[5],
                ddata.raw[6], ddata.raw[7], ddata.raw[8], ddata.raw[9], keys[0] ? " keys=[" : "", keys,
                keys[0] ? "]" : "");
            if (_direct_cb) _direct_cb(evt, &ddata, faces->getDirectHoldCount(), _direct_ctx);
            return true;
        }
        case M5FACES_DIRECT_CHANGED: {
            char keys[96] = {};
            if (model_id == M5FACES_MODEL_KEYBOARD3)
                M5Faces_Keyboard3::keyboard3_direct_parse(ddata.raw, keys, sizeof(keys));
            M5FACES_LOG_I(
                "Direct evt changed %s "
                "[%02X %02X %02X %02X %02X %02X %02X %02X %02X]%s%s%s (hold x%lu)",
                ddata.valid ? "OK" : "NG", ddata.raw[1], ddata.raw[2], ddata.raw[3], ddata.raw[4], ddata.raw[5],
                ddata.raw[6], ddata.raw[7], ddata.raw[8], ddata.raw[9], keys[0] ? " keys=[" : "", keys,
                keys[0] ? "]" : "", (unsigned long)faces->getDirectHoldCount());
            if (_direct_cb) _direct_cb(evt, &ddata, faces->getDirectHoldCount(), _direct_ctx);
            return true;
        }
        case M5FACES_DIRECT_RELEASED: {
            const m5faces_direct_data_t &prev = faces->getDirectPrevData();
            char prev_keys[96]                = {};
            if (model_id == M5FACES_MODEL_KEYBOARD3)
                M5Faces_Keyboard3::keyboard3_direct_parse(prev.raw, prev_keys, sizeof(prev_keys));
            M5FACES_LOG_I(
                "Direct evt released "
                "[%02X %02X %02X %02X %02X %02X %02X %02X %02X]%s%s%s (held x%lu)",
                prev.raw[1], prev.raw[2], prev.raw[3], prev.raw[4], prev.raw[5], prev.raw[6], prev.raw[7], prev.raw[8],
                prev.raw[9], prev_keys[0] ? " keys=[" : "", prev_keys, prev_keys[0] ? "]" : "",
                (unsigned long)faces->getDirectHoldCount());
            if (_direct_cb) _direct_cb(evt, &ddata, faces->getDirectHoldCount(), _direct_ctx);
            return true;
        }
        case M5FACES_DIRECT_HELD:
        case M5FACES_DIRECT_IDLE:
        default:
            return false;
    }
}

/** @brief 判断 Direct 帧中是否存在活动按键 / Check whether a Direct frame contains active keys. */
bool M5FacesPoller::_direct_has_keys(const m5faces_direct_data_t &ddata) const
{
    for (int row = 0; row < 3; row++) {
        if (ddata.rows[row].key_bits != 0x3FFU) {
            return true;
        }
    }

    return ddata.modifier.aA || ddata.modifier.alt || ddata.modifier.enter || ddata.modifier.sym || ddata.modifier.fn;
}

/* ======================================================================
 * Direct 模式轮询路径（寄存器轮询，无 GPIO）
 * Direct mode polling path (register polling, no GPIO)
 * ====================================================================== */

/** @brief 从寄存器 FIFO 读取并分发 Direct 事件 / Read and dispatch Direct events from the register FIFO. */
void M5FacesPoller::_tick_direct_poll(M5FacesBase *faces, uint8_t model_id)
{
    static constexpr int kMaxEventsPerTick = 8;

    for (int idx = 0; idx < kMaxEventsPerTick; idx++) {
        m5faces_direct_data_t ddata = {};
        m5faces_direct_event_t evt  = faces->pollDirect(&ddata);

        _trace_direct_frame(ddata, model_id);

        if (_dispatch_direct_event(faces, model_id, evt, ddata)) {
            continue;
        }

        break;
    }
}

/* ======================================================================
 * Direct 模式中断路径（使用 GPIO INT 引脚）
 * Direct mode interrupt path (uses GPIO INT pin)
 *
 * 按 [M5Faces_report.md] 中 7.9 / 7.10 的当前约束，IRQ 路径保持独立：
 * - 只在外部 INT 触发后读取当前 1 帧
 * - 不复用 pollDirect() 的轮询 FIFO
 * - 事件分类仅基于 IRQ 路径自身的上一帧状态
 * ====================================================================== */

/** @brief 在 INT 有效时读取并分发一个 Direct 帧 / Read and dispatch one Direct frame while INT is asserted. */
void M5FacesPoller::_tick_direct_int(M5FacesBase *faces, uint8_t model_id)
{
    if (!faces->isInterruptLow()) return;

    m5faces_direct_data_t ddata = {};
    m5faces_err_t derr          = faces->updateDirect(&ddata);
    if (derr == M5FACES_OK) {
        char keys[96] = {};
        if (model_id == M5FACES_MODEL_KEYBOARD3) {
            M5Faces_Keyboard3::keyboard3_direct_parse(ddata.raw, keys, sizeof(keys));
        }

        M5FACES_LOG_I(
            "Direct irq %s "
            "[%02X %02X %02X %02X %02X %02X %02X %02X %02X]%s%s%s",
            ddata.valid ? "OK" : "NG", ddata.raw[1], ddata.raw[2], ddata.raw[3], ddata.raw[4], ddata.raw[5],
            ddata.raw[6], ddata.raw[7], ddata.raw[8], ddata.raw[9], keys[0] ? " keys=[" : "", keys, keys[0] ? "]" : "");

        const bool has_keys = _direct_has_keys(ddata);
        const bool raw_changed =
            !_dir_irq_active || (memcmp(ddata.raw, _dir_irq_prev_data.raw, sizeof(ddata.raw)) != 0);

        if (has_keys) {
            if (!_dir_irq_active) {
                _dir_irq_active    = true;
                _dir_irq_prev_data = ddata;
                _dir_irq_hold_cnt  = 1;
                if (_direct_cb) {
                    _direct_cb(M5FACES_DIRECT_PRESSED, &ddata, _dir_irq_hold_cnt, _direct_ctx);
                }
            } else if (raw_changed) {
                _dir_irq_prev_data = ddata;
                _dir_irq_hold_cnt  = 1;
                if (_direct_cb) {
                    _direct_cb(M5FACES_DIRECT_CHANGED, &ddata, _dir_irq_hold_cnt, _direct_ctx);
                }
            } else {
                _dir_irq_hold_cnt++;
            }
            return;
        }

        if (_dir_irq_active) {
            const uint32_t hold_cnt = _dir_irq_hold_cnt;
            _dir_irq_active         = false;
            _dir_irq_prev_data      = {};
            _dir_irq_hold_cnt       = 0;
            if (_direct_cb) {
                _direct_cb(M5FACES_DIRECT_RELEASED, &ddata, hold_cnt, _direct_ctx);
            }
        }
    } else if (derr != M5FACES_NO_DATA) {
        M5FACES_LOG_W("updateDirect err=%d", (int)derr);
    }
}
