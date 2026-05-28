/**
 * @file    motor_protocol.c
 * @brief   电机驱动板 Modbus RTU 协议构帧与解析实现。
 */

#include "motor_protocol.h"

/* Modbus RTU CRC16 初值，标准多项式为 0xA001。 */
#define MOTOR_PROTOCOL_CRC_INIT        (0xFFFFU)

/* Modbus RTU CRC16 反向多项式。 */
#define MOTOR_PROTOCOL_CRC_POLY        (0xA001U)

/* 写单寄存器帧长度：地址、功能码、寄存器、值、CRC。 */
#define MOTOR_PROTOCOL_WRITE_SINGLE_SIZE (8U)

/* 四电机速度帧长度：7 字节头、8 字节速度数据、2 字节 CRC。 */
#define MOTOR_PROTOCOL_SPEED_FRAME_SIZE  (17U)

/* 四电机 PID 帧长度：7 字节头、24 字节 PID 数据、2 字节 CRC。 */
#define MOTOR_PROTOCOL_PID_FRAME_SIZE    (33U)

static void MotorProtocol_WriteU16Be(uint8_t *buffer, uint16_t value);
static void MotorProtocol_AppendCrc(uint8_t *frame, size_t data_length);
static uint16_t MotorProtocol_PidFloatToRegister(float value);

/**
 * @brief  按大端顺序写入 16 位寄存器值。
 *
 * @note   Modbus 寄存器在线上固定为高字节在前、低字节在后。
 *
 * @param  buffer 输出缓冲，必须至少有 2 字节空间。
 * @param  value  需要写入的寄存器值。
 * @return 无。
 */
static void MotorProtocol_WriteU16Be(uint8_t *buffer, uint16_t value)
{
    /* 缓冲为空时无法写入，直接返回保护调用路径。 */
    if (buffer == NULL)
    {
        /* 空指针没有可写空间。 */
        return;
    }

    /* 写入高字节，符合 Modbus 寄存器大端顺序。 */
    buffer[0] = (uint8_t)((value >> 8U) & 0xFFU);
    /* 写入低字节，完成 16 位寄存器编码。 */
    buffer[1] = (uint8_t)(value & 0xFFU);
}

/**
 * @brief  在帧尾追加低字节在前的 Modbus CRC。
 *
 * @note   本函数假设 frame[data_length] 和 frame[data_length + 1] 有可写空间。
 *
 * @param  frame       需要补 CRC 的帧缓冲。
 * @param  data_length 不包含 CRC 的有效数据长度。
 * @return 无。
 */
static void MotorProtocol_AppendCrc(uint8_t *frame, size_t data_length)
{
    /* 保存对 data_length 字节计算出的 CRC。 */
    uint16_t crc;

    /* 帧为空时无法追加校验。 */
    if (frame == NULL)
    {
        /* 空指针没有可写空间。 */
        return;
    }

    /* 对帧头和数据区计算 Modbus CRC16。 */
    crc = MotorProtocol_Crc16(frame, data_length);
    /* Modbus RTU 在线路上发送 CRC 低字节在前。 */
    frame[data_length] = (uint8_t)(crc & 0xFFU);
    /* 追加 CRC 高字节。 */
    frame[data_length + 1U] = (uint8_t)((crc >> 8U) & 0xFFU);
}

/**
 * @brief  把 PID 浮点参数转换为 0.001 精度寄存器值。
 *
 * @note   参考例程直接执行 (uint16_t)(kp * 1000)，这里保留同样语义。
 *         负数 PID 不符合当前电机板参数含义，因此小于 0 时钳制为 0；
 *         超过 uint16_t 最大值时钳制为 65535，避免转换溢出。
 *
 * @param  value 浮点 PID 参数。
 * @return 放大 1000 后的 16 位寄存器值。
 */
static uint16_t MotorProtocol_PidFloatToRegister(float value)
{
    /* 保存放大后的浮点值，用于边界钳制。 */
    float scaled;

    /* PID 参数小于等于 0 时发送 0，避免负数转换成很大的无符号值。 */
    if (value <= 0.0f)
    {
        /* 返回 0 表示该项关闭或未配置。 */
        return 0U;
    }

    /* 按参考协议把 PID 参数放大 1000 倍。 */
    scaled = value * 1000.0f;
    /* 超出 16 位寄存器最大值时钳制，防止溢出回绕。 */
    if (scaled >= 65535.0f)
    {
        /* 返回最大寄存器值。 */
        return 0xFFFFU;
    }

    /* 正常范围内直接截断为 uint16_t，匹配参考例程语义。 */
    return (uint16_t)scaled;
}

/**
 * @brief  计算 Modbus RTU CRC16。
 *
 * @note   初值 0xFFFF，多项式 0xA001。返回值的低字节需要先发送。
 *
 * @param  data   输入数据缓冲。
 * @param  length 参与 CRC 计算的字节数。
 * @return CRC16 结果；输入为空且 length 非 0 时返回初值。
 */
uint16_t MotorProtocol_Crc16(const uint8_t *data, size_t length)
{
    /* CRC 累加寄存器从标准 Modbus 初值开始。 */
    uint16_t crc = MOTOR_PROTOCOL_CRC_INIT;

    /* 数据指针为空时无法逐字节计算，直接返回初值作为保护。 */
    if ((data == NULL) && (length > 0U))
    {
        /* 空指针输入没有有效数据。 */
        return crc;
    }

    /* 逐字节处理输入数据。 */
    for (size_t i = 0U; i < length; i++)
    {
        /* 先把当前字节异或进 CRC 低 8 位。 */
        crc ^= (uint16_t)data[i];
        /* 每个字节按 8 个 bit 迭代右移和异或多项式。 */
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            /* 如果最低位为 1，右移后需要异或反向多项式。 */
            if ((crc & 0x0001U) != 0U)
            {
                /* 处理带进位的 CRC bit。 */
                crc = (uint16_t)((crc >> 1U) ^ MOTOR_PROTOCOL_CRC_POLY);
            }
            else
            {
                /* 最低位为 0 时只右移。 */
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }

    /* 返回完整 CRC，调用者决定发送字节顺序。 */
    return crc;
}

/**
 * @brief  校验完整 Modbus 帧尾部 CRC 是否正确。
 *
 * @note   传入 length 必须包含最后 2 个 CRC 字节。
 *
 * @param  frame  完整 Modbus 帧。
 * @param  length 帧总长度。
 * @return true 表示 CRC 匹配；false 表示参数错误或 CRC 不匹配。
 */
bool MotorProtocol_CheckFrameCrc(const uint8_t *frame, size_t length)
{
    /* 保存计算出的 CRC。 */
    uint16_t crc_calc;
    /* 保存从帧尾读取出的 CRC。 */
    uint16_t crc_recv;

    /* 完整帧至少应包含 1 字节数据和 2 字节 CRC。 */
    if ((frame == NULL) || (length < 3U))
    {
        /* 参数错误时无法校验。 */
        return false;
    }

    /* 对不含 CRC 的前 length-2 字节重新计算 CRC。 */
    crc_calc = MotorProtocol_Crc16(frame, length - 2U);
    /* 帧尾 CRC 低字节在前，高字节在后。 */
    crc_recv = (uint16_t)frame[length - 2U] | ((uint16_t)frame[length - 1U] << 8U);
    /* 返回计算结果与接收结果是否一致。 */
    return (crc_calc == crc_recv);
}

/**
 * @brief  构造写单寄存器帧。
 *
 * @note   当前用于闭环使能和编码器 A/B/C/D 选择。
 *
 * @param  reg_addr   目标寄存器地址。
 * @param  value      写入值。
 * @param  out_frame  输出帧缓冲。
 * @param  max_length 输出缓冲容量。
 * @return 实际帧长度；返回 0 表示参数错误或容量不足。
 */
size_t MotorProtocol_BuildWriteSingleFrame(uint16_t reg_addr, uint16_t value,
    uint8_t *out_frame, size_t max_length)
{
    /* 检查输出缓冲容量是否足够容纳 8 字节写单寄存器帧。 */
    if ((out_frame == NULL) || (max_length < MOTOR_PROTOCOL_WRITE_SINGLE_SIZE))
    {
        /* 参数无效或缓冲不足时不写入。 */
        return 0U;
    }

    /* 写入默认从站地址。 */
    out_frame[0] = MOTOR_PROTOCOL_SLAVE_ADDR;
    /* 写入功能码 0x06。 */
    out_frame[1] = MOTOR_PROTOCOL_FUNC_WRITE_SINGLE;
    /* 写入目标寄存器地址。 */
    MotorProtocol_WriteU16Be(&out_frame[2], reg_addr);
    /* 写入目标寄存器值。 */
    MotorProtocol_WriteU16Be(&out_frame[4], value);
    /* 对前 6 字节追加 2 字节 CRC。 */
    MotorProtocol_AppendCrc(out_frame, 6U);

    /* 返回完整帧长度。 */
    return MOTOR_PROTOCOL_WRITE_SINGLE_SIZE;
}

/**
 * @brief  构造进入闭环控制的命令帧。
 *
 * @note   写寄存器 0x0008 = 0x0001，和参考例程 Motor_Set_ClosedLoop() 一致。
 *
 * @param  out_frame  输出帧缓冲。
 * @param  max_length 输出缓冲容量。
 * @return 实际帧长度；返回 0 表示失败。
 */
size_t MotorProtocol_BuildClosedLoopFrame(uint8_t *out_frame, size_t max_length)
{
    /* 复用写单寄存器构帧函数，写入闭环使能值 1。 */
    return MotorProtocol_BuildWriteSingleFrame(MOTOR_PROTOCOL_REG_CLOSED_LOOP,
        0x0001U, out_frame, max_length);
}

/**
 * @brief  构造编码器选择命令帧。
 *
 * @note   motor_index 为 0~3，分别写 0x0009~0x000C = 1，用于选择读取 A/B/C/D 编码器。
 *
 * @param  motor_index 电机索引，0~3 有效。
 * @param  out_frame   输出帧缓冲。
 * @param  max_length  输出缓冲容量。
 * @return 实际帧长度；返回 0 表示索引非法或容量不足。
 */
size_t MotorProtocol_BuildEncoderSelectFrame(uint8_t motor_index,
    uint8_t *out_frame, size_t max_length)
{
    /* 电机索引必须在 0~3 范围内。 */
    if (motor_index >= MOTOR_PROTOCOL_MOTOR_COUNT)
    {
        /* 索引非法时不构帧。 */
        return 0U;
    }

    /* 按参考例程的寄存器连续关系计算目标寄存器地址。 */
    return MotorProtocol_BuildWriteSingleFrame(
        (uint16_t)(MOTOR_PROTOCOL_REG_ENCODER_SELECT_BASE + motor_index),
        0x0001U, out_frame, max_length);
}

/**
 * @brief  构造四路电机速度写入帧。
 *
 * @note   写寄存器 0x0000 起连续 4 个 int16 速度，速度在线路上按大端寄存器发送。
 *
 * @param  speeds     四路速度数组。
 * @param  out_frame  输出帧缓冲。
 * @param  max_length 输出缓冲容量。
 * @return 实际帧长度；返回 0 表示参数错误或容量不足。
 */
size_t MotorProtocol_BuildSpeedFrame(const int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT],
    uint8_t *out_frame, size_t max_length)
{
    /* 检查输入速度数组、输出缓冲和容量。 */
    if ((speeds == NULL) || (out_frame == NULL) ||
        (max_length < MOTOR_PROTOCOL_SPEED_FRAME_SIZE))
    {
        /* 参数错误时不写入帧。 */
        return 0U;
    }

    /* 写入从站地址和写多寄存器功能码。 */
    out_frame[0] = MOTOR_PROTOCOL_SLAVE_ADDR;
    out_frame[1] = MOTOR_PROTOCOL_FUNC_WRITE_MULTI;
    /* 写入速度起始寄存器 0x0000。 */
    MotorProtocol_WriteU16Be(&out_frame[2], MOTOR_PROTOCOL_REG_SPEED_BASE);
    /* 写入寄存器数量 4。 */
    MotorProtocol_WriteU16Be(&out_frame[4], MOTOR_PROTOCOL_MOTOR_COUNT);
    /* 写入数据字节数 8。 */
    out_frame[6] = (uint8_t)(MOTOR_PROTOCOL_MOTOR_COUNT * 2U);

    /* 逐个写入四路 int16 速度，按寄存器大端顺序编码。 */
    for (uint8_t i = 0U; i < MOTOR_PROTOCOL_MOTOR_COUNT; i++)
    {
        /* int16 按补码转换成 uint16 后保持位模式不变。 */
        MotorProtocol_WriteU16Be(&out_frame[7U + ((size_t)i * 2U)], (uint16_t)speeds[i]);
    }

    /* 对前 15 字节追加 CRC。 */
    MotorProtocol_AppendCrc(out_frame, MOTOR_PROTOCOL_SPEED_FRAME_SIZE - 2U);
    /* 返回完整速度帧长度。 */
    return MOTOR_PROTOCOL_SPEED_FRAME_SIZE;
}

/**
 * @brief  构造四路电机 PID 参数写入帧。
 *
 * @note   每个电机写 Kp/Ki/Kd 三个寄存器，共 12 个寄存器，从 0x0015 开始。
 *
 * @param  pid        四路 PID 参数数组。
 * @param  out_frame  输出帧缓冲。
 * @param  max_length 输出缓冲容量。
 * @return 实际帧长度；返回 0 表示参数错误或容量不足。
 */
size_t MotorProtocol_BuildPidFrame(const motor_protocol_pid_t pid[MOTOR_PROTOCOL_MOTOR_COUNT],
    uint8_t *out_frame, size_t max_length)
{
    /* PID 写入寄存器总数为 4 路 * 3 参数。 */
    const uint16_t register_count = (uint16_t)(MOTOR_PROTOCOL_MOTOR_COUNT * 3U);
    /* 数据写入起始位置，前 7 字节为 Modbus 写多寄存器头。 */
    size_t index = 7U;

    /* 检查 PID 数组、输出缓冲和容量。 */
    if ((pid == NULL) || (out_frame == NULL) ||
        (max_length < MOTOR_PROTOCOL_PID_FRAME_SIZE))
    {
        /* 参数错误时不写入帧。 */
        return 0U;
    }

    /* 写入从站地址和写多寄存器功能码。 */
    out_frame[0] = MOTOR_PROTOCOL_SLAVE_ADDR;
    out_frame[1] = MOTOR_PROTOCOL_FUNC_WRITE_MULTI;
    /* 写入 PID 起始寄存器 0x0015。 */
    MotorProtocol_WriteU16Be(&out_frame[2], MOTOR_PROTOCOL_REG_PID_BASE);
    /* 写入寄存器数量 12。 */
    MotorProtocol_WriteU16Be(&out_frame[4], register_count);
    /* 写入数据字节数 24。 */
    out_frame[6] = (uint8_t)(register_count * 2U);

    /* 按电机顺序依次写入 Kp、Ki、Kd。 */
    for (uint8_t motor = 0U; motor < MOTOR_PROTOCOL_MOTOR_COUNT; motor++)
    {
        /* 写入当前电机 Kp。 */
        MotorProtocol_WriteU16Be(&out_frame[index], MotorProtocol_PidFloatToRegister(pid[motor].kp));
        index += 2U;
        /* 写入当前电机 Ki。 */
        MotorProtocol_WriteU16Be(&out_frame[index], MotorProtocol_PidFloatToRegister(pid[motor].ki));
        index += 2U;
        /* 写入当前电机 Kd。 */
        MotorProtocol_WriteU16Be(&out_frame[index], MotorProtocol_PidFloatToRegister(pid[motor].kd));
        index += 2U;
    }

    /* 对前 31 字节追加 CRC。 */
    MotorProtocol_AppendCrc(out_frame, MOTOR_PROTOCOL_PID_FRAME_SIZE - 2U);
    /* 返回完整 PID 帧长度。 */
    return MOTOR_PROTOCOL_PID_FRAME_SIZE;
}

/**
 * @brief  解析编码器读保持寄存器响应帧。
 *
 * @note   期望响应格式为：0A 03 BYTE_COUNT DATA... CRC_LO CRC_HI。
 *         每个编码器值按 int16 大端寄存器还原。
 *
 * @param  frame       完整响应帧。
 * @param  length      帧总长度。
 * @param  out_encoder 输出编码器数组。
 * @param  max_count   输出数组最多可写入的 int16 数量。
 * @return 实际解析出的寄存器数量；返回 0 表示帧非法或 CRC 错误。
 */
size_t MotorProtocol_ParseEncoderResponse(const uint8_t *frame, size_t length,
    int16_t *out_encoder, size_t max_count)
{
    /* 保存响应中的数据字节数。 */
    uint8_t byte_count;
    /* 保存寄存器数量。 */
    size_t register_count;

    /* 检查基本参数和最小响应长度：地址、功能码、字节数、2 字节 CRC。 */
    if ((frame == NULL) || (out_encoder == NULL) || (length < 5U) || (max_count == 0U))
    {
        /* 参数错误时无法解析。 */
        return 0U;
    }

    /* 校验站号和功能码，避免把其它响应误解析为编码器数据。 */
    if ((frame[0] != MOTOR_PROTOCOL_SLAVE_ADDR) ||
        (frame[1] != MOTOR_PROTOCOL_FUNC_READ_HOLDING))
    {
        /* 非目标响应帧。 */
        return 0U;
    }

    /* 第 3 字节是数据字节数，必须为偶数。 */
    byte_count = frame[2];
    if ((byte_count == 0U) || ((byte_count & 0x01U) != 0U))
    {
        /* 非法字节数不能拆成完整寄存器。 */
        return 0U;
    }

    /* 长度必须刚好覆盖头、数据和 CRC，避免半帧或粘连误判。 */
    if (length < ((size_t)byte_count + 5U))
    {
        /* 响应帧不完整。 */
        return 0U;
    }

    /* CRC 不匹配时拒绝解析。 */
    if (MotorProtocol_CheckFrameCrc(frame, (size_t)byte_count + 5U) == false)
    {
        /* 校验失败说明数据损坏或帧未对齐。 */
        return 0U;
    }

    /* 数据字节数除以 2 得到寄存器数量。 */
    register_count = (size_t)byte_count / 2U;
    /* 如果输出数组更小，只解析调用者能接收的数量。 */
    if (register_count > max_count)
    {
        /* 截断到输出数组容量，避免越界写入。 */
        register_count = max_count;
    }

    /* 逐个还原 int16 编码器寄存器。 */
    for (size_t i = 0U; i < register_count; i++)
    {
        /* 每个寄存器从 frame[3] 开始，高字节在前。 */
        uint16_t value = ((uint16_t)frame[3U + (i * 2U)] << 8U) |
                         (uint16_t)frame[4U + (i * 2U)];
        /* 保持二进制位模式转换为 int16，得到正负编码器值。 */
        out_encoder[i] = (int16_t)value;
    }

    /* 返回成功解析的寄存器数量。 */
    return register_count;
}
