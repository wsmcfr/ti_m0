/**
 * @file    led_app.c
 * @brief   LED 应用层实现，负责把业务任务映射到底层 LED 驱动动作。
 */

#include "led_app.h"

#include "led_driver.h"

/**
 * @brief  初始化 LED 应用层。
 *
 * @note   目前没有独立业务状态，只调用驱动层初始化，保证分层入口统一。
 *
 * @param  无。
 * @return 无。
 */
void Led_AppInit(void)
{
    /* 初始化 LED 驱动层，让应用层入口不用直接操作 GPIO。 */
    Led_DriverInit();
}

/**
 * @brief  LED 周期任务。
 *
 * @note   每次被调度器调用时翻转一次蓝灯。任务本身不调用延时函数，
 *         周期完全由调度器任务表控制，避免阻塞主循环。
 *
 * @param  无。
 * @return 无。
 */
void Led_AppTask(void)
{
    /* 每次任务被调度时翻转蓝灯一次，闪烁周期由调度器任务表决定。 */
    Led_DriverToggleBlue();
}
