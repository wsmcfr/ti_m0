/**
 * @file    scheduler.h
 * @brief   简单轮询调度器接口，提供毫秒 tick、系统初始化和周期任务扫描入口。
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  单个周期任务描述结构体。
 *
 * @note   该结构体保留参考工程的三字段模型：
 *         task_fun 是任务函数，rate_time 是运行周期，last_time 是上次运行 tick。
 */
typedef struct
{
    void (*task_fun)(void);   /* 调度器到期后调用的任务函数。 */
    uint32_t rate_time;       /* 任务运行周期，单位 ms。 */
    uint32_t last_time;       /* 任务上一次运行时的毫秒 tick。 */
} task_t;

/* 全局毫秒 tick，由 SysTick_Handler() 调用 Scheduler_TickInc() 递增。 */
extern volatile uint32_t uwTick;

/**
 * @brief  初始化调度器任务数量和各任务起始 tick。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_Init(void);

/**
 * @brief  扫描一次任务表并执行到期任务。
 *
 * @param  无。
 * @return true 表示本轮至少执行过一个任务；false 表示本轮没有任务到期。
 */
bool Scheduler_Run(void);

/**
 * @brief  SysTick 中断中的毫秒 tick 递增入口。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_TickInc(void);

/**
 * @brief  获取当前毫秒 tick。
 *
 * @param  无。
 * @return 当前系统毫秒计数。
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
