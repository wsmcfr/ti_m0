/*
 * 电机 Modbus 协议单元测试。
 * 该测试只验证 CRC、命令构帧和编码器响应解析，不依赖 MSPM0 UART 硬件。
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "motor_protocol.h"

static void test_crc16_known_frame(void)
{
    /* 闭环设置帧的前 6 字节，CRC 低字节应为 C8，高字节应为 B3。 */
    const uint8_t frame[] = {0x0A, 0x06, 0x00, 0x08, 0x00, 0x01};

    /* 验证 Modbus RTU CRC16 使用低字节在前的标准结果。 */
    assert(MotorProtocol_Crc16(frame, sizeof(frame)) == 0xB3C8U);
}

static void test_build_closed_loop_frame(void)
{
    /* 准备输出缓冲，闭环命令固定为 8 字节。 */
    uint8_t frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
    /* 期望帧：站号 0A，功能码 06，寄存器 0008，值 0001，CRC C8 B3。 */
    const uint8_t expected[] = {0x0A, 0x06, 0x00, 0x08, 0x00, 0x01, 0xC8, 0xB3};

    /* 验证闭环命令构造函数返回长度正确，并写出完整帧内容。 */
    assert(MotorProtocol_BuildClosedLoopFrame(frame, sizeof(frame)) == sizeof(expected));
    /* 验证构造出的每个字节和参考例程协议一致。 */
    assert(memcmp(frame, expected, sizeof(expected)) == 0);
}

static void test_build_speed_frame(void)
{
    /* 准备输出缓冲，4 电机速度写入帧固定为 17 字节。 */
    uint8_t frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
    /* 速度依次为 100、-100、0、300，验证正负 int16 大端寄存器编码和 CRC。 */
    const int16_t speed[MOTOR_PROTOCOL_MOTOR_COUNT] = {100, -100, 0, 300};
    const uint8_t expected[] = {
        0x0A, 0x10, 0x00, 0x00, 0x00, 0x04, 0x08,
        0x00, 0x64, 0xFF, 0x9C, 0x00, 0x00, 0x01, 0x2C,
        0x1C, 0xE5
    };

    /* 验证速度帧长度、寄存器数量、字节数和 CRC 均符合参考协议。 */
    assert(MotorProtocol_BuildSpeedFrame(speed, frame, sizeof(frame)) == sizeof(expected));
    /* 验证构造出的速度帧字节完全匹配期望值。 */
    assert(memcmp(frame, expected, sizeof(expected)) == 0);
}

static void test_build_pid_frame(void)
{
    /* 准备输出缓冲，PID 写入帧包含 12 个寄存器。 */
    uint8_t frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
    /* 四个电机都使用简单小数，验证乘 1000 后的寄存器编码。 */
    motor_protocol_pid_t pid[MOTOR_PROTOCOL_MOTOR_COUNT] = {
        {1.0f, 0.1f, 0.01f},
        {2.0f, 0.2f, 0.02f},
        {3.0f, 0.3f, 0.03f},
        {4.0f, 0.4f, 0.04f},
    };
    size_t length;

    /* 构造 PID 帧，并验证长度为 33 字节：7 字节头 + 24 字节数据 + 2 字节 CRC。 */
    length = MotorProtocol_BuildPidFrame(pid, frame, sizeof(frame));
    /* 验证 PID 帧长度符合 12 寄存器写入格式。 */
    assert(length == 33U);
    /* 验证站号、功能码、起始寄存器、寄存器数量和数据字节数。 */
    assert(memcmp(frame, (uint8_t[]){0x0A, 0x10, 0x00, 0x15, 0x00, 0x0C, 0x18}, 7U) == 0);
    /* 验证第一个电机的 Kp=1000、Ki=100、Kd=10 按大端寄存器写入。 */
    assert(memcmp(&frame[7], (uint8_t[]){0x03, 0xE8, 0x00, 0x64, 0x00, 0x0A}, 6U) == 0);
    /* 验证帧尾 CRC 与前面所有字节重新计算的结果一致。 */
    assert(MotorProtocol_CheckFrameCrc(frame, length) == true);
}

static void test_parse_encoder_response(void)
{
    /* 构造 4 个编码器寄存器响应：1、-2、100、-100，并带合法 CRC。 */
    uint8_t frame[] = {
        0x0A, 0x03, 0x08,
        0x00, 0x01, 0xFF, 0xFE, 0x00, 0x64, 0xFF, 0x9C,
        0x9D, 0xAE
    };
    int16_t encoder[MOTOR_PROTOCOL_MOTOR_COUNT] = {0};

    /* 验证解析函数接受合法帧，并返回 4 个寄存器值。 */
    assert(MotorProtocol_ParseEncoderResponse(frame, sizeof(frame), encoder, MOTOR_PROTOCOL_MOTOR_COUNT) == 4U);
    /* 验证 int16 编码按 Modbus 大端寄存器正确还原。 */
    assert(encoder[0] == 1);
    assert(encoder[1] == -2);
    assert(encoder[2] == 100);
    assert(encoder[3] == -100);

    /* 破坏 CRC 低字节，验证解析器能拒绝错误帧。 */
    frame[11] ^= 0x01U;
    assert(MotorProtocol_ParseEncoderResponse(frame, sizeof(frame), encoder, MOTOR_PROTOCOL_MOTOR_COUNT) == 0U);
}

int main(void)
{
    /* 运行 CRC 已知值测试。 */
    test_crc16_known_frame();
    /* 运行闭环命令构帧测试。 */
    test_build_closed_loop_frame();
    /* 运行四电机速度构帧测试。 */
    test_build_speed_frame();
    /* 运行四电机 PID 构帧测试。 */
    test_build_pid_frame();
    /* 运行编码器响应解析测试。 */
    test_parse_encoder_response();

    /* 所有 assert 均通过后输出成功提示。 */
    puts("motor_protocol_test: all tests passed");
    /* 返回 0 表示测试正常结束。 */
    return 0;
}
