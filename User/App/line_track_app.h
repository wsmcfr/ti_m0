/**
 * @file    line_track_app.h
 * @brief   八路灰度循迹应用层接口，包含纯算法和运行时任务入口。
 *
 * @details 本模块把灰度传感器原始 ADC 值转换为归一化值、二值化状态和位置误差。
 *          其中校准、归一化和误差计算不依赖硬件，方便在 PC 端单元测试。
 */

#ifndef LINE_TRACK_APP_H
#define LINE_TRACK_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 灰度模块为 8 路传感器，硬件通过 3 根地址线选通。 */
#define LINE_TRACK_SENSOR_COUNT          (8U)

/* 归一化输出范围，0 表示校准低端，1000 表示校准高端。 */
#define LINE_TRACK_NORMALIZED_MAX        (1000U)

/* 默认二值化阈值，归一化值大于等于该值时认为该路压线。 */
#define LINE_TRACK_DEFAULT_THRESHOLD     (500U)

/**
 * @brief  灰度传感器校准范围。
 *
 * @note   min_value/max_value 分别记录每一路历史最小/最大 ADC 值。
 *         calibrated 表示至少已经用一组样本更新过校准数据。
 */
typedef struct
{
    uint16_t min_value[LINE_TRACK_SENSOR_COUNT];  /* 每一路校准最小值。 */
    uint16_t max_value[LINE_TRACK_SENSOR_COUNT];  /* 每一路校准最大值。 */
    bool calibrated;                              /* true 表示校准范围已经有效。 */
} line_track_calibration_t;

/**
 * @brief  灰度循迹运行时快照。
 *
 * @note   App 层任务每次采样成功后更新该结构，其他模块可通过 getter 获取副本。
 */
typedef struct
{
    uint16_t raw[LINE_TRACK_SENSOR_COUNT];        /* 最近一次采到的 8 路 ADC 原始值。 */
    uint16_t normalized[LINE_TRACK_SENSOR_COUNT]; /* 最近一次归一化后的 8 路数值。 */
    uint8_t binary[LINE_TRACK_SENSOR_COUNT];      /* 最近一次二值化后的 0/1 状态。 */
    uint8_t bit_mask;                             /* binary 打包后的 8 位掩码，第 i 位对应第 i 路。 */
    int16_t position_error;                       /* 当前线位置误差，负数偏左，正数偏右。 */
    uint32_t sample_count;                        /* 成功完成的整组采样次数。 */
    uint32_t last_update_ms;                      /* 最近一次更新时的系统 tick。 */
    bool valid;                                   /* true 表示快照至少更新过一次。 */
} line_track_snapshot_t;

void LineTrack_InitCalibration(line_track_calibration_t *calibration);
void LineTrack_UpdateCalibration(line_track_calibration_t *calibration,
    const uint16_t *raw, size_t raw_count);
void LineTrack_Normalize(const uint16_t *raw, size_t raw_count,
    const line_track_calibration_t *calibration, uint16_t *out_normalized,
    size_t out_count);
void LineTrack_BuildBinary(const uint16_t *normalized, size_t normalized_count,
    uint16_t threshold, uint8_t *out_binary, size_t out_count);
uint8_t LineTrack_BuildBitMask(const uint8_t *binary, size_t binary_count);
int16_t LineTrack_CalculatePositionError(const uint8_t *binary,
    size_t binary_count, int16_t last_error);

void LineTrack_AppInit(void);
void LineTrack_AppTask(void);
bool LineTrack_AppGetSnapshot(line_track_snapshot_t *out_snapshot);
void LineTrack_AppStartCalibration(void);
void LineTrack_AppStopCalibration(void);
void LineTrack_AppSetThreshold(uint16_t threshold);
uint16_t LineTrack_AppGetThreshold(void);

#ifdef __cplusplus
}
#endif

#endif /* LINE_TRACK_APP_H */
