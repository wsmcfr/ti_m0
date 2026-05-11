/*
 * Copyright (c) 2023, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ============ ti_msp_dl_config.c =============
 *  Configured MSPM0 DriverLib module definitions
 *
 *  DO NOT EDIT - This file is generated for the MSPM0G350X
 *  by the SysConfig tool.
 */

#include "ti_msp_dl_config.h"

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform any initialization needed before using any board APIs
 */
SYSCONFIG_WEAK void SYSCFG_DL_init(void)
{
    SYSCFG_DL_initPower();
    SYSCFG_DL_GPIO_init();
    /* Module-Specific Initializations*/
    SYSCFG_DL_SYSCTL_init();
    SYSCFG_DL_UART_PC_init();
    SYSCFG_DL_UART_GYRO_init();
    SYSCFG_DL_DMA_init();
    SYSCFG_DL_SYSTICK_init();
}



SYSCONFIG_WEAK void SYSCFG_DL_initPower(void)
{
    DL_GPIO_reset(GPIOA);
    DL_GPIO_reset(GPIOB);
    DL_UART_Main_reset(UART_PC_INST);
    DL_UART_Main_reset(UART_GYRO_INST);



    DL_GPIO_enablePower(GPIOA);
    DL_GPIO_enablePower(GPIOB);
    DL_UART_Main_enablePower(UART_PC_INST);
    DL_UART_Main_enablePower(UART_GYRO_INST);


    delay_cycles(POWER_STARTUP_DELAY);
}

SYSCONFIG_WEAK void SYSCFG_DL_GPIO_init(void)
{

    
	DL_GPIO_initPeripheralOutputFunctionFeatures(
		 GPIO_UART_PC_IOMUX_TX, GPIO_UART_PC_IOMUX_TX_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_DRIVE_STRENGTH_LOW, DL_GPIO_HIZ_DISABLE);
	DL_GPIO_initPeripheralInputFunctionFeatures(
		 GPIO_UART_PC_IOMUX_RX, GPIO_UART_PC_IOMUX_RX_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    
	DL_GPIO_initPeripheralOutputFunctionFeatures(
		 GPIO_UART_GYRO_IOMUX_TX, GPIO_UART_GYRO_IOMUX_TX_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_DRIVE_STRENGTH_LOW, DL_GPIO_HIZ_DISABLE);
	DL_GPIO_initPeripheralInputFunctionFeatures(
		 GPIO_UART_GYRO_IOMUX_RX, GPIO_UART_GYRO_IOMUX_RX_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalOutput(LED_PORT_LED_BLUE_IOMUX);

    DL_GPIO_clearPins(LED_PORT_PORT, LED_PORT_LED_BLUE_PIN);
    DL_GPIO_enableOutput(LED_PORT_PORT, LED_PORT_LED_BLUE_PIN);

}



SYSCONFIG_WEAK void SYSCFG_DL_SYSCTL_init(void)
{

	//Low Power Mode is configured to be SLEEP0
    DL_SYSCTL_setBORThreshold(DL_SYSCTL_BOR_THRESHOLD_LEVEL_0);

    
	DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_BASE);
	/* Set default configuration */
	DL_SYSCTL_disableHFXT();
	DL_SYSCTL_disableSYSPLL();

}



static const DL_UART_Main_ClockConfig gUART_PCClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gUART_PCConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_PC_init(void)
{
    DL_UART_Main_setClockConfig(UART_PC_INST, (DL_UART_Main_ClockConfig *) &gUART_PCClockConfig);

    DL_UART_Main_init(UART_PC_INST, (DL_UART_Main_Config *) &gUART_PCConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 115200
     *  Actual baud rate: 115211.52
     */
    DL_UART_Main_setOversampling(UART_PC_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(UART_PC_INST, UART_PC_IBRD_32_MHZ_115200_BAUD, UART_PC_FBRD_32_MHZ_115200_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(UART_PC_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_RX |
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
                                 DL_UART_MAIN_INTERRUPT_EOT_DONE |
                                 DL_UART_MAIN_INTERRUPT_RX_TIMEOUT_ERROR);

    /* Configure DMA Receive Event */
    DL_UART_Main_enableDMAReceiveEvent(UART_PC_INST, DL_UART_DMA_INTERRUPT_RX);
    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(UART_PC_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(UART_PC_INST);
    DL_UART_Main_setRXFIFOThreshold(UART_PC_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setTXFIFOThreshold(UART_PC_INST, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

    DL_UART_Main_setRXInterruptTimeout(UART_PC_INST, 15);

    DL_UART_Main_enable(UART_PC_INST);
}

static const DL_UART_Main_ClockConfig gUART_GYROClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gUART_GYROConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_GYRO_init(void)
{
    DL_UART_Main_setClockConfig(UART_GYRO_INST, (DL_UART_Main_ClockConfig *) &gUART_GYROClockConfig);

    DL_UART_Main_init(UART_GYRO_INST, (DL_UART_Main_Config *) &gUART_GYROConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 115200
     *  Actual baud rate: 115211.52
     */
    DL_UART_Main_setOversampling(UART_GYRO_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(UART_GYRO_INST, UART_GYRO_IBRD_32_MHZ_115200_BAUD, UART_GYRO_FBRD_32_MHZ_115200_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(UART_GYRO_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_RX |
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_TX |
                                 DL_UART_MAIN_INTERRUPT_EOT_DONE |
                                 DL_UART_MAIN_INTERRUPT_RX_TIMEOUT_ERROR);

    /* Configure DMA Receive Event */
    DL_UART_Main_enableDMAReceiveEvent(UART_GYRO_INST, DL_UART_DMA_INTERRUPT_RX);
    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(UART_GYRO_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(UART_GYRO_INST);
    DL_UART_Main_setRXFIFOThreshold(UART_GYRO_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setTXFIFOThreshold(UART_GYRO_INST, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

    DL_UART_Main_setRXInterruptTimeout(UART_GYRO_INST, 15);

    DL_UART_Main_enable(UART_GYRO_INST);
}

static const DL_DMA_Config gDMA_PC_RXConfig = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_INCREMENT,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_PC_INST_DMA_TRIGGER_0,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_PC_RX_init(void)
{
    DL_DMA_setTransferSize(DMA, DMA_PC_RX_CHAN_ID, 128);
    DL_DMA_initChannel(DMA, DMA_PC_RX_CHAN_ID , (DL_DMA_Config *) &gDMA_PC_RXConfig);
}
static const DL_DMA_Config gDMA_PC_TXConfig = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_PC_INST_DMA_TRIGGER_1,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_PC_TX_init(void)
{
    DL_DMA_initChannel(DMA, DMA_PC_TX_CHAN_ID , (DL_DMA_Config *) &gDMA_PC_TXConfig);
}
static const DL_DMA_Config gDMA_GYRO_RXConfig = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_INCREMENT,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_GYRO_INST_DMA_TRIGGER_0,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_GYRO_RX_init(void)
{
    DL_DMA_setTransferSize(DMA, DMA_GYRO_RX_CHAN_ID, 128);
    DL_DMA_initChannel(DMA, DMA_GYRO_RX_CHAN_ID , (DL_DMA_Config *) &gDMA_GYRO_RXConfig);
}
static const DL_DMA_Config gDMA_GYRO_TXConfig = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_GYRO_INST_DMA_TRIGGER_1,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_GYRO_TX_init(void)
{
    DL_DMA_initChannel(DMA, DMA_GYRO_TX_CHAN_ID , (DL_DMA_Config *) &gDMA_GYRO_TXConfig);
}
SYSCONFIG_WEAK void SYSCFG_DL_DMA_init(void){
    SYSCFG_DL_DMA_PC_RX_init();
    SYSCFG_DL_DMA_PC_TX_init();
    SYSCFG_DL_DMA_GYRO_RX_init();
    SYSCFG_DL_DMA_GYRO_TX_init();
}


SYSCONFIG_WEAK void SYSCFG_DL_SYSTICK_init(void)
{
    /*
     * Initializes the SysTick period to 1.00 ms,
     * enables the interrupt, and starts the SysTick Timer
     */
    DL_SYSTICK_config(32000);
}

