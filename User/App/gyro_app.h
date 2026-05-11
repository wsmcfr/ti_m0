/**
 * @file    gyro_app.h
 * @brief   陀螺仪应用层接口，负责协议解析、数据缓存和配置命令流程。
 */

#ifndef GYRO_APP_H
#define GYRO_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "gyro_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  陀螺仪应用层保存的最新数据。
 *
 * @note   has_xxx 标志表示对应物理量至少成功解析过一次。
 */
typedef struct
{
    bool has_angular_velocity_z;       /* true 表示 angular_velocity_z_dps 已经有效。 */
    bool has_yaw_z;                    /* true 表示 yaw_z_deg 已经有效。 */
    float angular_velocity_z_dps;      /* Z 轴角速度，单位 deg/s。 */
    float yaw_z_deg;                   /* Z 轴航向角，单位 deg。 */
    int16_t raw_angular_velocity_z;    /* Z 轴角速度原始 short 值。 */
    int16_t raw_yaw_z;                 /* Z 轴航向角原始 short 值。 */
    uint32_t last_update_ms;           /* 最近一次成功解析数据的系统 tick。 */
    uint32_t parsed_packet_count;      /* 成功解析出有效帧的 DMA 空闲包次数。 */
} gyro_app_data_t;

void Gyro_AppInit(void);
void Gyro_AppTask(void);
void Gyro_AppResetParser(void);
bool Gyro_AppGetLatest(gyro_app_data_t *out_data);
float Gyro_AppGetYaw(void);
float Gyro_AppGetGyroZ(void);

bool Gyro_AppSendRawCommand(const uint8_t command[GYRO_PROTOCOL_COMMAND_SIZE]);
bool Gyro_AppSendUnlock(void);
bool Gyro_AppSaveConfig(void);
bool Gyro_AppReboot(void);
bool Gyro_AppRestoreFactory(void);
bool Gyro_AppYawZero(void);
bool Gyro_AppAutoBiasBlocking(void);
bool Gyro_AppReadBiasStatus(void);
bool Gyro_AppSetOutputRate(gyro_protocol_rate_t rate, bool save_after_set);
bool Gyro_AppSetBaud(gyro_protocol_baud_t baud, bool save_after_set);
bool Gyro_AppStartScaleFactor(void);
bool Gyro_AppStopScaleFactor(void);

#ifdef __cplusplus
}
#endif

#endif /* GYRO_APP_H */
