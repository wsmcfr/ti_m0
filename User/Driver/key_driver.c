/**
 * @file    key_driver.c
 * @brief   四按键底层 GPIO 读取实现。
 *
 * @details 按键硬件使用内部上拉，按下时 GPIO 读到低电平。
 */

#include "key_driver.h"

#include "ti_msp_dl_config.h"

/**
 * @brief  初始化按键驱动层。
 *
 * @note   SysConfig 已经完成四个按键输入和内部上拉配置，本函数保留为分层初始化入口。
 *
 * @param  无。
 * @return 无。
 */
void Key_DriverInit(void)
{
    /* 按键 GPIO 模式由 SYSCFG_DL_GPIO_init() 生成，这里不重复配置寄存器。 */
}

/**
 * @brief  读取四个按键的原始按下状态。
 *
 * @note   返回值 bit0~bit3 对应 K1~K4；按下为 1，松开为 0。
 *
 * @param  无。
 * @return 四按键原始按下掩码。
 */
uint8_t Key_DriverReadRawMask(void)
{
    /* 保存读取到的按键掩码。 */
    uint8_t mask = 0U;

    /* K1 位于 PA7，内部上拉，低电平表示按下。 */
    if ((DL_GPIO_readPins(KEY_PORT_KEY_K1_PORT, KEY_PORT_KEY_K1_PIN) &
            KEY_PORT_KEY_K1_PIN) == 0U)
    {
        /* 设置 K1 对应 bit。 */
        mask |= KEY_DRIVER_MASK_K1;
    }

    /* K2 位于 PB10，内部上拉，低电平表示按下。 */
    if ((DL_GPIO_readPins(KEY_PORT_KEY_K2_PORT, KEY_PORT_KEY_K2_PIN) &
            KEY_PORT_KEY_K2_PIN) == 0U)
    {
        /* 设置 K2 对应 bit。 */
        mask |= KEY_DRIVER_MASK_K2;
    }

    /* K3 位于 PB11，内部上拉，低电平表示按下。 */
    if ((DL_GPIO_readPins(KEY_PORT_KEY_K3_PORT, KEY_PORT_KEY_K3_PIN) &
            KEY_PORT_KEY_K3_PIN) == 0U)
    {
        /* 设置 K3 对应 bit。 */
        mask |= KEY_DRIVER_MASK_K3;
    }

    /* K4 位于 PB14，内部上拉，低电平表示按下。 */
    if ((DL_GPIO_readPins(KEY_PORT_KEY_K4_PORT, KEY_PORT_KEY_K4_PIN) &
            KEY_PORT_KEY_K4_PIN) == 0U)
    {
        /* 设置 K4 对应 bit。 */
        mask |= KEY_DRIVER_MASK_K4;
    }

    /* 返回四个按键的原始按下状态。 */
    return mask;
}
