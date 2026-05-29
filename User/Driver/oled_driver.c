/**
 * @file    oled_driver.c
 * @brief   SSD1306 OLED 底层驱动实现。
 *
 * @details OLED 使用 I2C1，SSD1306 地址为 0x3C。驱动提供命令、数据、清屏和
 *          6x8 ASCII 显示能力，应用层可以在低频任务中刷新状态文本。
 */

#include "oled_driver.h"

#include <stddef.h>

#include "ti_msp_dl_config.h"

/* SSD1306 7 位 I2C 地址。外部 HAL 例程中的 0x78 是左移后的 8 位地址。 */
#define OLED_DRIVER_I2C_ADDRESS         (0x3CU)

/* SSD1306 控制字节：后续 1 字节解释为命令。 */
#define OLED_DRIVER_CONTROL_COMMAND     (0x00U)

/* SSD1306 控制字节：后续字节解释为显示数据。 */
#define OLED_DRIVER_CONTROL_DATA        (0x40U)

/* I2C 有界等待计数，避免 OLED 未连接时长时间阻塞调度器。 */
#define OLED_DRIVER_I2C_TIMEOUT         (30000UL)

/* 6x8 ASCII 字库覆盖 0x20~0x7E，一共 95 个可打印字符。 */
#define OLED_DRIVER_FONT_CHAR_COUNT     (95U)

/* 128 像素宽度下最多可一次显示 21 个 6x8 字符。 */
#define OLED_DRIVER_TEXT_COLUMNS        (OLED_DRIVER_WIDTH / 6U)

/* 6x8 ASCII 字体，覆盖 0x20~0x7E。来源为常见 SSD1306 6x8 字库，按列发送。 */
static const uint8_t s_oled_font_6x8[][6] =
{
    {0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x2F,0x00,0x00},
    {0x00,0x00,0x07,0x00,0x07,0x00}, {0x00,0x14,0x7F,0x14,0x7F,0x14},
    {0x00,0x24,0x2A,0x7F,0x2A,0x12}, {0x00,0x23,0x13,0x08,0x64,0x62},
    {0x00,0x36,0x49,0x55,0x22,0x50}, {0x00,0x00,0x05,0x03,0x00,0x00},
    {0x00,0x00,0x1C,0x22,0x41,0x00}, {0x00,0x00,0x41,0x22,0x1C,0x00},
    {0x00,0x14,0x08,0x3E,0x08,0x14}, {0x00,0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x00,0x50,0x30,0x00,0x00}, {0x00,0x08,0x08,0x08,0x08,0x08},
    {0x00,0x00,0x60,0x60,0x00,0x00}, {0x00,0x20,0x10,0x08,0x04,0x02},
    {0x00,0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x00,0x42,0x7F,0x40,0x00},
    {0x00,0x42,0x61,0x51,0x49,0x46}, {0x00,0x21,0x41,0x45,0x4B,0x31},
    {0x00,0x18,0x14,0x12,0x7F,0x10}, {0x00,0x27,0x45,0x45,0x45,0x39},
    {0x00,0x3C,0x4A,0x49,0x49,0x30}, {0x00,0x01,0x71,0x09,0x05,0x03},
    {0x00,0x36,0x49,0x49,0x49,0x36}, {0x00,0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x00,0x36,0x36,0x00,0x00}, {0x00,0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41,0x00}, {0x00,0x14,0x14,0x14,0x14,0x14},
    {0x00,0x00,0x41,0x22,0x14,0x08}, {0x00,0x02,0x01,0x51,0x09,0x06},
    {0x00,0x32,0x49,0x79,0x41,0x3E}, {0x00,0x7E,0x11,0x11,0x11,0x7E},
    {0x00,0x7F,0x49,0x49,0x49,0x36}, {0x00,0x3E,0x41,0x41,0x41,0x22},
    {0x00,0x7F,0x41,0x41,0x22,0x1C}, {0x00,0x7F,0x49,0x49,0x49,0x41},
    {0x00,0x7F,0x09,0x09,0x09,0x01}, {0x00,0x3E,0x41,0x49,0x49,0x7A},
    {0x00,0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x00,0x41,0x7F,0x41,0x00},
    {0x00,0x20,0x40,0x41,0x3F,0x01}, {0x00,0x7F,0x08,0x14,0x22,0x41},
    {0x00,0x7F,0x40,0x40,0x40,0x40}, {0x00,0x7F,0x02,0x0C,0x02,0x7F},
    {0x00,0x7F,0x04,0x08,0x10,0x7F}, {0x00,0x3E,0x41,0x41,0x41,0x3E},
    {0x00,0x7F,0x09,0x09,0x09,0x06}, {0x00,0x3E,0x41,0x51,0x21,0x5E},
    {0x00,0x7F,0x09,0x19,0x29,0x46}, {0x00,0x46,0x49,0x49,0x49,0x31},
    {0x00,0x01,0x01,0x7F,0x01,0x01}, {0x00,0x3F,0x40,0x40,0x40,0x3F},
    {0x00,0x1F,0x20,0x40,0x20,0x1F}, {0x00,0x3F,0x40,0x38,0x40,0x3F},
    {0x00,0x63,0x14,0x08,0x14,0x63}, {0x00,0x07,0x08,0x70,0x08,0x07},
    {0x00,0x61,0x51,0x49,0x45,0x43}, {0x00,0x00,0x7F,0x41,0x41,0x00},
    {0x00,0x02,0x04,0x08,0x10,0x20}, {0x00,0x00,0x41,0x41,0x7F,0x00},
    {0x00,0x04,0x02,0x01,0x02,0x04}, {0x00,0x40,0x40,0x40,0x40,0x40},
    {0x00,0x00,0x01,0x02,0x04,0x00}, {0x00,0x20,0x54,0x54,0x54,0x78},
    {0x00,0x7F,0x48,0x44,0x44,0x38}, {0x00,0x38,0x44,0x44,0x44,0x20},
    {0x00,0x38,0x44,0x44,0x48,0x7F}, {0x00,0x38,0x54,0x54,0x54,0x18},
    {0x00,0x08,0x7E,0x09,0x01,0x02}, {0x00,0x0C,0x52,0x52,0x52,0x3E},
    {0x00,0x7F,0x08,0x04,0x04,0x78}, {0x00,0x00,0x44,0x7D,0x40,0x00},
    {0x00,0x20,0x40,0x44,0x3D,0x00}, {0x00,0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x00,0x41,0x7F,0x40,0x00}, {0x00,0x7C,0x04,0x18,0x04,0x78},
    {0x00,0x7C,0x08,0x04,0x04,0x78}, {0x00,0x38,0x44,0x44,0x44,0x38},
    {0x00,0x7C,0x14,0x14,0x14,0x08}, {0x00,0x08,0x14,0x14,0x18,0x7C},
    {0x00,0x7C,0x08,0x04,0x04,0x08}, {0x00,0x48,0x54,0x54,0x54,0x20},
    {0x00,0x04,0x3F,0x44,0x40,0x20}, {0x00,0x3C,0x40,0x40,0x20,0x7C},
    {0x00,0x1C,0x20,0x40,0x20,0x1C}, {0x00,0x3C,0x40,0x30,0x40,0x3C},
    {0x00,0x44,0x28,0x10,0x28,0x44}, {0x00,0x0C,0x50,0x50,0x50,0x3C},
    {0x00,0x44,0x64,0x54,0x4C,0x44}, {0x00,0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x00,0x7F,0x00,0x00}, {0x00,0x00,0x41,0x36,0x08,0x00},
    {0x00,0x10,0x08,0x08,0x10,0x08}
};

/* 编译期检查字库长度，避免 ASCII 索引与实际表项数量不一致。 */
typedef char oled_driver_font_size_check[
    ((sizeof(s_oled_font_6x8) / sizeof(s_oled_font_6x8[0])) ==
        OLED_DRIVER_FONT_CHAR_COUNT) ? 1 : -1];

/* SSD1306 初始化命令序列，适配 128x64 常见 OLED 模块。 */
static const uint8_t s_oled_init_commands[] =
{
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
    0xDB, 0x40, 0xA4, 0xA6, 0x8D, 0x14, 0xAF
};

/* true 表示最近一次 OLED I2C 访问成功；应用层可据此决定是否继续刷新。 */
static bool s_oled_driver_available = false;

static bool Oled_DriverWriteBytes(uint8_t control, const uint8_t *data, uint16_t length);

/**
 * @brief  通过 I2C1 向 OLED 写入控制字节和数据。
 *
 * @note   写入前先把控制字节和部分数据填入 TX FIFO，再启动控制器传输；
 *         传输过程中持续补 FIFO，并用 BUSY 位和 ERROR 位做有界等待。
 *
 * @param  control SSD1306 控制字节，0x00 为命令，0x40 为数据。
 * @param  data    待写入数据。
 * @param  length  数据长度，不包含 control 字节。
 * @return true 表示 I2C 传输完成且未检测到错误；false 表示参数错误或超时。
 */
static bool Oled_DriverWriteBytes(uint8_t control, const uint8_t *data, uint16_t length)
{
    /* TX FIFO 先发送控制字节，再发送 payload。 */
    uint8_t control_byte = control;
    /* 已经写入 FIFO 的 payload 字节数。 */
    uint16_t sent = 0U;
    /* 等待 I2C 空闲的保护计数。 */
    uint32_t wait_count = OLED_DRIVER_I2C_TIMEOUT;

    /* 参数检查：payload 长度非 0 时数据指针必须有效。 */
    if ((data == NULL) || (length == 0U))
    {
        /* OLED 写入不接受空 payload。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 等待上一笔 I2C 事务结束，避免重入启动控制器传输。 */
    while (((DL_I2C_getControllerStatus(I2C_OLED_INST) &
                DL_I2C_CONTROLLER_STATUS_BUSY) != 0U) &&
           (wait_count > 0UL))
    {
        /* 每轮等待消耗一个计数。 */
        wait_count--;
    }
    if (wait_count == 0UL)
    {
        /* I2C 控制器长期忙，返回失败。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 清空 TX FIFO，避免上一笔失败事务残留数据。 */
    DL_I2C_flushControllerTXFIFO(I2C_OLED_INST);
    /* 先填控制字节。 */
    (void)DL_I2C_fillControllerTXFIFO(I2C_OLED_INST, &control_byte, 1U);
    /* 再尽量填入 payload 的前若干字节。 */
    sent = DL_I2C_fillControllerTXFIFO(I2C_OLED_INST, data, length);
    /* 启动 I2C 写传输，长度为 control + payload。 */
    DL_I2C_startControllerTransfer(I2C_OLED_INST, OLED_DRIVER_I2C_ADDRESS,
        DL_I2C_CONTROLLER_DIRECTION_TX, (uint16_t)(length + 1U));

    /* 等待传输完成，同时在 FIFO 有空间时继续填剩余 payload。 */
    wait_count = OLED_DRIVER_I2C_TIMEOUT + ((uint32_t)length * 500UL);
    while (((DL_I2C_getControllerStatus(I2C_OLED_INST) &
                DL_I2C_CONTROLLER_STATUS_BUSY) != 0U) &&
           (wait_count > 0UL))
    {
        /* 只要还有数据未填入 FIFO，就持续尝试补充。 */
        if (sent < length)
        {
            /* 将剩余 payload 尽量填入控制器 TX FIFO。 */
            sent = (uint16_t)(sent + DL_I2C_fillControllerTXFIFO(I2C_OLED_INST,
                &data[sent], (uint16_t)(length - sent)));
        }

        /* 如果硬件报告错误，立即复位本次控制器传输并失败返回。 */
        if ((DL_I2C_getControllerStatus(I2C_OLED_INST) &
                DL_I2C_CONTROLLER_STATUS_ERROR) != 0U)
        {
            /* 复位控制器传输状态，便于下次尝试恢复。 */
            DL_I2C_resetControllerTransfer(I2C_OLED_INST);
            /* 返回失败，提示 OLED 可能未连接或地址不匹配。 */
            s_oled_driver_available = false;
            return false;
        }

        /* 每轮等待消耗一个计数。 */
        wait_count--;
    }

    /* 超时则复位传输状态并返回失败。 */
    if (wait_count == 0UL)
    {
        /* 复位 I2C 控制器传输寄存器，避免后续调用一直忙。 */
        DL_I2C_resetControllerTransfer(I2C_OLED_INST);
        /* 返回超时失败。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 最终检查是否存在 I2C 错误状态。 */
    if ((DL_I2C_getControllerStatus(I2C_OLED_INST) &
            DL_I2C_CONTROLLER_STATUS_ERROR) != 0U)
    {
        /* 错误状态下不认为写入成功。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 写入完成。 */
    s_oled_driver_available = true;
    return true;
}

/**
 * @brief  初始化 OLED 屏幕。
 *
 * @note   I2C1 和引脚配置由 SysConfig 完成；本函数发送 SSD1306 初始化序列并清屏。
 *
 * @param  无。
 * @return 无。
 */
void Oled_DriverInit(void)
{
    /*
     * 批量发送 SSD1306 初始化命令，减少上电时的 START/STOP 事务数量。
     * 初始化失败不阻塞系统启动，后续清屏或显示调用仍会用返回值暴露失败。
     */
    if (Oled_DriverWriteCommandBuffer(s_oled_init_commands,
        (uint16_t)sizeof(s_oled_init_commands)) == false)
    {
        /* 未接屏或地址错误时不继续清屏，避免启动阶段多次进入 I2C 超时。 */
        return;
    }

    /* 初始化完成后清空屏幕。 */
    (void)Oled_DriverClear();
}

/**
 * @brief  向 SSD1306 写一条命令。
 *
 * @param  command 命令字节。
 * @return true 表示 I2C 写入成功；false 表示失败。
 */
bool Oled_DriverWriteCommand(uint8_t command)
{
    /* 使用控制字节 0x00 发送命令。 */
    return Oled_DriverWriteBytes(OLED_DRIVER_CONTROL_COMMAND, &command, 1U);
}

/**
 * @brief  向 SSD1306 连续写入多条命令。
 *
 * @note   多条命令共用一次 I2C 事务，可减少页/列定位和初始化阶段的总线开销。
 *
 * @param  commands 命令数组。
 * @param  length   命令字节数，不包含 SSD1306 控制字节。
 * @return true 表示 I2C 写入成功；false 表示参数非法或写入失败。
 */
bool Oled_DriverWriteCommandBuffer(const uint8_t *commands, uint16_t length)
{
    /* 使用控制字节 0x00 发送连续命令。 */
    return Oled_DriverWriteBytes(OLED_DRIVER_CONTROL_COMMAND, commands, length);
}

/**
 * @brief  向 SSD1306 写一个显示数据字节。
 *
 * @param  data 显示数据字节。
 * @return true 表示 I2C 写入成功；false 表示失败。
 */
bool Oled_DriverWriteData(uint8_t data)
{
    /* 使用控制字节 0x40 发送显示数据。 */
    return Oled_DriverWriteBytes(OLED_DRIVER_CONTROL_DATA, &data, 1U);
}

/**
 * @brief  向 SSD1306 连续写入一段显示数据。
 *
 * @note   调用者需要先设置 GDDRAM 位置；本函数只负责把连续列数据一次写出。
 *
 * @param  data   显示数据数组。
 * @param  length 显示数据字节数，不包含 SSD1306 控制字节。
 * @return true 表示 I2C 写入成功；false 表示参数非法或写入失败。
 */
bool Oled_DriverWriteDataBuffer(const uint8_t *data, uint16_t length)
{
    /* 使用控制字节 0x40 发送连续显存数据。 */
    return Oled_DriverWriteBytes(OLED_DRIVER_CONTROL_DATA, data, length);
}

/**
 * @brief  清空 OLED 全屏。
 *
 * @note   逐页写 128 个 0。该函数耗时较长，只在初始化或低频刷新时调用。
 *
 * @param  无。
 * @return true 表示全部页写入成功；false 表示任一步失败。
 */
bool Oled_DriverClear(void)
{
    /* 单页 128 字节清零缓冲，静态分配避免占用栈空间。 */
    static const uint8_t clear_line[OLED_DRIVER_WIDTH] = {0};

    /* 逐页清空 8 页显存。 */
    for (uint8_t page = 0U; page < OLED_DRIVER_PAGE_COUNT; page++)
    {
        /* 设置当前页起始位置。 */
        if (Oled_DriverSetPosition(0U, page) == false)
        {
            /* 定位失败则清屏失败。 */
            return false;
        }

        /* 写入整页 0 数据。 */
        if (Oled_DriverWriteDataBuffer(clear_line, OLED_DRIVER_WIDTH) == false)
        {
            /* 当前页写失败。 */
            return false;
        }
    }

    /* 全部页面清空成功。 */
    return true;
}

/**
 * @brief  设置 OLED 当前写入位置。
 *
 * @note   x 为列坐标 0~127，page 为页坐标 0~7，每页高 8 像素。
 *
 * @param  x    列坐标。
 * @param  page 页坐标。
 * @return true 表示命令写入成功；false 表示参数非法或 I2C 失败。
 */
bool Oled_DriverSetPosition(uint8_t x, uint8_t page)
{
    /* SSD1306 页地址和列地址命令，三条命令可放在一次 I2C 事务中发送。 */
    uint8_t position_commands[3];

    /* 坐标必须落在 OLED 显示范围内。 */
    if ((x >= OLED_DRIVER_WIDTH) || (page >= OLED_DRIVER_PAGE_COUNT))
    {
        /* 非法坐标不写屏。 */
        return false;
    }

    /* 设置页地址。 */
    position_commands[0] = (uint8_t)(0xB0U + page);
    /* 设置列地址高 4 位。 */
    position_commands[1] = (uint8_t)(0x10U | ((x >> 4U) & 0x0FU));
    /* 设置列地址低 4 位。 */
    position_commands[2] = (uint8_t)(x & 0x0FU);

    /* 批量写出三条定位命令，减少每次定位的 I2C 事务数量。 */
    return Oled_DriverWriteCommandBuffer(position_commands,
        (uint16_t)sizeof(position_commands));
}

/**
 * @brief  在指定页显示一个 6x8 ASCII 字符。
 *
 * @note   仅支持 0x20~0x7E 可打印 ASCII，超出范围显示为空格。
 *
 * @param  x    列坐标。
 * @param  page 页坐标。
 * @param  ch   待显示字符。
 * @return true 表示写入成功；false 表示坐标非法或 I2C 失败。
 */
bool Oled_DriverShowChar(uint8_t x, uint8_t page, char ch)
{
    /* 保存字体索引。 */
    uint8_t index;

    /* 字符宽 6 像素，超出右边界时不显示。 */
    if ((x > (OLED_DRIVER_WIDTH - 6U)) || (page >= OLED_DRIVER_PAGE_COUNT))
    {
        /* 坐标不足以显示完整字符。 */
        return false;
    }

    /* 非可打印 ASCII 统一显示为空格。 */
    if ((ch < ' ') || (ch > '~'))
    {
        /* 空格在字库索引 0。 */
        index = 0U;
    }
    else
    {
        /* 字库从 ASCII 0x20 开始。 */
        index = (uint8_t)((uint8_t)ch - (uint8_t)' ');
    }

    /* 设置字符写入起点。 */
    if (Oled_DriverSetPosition(x, page) == false)
    {
        /* 定位失败。 */
        return false;
    }

    /* 写入 6 列字模数据。 */
    return Oled_DriverWriteDataBuffer(s_oled_font_6x8[index], 6U);
}

/**
 * @brief  在指定页显示字符串。
 *
 * @note   字符串到达右边界或遇到 '\0' 时停止，不自动换行。
 *
 * @param  x    起始列坐标。
 * @param  page 页坐标。
 * @param  text 字符串。
 * @return true 表示可显示字符均写入成功；false 表示参数非法或 I2C 失败。
 */
bool Oled_DriverShowString(uint8_t x, uint8_t page, const char *text)
{
    /* 一行最多 21 个 6x8 字符，每个字符 6 列，刚好落在 128 像素宽度内。 */
    uint8_t row_buffer[OLED_DRIVER_TEXT_COLUMNS * 6U];
    /* 已经写入 row_buffer 的显存列数。 */
    uint16_t row_length = 0U;
    /* 当前待显示字符的字库索引。 */
    uint8_t index;

    /* 字符串不能为空，页坐标必须有效。 */
    if ((text == NULL) || (page >= OLED_DRIVER_PAGE_COUNT) || (x >= OLED_DRIVER_WIDTH))
    {
        /* 参数无效。 */
        return false;
    }

    /*
     * 把多个字符先拼成连续显存列数据，再一次写出。
     * 这样一整行只需要“定位命令 + 数据写入”两次 I2C 事务。
     */
    while ((*text != '\0') &&
           ((uint16_t)x + row_length + 6U <= OLED_DRIVER_WIDTH) &&
           (row_length + 6U <= (uint16_t)sizeof(row_buffer)))
    {
        /* 非可打印 ASCII 统一显示为空格，避免字库越界。 */
        if ((*text < ' ') || (*text > '~'))
        {
            /* 空格在字库索引 0。 */
            index = 0U;
        }
        else
        {
            /* 字库从 ASCII 0x20 开始。 */
            index = (uint8_t)((uint8_t)(*text) - (uint8_t)' ');
        }

        /* 复制当前字符 6 列字模到行缓冲尾部。 */
        for (uint8_t i = 0U; i < 6U; i++)
        {
            /* row_length 始终按 6 字节递增，不会越过 row_buffer。 */
            row_buffer[row_length + i] = s_oled_font_6x8[index][i];
        }

        /* 累加已经准备好的显存列数。 */
        row_length = (uint16_t)(row_length + 6U);
        /* 指向下一个输入字符。 */
        text++;
    }

    if (row_length == 0U)
    {
        /* 空字符串或剩余空间不足时无需写屏，视为成功。 */
        return true;
    }

    /* 先设置起始写入位置。 */
    if (Oled_DriverSetPosition(x, page) == false)
    {
        /* 定位失败则不继续写数据。 */
        return false;
    }

    /* 一次写出整段字符串显存列数据。 */
    return Oled_DriverWriteDataBuffer(row_buffer, row_length);
}

/**
 * @brief  在指定页显示有符号整数。
 *
 * @note   width 表示最小显示宽度，不足时用空格补齐；宽度过大会截断到本地缓冲容量。
 *
 * @param  x     起始列坐标。
 * @param  page  页坐标。
 * @param  value 待显示整数。
 * @param  width 最小显示宽度。
 * @return true 表示写入成功；false 表示参数非法或 I2C 失败。
 */
bool Oled_DriverShowSignedNumber(uint8_t x, uint8_t page, int32_t value, uint8_t width)
{
    /* 本地字符串缓冲，足够显示 int32 加符号和结束符。 */
    char buffer[13];
    /* 从缓冲尾部开始反向写数字。 */
    uint8_t pos = (uint8_t)(sizeof(buffer) - 1U);
    /* 保存数值绝对值，使用 uint32_t 处理 INT32_MIN。 */
    uint32_t magnitude;
    /* 标记是否为负数。 */
    bool negative = (value < 0);

    /* 结束符写在缓冲最后。 */
    buffer[pos] = '\0';

    /* 将有符号数转换成绝对值，避免 INT32_MIN 直接取负溢出。 */
    if (negative == true)
    {
        /* 先加 1 再取负，最后补 1，安全得到绝对值。 */
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    }
    else
    {
        /* 非负数直接转换。 */
        magnitude = (uint32_t)value;
    }

    /* 至少输出一位数字。 */
    do
    {
        /* 预留一个位置写当前最低位。 */
        pos--;
        /* 写入当前十进制最低位。 */
        buffer[pos] = (char)('0' + (char)(magnitude % 10U));
        /* 去掉已经写出的最低位。 */
        magnitude /= 10U;
    } while ((magnitude > 0U) && (pos > 0U));

    /* 负数补符号。 */
    if ((negative == true) && (pos > 0U))
    {
        /* 写入负号。 */
        pos--;
        buffer[pos] = '-';
    }

    /* 用前导空格补到指定宽度。 */
    while ((((uint8_t)sizeof(buffer) - 1U - pos) < width) && (pos > 0U))
    {
        /* 写入一个空格。 */
        pos--;
        buffer[pos] = ' ';
    }

    /* 显示整理好的字符串。 */
    return Oled_DriverShowString(x, page, &buffer[pos]);
}

/**
 * @brief  打开 OLED 显示。
 *
 * @param  无。
 * @return true 表示命令发送成功。
 */
bool Oled_DriverDisplayOn(void)
{
    /* 打开电荷泵并开启显示输出。 */
    static const uint8_t display_on_commands[] = {0x8DU, 0x14U, 0xAFU};

    /* 批量发送显示开启命令。 */
    return Oled_DriverWriteCommandBuffer(display_on_commands,
        (uint16_t)sizeof(display_on_commands));
}

/**
 * @brief  关闭 OLED 显示。
 *
 * @param  无。
 * @return true 表示命令发送成功。
 */
bool Oled_DriverDisplayOff(void)
{
    /* 关闭电荷泵并关闭显示输出。 */
    static const uint8_t display_off_commands[] = {0x8DU, 0x10U, 0xAEU};

    /* 批量发送显示关闭命令。 */
    return Oled_DriverWriteCommandBuffer(display_off_commands,
        (uint16_t)sizeof(display_off_commands));
}

/**
 * @brief  查询最近一次 OLED I2C 通信是否成功。
 *
 * @note   该状态用于应用层退避刷新。若 OLED 未接或地址错误，底层会在失败时清除此标志。
 *
 * @param  无。
 * @return true 表示最近一次 OLED 访问成功；false 表示尚未成功或最近访问失败。
 */
bool Oled_DriverIsAvailable(void)
{
    /* 返回底层最近一次 I2C 写入结果。 */
    return s_oled_driver_available;
}
