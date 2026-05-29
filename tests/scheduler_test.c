/**
 * @file    scheduler_test.c
 * @brief   调度器任务顺序、deadline 周期推进和运行统计的主机端测试。
 *
 * @details 本测试通过桩任务替代真实 App 任务，直接驱动 Scheduler_Run()。
 *          测试目标是验证调度器核心时序行为，不访问 MCU 寄存器或外设。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "scheduler.h"

/* 记录一次任务调用，便于验证调度器扫描顺序是否符合实时性优先级。 */
typedef struct
{
    scheduler_task_id_t task_id;  /* 被调用的任务 ID。 */
    uint32_t tick_ms;             /* 任务开始执行时的调度器 tick。 */
} scheduler_test_call_t;

/* 调用日志容量覆盖本测试内的所有任务调用次数。 */
#define SCHEDULER_TEST_CALL_CAPACITY     (32U)

/* 桩任务调用日志，按实际执行顺序写入。 */
static scheduler_test_call_t s_calls[SCHEDULER_TEST_CALL_CAPACITY];

/* 当前调用日志中的有效记录数量。 */
static uint8_t s_call_count;

/* 各任务模拟耗时，桩任务执行时直接推进 uwTick，用于测试运行统计。 */
static uint32_t s_task_cost_ms[SCHEDULER_TASK_ID_COUNT];

/**
 * @brief  清空测试桩状态。
 *
 * @param  无。
 * @return 无。
 */
static void SchedulerTest_ResetStubs(void)
{
    /* 清空调用日志数量，旧日志内容不再参与断言。 */
    s_call_count = 0U;

    /* 默认所有任务执行耗时为 0，单个测试可按需覆盖。 */
    for (uint8_t i = 0U; i < (uint8_t)SCHEDULER_TASK_ID_COUNT; i++)
    {
        s_task_cost_ms[i] = 0U;
    }
}

/**
 * @brief  记录桩任务调用并模拟任务耗时。
 *
 * @param  task_id 当前被调度的任务 ID。
 * @return 无。
 */
static void SchedulerTest_RecordTask(scheduler_task_id_t task_id)
{
    /* 调用日志容量固定，超出容量说明测试用例设计错误。 */
    assert(s_call_count < SCHEDULER_TEST_CALL_CAPACITY);

    /* 记录本次任务 ID 和开始 tick，用于后续顺序和耗时断言。 */
    s_calls[s_call_count].task_id = task_id;
    s_calls[s_call_count].tick_ms = Scheduler_GetTick();
    s_call_count++;

    /* 通过推进 uwTick 模拟任务执行耗时，避免依赖真实 CPU 周期计数器。 */
    uwTick += s_task_cost_ms[task_id];
}

/**
 * @brief  LED 桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Led_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TASK_ID_LED);
}

/**
 * @brief  按键桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Key_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TASK_ID_KEY);
}

/**
 * @brief  UART 桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Uart_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TASK_ID_UART);
}

/**
 * @brief  陀螺仪桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Gyro_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TASK_ID_GYRO);
}

/**
 * @brief  灰度循迹桩任务。
 *
 * @param  无。
 * @return 无。
 */
void LineTrack_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TASK_ID_LINE_TRACK);
}

/**
 * @brief  电机桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Motor_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TASK_ID_MOTOR);
}

/**
 * @brief  OLED 桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Oled_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TASK_ID_OLED);
}

/**
 * @brief  验证实时性关键任务先于低优先级显示和 LED 任务执行。
 *
 * @param  无。
 * @return 无。
 */
static void test_realtime_tasks_run_before_low_priority_tasks(void)
{
    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /* 推进到所有 1ms/5ms/20ms/100ms/250ms 任务都到期，便于一次扫描验证完整顺序。 */
    uwTick = 250U;
    Scheduler_Run();

    assert(s_call_count == 7U);
    assert(s_calls[0].task_id == SCHEDULER_TASK_ID_LINE_TRACK);
    assert(s_calls[1].task_id == SCHEDULER_TASK_ID_GYRO);
    assert(s_calls[2].task_id == SCHEDULER_TASK_ID_MOTOR);
    assert(s_calls[3].task_id == SCHEDULER_TASK_ID_KEY);
    assert(s_calls[4].task_id == SCHEDULER_TASK_ID_UART);
    assert(s_calls[5].task_id == SCHEDULER_TASK_ID_OLED);
    assert(s_calls[6].task_id == SCHEDULER_TASK_ID_LED);
}

/**
 * @brief  验证 deadline 推进避免任务耗时造成周期基准漂移。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_skips_backlog_after_overrun(void)
{
    scheduler_task_stats_t stats;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /* 灰度循迹任务模拟耗时 3ms，超过自身 1ms 周期。 */
    s_task_cost_ms[SCHEDULER_TASK_ID_LINE_TRACK] = 3U;

    /* 第一次到期运行后，任务耗时超过周期。 */
    uwTick = 1U;
    Scheduler_Run();
    assert(s_call_count >= 1U);
    assert(s_calls[0].task_id == SCHEDULER_TASK_ID_LINE_TRACK);

    /*
     * 当前 tick 已被桩任务推进到 4ms。
     * 新策略只补偿一次并跳过积压周期，不能立刻再次运行 1ms 任务，
     * 否则高频任务会把后续低频任务长期饿住。
     */
    s_call_count = 0U;
    Scheduler_Run();
    assert(s_call_count == 0U);

    assert(Scheduler_GetTaskStats(SCHEDULER_TASK_ID_LINE_TRACK, &stats) == true);
    assert(stats.run_count == 1UL);
    assert(stats.overrun_count == 1UL);
    assert(stats.last_lateness_ms == 0UL);
    assert(stats.max_lateness_ms == 0UL);
    assert(stats.missed_deadline_count == 0UL);
}

/**
 * @brief  验证长时间延迟后只执行一次任务，并记录迟到和跳过周期。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_records_lateness_and_missed_periods(void)
{
    scheduler_task_stats_t stats;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /*
     * 灰度循迹任务原 deadline 为 1ms。直接推进到 150ms 后运行，
     * 调度器应只调用一次该任务，并统计 149ms 迟到与 149 个被跳过周期。
     */
    uwTick = 150U;
    Scheduler_Run();

    assert(s_call_count >= 1U);
    assert(s_calls[0].task_id == SCHEDULER_TASK_ID_LINE_TRACK);
    assert(Scheduler_GetTaskStats(SCHEDULER_TASK_ID_LINE_TRACK, &stats) == true);
    assert(stats.run_count == 1UL);
    assert(stats.last_lateness_ms == 149UL);
    assert(stats.max_lateness_ms == 149UL);
    assert(stats.missed_deadline_count == 149UL);
}

/**
 * @brief  验证调度器统计任务最大耗时和超期次数。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_records_runtime_and_overrun_stats(void)
{
    scheduler_task_stats_t stats;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /* 用 3ms 耗时触发灰度 1ms 任务超期统计。 */
    s_task_cost_ms[SCHEDULER_TASK_ID_LINE_TRACK] = 3U;
    uwTick = 1U;
    Scheduler_Run();

    assert(Scheduler_GetTaskStats(SCHEDULER_TASK_ID_LINE_TRACK, &stats) == true);
    assert(stats.run_count == 1UL);
    assert(stats.max_runtime_ms == 3UL);
    assert(stats.last_runtime_ms == 3UL);
    assert(stats.overrun_count == 1UL);
    assert(stats.last_lateness_ms == 0UL);
    assert(stats.max_lateness_ms == 0UL);
    assert(stats.missed_deadline_count == 0UL);

    Scheduler_ClearTaskStats(SCHEDULER_TASK_ID_LINE_TRACK);
    assert(Scheduler_GetTaskStats(SCHEDULER_TASK_ID_LINE_TRACK, &stats) == true);
    assert(stats.run_count == 0UL);
    assert(stats.max_runtime_ms == 0UL);
    assert(stats.last_runtime_ms == 0UL);
    assert(stats.overrun_count == 0UL);
    assert(stats.last_lateness_ms == 0UL);
    assert(stats.max_lateness_ms == 0UL);
    assert(stats.missed_deadline_count == 0UL);
}

/**
 * @brief  调度器测试入口。
 *
 * @param  无。
 * @return 0 表示所有断言通过。
 */
int main(void)
{
    test_realtime_tasks_run_before_low_priority_tasks();
    test_scheduler_skips_backlog_after_overrun();
    test_scheduler_records_lateness_and_missed_periods();
    test_scheduler_records_runtime_and_overrun_stats();

    printf("scheduler tests passed\n");
    return 0;
}
