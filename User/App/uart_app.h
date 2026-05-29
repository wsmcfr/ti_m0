/**
 * @file    uart_app.h
 * @brief   UART 应用层接口，封装电脑、陀螺仪和电机驱动板串口收发。
 */

#ifndef UART_APP_H
#define UART_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "uart_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* my_printf 的单次格式化缓冲长度，避免在小 MCU 上使用过大的栈空间。 */
#define UART_APP_PRINTF_BUFFER_SIZE  (192U)

void Uart_AppInit(void);
void Uart_AppTask(void);
bool Uart_AppSendToPc(const uint8_t *data, uint16_t length);
bool Uart_AppTrySendToPc(const uint8_t *data, uint16_t length);
bool Uart_AppIsPcTxBusy(void);
bool Uart_AppSendToGyro(const uint8_t *data, uint16_t length);
bool Uart_AppSendToMotor(const uint8_t *data, uint16_t length);
uint16_t Uart_AppReadPcPacket(uint8_t *out_data, uint16_t max_length);
uint16_t Uart_AppReadGyroPacket(uint8_t *out_data, uint16_t max_length);
uint16_t Uart_AppReadMotorPacket(uint8_t *out_data, uint16_t max_length);
void Uart_AppGetStats(uart_driver_port_t port, uart_driver_stats_t *out_stats);
int my_printf(const char *format, ...);
int my_printf_try(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* UART_APP_H */
