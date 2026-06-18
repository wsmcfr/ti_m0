/*
 * 测试专用 SysConfig/DriverLib 桩头文件。
 * 该文件只给主机端 OLED Driver 单元测试使用，用最小符号集替代真实 TI 头文件。
 */

#ifndef TEST_TI_MSP_DL_CONFIG_H
#define TEST_TI_MSP_DL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 函数作用：
 *   用空结构体代表 DriverLib 的 I2C 寄存器映射类型。
 * 主要流程：
 *   主机测试只验证 OLED 驱动是否调用了正确的 I2C 初始化 API，不访问真实寄存器。
 * 关键字段：
 *   unused：避免空结构体在部分编译器上产生兼容性问题。
 */
typedef struct
{
    uint8_t unused; /* 占位字段，测试中不读取。 */
} I2C_Regs;

/* 测试中的 OLED I2C 实例，等价于真实工程里的 I2C_OLED_INST。 */
extern I2C_Regs g_test_i2c_oled_inst;

/* OLED 驱动通过该宏拿到 I2C 外设实例。 */
#define I2C_OLED_INST                       (&g_test_i2c_oled_inst)

/* I2C 控制器状态位，测试只需要 busy/error 两类状态。 */
#define DL_I2C_CONTROLLER_STATUS_BUSY       (0x01U)
#define DL_I2C_CONTROLLER_STATUS_ERROR      (0x02U)
#define DL_I2C_CONTROLLER_STATUS_IDLE       (0x04U)
#define DL_I2C_CONTROLLER_STATUS_BUSY_BUS   (0x08U)

/*
 * 函数作用：
 *   描述 I2C 控制器传输方向。
 * 主要流程：
 *   OLED 当前只做写操作，因此测试只声明 TX 枚举值。
 */
typedef enum
{
    DL_I2C_CONTROLLER_DIRECTION_TX = 0U /* 控制器向 OLED 写入数据。 */
} DL_I2C_CONTROLLER_DIRECTION;

/*
 * 函数作用：
 *   描述 I2C 控制器 TX FIFO 阈值。
 * 主要流程：
 *   测试验证 OLED 驱动使用官方 400kHz 轮询例程中的 EMPTY 阈值。
 */
typedef enum
{
    DL_I2C_TX_FIFO_LEVEL_EMPTY = 0U,  /* TX FIFO 空时触发，适合轮询补 FIFO。 */
    DL_I2C_TX_FIFO_LEVEL_BYTES_1 = 1U /* 保留给其它示例兼容，不作为本测试期望。 */
} DL_I2C_TX_FIFO_LEVEL;

/*
 * 函数作用：
 *   描述 I2C 控制器 RX FIFO 阈值。
 * 主要流程：
 *   OLED 写屏不读数据，但 DriverLib controller 初始化仍配置该阈值。
 */
typedef enum
{
    DL_I2C_RX_FIFO_LEVEL_BYTES_1 = 1U /* RX FIFO 有 1 字节时触发。 */
} DL_I2C_RX_FIFO_LEVEL;

uint32_t DL_I2C_getControllerStatus(const I2C_Regs *i2c);
void DL_I2C_flushControllerTXFIFO(I2C_Regs *i2c);
uint16_t DL_I2C_fillControllerTXFIFO(I2C_Regs *i2c, const uint8_t *buffer,
    uint16_t count);
void DL_I2C_startControllerTransfer(I2C_Regs *i2c, uint32_t targetAddr,
    DL_I2C_CONTROLLER_DIRECTION direction, uint16_t length);
void DL_I2C_resetControllerTransfer(I2C_Regs *i2c);
void DL_I2C_setTimerPeriod(I2C_Regs *i2c, uint8_t period);
void DL_I2C_setControllerTXFIFOThreshold(I2C_Regs *i2c,
    DL_I2C_TX_FIFO_LEVEL level);
void DL_I2C_setControllerRXFIFOThreshold(I2C_Regs *i2c,
    DL_I2C_RX_FIFO_LEVEL level);
void DL_I2C_enableControllerClockStretching(I2C_Regs *i2c);
void DL_I2C_enableController(I2C_Regs *i2c);
void DL_Common_delayCycles(uint32_t cycles);

#endif /* TEST_TI_MSP_DL_CONFIG_H */
