/**
 * @file    gyro_app.c
 * @brief   陀螺仪应用层实现。
 *
 * @details 本文件把“UART1 DMA 收到的字节流”转换成“角速度/航向角数据”，
 *          并提供手册中的解锁、保存、归零、自动零偏、输出速率、波特率和标定命令。
 */

#include "gyro_app.h"

#include <string.h>

#include "scheduler.h"
#include "uart_app.h"

/* 陀螺仪 UART1 单次读取缓冲，长度和 Driver 层 RX DMA 缓冲保持一致。 */
static uint8_t s_gyro_rx_buffer[UART_DRIVER_RX_DMA_BUFFER_SIZE];

/* 陀螺仪 5 字节协议流式解析器，用于处理 DMA 空闲包跨帧的情况。 */
static gyro_protocol_parser_t s_gyro_parser;

/* 应用层保存的最新陀螺仪数据，供其它业务随时读取。 */
static gyro_app_data_t s_gyro_latest;

/* 最近一次向电脑串口输出数据的 tick，用于限制文本打印频率。 */
static uint32_t s_gyro_last_print_ms = 0U;

/* 文本输出间隔。即使陀螺仪设置到 1000Hz，也不把所有帧都转成字符串挤爆 115200 串口。 */
#define GYRO_APP_PRINT_INTERVAL_MS      (20U)

/* 手册建议配置命令之间至少延时 100ms，确保模块内部寄存器写入完成。 */
#define GYRO_APP_COMMAND_DELAY_MS       (100U)

/* 手册说明自动获取零偏大约需要 20s，这里留 1s 余量。 */
#define GYRO_APP_AUTO_BIAS_WAIT_MS      (21000U)

static void Gyro_AppDelayMs(uint32_t delay_ms);
static int32_t Gyro_AppFloatToMilli(float value);
static bool Gyro_AppSendCommandByBuilder(void (*builder)(uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE]));
static void Gyro_AppUpdateLatest(const gyro_protocol_sample_t *sample);
static void Gyro_AppPrintLatestIfDue(void);

/**
 * @brief  基于调度器 tick 的阻塞延时。
 *
 * @note   该延时只用于人工触发的配置流程，例如归零或自动零偏。
 *         它会阻塞当前主循环，因此不要在高频周期任务里反复调用这些配置函数。
 *
 * @param  delay_ms 需要等待的毫秒数。
 * @return 无。
 */
static void Gyro_AppDelayMs(uint32_t delay_ms)
{
    /* 记录进入延时函数时的系统 tick，后续用差值判断是否到期。 */
    uint32_t start_ms = Scheduler_GetTick();

    /* 持续比较当前 tick 与起始 tick 的差值，直到达到目标延时时间。 */
    while ((uint32_t)(Scheduler_GetTick() - start_ms) < delay_ms)
    {
        /* 等待 SysTick 推进时间；这里不做其它工作，保证配置命令时序简单可控。 */
    }
}

/**
 * @brief  把 float 转成 0.001 精度的定点整数。
 *
 * @note   嵌入式 printf 默认不一定开启浮点格式化，因此日志输出用整数小数形式。
 *
 * @param  value 需要转换的浮点值。
 * @return value * 1000 四舍五入后的有符号整数。
 */
static int32_t Gyro_AppFloatToMilli(float value)
{
    /* 非负数使用加 0.5 的方式实现四舍五入到整数毫单位。 */
    if (value >= 0.0f)
    {
        /* 先放大 1000 倍，再加 0.5 后截断为 int32_t。 */
        return (int32_t)((value * 1000.0f) + 0.5f);
    }

    /* 负数使用减 0.5 的方式实现对称四舍五入，避免向零截断偏差。 */
    return (int32_t)((value * 1000.0f) - 0.5f);
}

/**
 * @brief  使用协议层 builder 构造命令并通过 UART1 发送。
 *
 * @note   该函数减少重复代码，但每个公开操作仍保留独立函数和注释，
 *         方便用户按手册逐项调用。
 *
 * @param  builder 命令构造函数指针。
 * @return true 表示命令发送完成；false 表示 builder 为空或 UART 发送失败。
 */
static bool Gyro_AppSendCommandByBuilder(void (*builder)(uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE]))
{
    /* 在栈上准备 5 字节命令缓冲，builder 会把完整命令写入这里。 */
    uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE];

    /* 检查命令构造函数是否有效，避免调用空函数指针。 */
    if (builder == NULL)
    {
        /* 没有构造函数就无法生成命令，返回发送失败。 */
        return false;
    }

    /* 调用协议层构造函数，把具体命令内容填入 command 数组。 */
    builder(command);
    /* 将构造好的原始 5 字节命令通过陀螺仪 UART 发送出去。 */
    return Gyro_AppSendRawCommand(command);
}

/**
 * @brief  用本次解析出的样本更新应用层最新数据。
 *
 * @note   一个 DMA 空闲包可能只解析到角速度，也可能只解析到航向角，
 *         所以这里根据 has_xxx 标志分别更新，避免未更新字段被清零。
 *
 * @param  sample 协议层解析出的样本。
 * @return 无。
 */
static void Gyro_AppUpdateLatest(const gyro_protocol_sample_t *sample)
{
    /* 解析样本为空时无法更新最新值，直接返回。 */
    if (sample == NULL)
    {
        /* 空样本没有可用数据。 */
        return;
    }

    /* 如果本次样本包含 Z 轴角速度，就更新应用层保存的角速度字段。 */
    if (sample->has_angular_velocity_z == true)
    {
        /* 标记应用层已经获得过有效 Z 轴角速度数据。 */
        s_gyro_latest.has_angular_velocity_z = true;
        /* 保存本次换算后的 Z 轴角速度，单位 deg/s。 */
        s_gyro_latest.angular_velocity_z_dps = sample->angular_velocity_z_dps;
        /* 保存本次原始 short 角速度值，便于调试对照手册公式。 */
        s_gyro_latest.raw_angular_velocity_z = sample->raw_angular_velocity_z;
    }

    /* 如果本次样本包含 Z 轴航向角，就更新应用层保存的航向角字段。 */
    if (sample->has_yaw_z == true)
    {
        /* 标记应用层已经获得过有效 Z 轴航向角数据。 */
        s_gyro_latest.has_yaw_z = true;
        /* 保存本次换算后的 Z 轴航向角，单位 deg。 */
        s_gyro_latest.yaw_z_deg = sample->yaw_z_deg;
        /* 保存本次原始 short 航向角值，便于调试对照手册公式。 */
        s_gyro_latest.raw_yaw_z = sample->raw_yaw_z;
    }

    /* 记录最新样本更新时刻，供业务判断数据新鲜度。 */
    s_gyro_latest.last_update_ms = Scheduler_GetTick();
    /* 成功处理一次样本后累加解析包计数。 */
    s_gyro_latest.parsed_packet_count++;
}

/**
 * @brief  到达打印周期后，把最新陀螺仪数据输出到电脑串口。
 *
 * @note   输出格式为一行 CSV 风格文本，便于串口助手或上位机解析：
 *         GYRO,wz=xxx.xxx,yaw=xxx.xxx
 *
 * @param  无。
 * @return 无。
 */
static void Gyro_AppPrintLatestIfDue(void)
{
    /* 读取当前系统 tick，用来判断是否到达文本输出周期。 */
    uint32_t now_ms = Scheduler_GetTick();
    /* 保存 Z 轴角速度的毫单位整数值，避免 printf 浮点格式化。 */
    int32_t wz_milli;
    /* 保存 Z 轴航向角的毫单位整数值，避免 printf 浮点格式化。 */
    int32_t yaw_milli;
    /* 保存角速度输出符号，正数时为空字符串。 */
    const char *wz_sign = "";
    /* 保存航向角输出符号，正数时为空字符串。 */
    const char *yaw_sign = "";
    /* 保存角速度毫单位绝对值，便于拆成整数和小数部分。 */
    uint32_t wz_abs;
    /* 保存航向角毫单位绝对值，便于拆成整数和小数部分。 */
    uint32_t yaw_abs;

    /* 如果距离上次打印还没达到设定间隔，就跳过本次输出。 */
    if ((uint32_t)(now_ms - s_gyro_last_print_ms) < GYRO_APP_PRINT_INTERVAL_MS)
    {
        /* 打印节流未到期，直接返回。 */
        return;
    }

    /* 如果角速度和航向角都还没有有效数据，就没有必要输出空行。 */
    if ((s_gyro_latest.has_angular_velocity_z == false) &&
        (s_gyro_latest.has_yaw_z == false))
    {
        /* 尚无任何有效陀螺仪数据，直接返回。 */
        return;
    }

    /* 更新最近打印时间，后续输出要等到下一个周期。 */
    s_gyro_last_print_ms = now_ms;

    /* 将浮点角速度转换成毫单位整数，用于无浮点 printf 的格式化输出。 */
    wz_milli = Gyro_AppFloatToMilli(s_gyro_latest.angular_velocity_z_dps);
    /* 将浮点航向角转换成毫单位整数，用于无浮点 printf 的格式化输出。 */
    yaw_milli = Gyro_AppFloatToMilli(s_gyro_latest.yaw_z_deg);

    /* 判断角速度是否为负数，以便单独输出符号和绝对值。 */
    if (wz_milli < 0)
    {
        /* 记录负号字符串，后续格式化时拼到数值前面。 */
        wz_sign = "-";
        /* 把负数毫单位转成绝对值，便于用无符号数拆分整数和小数部分。 */
        wz_abs = (uint32_t)(-wz_milli);
    }
    else
    {
        /* 非负数不需要符号，直接把毫单位值作为绝对值使用。 */
        wz_abs = (uint32_t)wz_milli;
    }

    /* 判断航向角是否为负数，以便单独输出符号和绝对值。 */
    if (yaw_milli < 0)
    {
        /* 记录负号字符串，后续格式化时拼到数值前面。 */
        yaw_sign = "-";
        /* 把负数毫单位转成绝对值，便于用无符号数拆分整数和小数部分。 */
        yaw_abs = (uint32_t)(-yaw_milli);
    }
    else
    {
        /* 非负数不需要符号，直接把毫单位值作为绝对值使用。 */
        yaw_abs = (uint32_t)yaw_milli;
    }

    /*
     * 通过 PC 串口输出一行 CSV 风格数据，便于串口助手或上位机读取。
     * 周期任务使用非阻塞日志接口；UART0 正忙时直接丢弃本条日志，避免影响传感器解析。
     */
    (void)my_printf_try("GYRO,wz=%s%lu.%03lu,yaw=%s%lu.%03lu\r\n",
        /* 输出角速度符号字符串。 */
        wz_sign,
        /* 输出角速度整数部分，毫单位除以 1000 得到原单位整数。 */
        (unsigned long)(wz_abs / 1000UL),
        /* 输出角速度三位小数部分，毫单位对 1000 取余。 */
        (unsigned long)(wz_abs % 1000UL),
        /* 输出航向角符号字符串。 */
        yaw_sign,
        /* 输出航向角整数部分，毫单位除以 1000 得到原单位整数。 */
        (unsigned long)(yaw_abs / 1000UL),
        /* 输出航向角三位小数部分，毫单位对 1000 取余。 */
        (unsigned long)(yaw_abs % 1000UL));
}

/**
 * @brief  初始化陀螺仪应用层。
 *
 * @note   默认不主动写陀螺仪配置，避免每次上电都保存参数造成不必要的 Flash 写入。
 *         如果需要修改输出速率或归零，请在业务代码中显式调用对应函数。
 *
 * @param  无。
 * @return 无。
 */
void Gyro_AppInit(void)
{
    /* 清空应用层最新数据结构，确保上电后标志和数值都从 0 开始。 */
    memset(&s_gyro_latest, 0, sizeof(s_gyro_latest));
    /* 初始化流式协议解析器，清空半帧缓存和索引。 */
    GyroProtocol_ParserInit(&s_gyro_parser);
    /* 记录当前 tick 作为打印节流起点，避免初始化后立即连续刷屏。 */
    s_gyro_last_print_ms = Scheduler_GetTick();

    /* 输出协议初始化提示；日志失败不影响后续接收解析，所以返回值忽略。 */
    (void)my_printf("[GYRO] protocol ready: head=0x5A, frame=5 bytes, UART1=115200\r\n");
}

/**
 * @brief  陀螺仪周期任务。
 *
 * @note   任务从 UART1 读取 DMA 空闲包，交给协议层流式解析器拼帧；
 *         成功解析后更新最新数据，并按固定间隔转发到电脑串口。
 *
 * @param  无。
 * @return 无。
 */
void Gyro_AppTask(void)
{
    /* 保存本次从 UART1 读取到的数据包长度。 */
    uint16_t length;
    /* 保存协议层解析出的样本，可能同时包含角速度和航向角。 */
    gyro_protocol_sample_t sample;

    /* 从陀螺仪 UART 读取一包由空闲中断或 DMA 满缓冲切出的数据。 */
    length = Uart_AppReadGyroPacket(s_gyro_rx_buffer,
        /* 传入本地接收缓冲容量，防止驱动层复制越界。 */
        (uint16_t)sizeof(s_gyro_rx_buffer));
    /* 如果本周期没有收到完整 UART 包，就直接结束任务。 */
    if (length == 0U)
    {
        /* 无数据可解析，返回调度器。 */
        return;
    }

    /* 清空样本结构，避免本次未解析到的字段残留上一次状态。 */
    memset(&sample, 0, sizeof(sample));
    /* 将本包字节送入流式解析器，解析成功时更新最新数据并按周期打印。 */
    if (GyroProtocol_FeedBytes(&s_gyro_parser, s_gyro_rx_buffer, length, &sample) == true)
    {
        /* 把协议层输出样本合并到应用层最新数据快照中。 */
        Gyro_AppUpdateLatest(&sample);
        /* 调试输出已关闭，减少 UART0 DMA 负载 */
    }
}

/**
 * @brief  重置陀螺仪协议解析器。
 *
 * @note   当接线、波特率或模块配置改变后，如果怀疑当前流式缓存处于半帧状态，
 *         可以调用本函数重新从下一个 0x5A 帧头开始同步。
 *
 * @param  无。
 * @return 无。
 */
void Gyro_AppResetParser(void)
{
    /* 重新初始化协议解析器，丢弃当前可能存在的半帧缓存。 */
    GyroProtocol_ParserInit(&s_gyro_parser);
}

/**
 * @brief  获取陀螺仪最新数据快照。
 *
 * @note   本工程无 RTOS，float 读写在主循环同一上下文中完成；
 *         若后续在中断中读取该结构，需要再加临界区保护。
 *
 * @param  out_data 输出数据结构体。
 * @return true 表示 out_data 已填充；false 表示参数为空。
 */
bool Gyro_AppGetLatest(gyro_app_data_t *out_data)
{
    /* 调用者必须提供有效输出结构地址。 */
    if (out_data == NULL)
    {
        /* 输出地址为空时无法返回快照。 */
        return false;
    }

    /* 复制当前最新数据快照给调用者。 */
    *out_data = s_gyro_latest;
    /* 返回 true 表示输出结构已经填充完成。 */
    return true;
}

/**
 * @brief  获取最近一次解析到的 Z 轴航向角。
 *
 * @note   如果还没有解析到航向角，返回上电初始化值 0.0f。
 *
 * @param  无。
 * @return 航向角，单位 deg。
 */
float Gyro_AppGetYaw(void)
{
    /* 返回应用层保存的最新 Z 轴航向角，单位 deg。 */
    return s_gyro_latest.yaw_z_deg;
}

/**
 * @brief  获取最近一次解析到的 Z 轴角速度。
 *
 * @note   如果还没有解析到角速度，返回上电初始化值 0.0f。
 *
 * @param  无。
 * @return Z 轴角速度，单位 deg/s。
 */
float Gyro_AppGetGyroZ(void)
{
    /* 返回应用层保存的最新 Z 轴角速度，单位 deg/s。 */
    return s_gyro_latest.angular_velocity_z_dps;
}

/**
 * @brief  直接向陀螺仪发送 5 字节原始命令。
 *
 * @note   该接口用于高级用户发送手册里的任意命令。常规操作优先调用下面的封装函数，
 *         因为封装函数已经按手册补齐了解锁、延时、保存等流程。
 *
 * @param  command 5 字节命令，格式通常为 55 AA ADDR DATAL DATAH。
 * @return true 表示发送完成；false 表示参数为空或 UART 发送失败。
 */
bool Gyro_AppSendRawCommand(const uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE])
{
    /* 调用者必须提供完整 5 字节命令缓冲。 */
    if (command == NULL)
    {
        /* 命令指针为空时不能发送。 */
        return false;
    }

    /* 通过 UART1/GYRO 发送 5 字节原始命令。 */
    return Uart_AppSendToGyro(command, GYRO_PROTOCOL_COMMAND_SIZE);
}

/**
 * @brief  发送解锁命令。
 *
 * @note   手册规定所有写配置操作前都要先写 KEY=0x8E5F，
 *         实际发送字节为 55 AA 13 8E 5F。
 *
 * @param  无。
 * @return true 表示命令发送完成。
 */
bool Gyro_AppSendUnlock(void)
{
    /* 使用协议层解锁命令构造函数生成命令并发送。 */
    return Gyro_AppSendCommandByBuilder(GyroProtocol_BuildUnlockCommand);
}

/**
 * @brief  保存当前陀螺仪配置。
 *
 * @note   写 SAVE=0x0000，实际发送字节为 55 AA 00 00 00。
 *         修改输出速率、波特率、归零等参数后，需要保存才能掉电保持。
 *
 * @param  无。
 * @return true 表示命令发送完成。
 */
bool Gyro_AppSaveConfig(void)
{
    /* 使用协议层保存命令构造函数生成 SAVE=0x0000 命令并发送。 */
    return Gyro_AppSendCommandByBuilder(GyroProtocol_BuildSaveCommand);
}

/**
 * @brief  重启陀螺仪模块。
 *
 * @note   按手册流程先解锁，延时 100ms，再写 SAVE=0x00FF。
 *         该操作会让模块重新启动，重启期间 UART 输出会短暂停止。
 *
 * @param  无。
 * @return true 表示整套命令流程发送完成。
 */
bool Gyro_AppReboot(void)
{
    /* 重启属于写配置动作，必须先发送解锁命令。 */
    if (Gyro_AppSendUnlock() == false)
    {
        /* 解锁失败时停止后续流程，避免发送无效配置命令。 */
        return false;
    }

    /* 按手册要求在解锁后等待 100ms，让模块接受后续写命令。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 发送 SAVE=0x00FF 重启命令。 */
    return Gyro_AppSendCommandByBuilder(GyroProtocol_BuildRebootCommand);
}

/**
 * @brief  恢复陀螺仪出厂配置。
 *
 * @note   按手册流程先解锁，延时 100ms，再写 SAVE=0x0001。
 *         恢复后波特率、输出速率等参数会回到默认值，调用前要确认上位机配置。
 *
 * @param  无。
 * @return true 表示整套命令流程发送完成。
 */
bool Gyro_AppRestoreFactory(void)
{
    /* 恢复出厂属于写配置动作，必须先发送解锁命令。 */
    if (Gyro_AppSendUnlock() == false)
    {
        /* 解锁失败时停止流程，避免恢复命令被模块拒绝或状态不明。 */
        return false;
    }

    /* 按手册要求在解锁后等待 100ms，再发送恢复出厂命令。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 发送 SAVE=0x0001 恢复出厂命令。 */
    return Gyro_AppSendCommandByBuilder(GyroProtocol_BuildRestoreFactoryCommand);
}

/**
 * @brief  执行 Z 轴航向角归零。
 *
 * @note   手册流程：解锁 -> 延时 100ms -> 发送 Yaw_Zero=0 -> 延时 100ms -> 保存。
 *         调用时应保持模块当前朝向作为新的 0 度参考。
 *
 * @param  无。
 * @return true 表示归零命令和保存命令都发送完成。
 */
bool Gyro_AppYawZero(void)
{
    /* 航向角归零属于写配置动作，必须先发送解锁命令。 */
    if (Gyro_AppSendUnlock() == false)
    {
        /* 解锁失败时停止流程，避免模块忽略后续归零命令。 */
        return false;
    }

    /* 解锁后等待 100ms，满足手册命令间隔要求。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 发送 Yaw_Zero=0x0000 命令，把当前朝向设为 0 度。 */
    if (Gyro_AppSendCommandByBuilder(GyroProtocol_BuildYawZeroCommand) == false)
    {
        /* 归零命令发送失败时不继续保存，避免误以为配置已生效。 */
        return false;
    }

    /* 归零命令后再等待 100ms，给模块内部处理和寄存器更新留时间。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 保存归零结果，使其按模块手册流程固化。 */
    return Gyro_AppSaveConfig();
}

/**
 * @brief  执行自动零偏获取。
 *
 * @note   手册流程：解锁 -> 延时 100ms -> 发送 BIAS_CAL=1 -> 静止等待约 20s -> 保存。
 *         这 21s 内必须保持模块静止，否则零偏效果会变差；该函数会阻塞主循环。
 *
 * @param  无。
 * @return true 表示自动零偏命令和保存命令都发送完成。
 */
bool Gyro_AppAutoBiasBlocking(void)
{
    /* 自动零偏属于写配置动作，必须先发送解锁命令。 */
    if (Gyro_AppSendUnlock() == false)
    {
        /* 解锁失败时停止流程，避免后续零偏命令无效。 */
        return false;
    }

    /* 解锁后等待 100ms，满足手册命令间隔要求。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 发送自动零偏开始命令。 */
    if (Gyro_AppSendCommandByBuilder(GyroProtocol_BuildAutoBiasCommand) == false)
    {
        /* 零偏命令发送失败时不等待也不保存。 */
        return false;
    }

    /* 静止等待约 21s，让模块完成自动零偏采集。 */
    Gyro_AppDelayMs(GYRO_APP_AUTO_BIAS_WAIT_MS);
    /* 保存自动零偏结果，使标定参数掉电保持。 */
    return Gyro_AppSaveConfig();
}

/**
 * @brief  读取自动零偏/标定寄存器状态。
 *
 * @note   手册示例为 55 AA 04 0A 00，用于读取 0x0A 寄存器状态。
 *         返回帧由模块发送到 UART1，后续可根据实际返回格式继续扩展解析。
 *
 * @param  无。
 * @return true 表示读状态命令发送完成。
 */
bool Gyro_AppReadBiasStatus(void)
{
    /* 准备 5 字节读寄存器命令缓冲。 */
    uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE];

    /* 构造读取 0x0A 自动零偏/标定寄存器状态的命令。 */
    GyroProtocol_BuildReadRegisterCommand(0x0AU, command);
    /* 通过陀螺仪 UART 发送读状态命令。 */
    return Gyro_AppSendRawCommand(command);
}

/**
 * @brief  设置陀螺仪串口输出速率。
 *
 * @note   手册流程：解锁 -> 延时 100ms -> 写 RRATE -> 可选延时 100ms 保存。
 *         例如 50Hz=0x08，100Hz=0x09，1000Hz=0x0E。
 *
 * @param  rate           输出速率枚举。
 * @param  save_after_set true 表示设置后立即保存，false 表示只临时写入。
 * @return true 表示命令流程发送完成。
 */
bool Gyro_AppSetOutputRate(gyro_protocol_rate_t rate, bool save_after_set)
{
    /* 准备 5 字节设置输出速率命令缓冲。 */
    uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE];

    /* 修改输出速率属于写配置动作，必须先解锁。 */
    if (Gyro_AppSendUnlock() == false)
    {
        /* 解锁失败时停止流程，避免设置命令无效。 */
        return false;
    }

    /* 解锁后等待 100ms，满足手册命令间隔要求。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 按目标速率枚举值构造 RRATE 写命令。 */
    GyroProtocol_BuildSetRateCommand(rate, command);
    /* 发送设置输出速率命令。 */
    if (Gyro_AppSendRawCommand(command) == false)
    {
        /* 设置命令发送失败时直接返回失败。 */
        return false;
    }

    /* 如果调用者要求掉电保持，就继续执行保存流程。 */
    if (save_after_set == true)
    {
        /* 设置命令后等待 100ms，让模块完成寄存器写入。 */
        Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
        /* 保存当前配置，使输出速率掉电保持。 */
        return Gyro_AppSaveConfig();
    }

    /* 不保存时只表示本次临时设置命令已经发送完成。 */
    return true;
}

/**
 * @brief  设置陀螺仪串口波特率。
 *
 * @note   手册流程：解锁 -> 延时 100ms -> 写 BAUD -> 可选保存。
 *         如果设置为非 115200，MCU 侧 UART1 也必须同步修改 SysConfig 波特率，
 *         否则保存重启后会因为两端波特率不一致而收不到数据。
 *
 * @param  baud           目标波特率枚举。
 * @param  save_after_set true 表示设置后立即保存，false 表示只临时写入。
 * @return true 表示命令流程发送完成。
 */
bool Gyro_AppSetBaud(gyro_protocol_baud_t baud, bool save_after_set)
{
    /* 准备 5 字节设置波特率命令缓冲。 */
    uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE];

    /* 修改波特率属于写配置动作，必须先解锁。 */
    if (Gyro_AppSendUnlock() == false)
    {
        /* 解锁失败时停止流程，避免波特率写命令无效。 */
        return false;
    }

    /* 解锁后等待 100ms，满足手册命令间隔要求。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 按目标波特率枚举值构造 BAUD 写命令。 */
    GyroProtocol_BuildSetBaudCommand(baud, command);
    /* 发送设置波特率命令。 */
    if (Gyro_AppSendRawCommand(command) == false)
    {
        /* 设置命令发送失败时直接返回失败。 */
        return false;
    }

    /* 如果调用者要求掉电保持，就继续执行保存流程。 */
    if (save_after_set == true)
    {
        /* 设置命令后等待 100ms，让模块完成寄存器写入。 */
        Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
        /* 保存当前配置，使波特率掉电保持。 */
        return Gyro_AppSaveConfig();
    }

    /* 不保存时只表示本次临时设置命令已经发送完成。 */
    return true;
}

/**
 * @brief  开始 Z 轴比例因子手动标定。
 *
 * @note   手册流程：解锁 -> 延时 100ms -> 写 Scale_Factor=0x0003。
 *         发送后需要人工让传感器绕 Z 轴准确旋转 360 度，再调用 Gyro_AppStopScaleFactor()。
 *
 * @param  无。
 * @return true 表示开始标定命令发送完成。
 */
bool Gyro_AppStartScaleFactor(void)
{
    /* 手动比例因子标定属于写配置动作，必须先解锁。 */
    if (Gyro_AppSendUnlock() == false)
    {
        /* 解锁失败时停止流程，避免开始标定命令无效。 */
        return false;
    }

    /* 解锁后等待 100ms，满足手册命令间隔要求。 */
    Gyro_AppDelayMs(GYRO_APP_COMMAND_DELAY_MS);
    /* 发送 Scale_Factor=0x0003 命令，让模块进入手动 Z 轴标定流程。 */
    return Gyro_AppSendCommandByBuilder(GyroProtocol_BuildScaleFactorStartCommand);
}

/**
 * @brief  结束并保存 Z 轴比例因子手动标定。
 *
 * @note   手册示例中“结束标定”发送 SAVE=0x0000，因此这里直接调用保存配置。
 *
 * @param  无。
 * @return true 表示保存命令发送完成。
 */
bool Gyro_AppStopScaleFactor(void)
{
    /* 手动标定结束后通过保存配置命令固化比例因子结果。 */
    return Gyro_AppSaveConfig();
}
