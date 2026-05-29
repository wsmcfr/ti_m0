/**
 * @file    scheduler.h
 * @brief   简易轮询调度器接口，提供毫秒 tick、系统初始化和周期任务运行入口。
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#ifndef SCHEDULER_ENABLE_STATS
/* 默认开启调度器统计；最终版可通过 -DSCHEDULER_ENABLE_STATS=0 关闭运行期统计写入。 */
#define SCHEDULER_ENABLE_STATS  (1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  调度器任务 ID。
 *
 * @note   ID 用于公开统计查询，不依赖任务表顺序；后续调整优先级顺序时，
 *         外部仍可用固定 ID 查询对应任务的运行情况。
 */
typedef enum
{
    SCHEDULER_TASK_ID_LINE_TRACK = 0,    /* 灰度循迹任务，实时性最高。 */
    SCHEDULER_TASK_ID_GYRO,              /* 陀螺仪接收解析任务。 */
    SCHEDULER_TASK_ID_MOTOR,             /* 电机串口接收解析任务。 */
    SCHEDULER_TASK_ID_KEY,               /* 按键消抖和事件任务。 */
    SCHEDULER_TASK_ID_UART,              /* PC 串口维护任务。 */
    SCHEDULER_TASK_ID_OLED,              /* OLED 低频显示任务。 */
    SCHEDULER_TASK_ID_LED,               /* LED 心跳任务，实时性最低。 */
    SCHEDULER_TASK_ID_COUNT              /* 任务 ID 数量，用于数组边界检查。 */
} scheduler_task_id_t;

/**
 * @brief  单个调度任务的运行统计。
 *
 * @note   时间单位为毫秒 tick。统计数据用于定位任务耗时和超期情况，
 *         不参与调度决策；读取后不会自动清零。
 */
typedef struct
{
    uint32_t run_count;          /* 任务已经被调度执行的次数。 */
    uint32_t overrun_count;      /* 任务单次耗时大于等于自身周期的次数。 */
    uint32_t missed_deadline_count; /* 任务迟到时被跳过的周期累计数量。 */
    uint32_t max_runtime_ms;     /* 观察到的最大单次运行耗时。 */
    uint32_t last_runtime_ms;    /* 最近一次运行耗时。 */
    uint32_t max_lateness_ms;    /* 观察到的最大调度迟到时间。 */
    uint32_t last_lateness_ms;   /* 最近一次调度迟到时间。 */
    uint32_t last_start_ms;      /* 最近一次开始运行的 tick。 */
} scheduler_task_stats_t;

/**
 * @brief  调度器任务描述结构体。
 *
 * @note   run_task 指向实际任务函数，interval_time_ms 表示任务间隔，
 *         deadline_ms 记录下一次到期时间，用于无符号减法处理 tick 溢出。
 */
typedef struct
{
    scheduler_task_id_t task_id; /* 任务固定 ID，用于统计查询和调试定位。 */
    void (*run_task)(void);        /* 任务函数指针，调度器到期后调用该函数。 */
    uint32_t interval_time_ms;     /* 任务运行间隔，单位为毫秒。 */
    uint32_t initial_offset_ms;    /* 首次运行相对初始化时刻的错峰偏移，降低同 tick 峰值。 */
    uint32_t deadline_ms;          /* 下一次应运行的时间戳，单位为毫秒。 */
#if SCHEDULER_ENABLE_STATS
    scheduler_task_stats_t stats;  /* 任务运行统计，便于定位调度瓶颈。 */
#endif
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
 * @return true 表示本轮至少执行过一个任务；false 表示没有任务到期。
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
 * @brief  读取当前毫秒 tick。
 *
 * @param  无。
 * @return 当前系统毫秒计数值。
 */
uint32_t Scheduler_GetTick(void);

/**
 * @brief  读取指定任务的运行统计。
 *
 * @param  task_id   任务 ID。
 * @param  out_stats 输出统计结构。
 * @return true 表示输出成功；false 表示参数无效或任务不存在。
 */
bool Scheduler_GetTaskStats(scheduler_task_id_t task_id, scheduler_task_stats_t *out_stats);

/**
 * @brief  清除指定任务的运行统计。
 *
 * @param  task_id 任务 ID。
 * @return 无。
 */
void Scheduler_ClearTaskStats(scheduler_task_id_t task_id);

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
