/**
 * @file    motor_app.h
 * @brief   电机驱动板应用层接口，封装 Modbus RTU 命令发送和编码器缓存。
 */

#ifndef MOTOR_APP_H
#define MOTOR_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "motor_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 电机通道识别模式的点动速度。数值较小，方便架空车轮时观察通道和方向。 */
#define MOTOR_APP_IDENTIFY_JOG_SPEED        (300)

/* 电机通道识别模式中“没有有效目标通道”的公开标记。 */
#define MOTOR_APP_IDENTIFY_NO_MOTOR         (0xFFU)

/**
 * @brief  电机应用层保存的最新运行状态。
 *
 * @note   desired_speed 是最近一次成功请求发送的目标速度；
 *         encoder 是最近一次从电机驱动板返回帧解析出的编码器值。
 */
typedef struct
{
    int16_t desired_speed[MOTOR_PROTOCOL_MOTOR_COUNT]; /* 四路目标速度，按 A/B/C/D 顺序保存。 */
    int16_t encoder[MOTOR_PROTOCOL_MOTOR_COUNT];       /* 四路编码器最新值，按 A/B/C/D 顺序保存。 */
    bool encoder_valid[MOTOR_PROTOCOL_MOTOR_COUNT];    /* true 表示对应编码器值已经解析过。 */
    uint32_t tx_count;                                 /* 成功发送的电机命令帧数量。 */
    uint32_t rx_count;                                 /* 成功解析的电机返回帧数量。 */
    uint32_t crc_error_count;                          /* 收到但 CRC 或格式错误的返回帧数量。 */
    uint32_t last_rx_ms;                               /* 最近一次成功解析返回帧的系统 tick。 */
    uint8_t identify_key_mask;                         /* 点动识别最近读取到的 K1~K4 稳定按键掩码。 */
    uint8_t identify_selected_motor;                   /* 点动识别选中的电机索引，0xFF 表示无键或多键。 */
    bool identify_last_send_ok;                        /* 最近一次点动状态变化时速度帧是否发送成功。 */
} motor_app_status_t;

void Motor_AppInit(void);
void Motor_AppTask(void);
bool Motor_AppEnableClosedLoop(void);
bool Motor_AppSetSpeeds(const int16_t speeds[MOTOR_PROTOCOL_MOTOR_COUNT]);
bool Motor_AppSetSpeed4(int16_t motor_a, int16_t motor_b, int16_t motor_c, int16_t motor_d);
bool Motor_AppSetSpeed(uint8_t motor_index, int16_t speed);
bool Motor_AppSetSpeed2(uint8_t first_motor_index, int16_t first_speed,
    uint8_t second_motor_index, int16_t second_speed);
bool Motor_AppStop(uint8_t motor_index);
bool Motor_AppSetPid(const motor_protocol_pid_t pid[MOTOR_PROTOCOL_MOTOR_COUNT]);
bool Motor_AppRequestEncoder(uint8_t motor_index);
bool Motor_AppGetStatus(motor_app_status_t *out_status);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_APP_H */
