/**
 * @file    key_app.h
 * @brief   四按键应用层接口，提供消抖、按下、抬起和长按事件。
 */

#ifndef KEY_APP_H
#define KEY_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "key_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  按键应用层快照。
 *
 * @note   stable_mask 表示当前稳定按下状态；down/up/long_press 是边沿事件，
 *         调用 Key_AppConsumeEvents() 后会被清除。
 */
typedef struct
{
    uint8_t stable_mask;       /* 当前稳定按下掩码。 */
    uint8_t down_mask;         /* 自上次消费以来出现的按下事件。 */
    uint8_t up_mask;           /* 自上次消费以来出现的抬起事件。 */
    uint8_t long_press_mask;   /* 自上次消费以来出现的长按事件。 */
} key_app_state_t;

void Key_AppInit(void);
void Key_AppTask(void);
uint8_t Key_AppGetStableMask(void);
uint8_t Key_AppGetDownMask(void);
uint8_t Key_AppGetUpMask(void);
uint8_t Key_AppGetLongPressMask(void);
bool Key_AppConsumeEvents(key_app_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* KEY_APP_H */
