/**
 * @file    oled_app.h
 * @brief   OLED 应用层接口，低频显示系统状态。
 */

#ifndef OLED_APP_H
#define OLED_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void Oled_AppInit(void);
void Oled_AppTask(void);
void Oled_AppForceRefresh(void);
bool Oled_AppIsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_APP_H */
