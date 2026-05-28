/**
 * @file    key_driver.h
 * @brief   四按键底层 GPIO 读取接口。
 */

#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 工程使用 4 个独立按键。 */
#define KEY_DRIVER_COUNT        (4U)

/* 按键位掩码，按 K1~K4 对应 bit0~bit3。 */
#define KEY_DRIVER_MASK_K1      (0x01U)
#define KEY_DRIVER_MASK_K2      (0x02U)
#define KEY_DRIVER_MASK_K3      (0x04U)
#define KEY_DRIVER_MASK_K4      (0x08U)

void Key_DriverInit(void);
uint8_t Key_DriverReadRawMask(void);

#ifdef __cplusplus
}
#endif

#endif /* KEY_DRIVER_H */
