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
 *   验证第 0 行会把按键掩码格式化成两位十六进制并补齐整行宽度。
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
    state.key_mask = 0x0AU;
    build_expected_line(expected, "MSPM0 K=0A");

    assert(Oled_AppFormatLine(0U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证第 1 行有灰度快照时会显示灰度掩码和位置误差。
 * 主要流程：
 *   构造有效灰度快照，调用格式化函数并比较固定宽度字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_gray_line_with_snapshot(void)
{
    oled_app_display_state_t state;
    char line[OLED_APP_TEXT_COLUMNS + 1U];
    char expected[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));
    state.has_line_snapshot = true;
    state.line_snapshot.bit_mask = 0x5CU;
    state.line_snapshot.position_error = -42;
    build_expected_line(expected, "GRAY=5C E=  -42");

    assert(Oled_AppFormatLine(1U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证第 1 行没有灰度快照时显示等待状态。
 * 主要流程：
 *   构造无快照状态，调用格式化函数并比较固定宽度字符串。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_format_gray_wait_line(void)
{
    oled_app_display_state_t state;
    char line[OLED_APP_TEXT_COLUMNS + 1U];
    char expected[OLED_APP_TEXT_COLUMNS + 1U];

    memset(&state, 0, sizeof(state));
    build_expected_line(expected, "GRAY=WAIT");

    assert(Oled_AppFormatLine(1U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);
}

/*
 * 函数作用：
 *   验证电机速度行会按固定宽度显示两路目标速度。
 * 主要流程：
 *   构造电机状态，分别验证第 2 行和第 3 行的格式化结果。
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
    assert(Oled_AppFormatLine(2U, &state, line, sizeof(line)) == true);
    assert(strcmp(line, expected) == 0);

    build_expected_line(expected, "VC=    0 VD=  300");
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

int main(void)
{
    test_format_key_line();
    test_format_gray_line_with_snapshot();
    test_format_gray_wait_line();
    test_format_motor_lines();
    test_format_rejects_invalid_arguments();

    puts("oled_app_format_test: all tests passed");
    return 0;
}
