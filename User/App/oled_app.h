/**
 * @file    oled_app.h
 * @brief   OLED 应用层接口，低频显示系统状态。
 */

#ifndef OLED_APP_H
#define OLED_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "line_track_app.h"
#include "motor_app.h"
#include "uart_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 128 像素宽度使用 6x8 字体时，一行最多稳定显示 21 个字符。 */
#define OLED_APP_TEXT_COLUMNS       (21U)

/**
 * @brief  OLED 单次刷新使用的显示状态快照。
 *
 * @note   App 层先把其它模块状态复制到该结构，再格式化成固定宽度行文本。
 *         这样显示格式可以在 PC 端单元测试，不依赖 I2C 硬件。
 */
typedef struct
{
    uint8_t key_mask;                         /* 当前稳定按键掩码。 */
    bool has_line_snapshot;                   /* true 表示 line_snapshot 内有有效灰度数据。 */
    line_track_snapshot_t line_snapshot;      /* 灰度循迹状态快照。 */
    bool has_motor_status;                    /* true 表示 motor_status 内有有效电机数据。 */
    motor_app_status_t motor_status;          /* 电机运行状态快照。 */
    bool has_motor_uart_stats;                /* true 表示 motor_uart_stats 内有 UART3 诊断统计。 */
    uart_driver_stats_t motor_uart_stats;     /* 电机 UART3 收发统计，用于判断命令是否发出或超时。 */
} oled_app_display_state_t;

void Oled_AppInit(void);
void Oled_AppTask(void);
void Oled_AppForceRefresh(void);
bool Oled_AppIsReady(void);
bool Oled_AppFormatLine(uint8_t line_index, const oled_app_display_state_t *state,
    char *out_text, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* OLED_APP_H */
