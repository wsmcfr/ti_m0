/**
 * @file    led_driver.c
 * @brief   蓝色 LED 底层驱动实现，集中管理 PB22 相关 GPIO 操作。
 */

#include "led_driver.h"

#include "ti_msp_dl_config.h"

/**
 * @brief  初始化 LED 驱动层，并确保 LED 默认熄灭。
 *
 * @note   SysConfig 已经在 SYSCFG_DL_GPIO_init() 中完成 PB22 输出模式配置。
 *         这里不重复初始化引脚，只设置确定电平，避免和自动生成代码职责重叠。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverInit(void)
{
    /* 关闭蓝色 LED，保证应用启动时的初始显示状态可预测。 */
    /* 调用熄灭函数统一处理输出电平，避免初始化路径重复写 GPIO 细节。 */
    Led_DriverBlueOff();
}

/**
 * @brief  翻转蓝色 LED 输出状态。
 *
 * @note   关键硬件动作通过 DriverLib 完成。
 *         寄存器方式等价于写 GPIO DOUTTGL 寄存器对应位。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverToggleBlue(void)
{
    /* 翻转 PB22 对应输出位，使 LED 在亮和灭之间切换。 */
    DL_GPIO_togglePins(LED_PORT_PORT, LED_PORT_LED_BLUE_PIN);
}

/**
 * @brief  点亮蓝色 LED。
 *
 * @note   当前例程原始代码通过 setPins 点亮 PB22，因此保留同样电平语义。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverBlueOn(void)
{
    /* 将 PB22 输出置 1，按当前硬件连接语义点亮蓝色 LED。 */
    DL_GPIO_setPins(LED_PORT_PORT, LED_PORT_LED_BLUE_PIN);
}

/**
 * @brief  熄灭蓝色 LED。
 *
 * @note   当前例程原始代码通过 clearPins 熄灭 PB22，因此保留同样电平语义。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverBlueOff(void)
{
    /* 将 PB22 输出清 0，按当前硬件连接语义熄灭蓝色 LED。 */
    DL_GPIO_clearPins(LED_PORT_PORT, LED_PORT_LED_BLUE_PIN);
}
