/**
 * @file    gray_sensor_driver.h
 * @brief   感为无 MCU 八路灰度模块底层驱动接口。
 *
 * @details 本驱动负责三根地址线选通、ADC0 采样和 DMA/轮询读取。
 *          校准、归一化和循迹误差计算放在 App 层，避免 Driver 层包含业务逻辑。
 */

#ifndef GRAY_SENSOR_DRIVER_H
#define GRAY_SENSOR_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 灰度传感器物理通道数量。 */
#define GRAY_SENSOR_CHANNEL_COUNT       (8U)

void GraySensor_DriverInit(void);
bool GraySensor_DriverSelectChannel(uint8_t channel);
bool GraySensor_DriverReadChannel(uint8_t channel, uint16_t *out_value);
bool GraySensor_DriverReadAll(uint16_t *out_values, uint8_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* GRAY_SENSOR_DRIVER_H */
