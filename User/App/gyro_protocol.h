/**
 * @file    gyro_protocol.h
 * @brief   陀螺仪串口协议解析与配置命令构造接口。
 *
 * @details 本模块只处理手册中的 5 字节串口协议，不访问 UART/DMA 硬件。
 *          这样协议解析可以在 PC 上单元测试，也方便后续换串口或换 DMA 实现。
 */

#ifndef GYRO_PROTOCOL_H
#define GYRO_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 陀螺仪普通数据帧长度：0x5A + TYPE + DATAL + DATAH + SUM。 */
#define GYRO_PROTOCOL_FRAME_SIZE       (5U)

/* 陀螺仪写寄存器命令长度：0x55 + 0xAA + ADDR + DATAL + DATAH。 */
#define GYRO_PROTOCOL_COMMAND_SIZE     (5U)

/* 数据帧帧头，手册规定所有输出帧均以 0x5A 开始。 */
#define GYRO_PROTOCOL_DATA_HEAD        (0x5AU)

/* 写寄存器命令第 1、2 字节，手册规定写格式固定为 0x55 0xAA。 */
#define GYRO_PROTOCOL_WRITE_HEAD_0     (0x55U)
#define GYRO_PROTOCOL_WRITE_HEAD_1     (0xAAU)

/* TYPE=0xAA 表示 Z 轴角速度输出帧。 */
#define GYRO_PROTOCOL_TYPE_WZ          (0xAAU)

/* TYPE=0xBB 表示 Z 轴航向角输出帧。 */
#define GYRO_PROTOCOL_TYPE_YAW         (0xBBU)

/**
 * @brief 陀螺仪串口波特率寄存器枚举。
 *
 * @note  枚举值等于手册 BAUD 寄存器低 4 位的配置值，可直接写入命令 DATA 低字节。
 */
typedef enum
{
    GYRO_PROTOCOL_BAUD_2400   = 0x00U,
    GYRO_PROTOCOL_BAUD_4800   = 0x01U,
    GYRO_PROTOCOL_BAUD_9600   = 0x02U,
    GYRO_PROTOCOL_BAUD_19200  = 0x03U,
    GYRO_PROTOCOL_BAUD_38400  = 0x04U,
    GYRO_PROTOCOL_BAUD_57600  = 0x05U,
    GYRO_PROTOCOL_BAUD_115200 = 0x06U,
    GYRO_PROTOCOL_BAUD_230400 = 0x07U,
} gyro_protocol_baud_t;

/**
 * @brief 陀螺仪输出速率寄存器枚举。
 *
 * @note  枚举值等于手册 RRATE 寄存器低 4 位的配置值，可直接写入命令 DATA 低字节。
 */
typedef enum
{
    GYRO_PROTOCOL_RATE_0_1HZ  = 0x00U,
    GYRO_PROTOCOL_RATE_0_2HZ  = 0x01U,
    GYRO_PROTOCOL_RATE_0_5HZ  = 0x02U,
    GYRO_PROTOCOL_RATE_1HZ    = 0x03U,
    GYRO_PROTOCOL_RATE_2HZ    = 0x04U,
    GYRO_PROTOCOL_RATE_5HZ    = 0x05U,
    GYRO_PROTOCOL_RATE_10HZ   = 0x06U,
    GYRO_PROTOCOL_RATE_20HZ   = 0x07U,
    GYRO_PROTOCOL_RATE_50HZ   = 0x08U,
    GYRO_PROTOCOL_RATE_100HZ  = 0x09U,
    GYRO_PROTOCOL_RATE_125HZ  = 0x0AU,
    GYRO_PROTOCOL_RATE_200HZ  = 0x0BU,
    GYRO_PROTOCOL_RATE_250HZ  = 0x0CU,
    GYRO_PROTOCOL_RATE_500HZ  = 0x0DU,
    GYRO_PROTOCOL_RATE_1000HZ = 0x0EU,
} gyro_protocol_rate_t;

/**
 * @brief 陀螺仪协议解析结果。
 *
 * @note  该结果帮助调用者区分帧头错误、长度不足、校验失败和未知 TYPE。
 */
typedef enum
{
    GYRO_PROTOCOL_PARSE_OK = 0,
    GYRO_PROTOCOL_PARSE_NULL,
    GYRO_PROTOCOL_PARSE_SHORT,
    GYRO_PROTOCOL_PARSE_BAD_HEAD,
    GYRO_PROTOCOL_PARSE_BAD_CHECKSUM,
    GYRO_PROTOCOL_PARSE_UNKNOWN_TYPE,
} gyro_protocol_parse_result_t;

/**
 * @brief 陀螺仪最新物理量样本。
 *
 * @note  单个串口数据包可能只包含角速度或航向角，因此用 has_xxx 标记对应字段是否更新。
 */
typedef struct
{
    bool has_angular_velocity_z;   /* true 表示 angular_velocity_z_dps 是本次解析得到的新角速度。 */
    bool has_yaw_z;                /* true 表示 yaw_z_deg 是本次解析得到的新航向角。 */
    float angular_velocity_z_dps;  /* Z 轴角速度，单位 deg/s。 */
    float yaw_z_deg;               /* Z 轴航向角，单位 deg。 */
    int16_t raw_angular_velocity_z;/* 角速度原始有符号 short 数据，便于调试原始比例换算。 */
    int16_t raw_yaw_z;             /* 航向角原始有符号 short 数据，便于调试原始比例换算。 */
} gyro_protocol_sample_t;

/**
 * @brief 陀螺仪流式解析器状态。
 *
 * @note  UART DMA 空闲中断可能一次交付多个帧，也可能从半帧开始。
 *        该结构保存跨调用缓存，用于从连续字节流中提取 5 字节数据帧。
 */
typedef struct
{
    uint8_t buffer[GYRO_PROTOCOL_FRAME_SIZE]; /* 当前正在拼接的候选 5 字节帧。 */
    uint8_t index;                            /* buffer 已写入字节数。 */
} gyro_protocol_parser_t;

void GyroProtocol_ParserInit(gyro_protocol_parser_t *parser);
uint8_t GyroProtocol_Checksum(const uint8_t *frame);
gyro_protocol_parse_result_t GyroProtocol_ParseFrame(
    const uint8_t *frame, size_t length, gyro_protocol_sample_t *sample);
bool GyroProtocol_FeedBytes(gyro_protocol_parser_t *parser,
    const uint8_t *data, size_t length, gyro_protocol_sample_t *sample);

void GyroProtocol_BuildWriteCommand(uint8_t reg_addr, int16_t value, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildUnlockCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildSaveCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildRebootCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildRestoreFactoryCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildYawZeroCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildAutoBiasCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildScaleFactorStartCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildScaleFactorStopCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildReadRegisterCommand(uint8_t reg_addr, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildSetBaudCommand(gyro_protocol_baud_t baud, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);
void GyroProtocol_BuildSetRateCommand(gyro_protocol_rate_t rate, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* GYRO_PROTOCOL_H */
