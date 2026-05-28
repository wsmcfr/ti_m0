/**
 * @file    oled_app.c
 * @brief   OLED 应用层实现。
 *
 * @details 本模块低频刷新 OLED，展示按键、灰度循迹和电机状态摘要。
 *          高频采样和控制不依赖 OLED，避免 I2C 显示阻塞主业务。
 */

#include "oled_app.h"

#include "key_app.h"
#include "line_track_app.h"
#include "motor_app.h"
#include "oled_driver.h"

/* OLED 正常刷新间隔。显示状态不需要高频刷新，降低 I2C 占用。 */
#define OLED_APP_REFRESH_INTERVAL_TICKS     (10U)

/* 低频刷新分片计数，每次任务只刷一行，避免单次任务写太多 I2C 字节。 */
static uint8_t s_oled_refresh_slice = 0U;

/* true 表示 OLED 初始化流程已经执行。 */
static bool s_oled_ready = false;

/* 非零时下一次任务会从第 0 行开始完整刷新。 */
static bool s_oled_force_refresh = true;

/* 用任务调用次数做低频分频，避免 OLED App 直接依赖 tick。 */
static uint8_t s_oled_refresh_divider = 0U;

static void Oled_AppClearLine(uint8_t page);
static void Oled_AppShowHexByte(uint8_t x, uint8_t page, uint8_t value);
static void Oled_AppRefreshLine0(void);
static void Oled_AppRefreshLine1(void);
static void Oled_AppRefreshLine2(void);
static void Oled_AppRefreshLine3(void);

/**
 * @brief  清空 OLED 指定页的一整行字符区域。
 *
 * @note   SSD1306 每页高度 8 像素，本函数用空格覆盖 21 个 6x8 字符。
 *
 * @param  page 页坐标。
 * @return 无。
 */
static void Oled_AppClearLine(uint8_t page)
{
    /* 用空格覆盖一行，防止旧字符残留。 */
    (void)Oled_DriverShowString(0U, page, "                     ");
}

/**
 * @brief  以两位十六进制显示 1 字节数值。
 *
 * @note   用于显示按键和灰度 bit mask，避免引入 sprintf。
 *
 * @param  x     起始列坐标。
 * @param  page  页坐标。
 * @param  value 待显示字节。
 * @return 无。
 */
static void Oled_AppShowHexByte(uint8_t x, uint8_t page, uint8_t value)
{
    /* 十六进制字符表。 */
    static const char hex[] = "0123456789ABCDEF";
    /* 准备 2 位十六进制字符串。 */
    char text[3];

    /* 高 4 位字符。 */
    text[0] = hex[(value >> 4U) & 0x0FU];
    /* 低 4 位字符。 */
    text[1] = hex[value & 0x0FU];
    /* 字符串结束符。 */
    text[2] = '\0';
    /* 显示两位十六进制字符串。 */
    (void)Oled_DriverShowString(x, page, text);
}

/**
 * @brief  刷新第 0 行：标题和按键状态。
 *
 * @param  无。
 * @return 无。
 */
static void Oled_AppRefreshLine0(void)
{
    /* 读取当前稳定按键掩码。 */
    uint8_t key_mask = Key_AppGetStableMask();

    /* 先清空本行，避免短字符串覆盖不完全。 */
    Oled_AppClearLine(0U);
    /* 显示固定标题。 */
    (void)Oled_DriverShowString(0U, 0U, "MSPM0 K=");
    /* 显示按键 bit mask。 */
    Oled_AppShowHexByte(48U, 0U, key_mask);
}

/**
 * @brief  刷新第 1 行：灰度状态和位置误差。
 *
 * @param  无。
 * @return 无。
 */
static void Oled_AppRefreshLine1(void)
{
    /* 保存灰度循迹快照。 */
    line_track_snapshot_t line_snapshot;

    /* 清空本行。 */
    Oled_AppClearLine(1U);
    /* 尝试读取灰度快照。 */
    if (LineTrack_AppGetSnapshot(&line_snapshot) == true)
    {
        /* 显示灰度 bit mask。 */
        (void)Oled_DriverShowString(0U, 1U, "GRAY=");
        Oled_AppShowHexByte(30U, 1U, line_snapshot.bit_mask);
        /* 显示位置误差。 */
        (void)Oled_DriverShowString(48U, 1U, "E=");
        (void)Oled_DriverShowSignedNumber(60U, 1U, line_snapshot.position_error, 5U);
    }
    else
    {
        /* 尚无灰度数据时显示等待状态。 */
        (void)Oled_DriverShowString(0U, 1U, "GRAY=WAIT");
    }
}

/**
 * @brief  刷新第 2 行：四路目标速度前两路。
 *
 * @param  无。
 * @return 无。
 */
static void Oled_AppRefreshLine2(void)
{
    /* 保存电机状态。 */
    motor_app_status_t motor_status;

    /* 清空本行。 */
    Oled_AppClearLine(2U);
    /* 读取电机状态快照。 */
    if (Motor_AppGetStatus(&motor_status) == true)
    {
        /* 显示 A/B 两路目标速度。 */
        (void)Oled_DriverShowString(0U, 2U, "VA=");
        (void)Oled_DriverShowSignedNumber(18U, 2U, motor_status.desired_speed[0], 5U);
        (void)Oled_DriverShowString(54U, 2U, "VB=");
        (void)Oled_DriverShowSignedNumber(72U, 2U, motor_status.desired_speed[1], 5U);
    }
}

/**
 * @brief  刷新第 3 行：四路目标速度后两路。
 *
 * @param  无。
 * @return 无。
 */
static void Oled_AppRefreshLine3(void)
{
    /* 保存电机状态。 */
    motor_app_status_t motor_status;

    /* 清空本行。 */
    Oled_AppClearLine(3U);
    /* 读取电机状态快照。 */
    if (Motor_AppGetStatus(&motor_status) == true)
    {
        /* 显示 C/D 两路目标速度。 */
        (void)Oled_DriverShowString(0U, 3U, "VC=");
        (void)Oled_DriverShowSignedNumber(18U, 3U, motor_status.desired_speed[2], 5U);
        (void)Oled_DriverShowString(54U, 3U, "VD=");
        (void)Oled_DriverShowSignedNumber(72U, 3U, motor_status.desired_speed[3], 5U);
    }
}

/**
 * @brief  初始化 OLED 应用层。
 *
 * @note   初始化驱动并清屏；如果硬件未连接，底层 I2C 会超时返回，但系统仍继续运行。
 *
 * @param  无。
 * @return 无。
 */
void Oled_AppInit(void)
{
    /* 执行 SSD1306 初始化序列。 */
    Oled_DriverInit();
    /* 标记 OLED 初始化入口已经执行。 */
    s_oled_ready = true;
    /* 下一次任务从第 0 行开始刷新。 */
    s_oled_force_refresh = true;
    /* 清空分片状态。 */
    s_oled_refresh_slice = 0U;
    /* 清空刷新分频计数。 */
    s_oled_refresh_divider = 0U;
}

/**
 * @brief  OLED 周期刷新任务。
 *
 * @note   每达到分频间隔只刷新一行，四次任务完成一轮完整状态刷新。
 *
 * @param  无。
 * @return 无。
 */
void Oled_AppTask(void)
{
    /* 未初始化时不访问 OLED。 */
    if (s_oled_ready == false)
    {
        /* OLED 不可用。 */
        return;
    }

    /* 非强制刷新时使用分频，降低 I2C 刷新速率。 */
    if (s_oled_force_refresh == false)
    {
        /* 累加分频计数。 */
        s_oled_refresh_divider++;
        /* 未达到刷新间隔时直接返回。 */
        if (s_oled_refresh_divider < OLED_APP_REFRESH_INTERVAL_TICKS)
        {
            /* 本次不刷新。 */
            return;
        }
    }

    /* 消耗本次刷新分频。 */
    s_oled_refresh_divider = 0U;
    /* 强制刷新只影响启动第一个分片，进入后恢复普通分频。 */
    s_oled_force_refresh = false;

    /* 根据分片编号刷新一行。 */
    switch (s_oled_refresh_slice)
    {
        case 0U:
            /* 刷新标题和按键状态。 */
            Oled_AppRefreshLine0();
            break;

        case 1U:
            /* 刷新灰度循迹状态。 */
            Oled_AppRefreshLine1();
            break;

        case 2U:
            /* 刷新 A/B 电机速度。 */
            Oled_AppRefreshLine2();
            break;

        case 3U:
        default:
            /* 刷新 C/D 电机速度。 */
            Oled_AppRefreshLine3();
            break;
    }

    /* 移动到下一行分片。 */
    s_oled_refresh_slice++;
    /* 四行刷新完成后回到第 0 行。 */
    if (s_oled_refresh_slice >= 4U)
    {
        /* 重置分片编号。 */
        s_oled_refresh_slice = 0U;
    }
}

/**
 * @brief  请求 OLED 尽快刷新。
 *
 * @note   调用后下一次 Oled_AppTask() 会跳过分频等待，从当前分片继续刷新。
 *
 * @param  无。
 * @return 无。
 */
void Oled_AppForceRefresh(void)
{
    /* 设置强制刷新标志。 */
    s_oled_force_refresh = true;
}

/**
 * @brief  查询 OLED 应用层是否已经初始化。
 *
 * @param  无。
 * @return true 表示初始化入口已经执行。
 */
bool Oled_AppIsReady(void)
{
    /* 返回 OLED 应用层初始化状态。 */
    return s_oled_ready;
}
