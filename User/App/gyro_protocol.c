/**
 * @file    gyro_protocol.c
 * @brief   陀螺仪串口协议解析与命令构造实现。
 */

#include "gyro_protocol.h"

#include <string.h>

/* 写寄存器地址：SAVE，负责保存配置、重启、恢复出厂。 */
#define GYRO_REG_SAVE          (0x00U)

/* 写寄存器地址：RRATE，负责设置串口输出速率。 */
#define GYRO_REG_RRATE         (0x02U)

/* 写寄存器地址：BAUD，负责设置串口通信波特率。 */
#define GYRO_REG_BAUD          (0x03U)

/* 写命令地址：READ，手册示例 55 AA 04 0A 00 表示读取 0x0A 寄存器状态。 */
#define GYRO_REG_READ          (0x04U)

/* 写寄存器地址：BIAS_CAL / Scale_Factor，负责自动零偏或手动标定。 */
#define GYRO_REG_BIAS_CAL      (0x0AU)

/* 写寄存器地址：KEY，写入 0x8E5F 后才能执行配置写操作。 */
#define GYRO_REG_KEY           (0x13U)

/* 写寄存器地址：Yaw_Zero，写入 0 后执行 Z 轴角度归零。 */
#define GYRO_REG_YAW_ZERO      (0x15U)

/**
 * @brief  初始化陀螺仪流式解析器。
 *
 * @note   清空缓存并把索引归零。每次系统上电或通信异常重同步时都可以调用。
 *
 * @param  parser 解析器状态指针。
 * @return 无。
 */
void GyroProtocol_ParserInit(gyro_protocol_parser_t *parser)
{
    /* 调用者必须提供解析器状态结构。 */
    if (parser == NULL)
    {
        /* 空指针无法初始化，直接返回。 */
        return;
    }

    /* 清零内部 5 字节候选帧缓存，避免旧数据影响调试观察。 */
    memset(parser->buffer, 0, sizeof(parser->buffer));
    /* 将缓存写入索引归零，表示解析器当前等待新的帧头。 */
    parser->index = 0U;
}

/**
 * @brief  计算 5 字节数据帧校验值。
 *
 * @note   手册规定 SUMCRC = 0x5A + TYPE + DATAL + DATAH，取低 8 位。
 *         本函数只计算前 4 字节，不比较 frame[4]，便于解析函数复用。
 *
 * @param  frame 指向 5 字节数据帧的指针。
 * @return 前 4 字节累加和的低 8 位；入参为空时返回 0。
 */
uint8_t GyroProtocol_Checksum(const uint8_t *frame)
{
    /* 使用 16 位累加器保存前 4 字节和，避免 8 位加法中途溢出影响表达。 */
    uint16_t sum = 0U;

    /* 入参为空时没有可计算的帧内容。 */
    if (frame == NULL)
    {
        /* 返回 0 作为空指针保护值。 */
        return 0U;
    }

    /* 按协议把帧头、类型、低字节和高字节相加。 */
    sum = (uint16_t)frame[0] + (uint16_t)frame[1] +
          (uint16_t)frame[2] + (uint16_t)frame[3];
    /* 只保留累加和低 8 位，作为协议校验字节。 */
    return (uint8_t)(sum & 0xFFU);
}

/**
 * @brief  解析单个 5 字节陀螺仪输出帧。
 *
 * @note   每个数据帧由低字节 DATAL 和高字节 DATAH 组成一个有符号 short。
 *         这里先把 DATAH 强制转换为 int16_t 后左移，再与 DATAL 组合，
 *         保证负数原始值能正确符号扩展。
 *
 * @param  frame  指向候选帧。
 * @param  length 候选帧长度，必须不小于 5。
 * @param  sample 输出物理量样本。
 * @return 解析状态。
 */
gyro_protocol_parse_result_t GyroProtocol_ParseFrame(
    const uint8_t *frame, size_t length, gyro_protocol_sample_t *sample)
{
    /* 保存由 DATAL/DATAH 组合出来的有符号原始值。 */
    int16_t raw_value;

    /* 帧指针或输出样本为空时无法解析。 */
    if ((frame == NULL) || (sample == NULL))
    {
        /* 返回空指针错误，方便测试和上层区分原因。 */
        return GYRO_PROTOCOL_PARSE_NULL;
    }

    /* 候选帧长度必须至少包含完整 5 字节。 */
    if (length < GYRO_PROTOCOL_FRAME_SIZE)
    {
        /* 长度不足时不访问后续字节，避免越界读取。 */
        return GYRO_PROTOCOL_PARSE_SHORT;
    }

    /* 第 1 字节必须是数据帧头 0x5A。 */
    if (frame[0] != GYRO_PROTOCOL_DATA_HEAD)
    {
        /* 帧头不匹配时返回坏帧头错误。 */
        return GYRO_PROTOCOL_PARSE_BAD_HEAD;
    }

    /* 计算前 4 字节校验，并与第 5 字节校验位比较。 */
    if (GyroProtocol_Checksum(frame) != frame[4])
    {
        /* 校验不一致说明候选帧损坏或不同步。 */
        return GYRO_PROTOCOL_PARSE_BAD_CHECKSUM;
    }

    /* 将高字节左移后与低字节组合，得到协议中的 int16 原始测量值。 */
    raw_value = (int16_t)(((int16_t)((uint16_t)frame[3] << 8U)) | (int16_t)frame[2]);

    /* 判断帧类型是否为 Z 轴角速度 Wz。 */
    if (frame[1] == GYRO_PROTOCOL_TYPE_WZ)
    {
        /* 手册公式：角速度 Z = 原始 short / 32768 * 2000 deg/s。 */
        /* 保存未换算的角速度原始值，便于调试和校验。 */
        sample->raw_angular_velocity_z = raw_value;
        /* 按手册比例把原始值换算为 deg/s。 */
        sample->angular_velocity_z_dps = ((float)raw_value / 32768.0f) * 2000.0f;
        /* 标记本次样本中包含有效角速度字段。 */
        sample->has_angular_velocity_z = true;
        /* 返回解析成功。 */
        return GYRO_PROTOCOL_PARSE_OK;
    }

    /* 判断帧类型是否为 Z 轴航向角 Yaw。 */
    if (frame[1] == GYRO_PROTOCOL_TYPE_YAW)
    {
        /* 手册公式：偏航角 Z = 原始 short / 32768 * 180 deg。 */
        /* 保存未换算的航向角原始值，便于调试和校验。 */
        sample->raw_yaw_z = raw_value;
        /* 按手册比例把原始值换算为 deg。 */
        sample->yaw_z_deg = ((float)raw_value / 32768.0f) * 180.0f;
        /* 标记本次样本中包含有效航向角字段。 */
        sample->has_yaw_z = true;
        /* 返回解析成功。 */
        return GYRO_PROTOCOL_PARSE_OK;
    }

    /* 校验通过但类型不是当前关心的 Wz/Yaw，返回未知类型。 */
    return GYRO_PROTOCOL_PARSE_UNKNOWN_TYPE;
}

/**
 * @brief  向流式解析器输入一段 UART 字节流。
 *
 * @note   空闲中断交付的数据不保证从帧头开始。本函数逐字节查找 0x5A，
 *         找到帧头后缓存 5 字节，再调用单帧解析函数。若遇到校验错误，
 *         会丢弃当前候选帧并等待下一个 0x5A 重新同步。
 *
 * @param  parser 解析器状态。
 * @param  data   输入字节数组。
 * @param  length 输入字节数。
 * @param  sample 输出样本，可能在一次调用内更新角速度和角度两个字段。
 * @return true 表示本次至少成功解析出一个有效帧；false 表示未得到有效帧。
 */
bool GyroProtocol_FeedBytes(gyro_protocol_parser_t *parser,
    const uint8_t *data, size_t length, gyro_protocol_sample_t *sample)
{
    /* 记录本次喂入的字节流中是否至少解析出一个有效帧。 */
    bool parsed_any = false;

    /* 检查解析器、输入数据和输出样本指针，任何一个为空都无法解析。 */
    if ((parser == NULL) || (data == NULL) || (sample == NULL))
    {
        /* 参数无效时返回 false，表示没有解析出有效帧。 */
        return false;
    }

    /* 逐字节扫描输入流，因为 UART 空闲包不保证帧对齐。 */
    for (size_t i = 0U; i < length; i++)
    {
        /* 取出当前要处理的 1 个字节。 */
        uint8_t byte = data[i];

        /* index 为 0 表示当前尚未锁定帧头，正在寻找新帧开始。 */
        if (parser->index == 0U)
        {
            /* 未进入帧同步状态时，只接受 0x5A 作为候选帧第 1 字节。 */
            /* 当前字节不是帧头时，丢弃它并继续扫描后续字节。 */
            if (byte != GYRO_PROTOCOL_DATA_HEAD)
            {
                /* 跳过非帧头噪声字节。 */
                continue;
            }
        }
        /* 已经在收候选帧时，如果中途再次遇到帧头，则认为前一帧可能丢字节。 */
        else if ((byte == GYRO_PROTOCOL_DATA_HEAD) &&
                 (parser->index < GYRO_PROTOCOL_FRAME_SIZE))
        {
            /*
             * 数据流中途又出现帧头，说明前一个候选帧可能丢字节。
             * 直接从新帧头重新开始，提高噪声或半帧场景下的重同步速度。
             */
            /* 把索引归零，让当前这个 0x5A 作为新候选帧的第 1 字节。 */
            parser->index = 0U;
        }

        /* 将当前字节存入候选帧缓存的当前位置。 */
        parser->buffer[parser->index] = byte;
        /* 缓存索引后移，指向下一次要写入的位置。 */
        parser->index++;

        /* 当缓存字节数达到完整帧长度时，尝试解析这一帧。 */
        if (parser->index >= GYRO_PROTOCOL_FRAME_SIZE)
        {
            /* 保存单帧解析结果，用于判断是否得到有效数据。 */
            gyro_protocol_parse_result_t result;

            /* 对 5 字节候选帧执行帧头、校验和类型解析。 */
            result = GyroProtocol_ParseFrame(parser->buffer,
                GYRO_PROTOCOL_FRAME_SIZE, sample);
            /* 如果单帧解析成功，记录本次输入流至少得到一帧有效数据。 */
            if (result == GYRO_PROTOCOL_PARSE_OK)
            {
                /* 标记成功，函数最终会返回 true。 */
                parsed_any = true;
            }

            /* 一个候选帧处理完成后清空索引，继续从后续字节找下一帧。 */
            /* 无论成功还是失败，都丢弃当前候选帧并重新寻找下一帧。 */
            parser->index = 0U;
        }
    }

    /* 返回本次输入流是否解析到至少一个有效帧。 */
    return parsed_any;
}

/**
 * @brief  构造通用写寄存器命令。
 *
 * @note   写格式固定为 0x55 0xAA ADDR DATAL DATAH。
 *         手册里的配置写入不包含求和校验，第 4/5 字节就是小端 int16 数据。
 *
 * @param  reg_addr 寄存器地址。
 * @param  value    需要写入的 16 位值。
 * @param  out_cmd  输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildWriteCommand(uint8_t reg_addr, int16_t value, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 输出命令缓冲不能为空，否则无法写入 5 字节命令。 */
    if (out_cmd == NULL)
    {
        /* 空缓冲无法构造命令，直接返回。 */
        return;
    }

    /* 第 1 字节固定为写命令头 0x55。 */
    out_cmd[0] = GYRO_PROTOCOL_WRITE_HEAD_0;
    /* 第 2 字节固定为写命令头 0xAA。 */
    out_cmd[1] = GYRO_PROTOCOL_WRITE_HEAD_1;
    /* 第 3 字节为目标寄存器地址。 */
    out_cmd[2] = reg_addr;
    /* 第 4 字节为 16 位写入值的低 8 位。 */
    out_cmd[3] = (uint8_t)((uint16_t)value & 0xFFU);
    /* 第 5 字节为 16 位写入值的高 8 位。 */
    out_cmd[4] = (uint8_t)(((uint16_t)value >> 8U) & 0xFFU);
}

/**
 * @brief  构造解锁命令。
 *
 * @note   手册示例命令为 55 AA 13 8E 5F。
 *         写命令按 DATAL、DATAH 小端顺序发送，因此传入 0x5F8E 后输出低字节 0x8E、
 *         高字节 0x5F。
 *         命令字节为 55 AA 13 8E 5F。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildUnlockCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 KEY=0x5F8E 解锁命令，发送字节顺序为低字节 0x8E、高字节 0x5F。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_KEY, (int16_t)0x5F8EU, out_cmd);
}

/**
 * @brief  构造保存配置命令。
 *
 * @note   写 SAVE=0x0000 表示保存当前配置，常用于设置波特率或输出速率后固化参数。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildSaveCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 SAVE=0x0000 命令，用于保存当前配置。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_SAVE, 0x0000, out_cmd);
}

/**
 * @brief  构造模块重启命令。
 *
 * @note   写 SAVE=0x00FF 表示重启，适合波特率修改后需要重新启动模块的场景。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildRebootCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 SAVE=0x00FF 命令，用于让模块重启。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_SAVE, 0x00FF, out_cmd);
}

/**
 * @brief  构造恢复出厂命令。
 *
 * @note   写 SAVE=0x0001 表示恢复出厂配置，执行前应先解锁。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildRestoreFactoryCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 SAVE=0x0001 命令，用于恢复出厂配置。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_SAVE, 0x0001, out_cmd);
}

/**
 * @brief  构造 Z 轴角度归零命令。
 *
 * @note   写 Yaw_Zero=0x0000 后，模块把当前航向角作为 0 度参考。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildYawZeroCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 Yaw_Zero=0x0000 命令，用于把当前 Z 轴航向角归零。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_YAW_ZERO, 0x0000, out_cmd);
}

/**
 * @brief  构造自动获取零偏命令。
 *
 * @note   写 BIAS_CAL=0x0001 后，模块开始自动零偏获取。
 *         手册提示该过程约 20s，应用层不要立即保存或断电。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildAutoBiasCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 BIAS_CAL=0x0001 命令，用于开始自动零偏获取。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_BIAS_CAL, 0x0001, out_cmd);
}

/**
 * @brief  构造手动 Z 轴标定开始命令。
 *
 * @note   写 Scale_Factor=0x0003 后进入手动标定模式。
 *         手册要求让传感器以 Z 轴为参考旋转 360 度。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildScaleFactorStartCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 Scale_Factor=0x0003 命令，用于进入手动 Z 轴比例因子标定。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_BIAS_CAL, 0x0003, out_cmd);
}

/**
 * @brief  构造手动 Z 轴标定结束命令。
 *
 * @note   写 SAVE=0x0000 可结束并保存手动标定结果，和手册示例一致。
 *
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildScaleFactorStopCommand(uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 手动标定结束命令与保存配置命令一致，因此复用保存命令构造函数。 */
    GyroProtocol_BuildSaveCommand(out_cmd);
}

/**
 * @brief  构造读取寄存器状态命令。
 *
 * @note   手册在自动零偏示例中给出了 55 AA 04 0A 00，
 *         其中 0x04 可理解为“读取寄存器”的命令地址，DATAL 放目标寄存器地址，
 *         DATAH 固定为 0。该命令主要用于读取 BIAS_CAL 等配置状态。
 *
 * @param  reg_addr 需要读取的目标寄存器地址，例如 0x0A 表示 BIAS_CAL。
 * @param  out_cmd  输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildReadRegisterCommand(uint8_t reg_addr, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 输出命令缓冲不能为空，否则无法写入读寄存器命令。 */
    if (out_cmd == NULL)
    {
        /* 空缓冲无法构造命令，直接返回。 */
        return;
    }

    /* 第 1 字节固定为命令头 0x55。 */
    out_cmd[0] = GYRO_PROTOCOL_WRITE_HEAD_0;
    /* 第 2 字节固定为命令头 0xAA。 */
    out_cmd[1] = GYRO_PROTOCOL_WRITE_HEAD_1;
    /* 第 3 字节为 READ 命令寄存器地址 0x04。 */
    out_cmd[2] = GYRO_REG_READ;
    /* 第 4 字节放入需要读取的目标寄存器地址。 */
    out_cmd[3] = reg_addr;
    /* 第 5 字节固定为 0，符合手册读寄存器命令格式。 */
    out_cmd[4] = 0x00U;
}

/**
 * @brief  构造设置串口波特率命令。
 *
 * @note   写 BAUD 寄存器，value 使用 gyro_protocol_baud_t 枚举值。
 *         手册提示：设置前解锁，设置后延时并保存；若改动当前通信波特率，
 *         MCU 侧 UART 也必须切换到同样波特率。
 *
 * @param  baud    目标波特率枚举。
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildSetBaudCommand(gyro_protocol_baud_t baud, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 BAUD 写命令，将波特率枚举值写入模块波特率寄存器。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_BAUD, (int16_t)baud, out_cmd);
}

/**
 * @brief  构造设置输出速率命令。
 *
 * @note   写 RRATE 寄存器，value 使用 gyro_protocol_rate_t 枚举值。
 *         例如 50Hz 为 0x08，100Hz 为 0x09，1000Hz 为 0x0E。
 *
 * @param  rate    目标输出速率枚举。
 * @param  out_cmd 输出 5 字节命令缓存。
 * @return 无。
 */
void GyroProtocol_BuildSetRateCommand(gyro_protocol_rate_t rate, uint8_t out_cmd[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 构造 RRATE 写命令，将输出速率枚举值写入模块输出速率寄存器。 */
    GyroProtocol_BuildWriteCommand(GYRO_REG_RRATE, (int16_t)rate, out_cmd);
}
