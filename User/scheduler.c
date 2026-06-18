/**
 * @file    scheduler.c
 * @brief   简单轮询调度器实现，按“任务函数 + 周期 + 上次运行时间”扫描任务表。
 */

#include "scheduler.h"

#include <stddef.h>

#include "gyro_app.h"
#include "key_app.h"
#include "led_app.h"
#include "line_track_app.h"
#include "motor_app.h"
#include "oled_app.h"
#include "uart_app.h"

/*
 * 目标板当前默认关闭循迹任务，避免第二版循迹引脚和未接模块影响硬件联调。
 * 主机测试显式打开循迹任务，用来覆盖 1ms 高频任务的简单调度行为。
 */
#ifndef SCHEDULER_ENABLE_LINE_TRACK_TASK
#if defined(SCHEDULER_HOST_TEST)
#define SCHEDULER_ENABLE_LINE_TRACK_TASK    (1)
#else
#define SCHEDULER_ENABLE_LINE_TRACK_TASK    (0)
#endif
#endif

/* 调度器依赖的全局毫秒 tick，由 SysTick_Handler() 调用 Scheduler_TickInc() 递增。 */
volatile uint32_t uwTick = 0U;

/* 当前任务表中的任务数量，初始化时由数组长度计算得到。 */
static uint8_t s_scheduler_task_num = 0U;

/*
 * 简化后的任务表。
 * 字段含义依次为：任务函数、运行周期、上次运行 tick。
 * 任务顺序仍保持实时性较高的传感器/电机/按键在前，显示和 LED 在后。
 */
static task_t s_scheduler_task[] =
{
#if SCHEDULER_ENABLE_LINE_TRACK_TASK
    { LineTrack_AppTask, 1U, 0U },
#endif
    { Gyro_AppTask, 5U, 0U },
    { Motor_AppTask, 5U, 0U },
    { Key_AppTask, 5U, 0U },
    { Uart_AppTask, 20U, 0U },
    { Oled_AppTask, 250U, 0U },
    { Led_AppTask, 100U, 0U },
};

/* 任务表数组长度，编译期固定。 */
#define SCHEDULER_TASK_COUNT    ((uint8_t)(sizeof(s_scheduler_task) / sizeof(s_scheduler_task[0])))

/**
 * @brief  初始化调度器任务数量和各任务起始 tick。
 *
 * @note   这里贴近参考工程 scheduler_init() 的模型，只记录任务数量。
 *         额外把 last_time 统一设为当前 tick，避免重启调度器后旧时间影响第一次触发。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_Init(void)
{
    /* 读取当前 tick 作为所有任务的统一起点。 */
    uint32_t now_time = Scheduler_GetTick();

    /* 任务数量来自静态数组长度，运行期不再维护复杂注册表。 */
    s_scheduler_task_num = SCHEDULER_TASK_COUNT;

    /* 初始化每个任务的上次运行时间，使任务从初始化时刻重新开始计周期。 */
    for (uint8_t i = 0U; i < s_scheduler_task_num; i++)
    {
        /* 每个任务的首次运行时间为 now_time + rate_time。 */
        s_scheduler_task[i].last_time = now_time;
    }
}

/**
 * @brief  扫描任务表，并执行所有达到周期的任务。
 *
 * @note   使用无符号 tick 差值 `(now - last) >= rate`，兼容 uint32_t 回绕。
 *         该版本不做统计、不做 deadline 缓存、不做错峰，只保留最直接的轮询调度。
 *
 * @param  无。
 * @return true 表示本轮至少执行过一个任务；false 表示本轮没有任务到期。
 */
bool Scheduler_Run(void)
{
    /* 记录本轮是否执行过任务，供 main() 决定是否进入 WFI 等待中断。 */
    bool has_task_run = false;

    /* 逐项扫描当前任务表，和参考工程一样每轮都直接遍历任务数组。 */
    for (uint8_t i = 0U; i < s_scheduler_task_num; i++)
    {
        /* 当前任务项地址，便于后续读取周期和更新 last_time。 */
        task_t *task = &s_scheduler_task[i];
        /* 每个任务判定前读取一次当前 tick，允许前序任务耗时被后序任务感知。 */
        uint32_t now_time = Scheduler_GetTick();

        /* 空任务函数或 0 周期任务没有可调度意义，直接跳过。 */
        if ((task->task_fun == NULL) || (task->rate_time == 0U))
        {
            /* 任务配置无效。 */
            continue;
        }

        /* 到达任务周期后，先记录运行 tick，再调用任务函数。 */
        if ((uint32_t)(now_time - task->last_time) >= task->rate_time)
        {
            /*
             * 按参考工程方式在任务执行前更新 last_time。
             * 这样任务内部可以通过 Scheduler_GetTick() 看到本次触发时刻。
             */
            task->last_time = now_time;
            /* 调用实际业务任务，任务函数自身必须保持尽量短且不做长阻塞。 */
            task->task_fun();
            /* 标记本轮有任务运行。 */
            has_task_run = true;
        }
    }

    /* 返回本轮任务执行状态。 */
    return has_task_run;
}

/**
 * @brief  毫秒 tick 递增函数。
 *
 * @note   该函数设计给 SysTick_Handler() 调用，只做自增，保持中断路径短。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_TickInc(void)
{
    /* SysTick 每 1ms 调用一次本函数，因此全局毫秒计数递增 1。 */
    uwTick++;
}

/**
 * @brief  获取当前毫秒 tick。
 *
 * @note   Cortex-M0+ 对 32 位对齐变量读取是原子访问，主循环读取 uwTick 不需要关中断。
 *
 * @param  无。
 * @return 当前系统毫秒计数值。
 */
uint32_t Scheduler_GetTick(void)
{
    /* 返回当前系统毫秒计数，供调度器和应用层做非阻塞时间判断。 */
    return uwTick;
}

#if !defined(SCHEDULER_HOST_TEST)
/**
 * @brief  初始化系统应用模块。
 *
 * @note   底层时钟和 GPIO 已由 SYSCFG_DL_init() 完成，这里只初始化 User 分层模块
 *         和调度器，保持 main() 简洁。
 *
 * @param  无。
 * @return 无。
 */
void System_Init(void)
{
    /* 初始化 LED 应用层及其底层 GPIO 状态。 */
    Led_AppInit();
    /* 初始化 UART 应用层及三路 UART DMA 收发驱动。 */
    Uart_AppInit();
    /* 初始化陀螺仪应用层和协议解析器状态。 */
    Gyro_AppInit();
    /* 初始化按键应用层，建立消抖起始状态。 */
    Key_AppInit();
#if SCHEDULER_ENABLE_LINE_TRACK_TASK
    /* 初始化灰度循迹应用层，启动灰度底层驱动和校准状态。 */
    LineTrack_AppInit();
#endif
    /* 初始化电机应用层，清空目标速度、编码器和协议统计。 */
    Motor_AppInit();
    /* 初始化 OLED 应用层，后续低频刷新系统状态。 */
    Oled_AppInit();
    /* 最后初始化调度器时间基准，让任务从当前 tick 开始计周期。 */
    Scheduler_Init();
}
#endif
