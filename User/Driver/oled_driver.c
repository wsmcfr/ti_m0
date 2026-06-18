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

/*
 * I2C 有界等待计数，避免 OLED 未连接时长时间阻塞调度器。
 * 400kHz I2C 下 2 字节传输约 40μs，32MHz 对应约 1280 cycles。
 * 留 4 倍余量取 5000，可覆盖正常传输且单次最坏阻塞约 0.6ms。
 * 原值 30000 × 23 条初始化命令 × 2 次等待 ≈ 276ms，会拖死 100ms LED 任务。
 */
#define OLED_DRIVER_I2C_TIMEOUT         (5000UL)

/* OLED 上电等待周期，参考 STM32 HAL 可用驱动中 OLED_Init() 先 HAL_Delay(200)。 */
#define OLED_DRIVER_POWER_ON_DELAY_CYCLES   (6400000UL)

/* I2C controller 空闲状态掩码，DriverLib 官方示例用该位判断可启动下一笔传输。 */
#define OLED_DRIVER_I2C_STATUS_IDLE     (DL_I2C_CONTROLLER_STATUS_IDLE)

/*
 * I2C controller 传输中状态掩码。
 * 只检查 BUSY（控制器自身传输进行中），不 OR BUSY_BUS。
 * BUSY_BUS 反映的是总线电平，复位后 OLED 电容放电期间 SDA 可能被拉低使该位
 * 长时间置位，若将其加入等待条件会导致 wait_count 全部耗尽后才退出，拖死调度器。
 */
#define OLED_DRIVER_I2C_STATUS_ACTIVE   (DL_I2C_CONTROLLER_STATUS_BUSY)

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

/* true 表示 I2C controller 已经按 OLED 所需参数完成一次性补偿初始化。 */
static bool s_oled_i2c_controller_ready = false;

static void Oled_DriverI2cBusRecovery(void);
static void Oled_DriverEnsureI2cControllerReady(void);
static bool Oled_DriverWriteByte(uint8_t control, uint8_t value);

/**
 * @brief  I2C 总线 9-clock 恢复。
 *
 * @note   MCU 复位时若 OLED 传输未完成，OLED 从机（SSD1306）会把 SDA 拉低等待后续时钟，
 *         导致总线卡死。后续所有 I2C 命令（包括 0xD3,0x00 显示偏移归零）都无法送达，
 *         OLED 保留上次的显示偏移 → 内容显示在第 3 行，且任务长时间超时 → LED 饿死。
 *
 *         恢复步骤：
 *           1. 把 SCL(PA29) 临时切成 GPIO 输出，SDA(PA30) 切成 GPIO 输入
 *           2. 拉动 SCL 最多 9 次，让从机把剩余数据位移出并释放 SDA
 *           3. 手动产生 STOP（SCL 高时 SDA 上升沿）
 *           4. 把引脚切回 I2C 外设功能，复位并重新使能 I2C1 控制器
 *
 * @param  无。
 * @return 无。
 */
static void Oled_DriverI2cBusRecovery(void)
{
    /* 1. 把 SCL(PA29/PINCM4) 切换为 GPIO 数字输出 */
    DL_GPIO_initDigitalOutput(GPIO_I2C_OLED_IOMUX_SCL);
    /* DL_GPIO_initDigitalOutput 只配置 IOMUX，还需要单独使能 GPIO 方向寄存器才能驱动引脚。 */
    DL_GPIO_enableOutput(GPIO_I2C_OLED_SCL_PORT, GPIO_I2C_OLED_SCL_PIN);
    /* 把 SDA(PA30/PINCM5) 切换为 GPIO 输入（保留上拉，用于读取从机释放状态）*/
    DL_GPIO_initDigitalInputFeatures(GPIO_I2C_OLED_IOMUX_SDA,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    /* 2. SCL 先拉高，让总线进入已知高电平状态 */
    DL_GPIO_setPins(GPIO_I2C_OLED_SCL_PORT, GPIO_I2C_OLED_SCL_PIN);
    DL_Common_delayCycles(32000U); /* 1ms */

    /* 3. 拉动 SCL 最多 9 次，直到 SDA 被从机释放（读到高电平）*/
    for (uint8_t i = 0U; i < 9U; i++)
    {
        DL_GPIO_clearPins(GPIO_I2C_OLED_SCL_PORT, GPIO_I2C_OLED_SCL_PIN); /* SCL 低 */
        DL_Common_delayCycles(32000U); /* 1ms */
        DL_GPIO_setPins(GPIO_I2C_OLED_SCL_PORT, GPIO_I2C_OLED_SCL_PIN);   /* SCL 高 */
        DL_Common_delayCycles(32000U); /* 1ms */
        if (DL_GPIO_readPins(GPIO_I2C_OLED_SDA_PORT, GPIO_I2C_OLED_SDA_PIN) != 0U)
        {
            /* SDA 已经释放，从机不再占用总线 */
            break;
        }
    }

    /* 4. 手动产生 STOP 条件：把 SDA 切为输出，SDA低 → SCL高 → SDA高 */
    DL_GPIO_initDigitalOutput(GPIO_I2C_OLED_IOMUX_SDA);
    /* 同样需要使能 SDA 的 GPIO 方向寄存器。 */
    DL_GPIO_enableOutput(GPIO_I2C_OLED_SDA_PORT, GPIO_I2C_OLED_SDA_PIN);
    DL_GPIO_clearPins(GPIO_I2C_OLED_SDA_PORT, GPIO_I2C_OLED_SDA_PIN); /* SDA 低 */
    DL_Common_delayCycles(32000U);
    DL_GPIO_setPins(GPIO_I2C_OLED_SCL_PORT, GPIO_I2C_OLED_SCL_PIN);   /* SCL 高 */
    DL_Common_delayCycles(32000U);
    DL_GPIO_setPins(GPIO_I2C_OLED_SDA_PORT, GPIO_I2C_OLED_SDA_PIN);   /* SDA 高 = STOP */
    DL_Common_delayCycles(32000U);

    /* 5. 把 PA29/PA30 切回 I2C1 外设功能，完整复制 SysConfig GPIO_init 里的配置 */
    DL_GPIO_initPeripheralInputFunctionFeatures(
        GPIO_I2C_OLED_IOMUX_SDA, GPIO_I2C_OLED_IOMUX_SDA_FUNC,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_initPeripheralInputFunctionFeatures(
        GPIO_I2C_OLED_IOMUX_SCL, GPIO_I2C_OLED_IOMUX_SCL_FUNC,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_enableHiZ(GPIO_I2C_OLED_IOMUX_SDA);
    DL_GPIO_enableHiZ(GPIO_I2C_OLED_IOMUX_SCL);

    /* 6. 复位并重新使能 I2C1 外设，清除控制器侧的残留状态 */
    DL_I2C_reset(I2C_OLED_INST);
    DL_I2C_enablePower(I2C_OLED_INST);
    /*
     * POWER_STARTUP_DELAY = 16 cycles 不足以保证 I2C 外设从复位状态稳定退出。
     * 在 bus recovery 场景下，I2C 之前处于活跃状态，复位后需要更长的稳定时间。
     * 4800 cycles = 150μs @ 32MHz，参考 TI M0P 例程的保守等待做法。
     */
    DL_Common_delayCycles(4800U);
    SYSCFG_DL_I2C_OLED_init();

    /* 7. 重置就绪标志，使 EnsureI2cControllerReady 重新执行完整的 controller 初始化 */
    s_oled_i2c_controller_ready = false;
}

/**
 * @brief  确保 OLED 使用的 I2C controller 已经启用。
 *
 * @note   当前 SysConfig 生成的 SYSCFG_DL_I2C_OLED_init() 只配置了 I2C 时钟和
 *         glitch filter，没有生成 controller mode 的 reset、timer、FIFO 和 enable
 *         步骤。这里在用户驱动层补齐官方 400kHz controller 初始化序列，避免直接修改
 *         ti_msp_dl_config.* 生成文件。
 *
 * @param  无。
 * @return 无。
 */
static void Oled_DriverEnsureI2cControllerReady(void)
{
    /*
     * DriverLib 文档要求 controller 已启用后不要重复 enable。
     * 因此本补偿初始化只执行一次，后续 OLED 重试只重新发送 SSD1306 命令。
     */
    if (s_oled_i2c_controller_ready == true)
    {
        /* I2C controller 已经准备好，无需重复配置。 */
        return;
    }

    /* 复位 controller 传输寄存器，清理上电后的默认传输状态。 */
    DL_I2C_resetControllerTransfer(I2C_OLED_INST);
    /*
     * empty.syscfg 中 OLED I2C 配置为 400kHz，32MHz BUSCLK 下官方示例使用 TPR=7。
     * 计算关系为 SCL_PERIOD = (1 + TPR) * (6 + 4) * BUSCLK_PERIOD。
     */
    DL_I2C_setTimerPeriod(I2C_OLED_INST, 7U);
    /* 轮询写入大块 OLED 数据时，TX FIFO 空阈值便于持续补充后续 payload。 */
    DL_I2C_setControllerTXFIFOThreshold(I2C_OLED_INST, DL_I2C_TX_FIFO_LEVEL_EMPTY);
    /* OLED 当前只写不读，但 controller 初始化仍保持 RX 阈值为 1 字节的标准配置。 */
    DL_I2C_setControllerRXFIFOThreshold(I2C_OLED_INST, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
    /* 开启 clock stretching，保持 I2C controller 行为符合总线规范。 */
    DL_I2C_enableControllerClockStretching(I2C_OLED_INST);
    /* 最后启用 I2C controller，使后续 START/STOP 传输真正发到 PA29/PA30。 */
    DL_I2C_enableController(I2C_OLED_INST);

    /* 标记补偿初始化完成，避免后续 OLED 重试重复 enable controller。 */
    s_oled_i2c_controller_ready = true;
}

/**
 * @brief  通过 I2C1 向 OLED 写入 1 个控制字节和 1 个 payload 字节。
 *
 * @note   用户给出的可用 STM32 HAL 驱动使用 HAL_I2C_Mem_Write()，每次只写一个命令
 *         或一个显存数据字节。本函数保留 MSPM0 DriverLib 底层，但把事务粒度收敛到
 *         control+1 字节，用于规避当前 OLED 模块对长 I2C burst 不稳定的问题。
 *
 * @param  control SSD1306 控制字节，0x00 为命令，0x40 为数据。
 * @param  value   待写入的单字节命令或显存数据。
 * @return true 表示本次 I2C 事务完成且未检测到错误；false 表示超时或硬件错误。
 */
static bool Oled_DriverWriteByte(uint8_t control, uint8_t value)
{
    /* 两字节事务缓冲，第 0 字节为 SSD1306 控制字节，第 1 字节为实际命令/数据。 */
    uint8_t packet[2];
    /* 等待 I2C 空闲和传输完成共用的有界计数。 */
    uint32_t wait_count = OLED_DRIVER_I2C_TIMEOUT;

    /* 组装参考驱动等价的 control+单字节写入格式。 */
    packet[0] = control;
    packet[1] = value;

    /* 等待上一笔 I2C 事务完全结束，避免 START/STOP 相互重叠。 */
    while (((DL_I2C_getControllerStatus(I2C_OLED_INST) &
                OLED_DRIVER_I2C_STATUS_IDLE) == 0U) &&
           (wait_count > 0UL))
    {
        /* 每轮等待消耗一个计数。 */
        wait_count--;
    }
    if (wait_count == 0UL)
    {
        /* 总线长期不空闲，记录 OLED 不可用。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 清空 TX FIFO，保证本次短事务只包含这两个字节。 */
    DL_I2C_flushControllerTXFIFO(I2C_OLED_INST);
    /* 事务只有 2 字节，正常情况下可一次性写入 FIFO。 */
    if (DL_I2C_fillControllerTXFIFO(I2C_OLED_INST, packet,
            (uint16_t)sizeof(packet)) != (uint16_t)sizeof(packet))
    {
        /* 短事务都无法完全进入 FIFO 时，直接复位传输状态并失败返回。 */
        DL_I2C_resetControllerTransfer(I2C_OLED_INST);
        s_oled_driver_available = false;
        return false;
    }

    /* 启动 control+payload 两字节写传输，对应 HAL_I2C_Mem_Write 的 0x00/0x40 内存地址写法。 */
    DL_I2C_startControllerTransfer(I2C_OLED_INST, OLED_DRIVER_I2C_ADDRESS,
        DL_I2C_CONTROLLER_DIRECTION_TX, (uint16_t)sizeof(packet));

    /* 等待短事务完成，并在等待中检查 NACK/仲裁等错误。 */
    wait_count = OLED_DRIVER_I2C_TIMEOUT;
    while (((DL_I2C_getControllerStatus(I2C_OLED_INST) &
                OLED_DRIVER_I2C_STATUS_ACTIVE) != 0U) &&
           (wait_count > 0UL))
    {
        if ((DL_I2C_getControllerStatus(I2C_OLED_INST) &
                DL_I2C_CONTROLLER_STATUS_ERROR) != 0U)
        {
            /* 复位传输状态，便于下一次 OLED 任务重试。 */
            DL_I2C_resetControllerTransfer(I2C_OLED_INST);
            s_oled_driver_available = false;
            return false;
        }

        /* 每轮等待消耗一个计数。 */
        wait_count--;
    }

    if (wait_count == 0UL)
    {
        /* 传输超时后复位控制器传输寄存器，避免后续一直忙。 */
        DL_I2C_resetControllerTransfer(I2C_OLED_INST);
        s_oled_driver_available = false;
        return false;
    }

    if ((DL_I2C_getControllerStatus(I2C_OLED_INST) &
            DL_I2C_CONTROLLER_STATUS_ERROR) != 0U)
    {
        /* 结束时仍有错误位，不能认为写入成功。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 单字节事务完成。 */
    s_oled_driver_available = true;
    return true;
}

/**
 * @brief  初始化 OLED 屏幕。
 *
 * @note   I2C1 和引脚配置由 SysConfig 完成；本函数只发送 SSD1306 初始化序列。
 *         全屏清屏会产生大量 I2C 事务，不能放在调度器首次 OLED 任务里同步执行。
 *
 * @param  无。
 * @return 无。
 */
void Oled_DriverInit(void)
{
    /* 补齐 I2C controller 初始化，再发送 SSD1306 命令。 */
    Oled_DriverEnsureI2cControllerReady();

    /*
     * 参考可用 HAL 驱动在初始化命令前等待 200ms。
     * 这里使用 DriverLib 周期延时，不依赖调度器 tick，确保 OLED 电源和内部电荷泵启动稳定。
     */
    DL_Common_delayCycles(OLED_DRIVER_POWER_ON_DELAY_CYCLES);

    /*
     * 逐条发送 SSD1306 初始化命令。
     * 初始化失败不阻塞系统启动，应用层会根据可用状态进入退避，避免 OLED 异常拖死主循环。
     */
    if (Oled_DriverWriteCommandBuffer(s_oled_init_commands,
        (uint16_t)sizeof(s_oled_init_commands)) == false)
    {
        /* 未接屏或地址错误时直接返回，避免启动阶段继续进入更多 I2C 超时。 */
        return;
    }
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
    return Oled_DriverWriteByte(OLED_DRIVER_CONTROL_COMMAND, command);
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
    /* 命令数组不能为空，长度也必须非 0。 */
    if ((commands == NULL) || (length == 0U))
    {
        /* 参数无效时标记 OLED 不可用。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 逐条命令写入，完全贴近参考 HAL 驱动 OLED_WR_CMD() 的事务粒度。 */
    for (uint16_t i = 0U; i < length; i++)
    {
        if (Oled_DriverWriteByte(OLED_DRIVER_CONTROL_COMMAND, commands[i]) == false)
        {
            /* 任意一条命令失败就停止初始化/定位序列。 */
            return false;
        }
    }

    /* 全部命令写入成功。 */
    return true;
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
    return Oled_DriverWriteByte(OLED_DRIVER_CONTROL_DATA, data);
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
    /* 数据数组不能为空，长度也必须非 0。 */
    if ((data == NULL) || (length == 0U))
    {
        /* 参数无效时标记 OLED 不可用。 */
        s_oled_driver_available = false;
        return false;
    }

    /* 逐字节写显存，等价于参考 HAL 驱动 OLED_WR_DATA() 在循环中发送每列。 */
    for (uint16_t i = 0U; i < length; i++)
    {
        if (Oled_DriverWriteByte(OLED_DRIVER_CONTROL_DATA, data[i]) == false)
        {
            /* 某个显存字节发送失败时立即返回，由 App 层退避重试。 */
            return false;
        }
    }

    /* 全部显存数据写入成功。 */
    return true;
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
    /* 当前待显示字符的字库索引。 */
    uint8_t index;
    /* 当前字符起始列坐标。 */
    uint8_t current_x = x;

    /* 字符串不能为空，页坐标必须有效。 */
    if ((text == NULL) || (page >= OLED_DRIVER_PAGE_COUNT) || (x >= OLED_DRIVER_WIDTH))
    {
        /* 参数无效。 */
        return false;
    }

    /*
     * 逐字符定位和写入，贴近参考驱动 OLED_ShowString() -> OLED_ShowChar() 的路径。
     * 虽然 I2C 事务更多，但每次只写很短的数据，更适合当前硬件排障阶段验证 OLED 可用性。
     */
    while ((*text != '\0') && (current_x <= (OLED_DRIVER_WIDTH - 6U)))
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

        /* 每个字符都重新定位，降低连续显存写入对 SSD1306 地址自增状态的依赖。 */
        if (Oled_DriverSetPosition(current_x, page) == false)
        {
            /* 定位失败则停止本次字符串显示。 */
            return false;
        }

        /* 当前字符的 6 列字模逐字节写入。 */
        if (Oled_DriverWriteDataBuffer(s_oled_font_6x8[index], 6U) == false)
        {
            /* 字模写入失败，交给 App 层退避重试。 */
            return false;
        }

        /* 移动到下一个字符起点。 */
        current_x = (uint8_t)(current_x + 6U);
        /* 指向下一个输入字符。 */
        text++;
    }

    /* 已处理到字符串结尾或屏幕右边界，视为正常结束。 */
    return true;
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
