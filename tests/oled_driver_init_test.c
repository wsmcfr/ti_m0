/*
 * OLED 底层驱动初始化单元测试。
 * 该测试使用 I2C 桩函数验证 MSPM0 侧 OLED 初始化会先把 I2C controller 配完整。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "oled_driver.h"
#include "ti_msp_dl_config.h"

/* OLED SSD1306 的 7 位地址，测试用于确认驱动没有错用 STM32 HAL 的 8 位地址 0x78。 */
#define TEST_OLED_ADDRESS_7BIT          (0x3CU)

/* 初始化命令长度，来自当前 OLED 驱动的 SSD1306 初始化序列。 */
#define TEST_OLED_INIT_COMMAND_COUNT    (23U)

/*
 * 函数作用：
 *   保存测试桩观测到的 I2C controller 初始化和传输行为。
 * 主要流程：
 *   每个 DriverLib 桩函数只记录调用次数、关键参数和少量传输数据，测试用例再统一断言。
 * 关键字段：
 *   reset_transfer_count：reset controller transfer 的调用次数。
 *   timer_period：I2C SCL 定时器参数，32MHz BUSCLK 下 7 对应 400kHz。
 *   enable_controller_count：I2C controller enable 的调用次数。
 */
typedef struct
{
    uint32_t status;                                     /* 当前模拟 I2C 状态位。 */
    uint32_t reset_transfer_count;                       /* 复位 controller 传输寄存器次数。 */
    uint32_t set_timer_count;                            /* 配置 SCL timer 次数。 */
    uint8_t timer_period;                                /* 最近一次 timer period 参数。 */
    uint32_t set_tx_threshold_count;                     /* 配置 TX FIFO 阈值次数。 */
    DL_I2C_TX_FIFO_LEVEL tx_threshold;                   /* 最近一次 TX FIFO 阈值。 */
    uint32_t set_rx_threshold_count;                     /* 配置 RX FIFO 阈值次数。 */
    DL_I2C_RX_FIFO_LEVEL rx_threshold;                   /* 最近一次 RX FIFO 阈值。 */
    uint32_t enable_clock_stretching_count;              /* 开启 controller clock stretching 次数。 */
    uint32_t enable_controller_count;                    /* 开启 I2C controller 次数。 */
    uint32_t flush_tx_count;                             /* 清空 TX FIFO 次数。 */
    uint32_t transfer_count;                             /* 启动 I2C 传输次数。 */
    uint32_t last_target_address;                        /* 最近一次目标 I2C 地址。 */
    uint16_t last_transfer_length;                       /* 最近一次传输总长度。 */
    uint16_t max_transfer_length;                        /* 测试期间观察到的最大 I2C 事务长度。 */
    uint32_t two_byte_transfer_count;                    /* control+1 字节事务数量。 */
    uint16_t total_fifo_byte_count;                       /* 所有 FIFO 填充调用累计写入的字节数。 */
    uint16_t fifo_capacity_per_fill;                      /* 单次 FIFO 桩允许写入的最大字节数，0 表示不限制。 */
    const uint32_t *status_sequence;                      /* 状态读取序列，用于模拟硬件状态位随时间变化。 */
    uint16_t status_sequence_count;                       /* 状态序列长度。 */
    uint16_t status_sequence_index;                       /* 当前已经消费的状态序列项数。 */
    uint8_t first_transfer_bytes[TEST_OLED_INIT_COMMAND_COUNT + 1U]; /* 首笔传输的控制字节和命令。 */
    uint16_t first_transfer_byte_count;                  /* 首笔传输捕获到的字节数。 */
    uint32_t delay_cycles_count;                          /* 上电延时函数调用次数。 */
    uint32_t last_delay_cycles;                           /* 最近一次延时周期数。 */
} test_i2c_trace_t;

/* 测试用 I2C 实例，供 I2C_OLED_INST 宏引用。 */
I2C_Regs g_test_i2c_oled_inst;

/* 全局 I2C 桩跟踪状态，测试用例每次运行前会清零。 */
static test_i2c_trace_t s_i2c_trace;

/*
 * 函数作用：
 *   重置 I2C 桩跟踪状态。
 * 主要流程：
 *   清零全部计数和参数，保证每个测试用例之间互不影响。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值。
 */
static void reset_i2c_trace(void)
{
    memset(&s_i2c_trace, 0, sizeof(s_i2c_trace));
}

/*
 * 函数作用：
 *   模拟读取 I2C controller 状态。
 * 主要流程：
 *   返回测试用例设置的状态位，默认 0 表示总线空闲且无错误。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 * 返回值说明：
 *   返回 busy/error 等状态位。
 */
uint32_t DL_I2C_getControllerStatus(const I2C_Regs *i2c)
{
    (void)i2c;

    if ((s_i2c_trace.status_sequence != NULL) &&
        (s_i2c_trace.status_sequence_index < s_i2c_trace.status_sequence_count))
    {
        uint32_t status =
            s_i2c_trace.status_sequence[s_i2c_trace.status_sequence_index];

        s_i2c_trace.status_sequence_index++;
        s_i2c_trace.status = status;
        return status;
    }

    return s_i2c_trace.status;
}

/*
 * 函数作用：
 *   模拟清空 I2C controller TX FIFO。
 * 主要流程：
 *   只记录调用次数，不维护真实 FIFO。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_flushControllerTXFIFO(I2C_Regs *i2c)
{
    (void)i2c;
    s_i2c_trace.flush_tx_count++;
}

/*
 * 函数作用：
 *   模拟向 I2C controller TX FIFO 填充数据。
 * 主要流程：
 *   捕获第一笔 I2C 传输写入 FIFO 的控制字节和初始化命令，然后返回全部写入成功。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 *   buffer：待写入数据。
 *   count：待写入字节数。
 * 返回值说明：
 *   返回 count，表示桩环境中 FIFO 容量充足。
 */
uint16_t DL_I2C_fillControllerTXFIFO(I2C_Regs *i2c, const uint8_t *buffer,
    uint16_t count)
{
    uint16_t writable = count;

    (void)i2c;

    if ((s_i2c_trace.fifo_capacity_per_fill > 0U) &&
        (writable > s_i2c_trace.fifo_capacity_per_fill))
    {
        writable = s_i2c_trace.fifo_capacity_per_fill;
    }

    if ((buffer != NULL) && (writable > 0U) &&
        (s_i2c_trace.transfer_count == 0U) &&
        (s_i2c_trace.first_transfer_byte_count < sizeof(s_i2c_trace.first_transfer_bytes)))
    {
        uint16_t remaining = (uint16_t)(sizeof(s_i2c_trace.first_transfer_bytes) -
            s_i2c_trace.first_transfer_byte_count);
        uint16_t copy_count = (writable < remaining) ? writable : remaining;

        memcpy(&s_i2c_trace.first_transfer_bytes[s_i2c_trace.first_transfer_byte_count],
            buffer, copy_count);
        s_i2c_trace.first_transfer_byte_count =
            (uint16_t)(s_i2c_trace.first_transfer_byte_count + copy_count);
    }

    s_i2c_trace.total_fifo_byte_count =
        (uint16_t)(s_i2c_trace.total_fifo_byte_count + writable);
    return writable;
}

/*
 * 函数作用：
 *   模拟启动 I2C controller 传输。
 * 主要流程：
 *   记录目标地址、方向和总长度，测试确认 OLED 使用 0x3C 和 control+payload 长度。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 *   targetAddr：I2C 目标地址。
 *   direction：传输方向，OLED 初始化应为 TX。
 *   length：本次 I2C 总字节数。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_startControllerTransfer(I2C_Regs *i2c, uint32_t targetAddr,
    DL_I2C_CONTROLLER_DIRECTION direction, uint16_t length)
{
    (void)i2c;
    assert(direction == DL_I2C_CONTROLLER_DIRECTION_TX);

    s_i2c_trace.transfer_count++;
    s_i2c_trace.last_target_address = targetAddr;
    s_i2c_trace.last_transfer_length = length;
    if (length > s_i2c_trace.max_transfer_length)
    {
        s_i2c_trace.max_transfer_length = length;
    }
    if (length == 2U)
    {
        s_i2c_trace.two_byte_transfer_count++;
    }
}

/*
 * 函数作用：
 *   模拟复位 I2C controller 传输寄存器。
 * 主要流程：
 *   记录调用次数，用于验证初始化阶段和错误恢复阶段都会复位传输状态。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_resetControllerTransfer(I2C_Regs *i2c)
{
    (void)i2c;
    s_i2c_trace.reset_transfer_count++;
}

/*
 * 函数作用：
 *   模拟配置 I2C SCL timer period。
 * 主要流程：
 *   记录 period 参数，测试要求 400kHz 配置为 7。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 *   period：DriverLib SCL period 参数。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_setTimerPeriod(I2C_Regs *i2c, uint8_t period)
{
    (void)i2c;
    s_i2c_trace.set_timer_count++;
    s_i2c_trace.timer_period = period;
}

/*
 * 函数作用：
 *   模拟配置 I2C TX FIFO 阈值。
 * 主要流程：
 *   记录阈值参数，测试确认轮询写入使用 EMPTY 阈值。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 *   level：TX FIFO 阈值。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_setControllerTXFIFOThreshold(I2C_Regs *i2c,
    DL_I2C_TX_FIFO_LEVEL level)
{
    (void)i2c;
    s_i2c_trace.set_tx_threshold_count++;
    s_i2c_trace.tx_threshold = level;
}

/*
 * 函数作用：
 *   模拟配置 I2C RX FIFO 阈值。
 * 主要流程：
 *   记录阈值参数，保持与 SysConfig 官方 controller 初始化顺序一致。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 *   level：RX FIFO 阈值。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_setControllerRXFIFOThreshold(I2C_Regs *i2c,
    DL_I2C_RX_FIFO_LEVEL level)
{
    (void)i2c;
    s_i2c_trace.set_rx_threshold_count++;
    s_i2c_trace.rx_threshold = level;
}

/*
 * 函数作用：
 *   模拟开启 I2C controller clock stretching。
 * 主要流程：
 *   记录调用次数，测试确认初始化流程符合 I2C 规范推荐配置。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_enableControllerClockStretching(I2C_Regs *i2c)
{
    (void)i2c;
    s_i2c_trace.enable_clock_stretching_count++;
}

/*
 * 函数作用：
 *   模拟开启 I2C controller。
 * 主要流程：
 *   记录调用次数，测试确认 OLED 初始化前 controller 已经 enable。
 * 参数说明：
 *   i2c：I2C 外设实例，测试桩不需要读取。
 * 返回值说明：
 *   无返回值。
 */
void DL_I2C_enableController(I2C_Regs *i2c)
{
    (void)i2c;
    s_i2c_trace.enable_controller_count++;
}

/*
 * 函数作用：
 *   模拟 DriverLib 周期延时函数。
 * 主要流程：
 *   不实际等待，只记录调用次数和最近一次延时周期，验证 OLED 初始化遵循参考驱动的上电等待。
 * 参数说明：
 *   cycles：调用方请求等待的 CPU 周期数。
 * 返回值说明：
 *   无返回值。
 */
void DL_Common_delayCycles(uint32_t cycles)
{
    s_i2c_trace.delay_cycles_count++;
    s_i2c_trace.last_delay_cycles = cycles;
}

/*
 * 函数作用：
 *   验证 OLED 初始化会补齐 I2C controller 初始化并发送 SSD1306 初始化命令。
 * 主要流程：
 *   清零桩状态，调用 Oled_DriverInit()，断言 controller 初始化调用和首笔 I2C 参数。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_init_enables_i2c_controller_before_oled_commands(void)
{
    reset_i2c_trace();
    s_i2c_trace.status = DL_I2C_CONTROLLER_STATUS_IDLE;

    Oled_DriverInit();

    assert(s_i2c_trace.reset_transfer_count >= 1U);
    assert(s_i2c_trace.set_timer_count == 1U);
    assert(s_i2c_trace.timer_period == 7U);
    assert(s_i2c_trace.set_tx_threshold_count == 1U);
    assert(s_i2c_trace.tx_threshold == DL_I2C_TX_FIFO_LEVEL_EMPTY);
    assert(s_i2c_trace.set_rx_threshold_count == 1U);
    assert(s_i2c_trace.rx_threshold == DL_I2C_RX_FIFO_LEVEL_BYTES_1);
    assert(s_i2c_trace.enable_clock_stretching_count == 1U);
    assert(s_i2c_trace.enable_controller_count == 1U);
    assert(s_i2c_trace.transfer_count >= 1U);
    assert(s_i2c_trace.last_target_address == TEST_OLED_ADDRESS_7BIT);
    assert(s_i2c_trace.delay_cycles_count == 1U);
    assert(s_i2c_trace.last_delay_cycles > 0U);
    /*
     * 初始化阶段只允许发送 SSD1306 初始化命令。
     * 全屏清屏会产生 8 页定位命令和 1024 个显存字节事务，放在调度器任务里会
     * 长时间占用主循环；本测试用精确事务数防止初始化重新夹带清屏操作。
     */
    assert(s_i2c_trace.transfer_count == TEST_OLED_INIT_COMMAND_COUNT);
    assert(s_i2c_trace.two_byte_transfer_count == TEST_OLED_INIT_COMMAND_COUNT);
    assert(s_i2c_trace.max_transfer_length == 2U);
    assert(s_i2c_trace.first_transfer_byte_count == 2U);
    assert(s_i2c_trace.first_transfer_bytes[0] == 0x00U);
    assert(s_i2c_trace.first_transfer_bytes[1] == 0xAEU);
}

/*
 * 函数作用：
 *   验证连续显存写入会被拆成参考 STM32 HAL 驱动同款 control+单字节事务。
 * 主要流程：
 *   构造 126 字节行数据，调用数据缓冲写入接口；测试要求每个显存字节都独立
 *   形成 2 字节 I2C 事务，避免长事务在当前 OLED 模块或总线上不稳定。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   无返回值，断言失败时测试进程终止。
 */
static void test_data_buffer_uses_single_byte_i2c_transactions(void)
{
    uint8_t row_data[21U * 6U];

    memset(row_data, 0xA5, sizeof(row_data));
    reset_i2c_trace();
    s_i2c_trace.status = DL_I2C_CONTROLLER_STATUS_IDLE;

    assert(Oled_DriverWriteDataBuffer(row_data, (uint16_t)sizeof(row_data)) == true);
    assert(s_i2c_trace.transfer_count == (uint32_t)sizeof(row_data));
    assert(s_i2c_trace.two_byte_transfer_count == (uint32_t)sizeof(row_data));
    assert(s_i2c_trace.max_transfer_length == 2U);
    assert(s_i2c_trace.total_fifo_byte_count == ((uint16_t)sizeof(row_data) * 2U));
}

/*
 * 函数作用：
 *   OLED Driver 初始化测试入口。
 * 主要流程：
 *   顺序执行所有测试用例，全部断言通过后输出简短成功文本。
 * 参数说明：
 *   无参数。
 * 返回值说明：
 *   0 表示测试通过；断言失败时进程异常退出。
 */
int main(void)
{
    test_init_enables_i2c_controller_before_oled_commands();
    test_data_buffer_uses_single_byte_i2c_transactions();

    puts("oled_driver_init_test: all tests passed");
    return 0;
}
