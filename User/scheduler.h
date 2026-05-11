/**
 * @file    scheduler.h
 * @brief   简易轮询调度器接口，提供毫秒 tick、系统初始化和周期任务运行入口。
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  调度器任务描述结构体。
 *
 * @note   run_task 指向实际任务函数，interval_time_ms 表示任务间隔，
 *         last_time_ms 记录上次执行时刻，用于无符号减法处理 tick 溢出。
 */
typedef struct
{
    void (*run_task)(void);        /* 任务函数指针，调度器到期后调用该函数。 */
    uint32_t interval_time_ms;     /* 任务运行间隔，单位为毫秒。 */
    uint32_t last_time_ms;         /* 上一次运行时间戳，单位为毫秒。 */
} task_t;

/* 与 STM32 HAL 风格目标框架兼容的毫秒计数变量，由 SysTick 中断递增。 */
extern volatile uint32_t uwTick;

/**
 * @brief  初始化调度器任务表。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_Init(void);

/**
 * @brief  运行一次调度器扫描。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_Run(void);

/**
 * @brief  SysTick 中断中的毫秒 tick 递增入口。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_TickInc(void);

/**
 * @brief  读取当前毫秒 tick。
 *
 * @param  无。
 * @return 当前系统毫秒计数值。
 */
uint32_t Scheduler_GetTick(void);

/**
 * @brief  系统应用层初始化入口。
 *
 * @param  无。
 * @return 无。
 */
void System_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* SCHEDULER_H */
