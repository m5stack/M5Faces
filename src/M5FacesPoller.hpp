/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file M5FacesPoller.hpp
 * @brief M5Faces 轮询/中断封装器
 *        Poll / interrupt loop helper for M5FacesBase drivers.
 *
 * 将 Normal 和 Direct 两种模式的轮询逻辑封装为单次 tick() 调用，
 * 通过注册的回调将事件传递给上层应用。
 *
 * Encapsulates Normal + Direct polling into a single tick() call,
 * delivering events to the application via registered callbacks.
 *
 * 典型用法 / Typical usage:
 * @code
 *   M5FacesPoller poller;
 *
 *   // 轮询模式（无 GPIO INT 引脚）/ Polling mode (no GPIO INT pin)
 *   poller.setPollingMode(true);
 *
 *   // 注册回调 / Register callbacks
 *   poller.setNormalKeyCb([](M5FacesBase* f, uint8_t raw, void* ctx) {
 *       // handle Normal mode key
 *   }, nullptr);
 *   poller.setDirectEventCb([](m5faces_direct_event_t evt,
 *                              const m5faces_direct_data_t* data,
 *                              uint32_t hold_cnt, void* ctx) {
 *       // handle Direct mode event
 *   }, nullptr);
 *
 *   // 主循环 / Main loop (every ~20 ms)
 *   while (true) {
 *       poller.tick(faces, cur_mode, model_id);
 *       vTaskDelay(pdMS_TO_TICKS(20));
 *   }
 * @endcode
 */

#pragma once

#include "M5FacesBase.hpp"

/**
 * @brief M5Faces 轮询/中断封装器
 *        M5Faces poll/interrupt helper — wraps Normal + Direct mode event loops.
 */
class M5FacesPoller {
public:
    /**
     * @brief Normal 模式按键回调（keyChanged() 为 true 时触发）
     *        Normal mode key callback — fired when keyChanged() is true.
     *
     * @param faces     驱动指针（可向下转型至具体子类）/ Driver pointer (safe to downcast)
     * @param raw       本次变化的原始键值 / Raw key byte that changed
     * @param ctx       用户自定义上下文 / User-supplied context pointer
     */
    using NormalKeyCb = void (*)(M5FacesBase *faces, uint8_t raw, void *ctx);

    /**
     * @brief Direct 模式事件回调（PRESSED / CHANGED / RELEASED 时触发）
     *        Direct mode event callback — fired for PRESSED, CHANGED, RELEASED events.
     *
     * @param evt       事件类型 / Event type
     * @param data      当前帧数据（RELEASED 时为释放帧）
     *                  Current frame data (release frame for RELEASED)
     * @param hold_cnt  从按下到当前的轮询计数 / Polling cycles since press
     * @param ctx       用户自定义上下文 / User-supplied context pointer
     */
    using DirectEventCb = void (*)(m5faces_direct_event_t evt, const m5faces_direct_data_t *data, uint32_t hold_cnt,
                                   void *ctx);

    /** @brief 构造使用寄存器轮询模式的事件分发器 / Construct an event dispatcher using register polling by default. */
    M5FacesPoller() = default;

    /**
     * @brief 注册 Normal 模式按键回调 / Register Normal mode key callback.
     * @param cb   回调函数指针，传 nullptr 可取消注册 / Callback pointer, nullptr to unregister.
     * @param ctx  传递给回调的用户上下文 / Context passed to callback.
     */
    void setNormalKeyCb(NormalKeyCb cb, void *ctx = nullptr);

    /**
     * @brief 注册 Direct 模式事件回调 / Register Direct mode event callback.
     * @param cb   回调函数指针，传 nullptr 可取消注册 / Callback pointer, nullptr to unregister.
     * @param ctx  传递给回调的用户上下文 / Context passed to callback.
     */
    void setDirectEventCb(DirectEventCb cb, void *ctx = nullptr);

    /**
     * @brief 选择轮询模式或硬件中断模式
     *        Select register-polling mode or hardware interrupt mode.
     *
     * @param use_polling  true  = 寄存器轮询模式（无 GPIO，每次 tick() 直接读 I2C）
     *                             Register polling mode (no GPIO, reads I2C every tick).
     *                     false = 硬件中断模式（通过 isInterruptLow() 判断 INT 引脚）
     *                             Hardware interrupt mode (checks INT pin via isInterruptLow()).
     *
     * 默认为 true（轮询模式）/ Defaults to true (polling mode).
     */
    void setPollingMode(bool use_polling);

    /**
     * @brief 每个主循环周期调用一次（约 20 ms）
     *        Call once per main loop iteration (~20 ms).
     *
     * 根据 mode 参数自动分发到 Normal 或 Direct 轮询路径。
     * Dispatches to Normal or Direct polling path based on mode.
     *
     * @param faces     已初始化的驱动对象指针（不可为 nullptr）
     *                  Initialized driver object pointer (must not be nullptr).
     * @param mode      当前操作模式 (M5FACES_MODE_NORMAL 或 M5FACES_MODE_DIRECT)
     *                  Current operating mode.
     * @param model_id  型号 ID（M5FACES_MODEL_*），用于日志中解析键名
     *                  Model ID used to parse key names in log output.
     *                  传 0 表示未知 / Pass 0 for unknown.
     */
    void tick(M5FacesBase *faces, m5faces_mode_t mode, uint8_t model_id = 0);

private:
    NormalKeyCb _normal_cb   = nullptr;
    void *_normal_ctx        = nullptr;
    DirectEventCb _direct_cb = nullptr;
    void *_direct_ctx        = nullptr;
    bool _polling            = true;

    /* Normal 模式：原始键值变化追踪 / Normal mode: raw key change tracking */
    uint8_t _norm_prev = 0;
    bool _norm_init    = false;
    uint32_t _norm_cnt = 0;

    /* Direct 模式：10 字节帧变化追踪 / Direct mode: 10-byte frame change tracking */
    uint8_t _dir_prev[10]                    = {};
    bool _dir_init                           = false;
    uint32_t _dir_cnt                        = 0;
    bool _dir_irq_active                     = false;
    m5faces_direct_data_t _dir_irq_prev_data = {};
    uint32_t _dir_irq_hold_cnt               = 0;

    /** @brief 执行一次 Normal 模式更新并分发键值变化 / Run one Normal-mode update and dispatch key changes. */
    void _tick_normal(M5FacesBase *faces);

    /** @brief 记录变化后的 Direct 原始帧及解析结果 / Log a changed Direct frame and its parsed result. */
    void _trace_direct_frame(const m5faces_direct_data_t &ddata, uint8_t model_id);

    /** @brief 将 Direct 事件记录并传递给已注册回调 / Log and deliver a Direct event to the registered callback. */
    bool _dispatch_direct_event(M5FacesBase *faces, uint8_t model_id, m5faces_direct_event_t evt,
                                const m5faces_direct_data_t &ddata);

    /** @brief 判断 Direct 帧中是否有按键处于按下状态 / Check whether a Direct frame has any pressed key. */
    bool _direct_has_keys(const m5faces_direct_data_t &ddata) const;

    /** @brief 执行一次 Direct 寄存器轮询处理 / Run one Direct register-polling cycle. */
    void _tick_direct_poll(M5FacesBase *faces, uint8_t model_id);

    /** @brief 执行一次 Direct GPIO 中断处理 / Run one Direct GPIO-interrupt cycle. */
    void _tick_direct_int(M5FacesBase *faces, uint8_t model_id);
};
