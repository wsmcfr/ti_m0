/**
 * @file    led_driver.h
 * @brief   蓝色 LED 底层驱动接口，封装 MSPM0 DriverLib 的 GPIO 操作。
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 LED 驱动层。
 *
 * @note   当前工程的 GPIO 初始化已经由 SysConfig 生成的 SYSCFG_DL_init()
 *         完成，因此本函数只负责把 LED 置为确定的关闭状态，避免上电状态不明确。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverInit(void);

/**
 * @brief  翻转蓝色 LED 输出状态。
 *
 * @note   该函数是 App 层周期任务调用的最小硬件动作，不包含调度逻辑。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverToggleBlue(void);

/**
 * @brief  设置蓝色 LED 为亮。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverBlueOn(void);

/**
 * @brief  设置蓝色 LED 为灭。
 *
 * @param  无。
 * @return 无。
 */
void Led_DriverBlueOff(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_DRIVER_H */
