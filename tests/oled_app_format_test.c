/*
 * OLED 应用层格式化单元测试。
 * 该测试只验证显示行文本的纯格式化逻辑，不访问 I2C、GPIO 或 DMA 硬件。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "line_track_app.h"
#include "motor_app.h"
#include "oled_app.h"
#include "oled_driver.h"
#include "uart_app.h"

/* OLED 底层初始化桩调用次数，用于验证应用层启动阶段不会阻塞访问 I2C。 */
static uint32_t s_oled_driver_init_count = 0U;

/* OLED 字符串写屏桩调用次数，用于确认应用层不再整行同步刷新。 */
static uint32_t s_oled_driver_show_string_count = 0U;

/* OLED 单字符写屏桩调用次数，用于验证 OLED 任务已经拆成字符级分片。 */
static uint32_t s_oled_driver_show_char_count = 0U;

/*
 * 函数作用：
 *   提供按键应用层桩函数，满足 oled_app.c 链接依赖。
 * 主要流程：
 *   测试用例直接调用 Oled_AppFormatLine()，不会依赖真实按键状态。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   固定返回 0，表示没有按键按下。
 */
uint8_t Key_AppGetStableMask(void)
{
    return 0U;
}

/*
 * 函数作用：
 *   提供灰度循迹应用层桩函数，满足 oled_app.c 链接依赖。
 * 主要流程：
 *   不写出快照并返回 false，表示测试桩没有实时灰度数据。
 * 参数说明：
 *   out_snapshot：调用方传入的快照输出指针，测试桩不使用。
 * 返回值说明：
 *   固定返回 false。
 */
bool LineTrack_AppGetSnapshot(line_track_snapshot_t *out_snapshot)
{
    (void)out_snapshot;
    return false;
}

/*
 * 函数作用：
 *   提供电机应用层桩函数，满足 oled_app.c 链接依赖。
 * 主要流程：
 *   不写出电机状态并返回 false，表示测试桩没有实时电机数据。
 * 参数说明：
 *   out_status：调用方传入的状态输出指针，测试桩不使用。
 * 返回值说明：
 *   固定返回 false。
 */
bool Motor_AppGetStatus(motor_app_status_t *out_status)
{
    (void)out_status;
    return false;
}

/*
 * 函数作用：
 *   提供 UART 统计读取桩函数，满足 oled_app.c 诊断显示的链接依赖。
 * 主要流程：
 *   测试用例直接构造 oled_app_display_state_t，因此本桩只在刷新任务链接时兜底清零。
 * 参数说明：
 *   port：调用方请求读取的 UART 逻辑端口，测试桩不区分端口。
 *   out_stats：调用方传入的统计输出指针，非空时写入全 0。
 * 返回值说明：
 *   无返回值。
 */
void Uart_AppGetStats(uart_driver_port_t port, uart_driver_stats_t *out_stats)
{
    (void)port;

    if (out_stats != NULL)
    {
        memset(out_stats, 0, sizeof(*out_stats));
    }
}

/*
 * 函数作用：
 *   提供 OLED 底层初始化桩函数，满足 oled_app.c 链接依赖。
 * 主要流程：
 *   测试不访问硬件，因此函数体为空。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
void Oled_DriverInit(void)
{
    s_oled_driver_init_count++;
}

/*
 * 函数作用：
 *   提供 OLED 字符串显示桩函数，满足 oled_app.c 链接依赖。
 * 主要流程：
 *   测试只验证格式化结果，不实际写屏，因此参数全部忽略。
 * 参数说明：
 *   x：列坐标，测试桩不使用。
 *   page：页坐标，测试桩不使用。
 *   text：待显示字符串，测试桩不使用。
 * 返回值说明：
 *   固定返回 true，表示模拟写屏成功。
 */
bool Oled_DriverShowString(uint8_t x, uint8_t page, const char *text)
{
    (void)x;
    (void)page;
    (void)text;
    s_oled_driver_show_string_count++;
    return true;
}

/*
 * 函数作用：
 *   提供 OLED 单字符显示桩函数，满足 oled_app.c 字符级分片刷新路径的链接依赖。
 * 主要流程：
 *   记录调用次数，测试确认每次 OLED 任务最多只写一个字符，避免整行 I2C 写屏阻塞主循环。
 * 参数说明：
 *   x：列坐标，测试桩不使用。
 *   page：页坐标，测试桩不使用。
 *   ch：待显示字符，测试桩不使用。
 * 返回值说明：
 *   固定返回 true，表示模拟单字符写屏成功。
 */
bool Oled_DriverShowChar(uint8_t x, uint8_t page, char ch)
{
    (void)x;
    (void)page;
    (void)ch;
    s_oled_driver_show_char_count++;
    return true;
}

/*
 * 函数作用：
 *   提供 OLED 可用状态桩函数，满足 oled_app.c 新增退避逻辑的链接依赖。
 * 主要流程：
 *   测试不执行真实初始化和 I2C 通信，因此固定返回 true。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   true 表示测试环境模拟 OLED 可用。
 */
bool Oled_DriverIsAvailable(void)
{
    return true;
}

/*
 * 函数作用：
 *   构造固定宽度期望字符串。
 * 主要流程：
 *   先把整行填充为空格，再把前缀复制到行首，最后补结束符。
 * 参数说明：
 *   out_text：输出缓冲区，长度至少为 OLED_APP_TEXT_COLUMNS + 1。
 *   prefix：期望出现在行首的文本。
 * 返回值说明：
 *   无返回值。
 */
static void build_expected_line(char *out_text, const char *prefix)
{
    memset(out_text, ' ', OLED_APP_TEXT_COLUMNS);
    out_text[OLED_APP_TEXT_COLUMNS] = '\0';
    memcpy(out_text, prefix, strlen(prefix));
}

/*
 * 函数作用：
 *   验证第 0 行会把按键掩码、点动返回通道和发送结果补齐到整行宽度。
 * 主要流程：
 *   构造显示状态，调用 Oled_AppFormatLine()，比较固定宽度字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_key_line(void)
{
    oled_app_display_state_t state;
    char line[OLED_APP_TEXT_COLUMNS + 1U];
    char expected[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));
    state.has_motor_status = true;
    state.motor_status.identify_key_mask = 0x02U;
    state.motor_status.identify_selected_motor = 1U;
    state.motor_status.identify_last_send_ok = true;
    build_expected_line(expected, "KEY=02 SEL=1 OK=1");

    assert(Oled_AppFormatLine(0U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证第 0 行在没有有效点动通道时会显示 SEL=-。
 * 主要流程：
 *   构造多键/松开后的点动状态，调用格式化函数并比较固定宽度字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_key_line_without_selected_motor(void)
{
    oled_app_display_state_t state;
    char line[OLED_APP_TEXT_COLUMNS + 1U];
    char expected[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));
    state.has_motor_status = true;
    state.motor_status.identify_key_mask = 0x03U;
    state.motor_status.identify_selected_motor = MOTOR_APP_IDENTIFY_NO_MOTOR;
    state.motor_status.identify_last_send_ok = false;
    build_expected_line(expected, "KEY=03 SEL=- OK=0");

    assert(Oled_AppFormatLine(0U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证第 0 行没有电机状态时仍能显示按键掩码并标记诊断无效。
 * 主要流程：
 *   构造无电机状态的显示快照，调用格式化函数并比较固定宽度字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_key_line_without_motor_status(void)
{
    oled_app_display_state_t state;
    char line[OLED_APP_TEXT_COLUMNS + 1U];
    char expected[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));
    state.key_mask = 0x08U;
    build_expected_line(expected, "KEY=08 SEL=- OK=0");

    assert(Oled_AppFormatLine(0U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证电机速度行会按固定宽度显示四路目标速度。
 * 主要流程：
 *   构造电机状态，分别验证第 1 行和第 2 行的格式化结果。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_motor_lines(void)
{
    oled_app_display_state_t state;
    char line[OLED_APP_TEXT_COLUMNS + 1U];
    char expected[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));
    state.has_motor_status = true;
    state.motor_status.desired_speed[0] = 100;
    state.motor_status.desired_speed[1] = -100;
    state.motor_status.desired_speed[2] = 0;
    state.motor_status.desired_speed[3] = 300;

    build_expected_line(expected, "VA=  100 VB= -100");
    assert(Oled_AppFormatLine(1U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);

    build_expected_line(expected, "VC=    0 VD=  300");
    assert(Oled_AppFormatLine(2U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证第 3 行会显示 UART3/MOTOR 的发送、超时和拒绝统计。
 * 主要流程：
 *   构造 UART 统计快照，调用格式化函数并比较固定宽度字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_motor_uart_stats_line(void)
{
    oled_app_display_state_t state;
    char line[OLED_APP_TEXT_COLUMNS + 1U];
    char expected[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));
    state.has_motor_uart_stats = true;
    state.motor_uart_stats.tx_packet_count = 123U;
    state.motor_uart_stats.tx_timeout_count = 4U;
    state.motor_uart_stats.tx_reject_count = 5U;
    build_expected_line(expected, "TX=123 TO=04 RJ=05");

    assert(Oled_AppFormatLine(3U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证格式化接口会拒绝过小输出缓冲和非法行号。
 * 主要流程：
 *   分别传入不足一行的缓冲和超出范围的行号，检查返回 false。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_rejects_invalid_arguments(void)
{
    oled_app_display_state_t state;
    char small_line[OLED_APP_TEXT_COLUMNS];
    char line[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));

    assert(Oled_AppFormatLine(0U, &state, small_line, sizeof(small_line)) == false);
    assert(Oled_AppFormatLine(4U, &state, line, sizeof(line)) == false);
    assert(Oled_AppFormatLine(0U, NULL, line, sizeof(line)) == false);
    assert(Oled_AppFormatLine(0U, &state, NULL, sizeof(line)) == false);
}

/*
 * 函数作用：
 *   验证 OLED App 初始化阶段不会访问底层 I2C。
 * 主要流程：
 *   清零 OLED 底层桩计数，调用 Oled_AppInit()，确认不调用 Oled_DriverInit()；
 *   再调用一次 Oled_AppTask()，确认 OLED 的真实初始化被延后到调度器任务阶段。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_oled_app_init_defers_i2c_access_until_task(void)
{
    s_oled_driver_init_count = 0U;
    s_oled_driver_show_string_count = 0U;
    s_oled_driver_show_char_count = 0U;

    Oled_AppInit();

    assert(Oled_AppIsReady() == false);
    assert(s_oled_driver_init_count == 0U);
    assert(s_oled_driver_show_string_count == 0U);

    Oled_AppTask();

    assert(Oled_AppIsReady() == true);
    assert(s_oled_driver_init_count == 1U);
}

/*
 * 函数作用：
 *   验证 OLED 周期任务每次只刷新一个 6x8 字符。
 * 主要流程：
 *   先调用 Oled_AppTask() 完成延迟初始化，再连续调用两次任务，确认只发生两次
 *   Oled_DriverShowChar()，且不再调用 Oled_DriverShowString() 整行同步刷新。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_oled_app_task_refreshes_one_char_per_call(void)
{
    s_oled_driver_init_count = 0U;
    s_oled_driver_show_string_count = 0U;
    s_oled_driver_show_char_count = 0U;

    Oled_AppInit();
    Oled_AppTask();
    assert(Oled_AppIsReady() == true);
    assert(s_oled_driver_init_count == 1U);
    assert(s_oled_driver_show_char_count == 0U);
    assert(s_oled_driver_show_string_count == 0U);

    Oled_AppTask();
    assert(s_oled_driver_show_char_count == 1U);
    assert(s_oled_driver_show_string_count == 0U);

    Oled_AppTask();
    assert(s_oled_driver_show_char_count == 2U);
    assert(s_oled_driver_show_string_count == 0U);
}

int main(void)
{
    test_oled_app_init_defers_i2c_access_until_task();
    test_oled_app_task_refreshes_one_char_per_call();
    test_format_key_line();
    test_format_key_line_without_selected_motor();
    test_format_key_line_without_motor_status();
    test_format_motor_lines();
    test_format_motor_uart_stats_line();
    test_format_rejects_invalid_arguments();

    puts("oled_app_format_test: all tests passed");
    return 0;
}
