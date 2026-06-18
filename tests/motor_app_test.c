/*
 * 电机应用层单元测试。
 * 该测试使用 UART 和 tick 桩函数验证 motor_app.c 的业务接口，不访问 MSPM0 硬件寄存器。
 */

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "motor_app.h"
#include "motor_protocol.h"

/* 保存测试桩最近一次发往电机驱动板的完整 Modbus 帧。 */
static uint8_t s_last_motor_frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
/* 保存测试桩最近一次发往电机驱动板的帧长度。 */
static uint16_t s_last_motor_length;
/* 记录测试桩累计收到的电机发送请求次数。 */
static uint32_t s_motor_send_count;
/* 保存按键应用层桩函数返回的稳定按下状态。 */
static uint8_t s_key_stable_mask;

/*
 * 函数作用：
 *   清空 UART 发送桩函数保存的观测数据。
 * 主要流程：
 *   将最近发送帧、帧长度和发送次数全部清零，保证每个测试用例互不影响。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void reset_uart_stub(void)
{
    /* 清空最近一帧数据，避免上一用例残留影响断言。 */
    memset(s_last_motor_frame, 0, sizeof(s_last_motor_frame));
    /* 清空最近帧长度。 */
    s_last_motor_length = 0U;
    /* 清空发送次数统计。 */
    s_motor_send_count = 0U;
}

/*
 * 函数作用：
 *   提供按键稳定状态桩函数，让测试用例模拟 K1~K4 的当前按下状态。
 * 主要流程：
 *   直接返回测试全局变量，业务代码会把该掩码解释为 K1~K4。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   bit0~bit3 对应 K1~K4，1 表示按下。
 */
uint8_t Key_AppGetStableMask(void)
{
    /* 返回测试用例预设的按键状态。 */
    return s_key_stable_mask;
}

/*
 * 函数作用：
 *   提供 UART 电机发送桩函数，截获 motor_app.c 发出的 Modbus 帧。
 * 主要流程：
 *   检查输入有效后复制到测试全局缓冲，并返回 true 模拟发送成功。
 * 参数说明：
 *   data：待发送的 Modbus 帧。
 *   length：待发送帧长度。
 * 返回值说明：
 *   true 表示测试桩接受本次发送；false 表示参数或长度非法。
 */
bool Uart_AppSendToMotor(const uint8_t *data, uint16_t length)
{
    /* 测试桩只接受能放入本地观测缓冲的有效帧。 */
    if ((data == NULL) || (length == 0U) || (length > sizeof(s_last_motor_frame)))
    {
        /* 参数无效时模拟 UART 发送失败。 */
        return false;
    }

    /* 保存最近一次发送内容，供测试断言速度寄存器值。 */
    memcpy(s_last_motor_frame, data, length);
    /* 保存最近一次发送长度。 */
    s_last_motor_length = length;
    /* 累加发送次数，确认每个业务接口只发送一次命令帧。 */
    s_motor_send_count++;
    /* 模拟 UART3 发送成功。 */
    return true;
}

/*
 * 函数作用：
 *   提供 UART 电机读取桩函数，满足 Motor_AppTask() 链接依赖。
 * 主要流程：
 *   当前测试不注入接收数据，因此固定返回 0 表示没有新包。
 * 参数说明：
 *   out_data：调用方提供的接收缓冲，测试桩不写入。
 *   max_length：接收缓冲容量，测试桩不使用。
 * 返回值说明：
 *   固定返回 0。
 */
uint16_t Uart_AppReadMotorPacket(uint8_t *out_data, uint16_t max_length)
{
    (void)out_data;
    (void)max_length;
    return 0U;
}

/*
 * 函数作用：
 *   提供日志输出桩函数，满足 Motor_AppInit() 的启动提示依赖。
 * 主要流程：
 *   测试不关心串口日志内容，因此忽略所有参数并返回 0。
 * 参数说明：
 *   format：printf 风格格式字符串，测试桩不使用。
 * 返回值说明：
 *   固定返回 0，表示模拟日志输出完成。
 */
int my_printf(const char *format, ...)
{
    va_list args;

    (void)format;
    va_start(args, format);
    va_end(args);
    return 0;
}

/*
 * 函数作用：
 *   提供调度器 tick 桩函数，满足 motor_app.c 接收统计依赖。
 * 主要流程：
 *   当前测试不验证接收时间，因此固定返回 0。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   固定返回 0。
 */
uint32_t Scheduler_GetTick(void)
{
    return 0U;
}

/*
 * 函数作用：
 *   从最近发送的速度帧中读取指定电机速度寄存器。
 * 主要流程：
 *   按 Modbus 写多寄存器帧格式定位数据区，再把大端寄存器还原为 int16。
 * 参数说明：
 *   motor_index：电机索引，0~3 对应 A/B/C/D。
 * 返回值说明：
 *   返回最近速度帧中该电机的目标速度。
 */
static int16_t get_sent_speed(uint8_t motor_index)
{
    /* 每路速度在写多寄存器帧数据区占 2 字节，数据区从第 7 字节开始。 */
    size_t offset = 7U + ((size_t)motor_index * 2U);
    /* 按大端顺序还原寄存器位模式。 */
    uint16_t raw = ((uint16_t)s_last_motor_frame[offset] << 8U) |
                   (uint16_t)s_last_motor_frame[offset + 1U];

    /* 转回 int16，保持负速度补码语义。 */
    return (int16_t)raw;
}

/*
 * 函数作用：
 *   验证两路速度接口能按指定电机编号发送速度，未指定电机保持 0。
 * 主要流程：
 *   初始化应用层后调用 Motor_AppSetSpeed2()，再检查发送帧、状态缓存和发送次数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败会中止测试。
 */
static void test_set_two_selected_motors_and_zero_unused(void)
{
    /* 准备读取应用层状态缓存的结构体。 */
    motor_app_status_t status;

    /* 初始化电机应用层状态，模拟上电后默认速度全为 0。 */
    Motor_AppInit();
    /* 清空初始化期间可能产生的日志或旧发送观测。 */
    reset_uart_stub();

    /* 指定 A 路为 120、C 路为 -80，B/D 两路应保持 0。 */
    assert(Motor_AppSetSpeed2(0U, 120, 2U, -80) == true);

    /* 验证只发送了一帧四寄存器速度命令。 */
    assert(s_motor_send_count == 1U);
    /* 验证发送的是四路速度写入帧固定长度。 */
    assert(s_last_motor_length == 17U);
    /* 验证速度帧头部为站号 0A、写多寄存器、起始寄存器 0000、数量 4。 */
    assert(memcmp(s_last_motor_frame,
        (uint8_t[]){0x0A, 0x10, 0x00, 0x00, 0x00, 0x04, 0x08}, 7U) == 0);
    /* 验证 A/C 两路写入调用方指定速度。 */
    assert(get_sent_speed(0U) == 120);
    assert(get_sent_speed(2U) == -80);
    /* 验证 B/D 两路未被指定时写 0，适合只接两路电机的场景。 */
    assert(get_sent_speed(1U) == 0);
    assert(get_sent_speed(3U) == 0);
    /* 验证发送帧 CRC 正确，避免便捷接口破坏底层协议。 */
    assert(MotorProtocol_CheckFrameCrc(s_last_motor_frame, s_last_motor_length) == true);

    /* 读取状态缓存，确认 OLED 或其它模块看到的目标速度与发送帧一致。 */
    assert(Motor_AppGetStatus(&status) == true);
    assert(status.desired_speed[0] == 120);
    assert(status.desired_speed[1] == 0);
    assert(status.desired_speed[2] == -80);
    assert(status.desired_speed[3] == 0);
}

/*
 * 函数作用：
 *   验证单路速度接口只修改指定电机，并保留其它电机的缓存速度。
 * 主要流程：
 *   先设置两路速度建立缓存，再单独修改 B 路速度，最后检查发送帧和状态缓存。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败会中止测试。
 */
static void test_set_one_motor_keeps_cached_other_speeds(void)
{
    /* 准备读取应用层状态缓存的结构体。 */
    motor_app_status_t status;

    /* 初始化并先建立 A/C 两路速度缓存。 */
    Motor_AppInit();
    reset_uart_stub();
    assert(Motor_AppSetSpeed2(0U, 120, 2U, -80) == true);
    /* 清空观测后只统计后续单路设置产生的发送。 */
    reset_uart_stub();

    /* 单独设置 B 路速度，A/C 应保持缓存值，D 仍为 0。 */
    assert(Motor_AppSetSpeed(1U, 60) == true);

    /* 验证单路设置也只发送一帧四寄存器速度命令。 */
    assert(s_motor_send_count == 1U);
    /* 验证 A/C 缓存速度没有被单路设置意外清掉。 */
    assert(get_sent_speed(0U) == 120);
    assert(get_sent_speed(1U) == 60);
    assert(get_sent_speed(2U) == -80);
    assert(get_sent_speed(3U) == 0);

    /* 验证状态缓存与最近发送帧一致。 */
    assert(Motor_AppGetStatus(&status) == true);
    assert(status.desired_speed[0] == 120);
    assert(status.desired_speed[1] == 60);
    assert(status.desired_speed[2] == -80);
    assert(status.desired_speed[3] == 0);
}

/*
 * 函数作用：
 *   验证停止指定电机只把该路速度置 0，并拒绝非法电机编号。
 * 主要流程：
 *   先设置两路速度，再停止其中一路，同时检查非法索引不会发送命令。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败会中止测试。
 */
static void test_stop_selected_motor_and_reject_invalid_index(void)
{
    /* 初始化并建立 A/B 两路速度缓存。 */
    Motor_AppInit();
    reset_uart_stub();
    assert(Motor_AppSetSpeed2(0U, 100, 1U, -100) == true);
    reset_uart_stub();

    /* 停止 B 路，A 路速度应保持，B 路速度应置 0。 */
    assert(Motor_AppStop(1U) == true);
    assert(s_motor_send_count == 1U);
    assert(get_sent_speed(0U) == 100);
    assert(get_sent_speed(1U) == 0);
    assert(get_sent_speed(2U) == 0);
    assert(get_sent_speed(3U) == 0);

    /* 非法索引不能发送命令，避免误写未知寄存器。 */
    reset_uart_stub();
    assert(Motor_AppSetSpeed(MOTOR_PROTOCOL_MOTOR_COUNT, 50) == false);
    assert(Motor_AppStop(MOTOR_PROTOCOL_MOTOR_COUNT) == false);
    assert(Motor_AppSetSpeed2(0U, 10, MOTOR_PROTOCOL_MOTOR_COUNT, 20) == false);
    assert(Motor_AppSetSpeed2(1U, 10, 1U, 20) == false);
    assert(s_motor_send_count == 0U);
}

/*
 * 函数作用：
 *   验证单个按键会点动对应的单一路电机通道。
 * 主要流程：
 *   依次模拟 K1~K4 单键按下，调用 Motor_AppTask() 后检查速度帧中只有对应通道为低速。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败会中止测试。
 */
static void test_identify_mode_jogs_selected_channel_by_key(void)
{
    /* 准备读取应用层点动诊断状态。 */
    motor_app_status_t status;

    /* 按键到电机通道采用 K1=A、K2=B、K3=C、K4=D 的固定映射。 */
    for (uint8_t key_index = 0U; key_index < MOTOR_PROTOCOL_MOTOR_COUNT; key_index++)
    {
        /* 初始化电机应用层，确保每轮从停止状态开始。 */
        Motor_AppInit();
        reset_uart_stub();

        /* 只按下当前测试按键。 */
        s_key_stable_mask = (uint8_t)(1U << key_index);
        /* 调用电机任务，让点动识别逻辑读取按键并发送速度命令。 */
        Motor_AppTask();

        /* 单键点动应只发送一帧速度命令。 */
        assert(s_motor_send_count == 1U);
        /* 验证被选中的通道为低速正转。 */
        assert(get_sent_speed(key_index) == MOTOR_APP_IDENTIFY_JOG_SPEED);

        /* 验证其它未选中通道都被写 0，避免多个电机同时动作。 */
        for (uint8_t motor_index = 0U; motor_index < MOTOR_PROTOCOL_MOTOR_COUNT; motor_index++)
        {
            if (motor_index != key_index)
            {
                assert(get_sent_speed(motor_index) == 0);
            }
        }

        /* 验证状态快照会暴露本次点动识别的按键、返回通道和发送结果。 */
        assert(Motor_AppGetStatus(&status) == true);
        assert(status.identify_key_mask == (uint8_t)(1U << key_index));
        assert(status.identify_selected_motor == key_index);
        assert(status.identify_last_send_ok == true);
    }

    /* 测试结束后清空按键状态，避免影响后续用例。 */
    s_key_stable_mask = 0U;
}

/*
 * 函数作用：
 *   验证多键同时按下时点动识别逻辑会停机。
 * 主要流程：
 *   先让 K1 点动建立运动状态，再模拟 K1+K2 同时按下，检查四路速度都被写 0。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败会中止测试。
 */
static void test_identify_mode_stops_when_multiple_keys_pressed(void)
{
    /* 准备读取应用层点动诊断状态。 */
    motor_app_status_t status;

    /* 初始化电机应用层状态。 */
    Motor_AppInit();
    reset_uart_stub();

    /* 先按 K1，让 A 通道进入低速点动。 */
    s_key_stable_mask = 0x01U;
    Motor_AppTask();
    assert(s_motor_send_count == 1U);
    assert(get_sent_speed(0U) == MOTOR_APP_IDENTIFY_JOG_SPEED);

    /* 再模拟 K1 和 K2 同时按下，多键应触发停机。 */
    reset_uart_stub();
    s_key_stable_mask = 0x03U;
    Motor_AppTask();

    /* 多键状态变化应发送一次全停速度命令。 */
    assert(s_motor_send_count == 1U);
    for (uint8_t motor_index = 0U; motor_index < MOTOR_PROTOCOL_MOTOR_COUNT; motor_index++)
    {
        assert(get_sent_speed(motor_index) == 0);
    }

    /* 验证多键会被诊断为无有效返回通道，但停机命令已经发送成功。 */
    assert(Motor_AppGetStatus(&status) == true);
    assert(status.identify_key_mask == 0x03U);
    assert(status.identify_selected_motor == MOTOR_APP_IDENTIFY_NO_MOTOR);
    assert(status.identify_last_send_ok == true);

    /* 测试结束后清空按键状态。 */
    s_key_stable_mask = 0U;
}

/*
 * 函数作用：
 *   验证松开按键后点动识别逻辑会停机，并且状态不变时不会重复发送。
 * 主要流程：
 *   按下 K3 点动 C 通道，再松开全部按键，检查发送一次全停；再次调用不应重复发送。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败会中止测试。
 */
static void test_identify_mode_stops_on_release_without_repeating(void)
{
    /* 准备读取应用层点动诊断状态。 */
    motor_app_status_t status;

    /* 初始化电机应用层状态。 */
    Motor_AppInit();
    reset_uart_stub();

    /* 按下 K3，预期 C 通道点动。 */
    s_key_stable_mask = 0x04U;
    Motor_AppTask();
    assert(s_motor_send_count == 1U);
    assert(get_sent_speed(2U) == MOTOR_APP_IDENTIFY_JOG_SPEED);

    /* 松开全部按键，预期发送一次全停。 */
    reset_uart_stub();
    s_key_stable_mask = 0x00U;
    Motor_AppTask();
    assert(s_motor_send_count == 1U);
    for (uint8_t motor_index = 0U; motor_index < MOTOR_PROTOCOL_MOTOR_COUNT; motor_index++)
    {
        assert(get_sent_speed(motor_index) == 0);
    }

    /* 验证松开后 OLED 可看到按键为 0、返回通道无效、停机发送成功。 */
    assert(Motor_AppGetStatus(&status) == true);
    assert(status.identify_key_mask == 0x00U);
    assert(status.identify_selected_motor == MOTOR_APP_IDENTIFY_NO_MOTOR);
    assert(status.identify_last_send_ok == true);

    /* 保持松开状态再次运行任务，不应重复发送相同全停命令。 */
    reset_uart_stub();
    Motor_AppTask();
    assert(s_motor_send_count == 0U);

    /* 未重复发送时诊断状态仍保留最近一次有效停机结果。 */
    assert(Motor_AppGetStatus(&status) == true);
    assert(status.identify_key_mask == 0x00U);
    assert(status.identify_selected_motor == MOTOR_APP_IDENTIFY_NO_MOTOR);
    assert(status.identify_last_send_ok == true);
}

int main(void)
{
    /* 运行两路速度指定测试。 */
    test_set_two_selected_motors_and_zero_unused();
    /* 运行单路速度设置测试。 */
    test_set_one_motor_keeps_cached_other_speeds();
    /* 运行指定电机停止和非法索引拒绝测试。 */
    test_stop_selected_motor_and_reject_invalid_index();
    /* 运行按键点动识别单通道测试。 */
    test_identify_mode_jogs_selected_channel_by_key();
    /* 运行多键按下停机测试。 */
    test_identify_mode_stops_when_multiple_keys_pressed();
    /* 运行松开按键停机且不重复发送测试。 */
    test_identify_mode_stops_on_release_without_repeating();

    /* 所有 assert 均通过后输出测试成功提示。 */
    puts("motor_app_test: all tests passed");
    /* 返回 0 表示测试程序正常结束。 */
    return 0;
}
