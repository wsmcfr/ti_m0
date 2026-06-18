/**
 * @file    motor_app.c
 * @brief   电机驱动板应用层实现。
 *
 * @details 本文件把纯协议层构造出的 Modbus RTU 帧发送到 UART3，并缓存编码器返回值。
 *          串口驱动只负责收发字节，协议含义和业务状态都保持在 App 层。
 */

#include "motor_app.h"

#include <string.h>

#include "key_app.h"
#include "gyro_app.h"
#include "scheduler.h"
#include "uart_app.h"

/* 电机 UART3 单次读取缓冲，长度和 Driver 层 RX DMA 缓冲保持一致。 */
static uint8_t s_motor_rx_buffer[UART_DRIVER_RX_DMA_BUFFER_SIZE];

/* 应用层保存的电机最新状态。 */
static motor_app_status_t s_motor_status;

/* 记录最近一次请求读取的编码器索引，用于单寄存器响应映射到对应电机。 */
static uint8_t s_motor_last_encoder_request = 0U;

/*
 * 各路电机的方向系数：+1 表示正速度 = 前进，-1 表示正速度 = 后退（接线反向）。
 * 实测：电机 A（左轮）正速度为后退，因此系数取 -1；其余三路待确认后按需修改。
 * 所有通过 Motor_AppSetSpeeds / Motor_AppSetSpeed 等接口下发的速度都乘以此系数，
 * 使上层代码始终用"正值 = 该轮前进"的语义控制电机，屏蔽硬件接线差异。
 */
static const int16_t s_motor_direction[MOTOR_PROTOCOL_MOTOR_COUNT] = { -1, 1, 1, 1 };

/* 点动识别模式最近一次已经下发的目标通道，用于避免相同状态下重复发送速度帧。 */
static uint8_t s_motor_identify_active_motor = MOTOR_APP_IDENTIFY_NO_MOTOR;

/* ========== K3 直线行驶状态机 ========== */

/* 直线行驶基础速度，正值表示前进方向。 */
#define MOTOR_APP_STRAIGHT_BASE_SPEED   (40)

/* 直线行驶持续时间，单位 ms。 */
#define MOTOR_APP_STRAIGHT_DURATION_MS  (10000UL)

/* 陀螺仪航向校正 P 系数：偏 1° 修正 2 个速度单位。 */
#define MOTOR_APP_STRAIGHT_KP           (2.0f)

/* 直线行驶运行状态：false = 空闲，true = 正在行驶。 */
static bool s_straight_running = false;

/* 直线行驶起始系统 tick，用于超时判断。 */
static uint32_t s_straight_start_tick = 0U;

/* 直线行驶起始 yaw 角度，用于计算偏差。 */
static float s_straight_start_yaw = 0.0f;

static bool Motor_AppSendFrame(const uint8_t *frame, size_t length);
static bool Motor_AppSetSpeedSnapshot(const int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT]);
static void Motor_AppHandleRxPacket(const uint8_t *data, uint16_t length);
static uint8_t Motor_AppGetSinglePressedKeyIndex(uint8_t key_mask);
static void Motor_AppRunIdentifyJog(void);

/**
 * @brief  发送一帧电机 Modbus RTU 数据。
 *
 * @note   本函数统一检查帧长度和 UART 发送结果，发送成功后更新统计。
 *
 * @param  frame  待发送帧缓冲。
 * @param  length 待发送帧长度。
 * @return true 表示 UART3 发送完成；false 表示参数无效或发送失败。
 */
static bool Motor_AppSendFrame(const uint8_t *frame, size_t length)
{
    /* 电机命令必须有有效数据且不能超过 UART App 的 uint16_t 长度参数。 */
    if ((frame == NULL) || (length == 0U) || (length > 0xFFFFU))
    {
        /* 参数无效时不访问 UART。 */
        return false;
    }

    /* 通过 UART3/MOTOR 发送完整 Modbus 帧。 */
    if (Uart_AppSendToMotor(frame, (uint16_t)length) == false)
    {
        /* UART 发送失败时不更新成功统计。 */
        return false;
    }

    /* 成功发送后累加命令帧统计。 */
    s_motor_status.tx_count++;
    /* 返回成功给上层。 */
    return true;
}

/**
 * @brief  按完整四路速度快照发送速度命令并更新缓存。
 *
 * @note   电机驱动板协议要求从 0x0000 连续写 4 个速度寄存器。单路和两路便捷接口
 *         也必须整理成四路快照后发送，避免只写部分寄存器造成驱动板状态不一致。
 *
 * @param  speeds 四路目标速度快照，按 A/B/C/D 顺序排列。
 * @return true 表示速度帧发送完成；false 表示参数错误、构帧失败或 UART 发送失败。
 */
static bool Motor_AppSetSpeedSnapshot(const int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT])
{
    /* 准备速度命令帧缓冲。 */
    uint8_t frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
    /* 保存构造出的帧长度。 */
    size_t length;

    /* 速度快照不能为空，否则无法构造四路速度帧。 */
    if (speeds == NULL)
    {
        /* 参数错误时不发送命令，也不改内部缓存。 */
        return false;
    }

    /* 构造四路速度写入帧，底层协议仍保持一次写 4 个寄存器。 */
    length = MotorProtocol_BuildSpeedFrame(speeds, frame, sizeof(frame));
    if (Motor_AppSendFrame(frame, length) == false)
    {
        /* 发送失败时不更新目标速度快照，避免显示状态和实际命令不一致。 */
        return false;
    }

    /* 发送成功后缓存目标速度，供 OLED 或其它模块显示。 */
    memcpy(s_motor_status.desired_speed, speeds, sizeof(s_motor_status.desired_speed));
    /* 返回成功。 */
    return true;
}

/**
 * @brief  处理电机串口收到的一包数据。
 *
 * @note   当前重点解析读保持寄存器响应。写寄存器响应通常是回显命令帧，
 *         只做 CRC 校验统计，不额外更新业务状态。
 *
 * @param  data   UART3 空闲切包后的数据。
 * @param  length 数据长度。
 * @return 无。
 */
static void Motor_AppHandleRxPacket(const uint8_t *data, uint16_t length)
{
    /* 保存编码器解析结果，当前读取流程通常只返回 1 个寄存器。 */
    int16_t encoder[MOTOR_PROTOCOL_MOTOR_COUNT] = {0};
    /* 保存协议层实际解析出的寄存器数量。 */
    size_t parsed_count;

    /* 空数据没有可解析内容。 */
    if ((data == NULL) || (length == 0U))
    {
        /* 参数无效时直接返回。 */
        return;
    }

    /* 先尝试按读保持寄存器响应解析编码器数据。 */
    parsed_count = MotorProtocol_ParseEncoderResponse(data, length,
        encoder, MOTOR_PROTOCOL_MOTOR_COUNT);
    if (parsed_count > 0U)
    {
        /*
         * 如果只解析到 1 个寄存器，就认为它对应最近一次 Motor_AppRequestEncoder()
         * 请求的电机；如果解析到多寄存器，则按返回顺序更新前 N 路。
         */
        if (parsed_count == 1U)
        {
            /* 单寄存器响应映射到最近请求的编码器索引。 */
            s_motor_status.encoder[s_motor_last_encoder_request] = encoder[0];
            /* 标记该路编码器数据已有效。 */
            s_motor_status.encoder_valid[s_motor_last_encoder_request] = true;
        }
        else
        {
            /* 多寄存器响应按顺序更新前 parsed_count 路。 */
            for (size_t i = 0U; i < parsed_count; i++)
            {
                /* 保存对应电机编码器值。 */
                s_motor_status.encoder[i] = encoder[i];
                /* 标记该路编码器数据已有效。 */
                s_motor_status.encoder_valid[i] = true;
            }
        }

        /* 记录成功解析返回帧数量。 */
        s_motor_status.rx_count++;
        /* 记录最近一次成功接收时间。 */
        s_motor_status.last_rx_ms = Scheduler_GetTick();
        /* 编码器响应处理完成。 */
        return;
    }

    /* 非编码器帧也必须 CRC 正确，否则记录协议错误次数。 */
    if (MotorProtocol_CheckFrameCrc(data, length) == false)
    {
        /* CRC 或格式错误时累加错误统计，便于联调接线和波特率问题。 */
        s_motor_status.crc_error_count++;
    }
}

/**
 * @brief  从按键掩码中提取唯一按下的按键索引。
 *
 * @note   电机通道识别模式要求一次只按一个键。无键或多键都返回
 *         MOTOR_APP_IDENTIFY_NO_MOTOR，让上层进入全停状态，避免误判通道。
 *
 * @param  key_mask bit0~bit3 对应 K1~K4 的稳定按下状态。
 * @return 0~3 表示唯一按下的按键索引；0xFF 表示无键或多键。
 */
static uint8_t Motor_AppGetSinglePressedKeyIndex(uint8_t key_mask)
{
    /* 只保留当前工程实际使用的 K1~K4 四个 bit。 */
    uint8_t masked = (uint8_t)(key_mask & 0x0FU);
    /* 保存唯一按键的候选索引。 */
    uint8_t selected = MOTOR_APP_IDENTIFY_NO_MOTOR;

    /* 无键按下时没有需要点动的目标通道。 */
    if (masked == 0U)
    {
        /* 返回无目标通道，让调用者保持或发送停机命令。 */
        return MOTOR_APP_IDENTIFY_NO_MOTOR;
    }

    /* 扫描 K1~K4，寻找是否恰好只有一个 bit 被置位。 */
    for (uint8_t i = 0U; i < MOTOR_PROTOCOL_MOTOR_COUNT; i++)
    {
        /* 当前按键对应的 bit。 */
        uint8_t bit = (uint8_t)(1U << i);

        /* 当前 bit 未按下时跳过。 */
        if ((masked & bit) == 0U)
        {
            /* 继续检查下一个按键。 */
            continue;
        }

        /* 如果此前已经找到过一个按键，说明当前是多键按下。 */
        if (selected != MOTOR_APP_IDENTIFY_NO_MOTOR)
        {
            /* 多键状态不安全，返回无目标通道，由上层全停。 */
            return MOTOR_APP_IDENTIFY_NO_MOTOR;
        }

        /* 记录唯一按下按键的索引，后续还会继续扫描确认没有第二个键。 */
        selected = i;
    }

    /* 返回唯一按下按键的索引；如果没有找到则为无目标通道。 */
    return selected;
}

/**
 * @brief  运行按键点动式电机通道识别逻辑。
 *
 * @note   K1~K4 分别映射电机 A~D。只有单键按下时点动对应通道；
 *         松开或多键按下时发送四路全停。函数只在目标通道变化时发送速度帧，
 *         降低 UART3 重复命令占用。
 *
 * @param  无。
 * @return 无。
 */
static void Motor_AppRunIdentifyJog(void)
{
    /* 读取按键应用层确认后的稳定按下状态，避免直接响应抖动原始 GPIO。 */
    uint8_t key_mask = Key_AppGetStableMask();
    /* 用静态变量记录上一次按键状态，自行检测按下边沿。
     * 初始化为 0xFF 而非 0x00，确保第一次调用不产生虚假上升沿（防止上电瞬态触发 K3）。 */
    static uint8_t s_last_key_mask = 0xFFU;
    uint8_t key_pressed_edge = (uint8_t)(key_mask & (uint8_t)(~s_last_key_mask));
    s_last_key_mask = key_mask;
    /* 将单键按下状态转换为电机通道索引。 */
    uint8_t selected_motor = Motor_AppGetSinglePressedKeyIndex(key_mask);
    /* 准备四路速度快照，默认全停。 */
    int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT] = {0};

    /*
     * ======== K3 直线行驶状态机 ========
     * K3 按下边沿切换：IDLE → RUNNING，RUNNING → IDLE（急停）。
     * RUNNING 期间用陀螺仪 yaw 整数 P 校正，10 秒超时自动停止。
     */
    if ((key_pressed_edge & KEY_DRIVER_MASK_K1) != 0U)
    {
        if (s_straight_running == false)
        {
            /* 启动直线行驶 */
            s_straight_running = true;
            s_straight_start_tick = Scheduler_GetTick();
            s_straight_start_yaw = Gyro_AppGetYaw();
        }
        else
        {
            /* 急停 */
            s_straight_running = false;
            (void)Motor_AppSetSpeed4(0, 0, 0, 0);
        }
        /* K3 事件已处理，不再往下走点动逻辑 */
        s_motor_status.identify_key_mask = (uint8_t)(key_mask & 0x0FU);
        s_motor_status.identify_selected_motor = selected_motor;
        return;
    }

    /* 直线行驶中：每 50ms 更新一次速度命令，其它按键不响应 */
    if (s_straight_running == true)
    {
        uint32_t elapsed = (uint32_t)(Scheduler_GetTick() - s_straight_start_tick);
        if (elapsed >= MOTOR_APP_STRAIGHT_DURATION_MS)
        {
            /* 10 秒到，停止 */
            s_straight_running = false;
            (void)Motor_AppSetSpeed4(0, 0, 0, 0);
        }
        else if ((elapsed % 50U) < 5U)
        {
            /* 每 50ms 窗口内只发送一次速度命令，减轻 UART3 负载 */
            int16_t yaw_now = (int16_t)(Gyro_AppGetYaw() * 10.0f);
            int16_t yaw_start = (int16_t)(s_straight_start_yaw * 10.0f);
            int16_t yaw_error_x10 = (int16_t)(yaw_now - yaw_start);
            int16_t correction = (int16_t)(yaw_error_x10 / 5);

            int16_t left_speed  = (int16_t)(-(MOTOR_APP_STRAIGHT_BASE_SPEED + correction));
            int16_t right_speed = (int16_t)(MOTOR_APP_STRAIGHT_BASE_SPEED - correction);

            (void)Motor_AppSetSpeed4(left_speed, right_speed, 0, 0);
        }
        s_motor_status.identify_key_mask = (uint8_t)(key_mask & 0x0FU);
        s_motor_status.identify_selected_motor = selected_motor;
        return;
    }

    /* ======== 空闲时 K1/K2 点动逻辑（原有）======== */

    /*
     * 先更新诊断快照。即使后续状态未变化、不需要重复发送，OLED 也能看到
     * 当前按键实际读数和点动选择结果，便于区分 GPIO/消抖问题和发送问题。
     */
    s_motor_status.identify_key_mask = (uint8_t)(key_mask & 0x0FU);
    s_motor_status.identify_selected_motor = selected_motor;

    /* 如果目标通道没有变化，就不重复发送同样的速度命令。
     * 只有 K1(A)/K2(B) 参与点动判断，K3/K4 被直线行驶逻辑使用，不进入此分支。 */
    if ((selected_motor != 0U) && (selected_motor != 1U) &&
        (selected_motor != MOTOR_APP_IDENTIFY_NO_MOTOR))
    {
        /* K3/K4 对应的通道 2/3 不参与点动，直接忽略 */
        return;
    }
    if (selected_motor == s_motor_identify_active_motor)
    {
        /* 点动状态未变化，直接返回。 */
        return;
    }

    /* 单键按下时，只给对应通道低速点动。K1 已分配给直线行驶，这里只处理 K2。 */
    if (selected_motor < MOTOR_PROTOCOL_MOTOR_COUNT)
    {
        if (selected_motor == 1U)
        {
            /* 右轮：正方向即前进 */
            speeds[1] = MOTOR_APP_IDENTIFY_JOG_SPEED;
        }
    }

    /* 发送四路速度快照；发送成功后才更新当前点动状态。 */
    s_motor_status.identify_last_send_ok = Motor_AppSetSpeedSnapshot(speeds);
    if (s_motor_status.identify_last_send_ok == true)
    {
        /* 记录当前已经下发的点动目标，避免下一周期重复发送。 */
        s_motor_identify_active_motor = selected_motor;
    }
}

/**
 * @brief  初始化电机应用层。
 *
 * @note   初始化时会尝试写一次闭环使能，便于后续按键点动识别直接发送速度命令。
 *         这里不会写入非零速度，因此上电后仍保持电机静止。
 *
 * @param  无。
 * @return 无。
 */
void Motor_AppInit(void)
{
    /* 清空电机状态，保证上电后速度、编码器有效标志和统计从 0 开始。 */
    memset(&s_motor_status, 0, sizeof(s_motor_status));
    /* 默认最近请求索引为 0，后续请求编码器时会更新。 */
    s_motor_last_encoder_request = 0U;
    /* 初始化点动识别状态为无目标通道，确保上电默认不让任何电机动作。 */
    s_motor_identify_active_motor = MOTOR_APP_IDENTIFY_NO_MOTOR;
    /* 初始化点动诊断为无键、无通道、未发送，供 OLED 上电后显示确定状态。 */
    s_motor_status.identify_key_mask = 0U;
    s_motor_status.identify_selected_motor = MOTOR_APP_IDENTIFY_NO_MOTOR;
    s_motor_status.identify_last_send_ok = false;
    /* 输出一次初始化提示，日志失败不影响电机串口后续使用。 */
    (void)my_printf("[MOTOR] protocol ready: slave=0x%02X, UART3=115200\r\n",
        (unsigned int)MOTOR_PROTOCOL_SLAVE_ADDR);
    /*
     * 点动识别需要电机板处于闭环速度模式。
     * 这里尝试发送一次闭环使能；失败只影响电机响应，不阻塞系统其它任务。
     */
    (void)Motor_AppEnableClosedLoop();
}

/**
 * @brief  电机应用层周期任务。
 *
 * @note   任务只读取并解析 UART3 已切出的返回包，不主动发送速度命令，
 *         因此不会在调度器里产生高频阻塞发送。
 *
 * @param  无。
 * @return 无。
 */
void Motor_AppTask(void)
{
    /* 保存本次从 UART3 读取到的数据长度。 */
    uint16_t length;

    /* 先处理按键点动识别逻辑，让用户可以逐路确认 A/B/C/D 通道。 */
    Motor_AppRunIdentifyJog();

    /* 读取一包由 UART 空闲中断或 DMA 满缓冲切出的电机返回数据。 */
    length = Uart_AppReadMotorPacket(s_motor_rx_buffer,
        (uint16_t)sizeof(s_motor_rx_buffer));
    if (length == 0U)
    {
        /* 没有新数据时直接返回，保持任务非阻塞。 */
        return;
    }

    /* 将收到的数据交给电机协议处理函数。 */
    Motor_AppHandleRxPacket(s_motor_rx_buffer, length);
}

/**
 * @brief  发送进入闭环控制命令。
 *
 * @note   对应参考例程 Motor_Set_ClosedLoop()，写寄存器 0x0008 = 1。
 *
 * @param  无。
 * @return true 表示命令帧发送完成。
 */
bool Motor_AppEnableClosedLoop(void)
{
    /* 准备闭环命令帧缓冲。 */
    uint8_t frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
    /* 保存构造出的帧长度。 */
    size_t length = MotorProtocol_BuildClosedLoopFrame(frame, sizeof(frame));

    /* 发送构造好的闭环命令帧。 */
    return Motor_AppSendFrame(frame, length);
}

/**
 * @brief  设置四路电机速度。
 *
 * @note   对应参考例程 Motor_Set_Speeds()，从寄存器 0x0000 连续写 4 个 int16。
 *
 * @param  speeds 四路速度数组。
 * @return true 表示速度命令发送完成；false 表示参数错误或发送失败。
 */
bool Motor_AppSetSpeeds(const int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT])
{
    /* 复用快照发送函数，保持四路数组接口和便捷接口的发送/缓存语义完全一致。 */
    return Motor_AppSetSpeedSnapshot(speeds);
}

/**
 * @brief  以四个参数形式设置四路电机速度。
 *
 * @note   这是 Motor_AppSetSpeeds() 的便捷包装，方便业务代码直接传 A/B/C/D 四路值。
 *
 * @param  motor_a A 路速度。
 * @param  motor_b B 路速度。
 * @param  motor_c C 路速度。
 * @param  motor_d D 路速度。
 * @return true 表示速度命令发送完成。
 */
bool Motor_AppSetSpeed4(int16_t motor_a, int16_t motor_b, int16_t motor_c, int16_t motor_d)
{
    /* 将四个独立参数整理为协议层需要的数组。 */
    const int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT] =
    {
        motor_a, motor_b, motor_c, motor_d
    };

    /* 调用数组版本完成实际构帧和发送。 */
    return Motor_AppSetSpeeds(speeds);
}

/**
 * @brief  设置指定一路电机速度。
 *
 * @note   motor_index 为 0~3，分别对应 A/B/C/D。函数会先复制当前目标速度缓存，
 *         只修改指定一路，再按驱动板要求发送完整四路速度帧。
 *
 * @param  motor_index 电机索引，0~3 有效。
 * @param  speed       目标速度，正负方向由电机驱动板协议定义。
 * @return true 表示速度命令发送完成；false 表示索引非法或发送失败。
 */
bool Motor_AppSetSpeed(uint8_t motor_index, int16_t speed)
{
    /* 保存将要发送的四路速度快照。 */
    int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT];

    /* 索引必须落在 A/B/C/D 四路范围内。 */
    if (motor_index >= MOTOR_PROTOCOL_MOTOR_COUNT)
    {
        /* 非法索引不发送命令，避免误改未知电机寄存器。 */
        return false;
    }

    /* 从当前缓存复制四路速度，保证单路修改不会清掉其它路目标速度。 */
    memcpy(speeds, s_motor_status.desired_speed, sizeof(speeds));
    /* 只覆盖调用方指定的电机速度。 */
    speeds[motor_index] = speed;

    /* 发送完整四路速度快照，并在成功后更新缓存。 */
    return Motor_AppSetSpeedSnapshot(speeds);
}

/**
 * @brief  一次设置当前使用的两路电机速度。
 *
 * @note   该接口面向只接两路电机的场景。未指定的两路会主动写 0，避免上一次
 *         四路速度缓存残留导致未使用电机端口继续输出。
 *
 * @param  first_motor_index  第一台电机索引，0~3 有效。
 * @param  first_speed        第一台电机目标速度。
 * @param  second_motor_index 第二台电机索引，0~3 有效，且不能与第一台相同。
 * @param  second_speed       第二台电机目标速度。
 * @return true 表示速度命令发送完成；false 表示索引非法、重复或发送失败。
 */
bool Motor_AppSetSpeed2(uint8_t first_motor_index, int16_t first_speed,
    uint8_t second_motor_index, int16_t second_speed)
{
    /* 两路使用模式下，未指定电机必须明确置 0。 */
    int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT] = {0};

    /* 两个电机索引都必须在 A/B/C/D 范围内。 */
    if ((first_motor_index >= MOTOR_PROTOCOL_MOTOR_COUNT) ||
        (second_motor_index >= MOTOR_PROTOCOL_MOTOR_COUNT))
    {
        /* 非法索引不发送命令。 */
        return false;
    }

    /* 同一路电机不能在两路接口中被赋两个速度，否则调用意图不明确。 */
    if (first_motor_index == second_motor_index)
    {
        /* 重复索引不发送命令，让调用方显式修正参数。 */
        return false;
    }

    /* 写入第一路使用中的电机速度。 */
    speeds[first_motor_index] = first_speed;
    /* 写入第二路使用中的电机速度。 */
    speeds[second_motor_index] = second_speed;

    /* 发送完整四路速度快照，未指定电机为 0。 */
    return Motor_AppSetSpeedSnapshot(speeds);
}

/**
 * @brief  停止指定一路电机。
 *
 * @note   本函数等价于 Motor_AppSetSpeed(motor_index, 0)，但用明确命名表达
 *         “停止某一路”的业务意图，便于主控逻辑调用和阅读。
 *
 * @param  motor_index 电机索引，0~3 有效。
 * @return true 表示停止命令发送完成；false 表示索引非法或发送失败。
 */
bool Motor_AppStop(uint8_t motor_index)
{
    /* 通过单路速度接口把目标速度置 0，保持缓存和发送语义一致。 */
    return Motor_AppSetSpeed(motor_index, 0);
}

/**
 * @brief  设置四路电机 PID 参数。
 *
 * @note   对应参考例程 Motor_Set_KP_KI_KD()，从寄存器 0x0015 连续写 12 个寄存器。
 *
 * @param  pid 四路 PID 参数数组。
 * @return true 表示 PID 命令发送完成。
 */
bool Motor_AppSetPid(const motor_protocol_pid_t pid[MOTOR_PROTOCOL_MOTOR_COUNT])
{
    /* 准备 PID 命令帧缓冲。 */
    uint8_t frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
    /* 保存构造出的帧长度。 */
    size_t length = MotorProtocol_BuildPidFrame(pid, frame, sizeof(frame));

    /* 发送 PID 配置帧。 */
    return Motor_AppSendFrame(frame, length);
}

/**
 * @brief  请求读取指定电机编码器。
 *
 * @note   motor_index 为 0~3，分别对应参考例程中的 Enc1_A/B/C/D 选择寄存器。
 *         驱动板返回帧到达后由 Motor_AppTask() 解析并写入状态缓存。
 *
 * @param  motor_index 电机索引，0~3 有效。
 * @return true 表示读取请求命令发送完成。
 */
bool Motor_AppRequestEncoder(uint8_t motor_index)
{
    /* 准备编码器选择命令帧缓冲。 */
    uint8_t frame[MOTOR_PROTOCOL_MAX_FRAME_SIZE];
    /* 保存构造出的帧长度。 */
    size_t length;

    /* 索引必须在四路电机范围内。 */
    if (motor_index >= MOTOR_PROTOCOL_MOTOR_COUNT)
    {
        /* 非法索引不发送命令。 */
        return false;
    }

    /* 构造对应编码器选择寄存器写入帧。 */
    length = MotorProtocol_BuildEncoderSelectFrame(motor_index, frame, sizeof(frame));
    if (Motor_AppSendFrame(frame, length) == false)
    {
        /* 发送失败时不更新最近请求索引。 */
        return false;
    }

    /* 记录最近请求的编码器索引，便于单寄存器响应映射。 */
    s_motor_last_encoder_request = motor_index;
    /* 返回请求发送成功。 */
    return true;
}

/**
 * @brief  获取电机应用层状态快照。
 *
 * @note   输出的是内部状态副本，调用者修改不会影响电机应用层缓存。
 *
 * @param  out_status 输出状态结构。
 * @return true 表示输出成功；false 表示参数为空。
 */
bool Motor_AppGetStatus(motor_app_status_t *out_status)
{
    /* 调用者必须提供输出结构地址。 */
    if (out_status == NULL)
    {
        /* 空指针无法写出状态。 */
        return false;
    }

    /* 拷贝内部状态给调用者。 */
    *out_status = s_motor_status;
    /* 返回成功。 */
    return true;
}
