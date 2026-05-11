/*
 * 陀螺仪协议单元测试。
 * 该测试只验证纯协议解析和命令构造，不依赖 MSPM0 硬件寄存器。
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gyro_protocol.h"

static void test_parse_angular_velocity_frame(void)
{
    /* 准备用来接收解析结果的样本结构。 */
    gyro_protocol_sample_t sample;
    /* 构造一帧 Wz 数据：raw=0x2000，按公式应换算为 500.0 deg/s。 */
    const uint8_t frame[] = {0x5A, 0xAA, 0x00, 0x20, 0x24};

    /* 清空样本结构，确保解析前所有标志位都是 false。 */
    memset(&sample, 0, sizeof(sample));

    /* 验证协议解析函数能接受这帧合法角速度数据。 */
    assert(GyroProtocol_ParseFrame(frame, sizeof(frame), &sample) == GYRO_PROTOCOL_PARSE_OK);
    /* 验证解析后角速度有效标志被置位。 */
    assert(sample.has_angular_velocity_z == true);
    /* 验证角速度换算结果接近 500.0 deg/s。 */
    assert(fabsf(sample.angular_velocity_z_dps - 500.0f) < 0.01f);
}

static void test_parse_yaw_frame(void)
{
    /* 准备用来接收解析结果的样本结构。 */
    gyro_protocol_sample_t sample;
    /* 构造一帧 Yaw 数据：raw=0x2000，按公式应换算为 45.0 deg。 */
    const uint8_t frame[] = {0x5A, 0xBB, 0x00, 0x20, 0x35};

    /* 清空样本结构，确保解析前所有标志位都是 false。 */
    memset(&sample, 0, sizeof(sample));

    /* 验证协议解析函数能接受这帧合法航向角数据。 */
    assert(GyroProtocol_ParseFrame(frame, sizeof(frame), &sample) == GYRO_PROTOCOL_PARSE_OK);
    /* 验证解析后航向角有效标志被置位。 */
    assert(sample.has_yaw_z == true);
    /* 验证航向角换算结果接近 45.0 deg。 */
    assert(fabsf(sample.yaw_z_deg - 45.0f) < 0.01f);
}

static void test_reject_bad_checksum(void)
{
    /* 准备用来接收解析结果的样本结构。 */
    gyro_protocol_sample_t sample;
    /* 构造一帧校验字节故意错误的数据。 */
    const uint8_t frame[] = {0x5A, 0xAA, 0x00, 0x40, 0x00};

    /* 清空样本结构，避免旧值影响测试观察。 */
    memset(&sample, 0, sizeof(sample));

    /* 验证解析器能识别校验错误并返回 BAD_CHECKSUM。 */
    assert(GyroProtocol_ParseFrame(frame, sizeof(frame), &sample) == GYRO_PROTOCOL_PARSE_BAD_CHECKSUM);
}

static void test_stream_parser_accepts_split_frames(void)
{
    /* 准备流式解析器状态，用于模拟 UART 包跨帧输入。 */
    gyro_protocol_parser_t parser;
    /* 准备用来接收流式解析输出的样本结构。 */
    gyro_protocol_sample_t sample;
    /* 第一段输入包含一个噪声字节和半帧 Wz 数据。 */
    const uint8_t bytes1[] = {0x00, 0x5A, 0xAA};
    /* 第二段输入补齐 Wz 帧，并紧跟一帧完整 Yaw 数据。 */
    const uint8_t bytes2[] = {0x00, 0x20, 0x24, 0x5A, 0xBB, 0x00, 0x20, 0x35};

    /* 初始化流式解析器，使缓存和索引从空状态开始。 */
    GyroProtocol_ParserInit(&parser);
    /* 清空样本结构，确保解析前没有有效字段。 */
    memset(&sample, 0, sizeof(sample));

    /* 喂入第一段半帧数据，预期还不能解析出完整有效帧。 */
    assert(GyroProtocol_FeedBytes(&parser, bytes1, sizeof(bytes1), &sample) == false);
    /* 喂入第二段数据，预期能解析出 Wz 和 Yaw 至少一帧有效数据。 */
    assert(GyroProtocol_FeedBytes(&parser, bytes2, sizeof(bytes2), &sample) == true);
    /* 验证流式解析后角速度字段有效。 */
    assert(sample.has_angular_velocity_z == true);
    /* 验证流式解析后航向角字段有效。 */
    assert(sample.has_yaw_z == true);
}

static void test_build_commands(void)
{
    /* 准备命令输出缓冲，所有构造函数都会写入 5 字节命令。 */
    uint8_t cmd[GYRO_PROTOCOL_COMMAND_SIZE];

    /* 构造解锁命令。 */
    GyroProtocol_BuildUnlockCommand(cmd);
    /* 验证解锁命令字节为手册要求的 55 AA 13 8E 5F。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x13, 0x8E, 0x5F}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造保存配置命令。 */
    GyroProtocol_BuildSaveCommand(cmd);
    /* 验证保存命令字节为 55 AA 00 00 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x00, 0x00, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造模块重启命令。 */
    GyroProtocol_BuildRebootCommand(cmd);
    /* 验证重启命令字节为 55 AA 00 FF 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x00, 0xFF, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造恢复出厂命令。 */
    GyroProtocol_BuildRestoreFactoryCommand(cmd);
    /* 验证恢复出厂命令字节为 55 AA 00 01 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x00, 0x01, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造设置 115200 波特率命令。 */
    GyroProtocol_BuildSetBaudCommand(GYRO_PROTOCOL_BAUD_115200, cmd);
    /* 验证波特率命令字节为 55 AA 03 06 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x03, 0x06, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造设置 100Hz 输出速率命令。 */
    GyroProtocol_BuildSetRateCommand(GYRO_PROTOCOL_RATE_100HZ, cmd);
    /* 验证输出速率命令字节为 55 AA 02 09 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x02, 0x09, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造 Z 轴航向角归零命令。 */
    GyroProtocol_BuildYawZeroCommand(cmd);
    /* 验证归零命令字节为 55 AA 15 00 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x15, 0x00, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造自动零偏命令。 */
    GyroProtocol_BuildAutoBiasCommand(cmd);
    /* 验证自动零偏命令字节为 55 AA 0A 01 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x0A, 0x01, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造手动比例因子标定开始命令。 */
    GyroProtocol_BuildScaleFactorStartCommand(cmd);
    /* 验证开始标定命令字节为 55 AA 0A 03 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x0A, 0x03, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造手动比例因子标定结束命令。 */
    GyroProtocol_BuildScaleFactorStopCommand(cmd);
    /* 验证结束标定复用保存命令，字节为 55 AA 00 00 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x00, 0x00, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);

    /* 构造读取 0x0A 寄存器命令。 */
    GyroProtocol_BuildReadRegisterCommand(0x0AU, cmd);
    /* 验证读寄存器命令字节为 55 AA 04 0A 00。 */
    assert(memcmp(cmd, (uint8_t[]){0x55, 0xAA, 0x04, 0x0A, 0x00}, GYRO_PROTOCOL_COMMAND_SIZE) == 0);
}

int main(void)
{
    /* 运行角速度单帧解析测试。 */
    test_parse_angular_velocity_frame();
    /* 运行航向角单帧解析测试。 */
    test_parse_yaw_frame();
    /* 运行错误校验帧拒绝测试。 */
    test_reject_bad_checksum();
    /* 运行跨输入包的流式解析测试。 */
    test_stream_parser_accepts_split_frames();
    /* 运行所有命令构造字节序测试。 */
    test_build_commands();

    /* 所有 assert 均通过后输出测试成功提示。 */
    puts("gyro_protocol_test: all tests passed");
    /* 返回 0 表示测试程序正常结束。 */
    return 0;
}
