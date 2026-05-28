/**
 * @file    oled_driver.h
 * @brief   SSD1306 OLED 底层驱动接口。
 */

#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OLED 像素宽度和高度。 */
#define OLED_DRIVER_WIDTH           (128U)
#define OLED_DRIVER_HEIGHT          (64U)

/* SSD1306 以 8 像素为一页，64 像素高度共 8 页。 */
#define OLED_DRIVER_PAGE_COUNT      (8U)

void Oled_DriverInit(void);
bool Oled_DriverWriteCommand(uint8_t command);
bool Oled_DriverWriteData(uint8_t data);
bool Oled_DriverClear(void);
bool Oled_DriverSetPosition(uint8_t x, uint8_t page);
bool Oled_DriverShowChar(uint8_t x, uint8_t page, char ch);
bool Oled_DriverShowString(uint8_t x, uint8_t page, const char *text);
bool Oled_DriverShowSignedNumber(uint8_t x, uint8_t page, int32_t value, uint8_t width);
bool Oled_DriverDisplayOn(void);
bool Oled_DriverDisplayOff(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_DRIVER_H */
