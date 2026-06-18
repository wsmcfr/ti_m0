/**
 * @file    scheduler_test.c
 * @brief   简化轮询调度器的主机端测试。
 *
 * @details 本测试通过桩任务替代真实 App 任务，直接驱动 Scheduler_Run()。
 *          测试目标是验证“任务函数 + 周期 + 上次运行时间”的简单调度行为。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 主机测试打开循迹任务，覆盖任务表中最短 1ms 周期路径。 */
#ifndef SCHEDULER_ENABLE_LINE_TRACK_TASK
#define SCHEDULER_ENABLE_LINE_TRACK_TASK    (1)
#endif

#include "scheduler.h"

/**
 * @brief  测试用任务 ID。
 *
 * @note   生产调度器已经不再暴露任务 ID；测试内部保留 ID 只用于断言调用顺序。
 */
typedef enum
{
    SCHEDULER_TEST_TASK_LINE_TRACK = 0,  /* 灰度循迹桩任务。 */
    SCHEDULER_TEST_TASK_GYRO,            /* 陀螺仪桩任务。 */
    SCHEDULER_TEST_TASK_MOTOR,           /* 电机桩任务。 */
    SCHEDULER_TEST_TASK_KEY,             /* 按键桩任务。 */
    SCHEDULER_TEST_TASK_UART,            /* UART 桩任务。 */
    SCHEDULER_TEST_TASK_OLED,            /* OLED 桩任务。 */
    SCHEDULER_TEST_TASK_LED,             /* LED 桩任务。 */
    SCHEDULER_TEST_TASK_COUNT            /* 测试任务数量。 */
} scheduler_test_task_id_t;

/* 记录一次任务调用，便于验证调度器扫描顺序和触发时刻。 */
typedef struct
{
    scheduler_test_task_id_t task_id;    /* 被调用的测试任务 ID。 */
    uint32_t tick_ms;                    /* 任务开始执行时的调度器 tick。 */
} scheduler_test_call_t;

/* 调用日志容量覆盖本测试内的所有任务调用次数。 */
#define SCHEDULER_TEST_CALL_CAPACITY     (32U)

/* 桩任务调用日志，按实际执行顺序写入。 */
static scheduler_test_call_t s_calls[SCHEDULER_TEST_CALL_CAPACITY];

/* 当前调用日志中的有效记录数量。 */
static uint8_t s_call_count;

/* 各任务模拟耗时，桩任务执行时直接推进 uwTick，用于验证后续任务读取新 tick。 */
static uint32_t s_task_cost_ms[SCHEDULER_TEST_TASK_COUNT];

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
    for (uint8_t i = 0U; i < (uint8_t)SCHEDULER_TEST_TASK_COUNT; i++)
    {
        s_task_cost_ms[i] = 0U;
    }
}

/**
 * @brief  记录桩任务调用并模拟任务耗时。
 *
 * @param  task_id 当前被调度的测试任务 ID。
 * @return 无。
 */
static void SchedulerTest_RecordTask(scheduler_test_task_id_t task_id)
{
    /* 调用日志容量固定，超出容量说明测试用例设计错误。 */
    assert(s_call_count < SCHEDULER_TEST_CALL_CAPACITY);

    /* 记录本次任务 ID 和开始 tick，用于后续顺序断言。 */
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
    SchedulerTest_RecordTask(SCHEDULER_TEST_TASK_LED);
}

/**
 * @brief  按键桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Key_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TEST_TASK_KEY);
}

/**
 * @brief  UART 桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Uart_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TEST_TASK_UART);
}

/**
 * @brief  陀螺仪桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Gyro_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TEST_TASK_GYRO);
}

/**
 * @brief  灰度循迹桩任务。
 *
 * @param  无。
 * @return 无。
 */
void LineTrack_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TEST_TASK_LINE_TRACK);
}

/**
 * @brief  电机桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Motor_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TEST_TASK_MOTOR);
}

/**
 * @brief  OLED 桩任务。
 *
 * @param  无。
 * @return 无。
 */
void Oled_AppTask(void)
{
    SchedulerTest_RecordTask(SCHEDULER_TEST_TASK_OLED);
}

/**
 * @brief  验证初始化后未到周期时不运行任务。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_returns_false_when_no_task_is_due(void)
{
    bool has_task_run;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /* 初始化后所有任务 last_time 都是 0，0ms 立即扫描不应触发任何任务。 */
    has_task_run = Scheduler_Run();

    assert(has_task_run == false);
    assert(s_call_count == 0U);
}

/**
 * @brief  验证 1ms 时只运行最短周期的循迹任务。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_runs_only_due_tasks(void)
{
    bool has_task_run;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /* 1ms 时只有 1ms 周期任务到期，5ms/20ms/100ms/250ms 任务都不应运行。 */
    uwTick = 1U;
    has_task_run = Scheduler_Run();

    assert(has_task_run == true);
    assert(s_call_count == 1U);
    assert(s_calls[0].task_id == SCHEDULER_TEST_TASK_LINE_TRACK);
    assert(s_calls[0].tick_ms == 1U);
}

/**
 * @brief  验证同一 tick 中到期任务按任务表顺序运行。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_runs_due_tasks_in_table_order(void)
{
    bool has_task_run;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /*
     * 250ms 时所有任务周期都已到期。
     * 简化调度器不再做错峰，所有到期任务在同一轮按任务表顺序运行。
     */
    uwTick = 250U;
    has_task_run = Scheduler_Run();

    assert(has_task_run == true);
    assert(s_call_count == 7U);
    assert(s_calls[0].task_id == SCHEDULER_TEST_TASK_LINE_TRACK);
    assert(s_calls[1].task_id == SCHEDULER_TEST_TASK_GYRO);
    assert(s_calls[2].task_id == SCHEDULER_TEST_TASK_MOTOR);
    assert(s_calls[3].task_id == SCHEDULER_TEST_TASK_KEY);
    assert(s_calls[4].task_id == SCHEDULER_TEST_TASK_UART);
    assert(s_calls[5].task_id == SCHEDULER_TEST_TASK_OLED);
    assert(s_calls[6].task_id == SCHEDULER_TEST_TASK_LED);
}

/**
 * @brief  验证任务运行后会用本次 tick 更新 last_time。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_updates_last_time_after_run(void)
{
    bool has_task_run;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /* 5ms 时 1ms 和 5ms 任务运行。 */
    uwTick = 5U;
    has_task_run = Scheduler_Run();
    assert(has_task_run == true);
    assert(s_call_count == 4U);

    /* 同一个 tick 再扫一次，因为 last_time 已更新，不应重复运行。 */
    s_call_count = 0U;
    has_task_run = Scheduler_Run();
    assert(has_task_run == false);
    assert(s_call_count == 0U);

    /* 到 6ms 时，只有 1ms 任务距离上次运行满 1ms。 */
    uwTick = 6U;
    has_task_run = Scheduler_Run();
    assert(has_task_run == true);
    assert(s_call_count == 1U);
    assert(s_calls[0].task_id == SCHEDULER_TEST_TASK_LINE_TRACK);
}

/**
 * @brief  验证前序任务耗时会被后续任务读取到。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_reads_tick_before_each_task(void)
{
    bool has_task_run;

    SchedulerTest_ResetStubs();
    uwTick = 0U;
    Scheduler_Init();

    /*
     * 循迹任务模拟耗时 4ms。
     * 调度器每个任务判定前重新读取 tick，因此后面的 5ms 任务能看到 tick 已到 5ms。
     */
    s_task_cost_ms[SCHEDULER_TEST_TASK_LINE_TRACK] = 4U;
    uwTick = 1U;
    has_task_run = Scheduler_Run();

    assert(has_task_run == true);
    assert(s_call_count == 4U);
    assert(s_calls[0].task_id == SCHEDULER_TEST_TASK_LINE_TRACK);
    assert(s_calls[0].tick_ms == 1U);
    assert(s_calls[1].task_id == SCHEDULER_TEST_TASK_GYRO);
    assert(s_calls[1].tick_ms == 5U);
    assert(s_calls[2].task_id == SCHEDULER_TEST_TASK_MOTOR);
    assert(s_calls[2].tick_ms == 5U);
    assert(s_calls[3].task_id == SCHEDULER_TEST_TASK_KEY);
    assert(s_calls[3].tick_ms == 5U);
}

/**
 * @brief  验证 tick 回绕时仍能通过无符号差值触发任务。
 *
 * @param  无。
 * @return 无。
 */
static void test_scheduler_handles_tick_wrap(void)
{
    bool has_task_run;

    SchedulerTest_ResetStubs();
    uwTick = 0xFFFFFFFEUL;
    Scheduler_Init();

    /* tick 回绕到 3 后，距离初始化时刻已经过去 5ms，1ms 和 5ms 任务应运行。 */
    uwTick = 3U;
    has_task_run = Scheduler_Run();

    assert(has_task_run == true);
    assert(s_call_count == 4U);
    assert(s_calls[0].task_id == SCHEDULER_TEST_TASK_LINE_TRACK);
    assert(s_calls[1].task_id == SCHEDULER_TEST_TASK_GYRO);
    assert(s_calls[2].task_id == SCHEDULER_TEST_TASK_MOTOR);
    assert(s_calls[3].task_id == SCHEDULER_TEST_TASK_KEY);
}

/**
 * @brief  调度器测试入口。
 *
 * @param  无。
 * @return 0 表示所有断言通过。
 */
int main(void)
{
    test_scheduler_returns_false_when_no_task_is_due();
    test_scheduler_runs_only_due_tasks();
    test_scheduler_runs_due_tasks_in_table_order();
    test_scheduler_updates_last_time_after_run();
    test_scheduler_reads_tick_before_each_task();
    test_scheduler_handles_tick_wrap();

    printf("scheduler tests passed\n");
    return 0;
}
