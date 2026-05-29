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

/**
 * @brief  非阻塞单点采样状态。
 *
 * @note   App 层通过 Start/Poll 两阶段推进 ADC/DMA 采样，避免周期任务忙等。
 */
typedef enum
{
    GRAY_SENSOR_SAMPLE_STATUS_IDLE = 0,  /* 当前没有正在进行的单点采样。 */
    GRAY_SENSOR_SAMPLE_STATUS_BUSY,      /* ADC/DMA 采样仍在进行，调用者稍后再查。 */
    GRAY_SENSOR_SAMPLE_STATUS_READY,     /* 采样完成，输出值已经写入调用者缓冲。 */
    GRAY_SENSOR_SAMPLE_STATUS_ERROR      /* 参数错误或硬件状态异常。 */
} gray_sensor_sample_status_t;

void GraySensor_DriverInit(void);
bool GraySensor_DriverSelectChannel(uint8_t channel);
bool GraySensor_DriverStartSampleSelected(void);
gray_sensor_sample_status_t GraySensor_DriverPollSampleSelected(uint16_t *out_value);
void GraySensor_DriverAbortSampleSelected(void);
bool GraySensor_DriverSampleSelected(uint16_t *out_value);
bool GraySensor_DriverReadChannel(uint8_t channel, uint16_t *out_value);
bool GraySensor_DriverReadAll(uint16_t *out_values, uint8_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* GRAY_SENSOR_DRIVER_H */
