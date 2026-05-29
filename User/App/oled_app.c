/**
 * @file    oled_app.c
 * @brief   OLED 应用层实现。
 *
 * @details 本模块低频刷新 OLED，展示按键、灰度循迹和电机状态摘要。
 *          高频采样和控制不依赖 OLED，避免 I2C 显示阻塞主业务。
 */

#include "oled_app.h"

#include <stddef.h>
#include <string.h>

#include "key_app.h"
#include "line_track_app.h"
#include "motor_app.h"
#include "oled_driver.h"

/* OLED 正常刷新间隔。显示状态不需要高频刷新，降低 I2C 占用。 */
#define OLED_APP_REFRESH_INTERVAL_TICKS     (10U)

/* OLED 连续失败后的退避任务次数，避免未接屏时每轮都进入较长 I2C 超时。 */
#define OLED_APP_ERROR_BACKOFF_TICKS        (50U)

/* 低频刷新分片计数，每次任务只刷一行，避免单次任务写太多 I2C 字节。 */
static uint8_t s_oled_refresh_slice = 0U;

/* true 表示 OLED 初始化流程已经执行。 */
static bool s_oled_ready = false;

/* 非零时下一次任务会从第 0 行开始完整刷新。 */
static bool s_oled_force_refresh = true;

/* 用任务调用次数做低频分频，避免 OLED App 直接依赖 tick。 */
static uint8_t s_oled_refresh_divider = 0U;

/* OLED 写失败后的退避计数，非 0 时周期任务直接跳过 I2C 访问。 */
static uint8_t s_oled_error_backoff = 0U;

static void Oled_AppBuildDisplayState(uint8_t line_index, oled_app_display_state_t *out_state);
static void Oled_AppFillLine(char *out_text);
static bool Oled_AppAppendChar(char *out_text, size_t *cursor, char ch);
static bool Oled_AppAppendText(char *out_text, size_t *cursor, const char *text);
static bool Oled_AppAppendHexByte(char *out_text, size_t *cursor, uint8_t value);
static bool Oled_AppAppendSignedNumber(char *out_text, size_t *cursor,
    int32_t value, uint8_t width);
static bool Oled_AppRefreshLine(uint8_t line_index);

/**
 * @brief  采集指定 OLED 行显示所需的跨模块状态。
 *
 * @note   只读取当前行真正需要的模块状态，避免第 0 行刷新时还去读取电机或灰度快照。
 *
 * @param  line_index 行号，0~3。
 * @param  out_state 输出状态快照，调用者必须传入有效指针。
 * @return 无。
 */
static void Oled_AppBuildDisplayState(uint8_t line_index, oled_app_display_state_t *out_state)
{
    /* 空指针无法写入快照，直接返回避免访问无效地址。 */
    if (out_state == NULL)
    {
        /* 调用者传参错误。 */
        return;
    }

    /* 先清零，确保无效快照的内容不会被格式化函数误用。 */
    memset(out_state, 0, sizeof(*out_state));
    switch (line_index)
    {
        case 0U:
            /* 第 0 行只需要当前稳定按键掩码。 */
            out_state->key_mask = Key_AppGetStableMask();
            break;

        case 1U:
            /* 第 1 行只需要灰度循迹快照。 */
            out_state->has_line_snapshot =
                LineTrack_AppGetSnapshot(&out_state->line_snapshot);
            break;

        case 2U:
        case 3U:
            /* 第 2/3 行只需要电机状态快照。 */
            out_state->has_motor_status = Motor_AppGetStatus(&out_state->motor_status);
            break;

        default:
            /* 非法行号不会被格式化写屏，这里保持空快照即可。 */
            break;
    }
}

/**
 * @brief  把输出行初始化为固定宽度空格字符串。
 *
 * @note   每次刷新都写满固定 21 个字符，旧内容自然被覆盖，无需单独清行。
 *
 * @param  out_text 输出缓冲区，长度至少为 OLED_APP_TEXT_COLUMNS + 1。
 * @return 无。
 */
static void Oled_AppFillLine(char *out_text)
{
    /* 用空格填满整行，使短内容也能覆盖上一轮残留字符。 */
    memset(out_text, ' ', OLED_APP_TEXT_COLUMNS);
    /* 末尾补 C 字符串结束符，供 Oled_DriverShowString() 遍历。 */
    out_text[OLED_APP_TEXT_COLUMNS] = '\0';
}

/**
 * @brief  向固定宽度行缓冲追加 1 个字符。
 *
 * @param  out_text 输出行缓冲区。
 * @param  cursor   当前写入列，成功写入后自增。
 * @param  ch       待追加字符。
 * @return true 表示字符已写入；false 表示参数无效或行已满。
 */
static bool Oled_AppAppendChar(char *out_text, size_t *cursor, char ch)
{
    /* 指针参数必须有效，且 cursor 不能越过固定行宽。 */
    if ((out_text == NULL) || (cursor == NULL) || (*cursor >= OLED_APP_TEXT_COLUMNS))
    {
        /* 无法继续追加。 */
        return false;
    }

    /* 写入当前字符。 */
    out_text[*cursor] = ch;
    /* 移动到下一列字符位置。 */
    *cursor = *cursor + 1U;
    /* 追加成功。 */
    return true;
}

/**
 * @brief  向固定宽度行缓冲追加字符串。
 *
 * @param  out_text 输出行缓冲区。
 * @param  cursor   当前写入列，成功写入后前移。
 * @param  text     待追加字符串。
 * @return true 表示字符串已完整写入；false 表示参数无效或空间不足。
 */
static bool Oled_AppAppendText(char *out_text, size_t *cursor, const char *text)
{
    /* 字符串指针必须有效。 */
    if (text == NULL)
    {
        /* 无字符串可追加。 */
        return false;
    }

    /* 逐字符追加，遇到行尾会返回 false，提醒调用者当前格式超宽。 */
    while (*text != '\0')
    {
        if (Oled_AppAppendChar(out_text, cursor, *text) == false)
        {
            /* 行宽不足，追加失败。 */
            return false;
        }
        /* 继续处理下一个输入字符。 */
        text++;
    }

    /* 字符串完整追加成功。 */
    return true;
}

/**
 * @brief  向固定宽度行缓冲追加两位十六进制字节。
 *
 * @param  out_text 输出行缓冲区。
 * @param  cursor   当前写入列，成功写入后前移 2 列。
 * @param  value    待格式化字节。
 * @return true 表示写入成功；false 表示参数无效或空间不足。
 */
static bool Oled_AppAppendHexByte(char *out_text, size_t *cursor, uint8_t value)
{
    /* 十六进制字符表。 */
    static const char hex[] = "0123456789ABCDEF";

    /* 先追加高 4 位，再追加低 4 位。 */
    if (Oled_AppAppendChar(out_text, cursor, hex[(value >> 4U) & 0x0FU]) == false)
    {
        /* 行宽不足。 */
        return false;
    }
    /* 追加低 4 位并返回结果。 */
    return Oled_AppAppendChar(out_text, cursor, hex[value & 0x0FU]);
}

/**
 * @brief  向固定宽度行缓冲追加定宽有符号整数。
 *
 * @note   不使用 sprintf，避免在 MCU 端引入较重的格式化依赖。
 *
 * @param  out_text 输出行缓冲区。
 * @param  cursor   当前写入列，成功写入后前移。
 * @param  value    待格式化整数。
 * @param  width    最小显示宽度，不足时左侧补空格。
 * @return true 表示写入成功；false 表示参数无效或空间不足。
 */
static bool Oled_AppAppendSignedNumber(char *out_text, size_t *cursor,
    int32_t value, uint8_t width)
{
    /* 临时数字字符串，足够容纳 int32 符号、10 位数字和结束符。 */
    char digits[12];
    /* 本地定宽输出缓冲，最大宽度按 digits 容量限制。 */
    char padded[12];
    /* 从 digits 尾部开始反向写十进制数字。 */
    size_t pos = sizeof(digits) - 1U;
    /* 追加前缀空格后的总长度。 */
    size_t digit_length;
    /* 保存绝对值，使用 uint32_t 兼容 INT32_MIN。 */
    uint32_t magnitude;
    /* 记录数值是否为负数。 */
    bool negative = (value < 0);

    /* 结束符写在数字缓冲末尾。 */
    digits[pos] = '\0';

    /* 安全计算绝对值，避免 INT32_MIN 直接取负溢出。 */
    if (negative == true)
    {
        /* 先加 1 再取负，最后补 1，避免最小负数溢出。 */
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    }
    else
    {
        /* 非负数直接转换为无符号幅值。 */
        magnitude = (uint32_t)value;
    }

    /* 至少输出一位数字。 */
    do
    {
        /* 预留一位写入当前最低位。 */
        pos--;
        /* 写入当前最低位十进制字符。 */
        digits[pos] = (char)('0' + (char)(magnitude % 10U));
        /* 丢弃已输出的最低位。 */
        magnitude /= 10U;
    } while ((magnitude > 0U) && (pos > 0U));

    /* 负数补符号。 */
    if ((negative == true) && (pos > 0U))
    {
        /* 写入负号。 */
        pos--;
        digits[pos] = '-';
    }

    /* 计算原始数字串长度。 */
    digit_length = (sizeof(digits) - 1U) - pos;
    /* 本地缓冲最多保留 11 个可见字符，过大宽度会被截断到安全范围。 */
    if (width >= sizeof(padded))
    {
        /* 限制宽度，避免写穿本地定宽缓冲。 */
        width = (uint8_t)(sizeof(padded) - 1U);
    }

    /* 当数字本身超过 width 时，按数字实际长度显示。 */
    if (digit_length >= width)
    {
        /* 直接追加数字字符串。 */
        return Oled_AppAppendText(out_text, cursor, &digits[pos]);
    }

    /* 先填充前导空格。 */
    memset(padded, ' ', width);
    /* 再把数字字符串复制到右侧，实现右对齐。 */
    memcpy(&padded[(size_t)width - digit_length], &digits[pos], digit_length);
    /* 追加 C 字符串结束符。 */
    padded[width] = '\0';

    /* 追加定宽数字。 */
    return Oled_AppAppendText(out_text, cursor, padded);
}

/**
 * @brief  把显示状态格式化成固定宽度的一行文本。
 *
 * @note   输出始终为 21 个可见字符加结束符，调用者可以直接整行写屏。
 *
 * @param  line_index 行号，0~3 分别对应标题/灰度/电机 AB/电机 CD。
 * @param  state      显示状态快照。
 * @param  out_text   输出文本缓冲。
 * @param  out_size   输出缓冲大小，必须至少为 OLED_APP_TEXT_COLUMNS + 1。
 * @return true 表示格式化成功；false 表示参数非法或格式超宽。
 */
bool Oled_AppFormatLine(uint8_t line_index, const oled_app_display_state_t *state,
    char *out_text, size_t out_size)
{
    /* 当前写入字符列。 */
    size_t cursor = 0U;
    /* 记录格式化过程是否成功。 */
    bool ok = true;

    /* 检查参数和缓冲大小，避免写穿调用者缓冲。 */
    if ((state == NULL) || (out_text == NULL) ||
        (out_size < (OLED_APP_TEXT_COLUMNS + 1U)) || (line_index >= 4U))
    {
        /* 参数无效。 */
        return false;
    }

    /* 先用空格填满整行，后续只覆盖需要显示的前缀内容。 */
    Oled_AppFillLine(out_text);

    switch (line_index)
    {
        case 0U:
            /* 第 0 行显示固定标题和当前按键掩码。 */
            ok = Oled_AppAppendText(out_text, &cursor, "MSPM0 K=");
            ok = (ok && Oled_AppAppendHexByte(out_text, &cursor, state->key_mask));
            break;

        case 1U:
            if (state->has_line_snapshot == true)
            {
                /* 第 1 行显示灰度 bit mask 和位置误差。 */
                ok = Oled_AppAppendText(out_text, &cursor, "GRAY=");
                ok = (ok && Oled_AppAppendHexByte(out_text, &cursor,
                    state->line_snapshot.bit_mask));
                ok = (ok && Oled_AppAppendText(out_text, &cursor, " E="));
                ok = (ok && Oled_AppAppendSignedNumber(out_text, &cursor,
                    state->line_snapshot.position_error, 5U));
            }
            else
            {
                /* 灰度快照无效时显示等待状态。 */
                ok = Oled_AppAppendText(out_text, &cursor, "GRAY=WAIT");
            }
            break;

        case 2U:
            if (state->has_motor_status == true)
            {
                /* 第 2 行显示 A/B 两路目标速度。 */
                ok = Oled_AppAppendText(out_text, &cursor, "VA=");
                ok = (ok && Oled_AppAppendSignedNumber(out_text, &cursor,
                    state->motor_status.desired_speed[0], 5U));
                ok = (ok && Oled_AppAppendText(out_text, &cursor, " VB="));
                ok = (ok && Oled_AppAppendSignedNumber(out_text, &cursor,
                    state->motor_status.desired_speed[1], 5U));
            }
            break;

        case 3U:
        default:
            if (state->has_motor_status == true)
            {
                /* 第 3 行显示 C/D 两路目标速度。 */
                ok = Oled_AppAppendText(out_text, &cursor, "VC=");
                ok = (ok && Oled_AppAppendSignedNumber(out_text, &cursor,
                    state->motor_status.desired_speed[2], 5U));
                ok = (ok && Oled_AppAppendText(out_text, &cursor, " VD="));
                ok = (ok && Oled_AppAppendSignedNumber(out_text, &cursor,
                    state->motor_status.desired_speed[3], 5U));
            }
            break;
    }

    /* 返回格式化结果，调用者据此决定是否写屏。 */
    return ok;
}

/**
 * @brief  刷新指定 OLED 行。
 *
 * @note   本函数先构造固定宽度行文本，再调用底层一次性写出整行字符串。
 *
 * @param  line_index 行号，0~3。
 * @return true 表示本次行刷新成功或无需刷新；false 表示底层 I2C 写失败。
 */
static bool Oled_AppRefreshLine(uint8_t line_index)
{
    /* 保存一次刷新所需的跨模块状态快照。 */
    oled_app_display_state_t state;
    /* 固定宽度行文本，多 1 字节放字符串结束符。 */
    char line_text[OLED_APP_TEXT_COLUMNS + 1U];

    /* 采集当前行需要的显示状态。 */
    Oled_AppBuildDisplayState(line_index, &state);
    /* 格式化成功后整行写屏；失败时跳过本次刷新，避免写出半行内容。 */
    if (Oled_AppFormatLine(line_index, &state, line_text, sizeof(line_text)) == true)
    {
        /* 以 6x8 字符一次写出整行文本，减少 I2C 事务。 */
        return Oled_DriverShowString(0U, line_index, line_text);
    }

    /* 格式化失败时没有访问 I2C，总线状态不受影响。 */
    return true;
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
    /* 只有最近一次 I2C 访问成功时才认为 OLED 可周期刷新。 */
    s_oled_ready = Oled_DriverIsAvailable();
    /* 下一次任务从第 0 行开始刷新。 */
    s_oled_force_refresh = true;
    /* 清空分片状态。 */
    s_oled_refresh_slice = 0U;
    /* 清空刷新分频计数。 */
    s_oled_refresh_divider = 0U;
    /*
     * 初始化失败时进入退避。这样未接 OLED 的车不会在每个刷新周期反复等待 I2C 超时；
     * 后续周期仍会按退避间隔重试初始化，以支持运行中接回屏幕。
     */
    s_oled_error_backoff = (s_oled_ready == true) ? 0U : OLED_APP_ERROR_BACKOFF_TICKS;
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
    /* 记录本次分片刷新是否成功，用于失败退避。 */
    bool refresh_ok = true;

    /*
     * OLED 初始化失败或刷新失败后进入退避。
     * 退避计数用任务调用次数递减，归零后重试一次初始化，避免未接屏时持续拖慢主循环。
     */
    if (s_oled_error_backoff > 0U)
    {
        /* 本次任务消耗一个退避计数。 */
        s_oled_error_backoff--;
        /* 未到重试时机时直接返回，不访问 I2C。 */
        if (s_oled_error_backoff > 0U)
        {
            /* 仍处于退避窗口。 */
            return;
        }

        /* 退避结束后重试初始化，支持运行中接回 OLED。 */
        Oled_DriverInit();
        /* 根据底层最近一次 I2C 结果更新可用状态。 */
        s_oled_ready = Oled_DriverIsAvailable();
        if (s_oled_ready == false)
        {
            /* 重试仍失败，重新进入退避。 */
            s_oled_error_backoff = OLED_APP_ERROR_BACKOFF_TICKS;
            /* 本次不再继续刷新。 */
            return;
        }

        /* 重试成功后从第 0 行开始刷新，并跳过普通分频。 */
        s_oled_force_refresh = true;
        /* 从标题行开始恢复显示内容。 */
        s_oled_refresh_slice = 0U;
        /* 清空普通刷新分频计数。 */
        s_oled_refresh_divider = 0U;
    }

    /* 未初始化成功时不访问 OLED。 */
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
            refresh_ok = Oled_AppRefreshLine(0U);
            break;

        case 1U:
            /* 刷新灰度循迹状态。 */
            refresh_ok = Oled_AppRefreshLine(1U);
            break;

        case 2U:
            /* 刷新 A/B 电机速度。 */
            refresh_ok = Oled_AppRefreshLine(2U);
            break;

        case 3U:
        default:
            /* 刷新 C/D 电机速度。 */
            refresh_ok = Oled_AppRefreshLine(3U);
            break;
    }

    /* I2C 写失败时进入退避，避免下一轮继续长时间超时。 */
    if (refresh_ok == false)
    {
        /* 标记 OLED 当前不可用。 */
        s_oled_ready = false;
        /* 设置失败退避窗口。 */
        s_oled_error_backoff = OLED_APP_ERROR_BACKOFF_TICKS;
        /* 下次恢复时从当前行继续或由初始化重置到第 0 行。 */
        s_oled_force_refresh = true;
        /* 本次失败后不推进分片，保留当前行等待后续重试。 */
        return;
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
