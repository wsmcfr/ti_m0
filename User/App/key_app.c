/**
 * @file    key_app.c
 * @brief   四按键应用层实现。
 *
 * @details 本模块参考现有按键例程的 key_down/key_up 思路，但补充稳定消抖和长按事件。
 */

#include "key_app.h"

#include <string.h>

#include "scheduler.h"

/* 按键消抖确认时间，原始状态保持该时间后才更新稳定状态。 */
#define KEY_APP_DEBOUNCE_MS        (20U)

/* 长按判定时间，按键稳定按下超过该时间后触发一次 long_press 事件。 */
#define KEY_APP_LONG_PRESS_MS      (1000U)

/* 当前已经确认稳定的按键状态。 */
static uint8_t s_key_stable_mask = 0U;

/* 最近一次读取到的原始按键状态。 */
static uint8_t s_key_last_raw_mask = 0U;

/* 原始状态发生变化的 tick，用于消抖计时。 */
static uint32_t s_key_raw_change_ms = 0U;

/* 每个按键稳定按下的起始 tick，用于长按判断。 */
static uint32_t s_key_press_start_ms[KEY_DRIVER_COUNT];

/* 已经为当前按下周期触发过长按事件的按键掩码。 */
static uint8_t s_key_long_reported_mask = 0U;

/* 自上次消费以来累积的按下事件。 */
static uint8_t s_key_down_event_mask = 0U;

/* 自上次消费以来累积的抬起事件。 */
static uint8_t s_key_up_event_mask = 0U;

/* 自上次消费以来累积的长按事件。 */
static uint8_t s_key_long_event_mask = 0U;

static void Key_AppUpdateStableState(uint8_t new_stable_mask, uint32_t now_ms);
static void Key_AppUpdateLongPress(uint32_t now_ms);

/**
 * @brief  更新稳定状态并生成按下/抬起边沿事件。
 *
 * @note   new_stable_mask 已经经过消抖确认，本函数只负责和上一稳定值比较。
 *
 * @param  new_stable_mask 新的稳定按下状态。
 * @param  now_ms          当前系统 tick。
 * @return 无。
 */
static void Key_AppUpdateStableState(uint8_t new_stable_mask, uint32_t now_ms)
{
    /* 保存稳定状态变化的 bit。 */
    uint8_t changed = (uint8_t)(s_key_stable_mask ^ new_stable_mask);
    /* 保存新按下事件。 */
    uint8_t down = (uint8_t)(changed & new_stable_mask);
    /* 保存新抬起事件。 */
    uint8_t up = (uint8_t)(changed & (uint8_t)(~new_stable_mask));

    /* 没有稳定状态变化时无需更新事件。 */
    if (changed == 0U)
    {
        /* 稳定状态未变化。 */
        return;
    }

    /* 记录按下边沿事件，事件会一直保持到被消费。 */
    s_key_down_event_mask |= down;
    /* 记录抬起边沿事件，事件会一直保持到被消费。 */
    s_key_up_event_mask |= up;
    /* 更新当前稳定按下状态。 */
    s_key_stable_mask = new_stable_mask;

    /* 对新按下的按键记录起始时间。 */
    for (uint8_t i = 0U; i < KEY_DRIVER_COUNT; i++)
    {
        /* 当前按键对应的 bit。 */
        uint8_t bit = (uint8_t)(1U << i);

        /* 新按下时记录按下起点，并清除该按键上一轮长按已上报标志。 */
        if ((down & bit) != 0U)
        {
            /* 保存该键稳定按下的起始 tick。 */
            s_key_press_start_ms[i] = now_ms;
            /* 允许本次按下周期重新触发长按事件。 */
            s_key_long_reported_mask &= (uint8_t)(~bit);
        }

        /* 抬起时清除长按已上报标志，下一次按下可重新计时。 */
        if ((up & bit) != 0U)
        {
            /* 清除该键长按已上报状态。 */
            s_key_long_reported_mask &= (uint8_t)(~bit);
        }
    }
}

/**
 * @brief  根据稳定按下时长生成长按事件。
 *
 * @note   每次按下周期每个按键只产生一次长按事件，避免应用层重复处理。
 *
 * @param  now_ms 当前系统 tick。
 * @return 无。
 */
static void Key_AppUpdateLongPress(uint32_t now_ms)
{
    /* 逐个检查四个按键。 */
    for (uint8_t i = 0U; i < KEY_DRIVER_COUNT; i++)
    {
        /* 当前按键对应的 bit。 */
        uint8_t bit = (uint8_t)(1U << i);

        /* 只有稳定按下且尚未上报长按的按键需要计时。 */
        if (((s_key_stable_mask & bit) != 0U) &&
            ((s_key_long_reported_mask & bit) == 0U) &&
            ((uint32_t)(now_ms - s_key_press_start_ms[i]) >= KEY_APP_LONG_PRESS_MS))
        {
            /* 记录长按事件，等待应用层消费。 */
            s_key_long_event_mask |= bit;
            /* 标记该按键本次按下周期已经上报过长按。 */
            s_key_long_reported_mask |= bit;
        }
    }
}

/**
 * @brief  初始化按键应用层。
 *
 * @note   清空消抖状态和事件缓存，并读取一次当前原始状态作为起点。
 *
 * @param  无。
 * @return 无。
 */
void Key_AppInit(void)
{
    /* 初始化底层 GPIO 读取入口。 */
    Key_DriverInit();
    /* 清空按键按下起点数组。 */
    memset(s_key_press_start_ms, 0, sizeof(s_key_press_start_ms));
    /* 读取当前原始状态作为初始稳定状态，避免上电时产生假边沿。 */
    s_key_last_raw_mask = Key_DriverReadRawMask();
    /* 初始稳定状态等于当前原始状态。 */
    s_key_stable_mask = s_key_last_raw_mask;
    /* 记录当前 tick 作为消抖起点。 */
    s_key_raw_change_ms = Scheduler_GetTick();
    /* 清空所有事件缓存。 */
    s_key_down_event_mask = 0U;
    s_key_up_event_mask = 0U;
    s_key_long_event_mask = 0U;
    s_key_long_reported_mask = 0U;
}

/**
 * @brief  按键周期任务。
 *
 * @note   建议 5~10ms 调用一次。任务先做原始状态消抖，再生成边沿和长按事件。
 *
 * @param  无。
 * @return 无。
 */
void Key_AppTask(void)
{
    /* 读取当前系统 tick。 */
    uint32_t now_ms = Scheduler_GetTick();
    /* 读取底层原始按键状态。 */
    uint8_t raw_mask = Key_DriverReadRawMask();

    /* 原始状态变化时重置消抖计时。 */
    if (raw_mask != s_key_last_raw_mask)
    {
        /* 保存新的原始状态。 */
        s_key_last_raw_mask = raw_mask;
        /* 记录变化时刻，等待状态稳定。 */
        s_key_raw_change_ms = now_ms;
    }

    /* 原始状态保持足够久后，才更新稳定状态。 */
    if ((uint32_t)(now_ms - s_key_raw_change_ms) >= KEY_APP_DEBOUNCE_MS)
    {
        /* 将确认稳定的原始状态发布到应用层事件状态。 */
        Key_AppUpdateStableState(raw_mask, now_ms);
    }

    /* 在稳定状态基础上判断长按事件。 */
    Key_AppUpdateLongPress(now_ms);
}

/**
 * @brief  获取当前稳定按下状态。
 *
 * @param  无。
 * @return bit0~bit3 对应 K1~K4，按下为 1。
 */
uint8_t Key_AppGetStableMask(void)
{
    /* 返回当前稳定状态，不清除事件。 */
    return s_key_stable_mask;
}

/**
 * @brief  获取已累积的按下事件。
 *
 * @param  无。
 * @return 按下事件掩码；调用本函数不会清除事件。
 */
uint8_t Key_AppGetDownMask(void)
{
    /* 返回按下事件缓存。 */
    return s_key_down_event_mask;
}

/**
 * @brief  获取已累积的抬起事件。
 *
 * @param  无。
 * @return 抬起事件掩码；调用本函数不会清除事件。
 */
uint8_t Key_AppGetUpMask(void)
{
    /* 返回抬起事件缓存。 */
    return s_key_up_event_mask;
}

/**
 * @brief  获取已累积的长按事件。
 *
 * @param  无。
 * @return 长按事件掩码；调用本函数不会清除事件。
 */
uint8_t Key_AppGetLongPressMask(void)
{
    /* 返回长按事件缓存。 */
    return s_key_long_event_mask;
}

/**
 * @brief  读取并清除当前按键事件。
 *
 * @note   该接口适合主业务一次性取走 down/up/long_press 事件，避免重复响应同一事件。
 *
 * @param  out_state 输出按键状态和事件。
 * @return true 表示输出成功；false 表示参数为空。
 */
bool Key_AppConsumeEvents(key_app_state_t *out_state)
{
    /* 调用者必须提供输出结构。 */
    if (out_state == NULL)
    {
        /* 空指针无法写出事件。 */
        return false;
    }

    /* 输出当前稳定状态和事件缓存。 */
    out_state->stable_mask = s_key_stable_mask;
    out_state->down_mask = s_key_down_event_mask;
    out_state->up_mask = s_key_up_event_mask;
    out_state->long_press_mask = s_key_long_event_mask;

    /* 清除一次性事件，避免上层重复处理。 */
    s_key_down_event_mask = 0U;
    s_key_up_event_mask = 0U;
    s_key_long_event_mask = 0U;

    /* 返回成功。 */
    return true;
}
