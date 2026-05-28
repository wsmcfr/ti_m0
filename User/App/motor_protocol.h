/**
 * @file    motor_protocol.h
 * @brief   电机驱动板 Modbus RTU 协议构帧与解析接口。
 *
 * @details 本文件只定义和电机驱动板串口协议相关的纯 C 接口，不直接访问 UART。
 *          App 层可以把这里构造出的帧交给 UART3 发送，PC 单元测试也可以直接验证协议字节。
 */

#ifndef MOTOR_PROTOCOL_H
#define MOTOR_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 电机驱动板默认 Modbus 从站地址，来自参考例程。 */
#define MOTOR_PROTOCOL_SLAVE_ADDR              (0x0AU)

/* 四轮车当前使用 4 个电机寄存器，速度、PID 和编码器解析都按 4 路处理。 */
#define MOTOR_PROTOCOL_MOTOR_COUNT             (4U)

/* 电机协议最长帧为 PID 写入：7 字节头 + 24 字节数据 + 2 字节 CRC = 33 字节，留 40 字节余量。 */
#define MOTOR_PROTOCOL_MAX_FRAME_SIZE          (40U)

/* 写单寄存器功能码，用于闭环使能和编码器选择。 */
#define MOTOR_PROTOCOL_FUNC_WRITE_SINGLE       (0x06U)

/* 写多个寄存器功能码，用于四路速度和四路 PID 参数写入。 */
#define MOTOR_PROTOCOL_FUNC_WRITE_MULTI        (0x10U)

/* 读保持寄存器响应功能码，用于解析编码器返回数据。 */
#define MOTOR_PROTOCOL_FUNC_READ_HOLDING       (0x03U)

/* 速度写入起始寄存器，参考例程从 0x0000 连续写 4 个 int16 速度值。 */
#define MOTOR_PROTOCOL_REG_SPEED_BASE          (0x0000U)

/* 闭环模式控制寄存器，写 0x0001 进入闭环控制。 */
#define MOTOR_PROTOCOL_REG_CLOSED_LOOP         (0x0008U)

/* 编码器读取选择寄存器起点，A/B/C/D 分别为 0x0009~0x000C。 */
#define MOTOR_PROTOCOL_REG_ENCODER_SELECT_BASE (0x0009U)

/* PID 写入起始寄存器，参考例程从 0x0015 写 12 个寄存器。 */
#define MOTOR_PROTOCOL_REG_PID_BASE            (0x0015U)

/**
 * @brief  单个电机 PID 参数。
 *
 * @note   协议发送时会把 kp/ki/kd 乘以 1000 后转成 uint16_t 寄存器值。
 */
typedef struct
{
    float kp;  /* 比例系数，发送前按 0.001 精度放大。 */
    float ki;  /* 积分系数，发送前按 0.001 精度放大。 */
    float kd;  /* 微分系数，发送前按 0.001 精度放大。 */
} motor_protocol_pid_t;

uint16_t MotorProtocol_Crc16(const uint8_t *data, size_t length);
bool MotorProtocol_CheckFrameCrc(const uint8_t *frame, size_t length);
size_t MotorProtocol_BuildWriteSingleFrame(uint16_t reg_addr, uint16_t value,
    uint8_t *out_frame, size_t max_length);
size_t MotorProtocol_BuildClosedLoopFrame(uint8_t *out_frame, size_t max_length);
size_t MotorProtocol_BuildEncoderSelectFrame(uint8_t motor_index,
    uint8_t *out_frame, size_t max_length);
size_t MotorProtocol_BuildSpeedFrame(const int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT],
    uint8_t *out_frame, size_t max_length);
size_t MotorProtocol_BuildPidFrame(const motor_protocol_pid_t pid[MOTOR_PROTOCOL_MOTOR_COUNT],
    uint8_t *out_frame, size_t max_length);
size_t MotorProtocol_ParseEncoderResponse(const uint8_t *frame, size_t length,
    int16_t *out_encoder, size_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_PROTOCOL_H */
