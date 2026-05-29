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

/* 任务表：按实时性从高到低排列，避免低优先级显示或心跳任务排在关键控制任务前面。 */
static task_t Scheduler_Task[] =
{
#if SCHEDULER_ENABLE_STATS
    { SCHEDULER_TASK_ID_LINE_TRACK, LineTrack_AppTask, 1U, 0U, 0U, {0U} },
    { SCHEDULER_TASK_ID_GYRO, Gyro_AppTask, 5U, 0U, 0U, {0U} },
    { SCHEDULER_TASK_ID_MOTOR, Motor_AppTask, 5U, 0U, 0U, {0U} },
    { SCHEDULER_TASK_ID_KEY, Key_AppTask, 5U, 0U, 0U, {0U} },
    { SCHEDULER_TASK_ID_UART, Uart_AppTask, 20U, 2U, 0U, {0U} },
    { SCHEDULER_TASK_ID_OLED, Oled_AppTask, 250U, 13U, 0U, {0U} },
    { SCHEDULER_TASK_ID_LED, Led_AppTask, 100U, 7U, 0U, {0U} },
#else
    { SCHEDULER_TASK_ID_LINE_TRACK, LineTrack_AppTask, 1U, 0U, 0U },
    { SCHEDULER_TASK_ID_GYRO, Gyro_AppTask, 5U, 0U, 0U },
    { SCHEDULER_TASK_ID_MOTOR, Motor_AppTask, 5U, 0U, 0U },
    { SCHEDULER_TASK_ID_KEY, Key_AppTask, 5U, 0U, 0U },
    { SCHEDULER_TASK_ID_UART, Uart_AppTask, 20U, 2U, 0U },
    { SCHEDULER_TASK_ID_OLED, Oled_AppTask, 250U, 13U, 0U },
    { SCHEDULER_TASK_ID_LED, Led_AppTask, 100U, 7U, 0U },
#endif
};

/* 当前任务表中的任务数量由数组长度生成，编译期固定，避免运行期重复赋值。 */
#define SCHEDULER_TASK_COUNT    ((uint8_t)(sizeof(Scheduler_Task) / sizeof(Scheduler_Task[0])))

/* 当前所有任务中最早的下一次到期 tick，用于无任务到期时快速返回。 */
static uint32_t s_scheduler_next_deadline_ms = 0U;

#if defined(SCHEDULER_HOST_TEST)
/* 主机测试专用扫描计数，生产固件不导出，避免增加运行期接口。 */
static uint32_t s_scheduler_scan_count_for_test = 0U;
#endif

static task_t *Scheduler_FindTaskById(scheduler_task_id_t task_id);
static bool Scheduler_IsDeadlineReached(uint32_t now_time, uint32_t deadline_ms);
static void Scheduler_UpdateNextDeadline(void);
static void Scheduler_UpdateTaskDeadline(task_t *task, uint32_t now_time);
#if SCHEDULER_ENABLE_STATS
static void Scheduler_RecordTaskLateness(task_t *task, uint32_t lateness_ms);
static void Scheduler_RecordTaskRuntime(task_t *task, uint32_t start_time, uint32_t end_time);
#endif

/**
 * @brief  初始化调度器任务表的运行基准时间。
 *
 * @note   初始化时把每个任务的首次 deadline 设置为当前 tick + 周期 + 初始错峰。
 *         高频任务保持 0 offset，低频任务错开到不同相位，避免整百毫秒集中运行。
 *
 * @param  无。
 * @return 无。
 */
void Scheduler_Init(void)
{
    /* 读取当前系统毫秒 tick，作为所有任务的统一启动时间基准。 */
    uint32_t now_time = Scheduler_GetTick();

    /* 逐个遍历任务表，为每个任务写入初始调度时间戳。 */
    for (uint8_t i = 0U; i < SCHEDULER_TASK_COUNT; i++)
    {
        /*
         * 记录任务第一次到期时间，而不是上次运行时间。
         * 本调度器优先保证任务公平性：周期任务迟到后只运行一次，
         * 任务结束后再从结束 tick 排下一次 deadline，因此不会回放历史积压周期。
         */
        Scheduler_Task[i].deadline_ms = now_time +
            Scheduler_Task[i].interval_time_ms +
            Scheduler_Task[i].initial_offset_ms;
#if SCHEDULER_ENABLE_STATS
        /* 初始化时清空统计，保证每次 System_Init() 后统计都从零开始。 */
        Scheduler_Task[i].stats.run_count = 0U;
        Scheduler_Task[i].stats.overrun_count = 0U;
        Scheduler_Task[i].stats.missed_deadline_count = 0U;
        Scheduler_Task[i].stats.max_runtime_ms = 0U;
        Scheduler_Task[i].stats.last_runtime_ms = 0U;
        Scheduler_Task[i].stats.max_lateness_ms = 0U;
        Scheduler_Task[i].stats.last_lateness_ms = 0U;
        Scheduler_Task[i].stats.last_start_ms = 0U;
#endif
    }

    /* 初始化最早 deadline 缓存，让无任务到期时可以 O(1) 快速返回。 */
    Scheduler_UpdateNextDeadline();
}

/**
 * @brief  按任务 ID 查找任务表项。
 *
 * @note   任务 ID 是公开查询接口的稳定标识，不能要求外部知道任务表顺序。
 *
 * @param  task_id 任务 ID。
 * @return 找到时返回任务表项地址；未找到时返回 NULL。
 */
static task_t *Scheduler_FindTaskById(scheduler_task_id_t task_id)
{
    /* 非法 ID 没有对应任务，直接返回空指针。 */
    if ((uint32_t)task_id >= (uint32_t)SCHEDULER_TASK_ID_COUNT)
    {
        /* ID 超出枚举范围。 */
        return NULL;
    }

    /* 遍历当前任务表，找到 ID 完全匹配的任务项。 */
    for (uint8_t i = 0U; i < SCHEDULER_TASK_COUNT; i++)
    {
        /* 任务 ID 匹配时返回该任务项地址。 */
        if (Scheduler_Task[i].task_id == task_id)
        {
            /* 返回任务表项，供统计查询或清零使用。 */
            return &Scheduler_Task[i];
        }
    }

    /* 任务表中不存在该 ID，返回 NULL 表示查询失败。 */
    return NULL;
}

/**
 * @brief  判断当前 tick 是否已经到达 deadline。
 *
 * @note   使用纯无符号差值判断 tick 先后关系，避免 uint32_t 回绕时直接比较大小。
 *         本函数假设单次调度延迟不会超过 2^31ms，这远大于本工程裸机运行的合理延迟。
 *
 * @param  now_time    当前 tick。
 * @param  deadline_ms 任务下一次到期 tick。
 * @return true 表示 now_time 已经到达或越过 deadline_ms。
 */
static bool Scheduler_IsDeadlineReached(uint32_t now_time, uint32_t deadline_ms)
{
    /*
     * now - deadline 的差值小于半个 uint32_t 周期时，认为 now 位于 deadline 之后；
     * 如果 now 还在 deadline 之前，差值会回绕成一个很大的无符号数。
     */
    return ((uint32_t)(now_time - deadline_ms) < 0x80000000UL);
}

/**
 * @brief  重新计算任务表中的最早到期时间。
 *
 * @note   任务数量很少，只有在初始化或任务运行后才重算一次；
 *         无任务到期的主循环热路径只读 s_scheduler_next_deadline_ms，不再扫描任务表。
 *
 * @param  无。
 * @return 无。
 */
static void Scheduler_UpdateNextDeadline(void)
{
    /* 先用第 0 个任务 deadline 作为候选最早时间，任务表固定非空。 */
    uint32_t next_deadline = Scheduler_Task[0].deadline_ms;

    /* 从第 1 个任务开始比较，寻找相对当前候选更早的 deadline。 */
    for (uint8_t i = 1U; i < SCHEDULER_TASK_COUNT; i++)
    {
        /*
         * 如果候选 next_deadline 已经到达或越过当前任务 deadline，
         * 说明当前任务 deadline 更早或相等，应更新缓存。
         */
        if (Scheduler_IsDeadlineReached(next_deadline, Scheduler_Task[i].deadline_ms) == true)
        {
            /* 更新当前观察到的最早任务到期时间。 */
            next_deadline = Scheduler_Task[i].deadline_ms;
        }
    }

    /* 保存最早 deadline，供 Scheduler_Run() 的快速路径使用。 */
    s_scheduler_next_deadline_ms = next_deadline;
}

/**
 * @brief  推进任务下一次到期时间。
 *
 * @note   当前工程优先保证主循环公平性：任务迟到后只执行一次，不把历史积压周期
 *         逐个补跑。下一次 deadline 从本次任务结束时刻重新排到未来，避免高频任务
 *         因为长阻塞或自身超期而在后续 Scheduler_Run() 中连续抢占其它任务。
 *
 * @param  task     需要更新的任务。
 * @param  now_time 本次判定到期时的 tick。
 * @return 无。
 */
static void Scheduler_UpdateTaskDeadline(task_t *task, uint32_t now_time)
{
    /* 空任务或 0 周期任务无法安全推进 deadline，直接返回保护。 */
    if ((task == NULL) || (task->interval_time_ms == 0U))
    {
        /* 参数无效。 */
        return;
    }

    /* 从本次任务结束时刻重新安排下一次到期时间，只补偿一次，不补跑历史周期。 */
    task->deadline_ms = now_time + task->interval_time_ms;
}

#if SCHEDULER_ENABLE_STATS
/**
 * @brief  记录一次任务调度迟到信息。
 *
 * @note   lateness_ms 表示当前任务实际开始判定时已经晚于 deadline 的时间。
 *         missed_deadline_count 记录被跳过的完整周期数，用于定位长期被阻塞的任务。
 *
 * @param  task        已到期的任务。
 * @param  lateness_ms 本次调度迟到时间，单位 ms。
 * @return 无。
 */
static void Scheduler_RecordTaskLateness(task_t *task, uint32_t lateness_ms)
{
    /* 空任务无法记录统计。 */
    if (task == NULL)
    {
        /* 参数无效。 */
        return;
    }

    /* 保存最近一次迟到时间，便于实时观察本轮调度是否被前序任务拖慢。 */
    task->stats.last_lateness_ms = lateness_ms;

    /* 更新最大迟到时间，用于评估最坏调度响应。 */
    if (lateness_ms > task->stats.max_lateness_ms)
    {
        /* 当前迟到时间刷新了历史最大值。 */
        task->stats.max_lateness_ms = lateness_ms;
    }

    /* 迟到达到一个或多个完整周期时，累计这些被跳过的历史周期。 */
    if ((task->interval_time_ms > 0U) && (lateness_ms >= task->interval_time_ms))
    {
        /* 只统计完整周期数量，不把亚周期级迟到记为漏周期。 */
        task->stats.missed_deadline_count += (lateness_ms / task->interval_time_ms);
    }
}

/**
 * @brief  记录一次任务运行耗时。
 *
 * @note   使用 tick 差值统计毫秒级运行时间。若任务在同一毫秒内完成，耗时会记为 0；
 *         这仍然足够定位明显阻塞点和周期超期任务。
 *
 * @param  task       已运行的任务。
 * @param  start_time 任务开始 tick。
 * @param  end_time   任务结束 tick。
 * @return 无。
 */
static void Scheduler_RecordTaskRuntime(task_t *task, uint32_t start_time, uint32_t end_time)
{
    /* 保存本次运行耗时，使用无符号差值兼容 tick 回绕。 */
    uint32_t runtime_ms;

    /* 空任务无法记录统计。 */
    if (task == NULL)
    {
        /* 参数无效。 */
        return;
    }

    /* 计算本次任务运行耗时。 */
    runtime_ms = (uint32_t)(end_time - start_time);
    /* 记录最近一次开始时间，便于调试对照任务触发点。 */
    task->stats.last_start_ms = start_time;
    /* 记录最近一次运行耗时。 */
    task->stats.last_runtime_ms = runtime_ms;
    /* 累加任务运行次数。 */
    task->stats.run_count++;

    /* 更新最大运行耗时。 */
    if (runtime_ms > task->stats.max_runtime_ms)
    {
        /* 当前运行耗时刷新了历史最大值。 */
        task->stats.max_runtime_ms = runtime_ms;
    }

    /* 运行耗时达到或超过任务周期时，记录一次超期。 */
    if (runtime_ms >= task->interval_time_ms)
    {
        /* 超期次数累加，用于后续定位需要拆分或降频的任务。 */
        task->stats.overrun_count++;
    }
}
#endif

/**
 * @brief  扫描任务表，并执行所有到期任务。
 *
 * @note   使用无符号减法判断时间差，可以自然兼容 uint32_t tick 回绕。
 *         任务函数必须保持非阻塞，否则会影响其它任务的调度及时性。
 *
 * @param  无。
 * @return true 表示本轮至少执行过一个任务；false 表示没有任务到期。
 */
bool Scheduler_Run(void)
{
    /* 记录本轮扫描是否真的执行过任务，供主循环决定是否进入低功耗等待。 */
    bool has_task_run = false;

    /* 当前 tick 还没到任何任务的最早 deadline 时，直接返回，避免空闲期反复扫描任务表。 */
    if (Scheduler_IsDeadlineReached(Scheduler_GetTick(), s_scheduler_next_deadline_ms) == false)
    {
        /* 没有任务到期。 */
        return false;
    }

    /* 从任务表第 0 项开始逐项扫描，直到处理完当前登记的全部任务。 */
    for (uint8_t i = 0U; i < SCHEDULER_TASK_COUNT; i++)
    {
#if defined(SCHEDULER_HOST_TEST)
        /* 主机测试统计扫描次数，用于证明快速路径没有进入任务表循环。 */
        s_scheduler_scan_count_for_test++;
#endif
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
        if (Scheduler_IsDeadlineReached(now_time, task->deadline_ms) == true)
        {
            /* 保存相对任务 deadline 的迟到时间，0 表示刚好到期或未迟到。 */
            uint32_t lateness_ms = (uint32_t)(now_time - task->deadline_ms);
            /* 保存任务开始时间，用于统计本次任务运行耗时。 */
            uint32_t start_time = Scheduler_GetTick();
#if SCHEDULER_ENABLE_STATS
            /* 记录本次任务调度迟到和被跳过的历史周期。 */
            Scheduler_RecordTaskLateness(task, lateness_ms);
#else
            /* 关闭统计时仍保留变量计算路径，显式转 void 防止编译器告警。 */
            (void)lateness_ms;
#endif
            /* 调用当前任务函数，实际业务逻辑由 App 层函数完成。 */
            task->run_task();
            /* 标记本轮已经执行过任务，主循环本轮不需要进入空闲等待。 */
            has_task_run = true;
            /* 记录任务结束后的 tick，用于计算毫秒级运行耗时。 */
            now_time = Scheduler_GetTick();
            /* 任务结束后只安排下一次未来 deadline，不补跑历史积压周期。 */
            Scheduler_UpdateTaskDeadline(task, now_time);
#if SCHEDULER_ENABLE_STATS
            /* 记录任务运行耗时，用于定位长阻塞任务。 */
            Scheduler_RecordTaskRuntime(task, start_time, now_time);
#else
            /* 关闭统计时不记录运行耗时，显式转 void 防止未使用变量告警。 */
            (void)start_time;
#endif
        }
    }

    /* 本轮扫描后任务 deadline 可能变化，重新缓存下一轮最早到期时间。 */
    Scheduler_UpdateNextDeadline();

    /* 返回本轮任务执行状态，便于裸机主循环在无任务时等待下一次中断唤醒。 */
    return has_task_run;
}

#if defined(SCHEDULER_HOST_TEST)
/**
 * @brief  返回调度器任务表扫描次数。
 *
 * @note   该函数只在主机测试构建中存在，用于验证 next-deadline 快速路径是否生效。
 *
 * @param  无。
 * @return 调度器进入任务表循环的累计次数。
 */
uint32_t Scheduler_GetScanCountForTest(void)
{
    /* 返回主机测试诊断计数。 */
    return s_scheduler_scan_count_for_test;
}
#endif

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
 * @brief  读取指定任务的运行统计。
 *
 * @param  task_id   任务 ID。
 * @param  out_stats 输出统计结构。
 * @return true 表示输出成功；false 表示参数无效或任务不存在。
 */
bool Scheduler_GetTaskStats(scheduler_task_id_t task_id, scheduler_task_stats_t *out_stats)
{
    /* 根据任务 ID 查找对应任务表项。 */
    task_t *task = Scheduler_FindTaskById(task_id);

    /* 输出指针或任务项无效时无法返回统计。 */
    if ((task == NULL) || (out_stats == NULL))
    {
        /* 参数无效或任务不存在。 */
        return false;
    }

#if SCHEDULER_ENABLE_STATS
    /* 拷贝统计快照，避免外部直接修改调度器内部状态。 */
    *out_stats = task->stats;
#else
    /* 关闭统计时不读取 task 内容，显式转 void 防止编译器告警。 */
    (void)task;
    /*
     * 关闭统计时仍保持 API 可用，但返回全 0 快照。
     * 这样上层调试代码无需条件编译，只是看不到运行期统计。
     */
    out_stats->run_count = 0U;
    out_stats->overrun_count = 0U;
    out_stats->missed_deadline_count = 0U;
    out_stats->max_runtime_ms = 0U;
    out_stats->last_runtime_ms = 0U;
    out_stats->max_lateness_ms = 0U;
    out_stats->last_lateness_ms = 0U;
    out_stats->last_start_ms = 0U;
#endif
    /* 返回成功。 */
    return true;
}

/**
 * @brief  清除指定任务的运行统计。
 *
 * @param  task_id 任务 ID。
 * @return 无。
 */
void Scheduler_ClearTaskStats(scheduler_task_id_t task_id)
{
    /* 根据任务 ID 查找对应任务表项。 */
    task_t *task = Scheduler_FindTaskById(task_id);

    /* 未找到任务时无需处理。 */
    if (task == NULL)
    {
        /* 非法任务 ID 或任务不存在。 */
        return;
    }

#if SCHEDULER_ENABLE_STATS
    /* 逐项清零统计，避免引入 memset 依赖也便于后续字段扩展时显式维护。 */
    task->stats.run_count = 0U;
    task->stats.overrun_count = 0U;
    task->stats.missed_deadline_count = 0U;
    task->stats.max_runtime_ms = 0U;
    task->stats.last_runtime_ms = 0U;
    task->stats.max_lateness_ms = 0U;
    task->stats.last_lateness_ms = 0U;
    task->stats.last_start_ms = 0U;
#else
    /* 关闭统计时不维护运行期计数，本函数保留为空操作以兼容调用方。 */
    (void)task;
#endif
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
    /* 初始化灰度循迹应用层，启动灰度底层驱动和校准状态。 */
    LineTrack_AppInit();
    /* 初始化电机应用层，清空目标速度、编码器和协议统计。 */
    Motor_AppInit();
    /* 初始化 OLED 应用层，后续低频刷新系统状态。 */
    Oled_AppInit();
    /* 最后初始化调度器时间基准，让任务从当前 tick 开始计周期。 */
    Scheduler_Init();
}
#endif
