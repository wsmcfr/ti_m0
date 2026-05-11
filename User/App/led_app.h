/**
 * @file    led_app.h
 * @brief   LED 应用层接口，提供可被调度器周期调用的任务函数。
 */

#ifndef LED_APP_H
#define LED_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 LED 应用层。
 *
 * @note   当前应用层只依赖 LED Driver，后续可在这里添加状态机初始状态。
 *
 * @param  无。
 * @return 无。
 */
void Led_AppInit(void);

/**
 * @brief  LED 周期任务函数。
 *
 * @note   该函数由 Scheduler_Run() 按任务表中的周期调用，函数内部不阻塞。
 *
 * @param  无。
 * @return 无。
 */
void Led_AppTask(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_APP_H */
