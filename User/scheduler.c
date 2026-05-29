/**
 * @file    scheduler.c
 * @brief   简易轮询调度器实现，移植自 STM32 HAL 风格的 uwTick 任务框架。
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

/* 调度器依赖的全局毫秒 tick，由 SysTick_Handler() 调用 Scheduler_TickInc() 递增。 */
volatile uint32_t uwTick = 0U;

/* 当前任务表中的任务数量，初始化时根据数组长度计算，避免手工维护数量出错。 */
static uint8_t Task_Num = 0U;

/* 任务表：后续新增 App 层任务时，只需要在此处补充任务函数和周期。 */
static task_t Scheduler_Task[] =
{
    { Led_AppTask, 100U, 0U },
    { Key_AppTask, 5U, 0U },
    { Uart_AppTask, 20U, 0U },
    { Gyro_AppTask, 5U, 0U },
    { LineTrack_AppTask, 1U, 0U },
    { Motor_AppTask, 5U, 0U },
    { Oled_AppTask, 20U, 0U },
};

/**
 * @brief  初始化调度器任务表的运行基准时间。
 *
 * @note   初始化时把每个任务的 last_time_ms 设置为当前 tick，
 *         这样任务会在完整周期到期后第一次运行，而不是上电后立即运行。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_Init(void)
{
    /* 读取当前系统毫秒 tick，作为所有任务的统一启动时间基准。 */
    uint32_t now_time = Scheduler_GetTick();

    /* 根据静态任务表的数组长度计算任务数量，避免手写数量和数组内容不一致。 */
    Task_Num = (uint8_t)(sizeof(Scheduler_Task) / sizeof(Scheduler_Task[0]));

    /* 逐个遍历任务表，为每个任务写入初始调度时间戳。 */
    for (uint8_t i = 0U; i < Task_Num; i++)
    {
        /* 记录任务初始时间戳，保证所有任务从同一时间基准开始计时。 */
        Scheduler_Task[i].last_time_ms = now_time;
    }
}

/**
 * @brief  扫描任务表，并执行所有到期任务。
 *
 * @note   使用无符号减法判断时间差，可以自然兼容 uint32_t tick 回绕。
 *         任务函数必须保持非阻塞，否则会影响其它任务的调度及时性。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_Run(void)
{
    /* 从任务表第 0 项开始逐项扫描，直到处理完当前登记的全部任务。 */
    for (uint8_t i = 0U; i < Task_Num; i++)
    {
        /* 每处理一个任务前读取当前 tick，减少任务间等待造成的时间误差。 */
        uint32_t now_time = Scheduler_GetTick();
        /* 取得当前任务项地址，后续读写周期、上次运行时间和回调函数。 */
        task_t *task = &Scheduler_Task[i];

        /* 空函数指针不执行，防止任务表配置错误导致跳转到无效地址。 */
        if (task->run_task == NULL)
        {
            /* 当前任务没有绑定函数时跳过，继续扫描后面的任务。 */
            continue;
        }

        /* 到达任务周期后更新时间戳，再调用任务，避免任务耗时影响下一次基准。 */
        if ((uint32_t)(now_time - task->last_time_ms) >= task->interval_time_ms)
        {
            /* 记录本次调度时间，作为下一次周期判断的起点。 */
            task->last_time_ms = now_time;
            /* 调用当前任务函数，实际业务逻辑由 App 层函数完成。 */
            task->run_task();
        }
    }
}

/**
 * @brief  毫秒 tick 递增函数。
 *
 * @note   该函数设计给 SysTick_Handler() 调用，只做自增，保持中断处理尽量短。
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
    /* 初始化灰度循迹应用层，启动灰度底层驱动和校准状态。 */
    LineTrack_AppInit();
    /* 初始化电机应用层，清空目标速度、编码器和协议统计。 */
    Motor_AppInit();
    /* 初始化 OLED 应用层，后续低频刷新系统状态。 */
    Oled_AppInit();
    /* 最后初始化调度器时间基准，让任务从当前 tick 开始计周期。 */
    Scheduler_Init();
}
