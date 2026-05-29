/**
 * @file    gray_sensor_driver.c
 * @brief   感为无 MCU 八路灰度模块底层驱动实现。
 *
 * @details 参考例程通过 3 根地址线反相选通 8 路传感器，再读取同一 ADC 引脚。
 *          本实现沿用该选通逻辑，并对每路做少量多次采样平均以降低噪声。
 */

#include "gray_sensor_driver.h"

#include "ti_msp_dl_config.h"

/* 每路采样次数。次数越高越稳，但会增加调度任务耗时。 */
#define GRAY_SENSOR_SAMPLE_COUNT        (8U)

/* 地址线切换后的短等待计数，用于给模拟开关和传感器输出一点稳定时间。 */
#define GRAY_SENSOR_SETTLE_LOOP_COUNT   (120U)

/* 单次 ADC/DMA 等待保护计数，避免硬件异常时永久阻塞调度器。 */
#define GRAY_SENSOR_ADC_WAIT_TIMEOUT    (60000UL)

/* DMA 单点采样结果缓冲，ADC0 MEM0 通过 DMA_CH6 搬运到这里。 */
static volatile uint16_t s_gray_adc_dma_value = 0U;

/* true 表示 ADC/DMA 基础搬运地址已经配置过。 */
static bool s_gray_driver_initialized = false;

/* true 表示当前已经启动一次非阻塞 ADC/DMA 单点采样。 */
static bool s_gray_sample_busy = false;

static void GraySensor_DriverDelayLoops(uint32_t loops);
static bool GraySensor_DriverSampleOnce(uint16_t *out_value);
static void GraySensor_DriverStopSampleHardware(void);

/**
 * @brief  执行一个短忙等待。
 *
 * @note   地址线切换后传感器输出不会完全瞬时稳定。这里用短循环代替毫秒延时，
 *         避免灰度周期任务引入明显阻塞。
 *
 * @param  loops 循环次数。
 * @return 无。
 */
static void GraySensor_DriverDelayLoops(uint32_t loops)
{
    /* volatile 防止编译器把等待循环整体优化掉。 */
    volatile uint32_t counter = loops;

    /* 递减到 0 完成短等待。 */
    while (counter > 0UL)
    {
        /* 每次循环消耗一个计数。 */
        counter--;
    }
}

/**
 * @brief  触发并读取一次 ADC0 采样。
 *
 * @note   优先使用 SysConfig 配好的 ADC DMA 通道；如果 DMA 等待超时，
 *         返回 false 让上层保留上一组有效数据。
 *
 * @param  out_value 输出 ADC 值。
 * @return true 表示采样成功；false 表示参数无效或等待超时。
 */
static bool GraySensor_DriverSampleOnce(uint16_t *out_value)
{
    /* 等待 DMA 完成或 ADC 结果中断的保护计数。 */
    uint32_t wait_count = GRAY_SENSOR_ADC_WAIT_TIMEOUT;

    /* 输出指针不能为空。 */
    if (out_value == NULL)
    {
        /* 参数无效，不能写出结果。 */
        return false;
    }

    /* 每次采样前清空旧结果，避免超时时误用上一次 DMA 值。 */
    s_gray_adc_dma_value = 0U;
    /* 停止 DMA 通道，重新写入单次采样的源、目的和长度。 */
    DL_DMA_disableChannel(DMA, DMA_GRAY_CHAN_ID);
    /* 设置 DMA 源地址为 ADC0 MEM0 结果寄存器地址。 */
    DL_DMA_setSrcAddr(DMA, DMA_GRAY_CHAN_ID,
        DL_ADC12_getMemResultAddress(ADC_GRAY_INST, ADC_GRAY_ADCMEM_0));
    /* 设置 DMA 目的地址为本驱动的单点采样变量。 */
    DL_DMA_setDestAddr(DMA, DMA_GRAY_CHAN_ID, (uint32_t)&s_gray_adc_dma_value);
    /* 本次只搬运 1 个半字结果。 */
    DL_DMA_setTransferSize(DMA, DMA_GRAY_CHAN_ID, 1U);
    /* 清除 ADC DMA 完成标志，避免旧中断状态影响本次判断。 */
    DL_ADC12_clearInterruptStatus(ADC_GRAY_INST, DL_ADC12_INTERRUPT_DMA_DONE);
    /* 开启 DMA 通道，等待 ADC 结果触发搬运。 */
    DL_DMA_enableChannel(DMA, DMA_GRAY_CHAN_ID);
    /* 启动一次 ADC 转换。 */
    DL_ADC12_startConversion(ADC_GRAY_INST);

    /* 轮询 ADC DMA 完成标志，使用超时保护防止硬件异常时死等。 */
    while ((DL_ADC12_getRawInterruptStatus(ADC_GRAY_INST,
                DL_ADC12_INTERRUPT_DMA_DONE) == 0U) &&
           (wait_count > 0UL))
    {
        /* 每轮消耗一个等待计数。 */
        wait_count--;
    }

    /* 停止 ADC 转换，确保下一次采样重新从明确状态启动。 */
    DL_ADC12_stopConversion(ADC_GRAY_INST);
    /* 关闭 DMA 通道，避免后续非预期 ADC 事件改写缓冲。 */
    DL_DMA_disableChannel(DMA, DMA_GRAY_CHAN_ID);

    /* 等待超时时返回失败，不更新调用者结果。 */
    if (wait_count == 0UL)
    {
        /* 超时通常表示 ADC/DMA 配置或硬件触发异常。 */
        return false;
    }

    /* 清除本次 DMA 完成标志，为下一次采样做准备。 */
    DL_ADC12_clearInterruptStatus(ADC_GRAY_INST, DL_ADC12_INTERRUPT_DMA_DONE);
    /* 返回 DMA 搬运出的 ADC 值。 */
    *out_value = s_gray_adc_dma_value;
    /* 采样成功。 */
    return true;
}

/**
 * @brief  停止灰度 ADC/DMA 单点采样硬件状态。
 *
 * @note   非阻塞和阻塞采样共用该收尾流程，确保 ADC 转换和 DMA 通道不会残留运行。
 *
 * @param  无。
 * @return 无。
 */
static void GraySensor_DriverStopSampleHardware(void)
{
    /* 停止 ADC 转换，确保下一次采样重新从明确状态启动。 */
    DL_ADC12_stopConversion(ADC_GRAY_INST);
    /* 关闭 DMA 通道，避免后续非预期 ADC 事件改写缓冲。 */
    DL_DMA_disableChannel(DMA, DMA_GRAY_CHAN_ID);
}

/**
 * @brief  初始化灰度传感器驱动。
 *
 * @note   SysConfig 已完成 ADC0、DMA_CH6 和地址 GPIO 基础配置。
 *         本函数补充 DMA 源/目的地址，并默认选中第 0 路通道。
 *
 * @param  无。
 * @return 无。
 */
void GraySensor_DriverInit(void)
{
    /* 默认清空三根地址线，后续再按反相选通逻辑选择第 0 路。 */
    DL_GPIO_clearPins(GRAY_ADDR_PORT_PORT,
        GRAY_ADDR_PORT_GRAY_AD0_PIN |
        GRAY_ADDR_PORT_GRAY_AD1_PIN |
        GRAY_ADDR_PORT_GRAY_AD2_PIN);
    /* 预先设置 DMA 源地址为 ADC 结果寄存器。 */
    DL_DMA_setSrcAddr(DMA, DMA_GRAY_CHAN_ID,
        DL_ADC12_getMemResultAddress(ADC_GRAY_INST, ADC_GRAY_ADCMEM_0));
    /* 预先设置 DMA 目的地址为单点采样变量。 */
    DL_DMA_setDestAddr(DMA, DMA_GRAY_CHAN_ID, (uint32_t)&s_gray_adc_dma_value);
    /* 单次采样只需要 1 个 half-word。 */
    DL_DMA_setTransferSize(DMA, DMA_GRAY_CHAN_ID, 1U);
    /* 初始化时没有挂起的非阻塞采样。 */
    s_gray_sample_busy = false;
    /* 标记驱动已经配置过基础地址。 */
    s_gray_driver_initialized = true;
    /* 默认选中第 0 路，给硬件一个确定状态。 */
    (void)GraySensor_DriverSelectChannel(0U);
}

/**
 * @brief  选择灰度模块的某一路通道。
 *
 * @note   参考例程对地址位使用取反逻辑：bit 为 0 时输出高电平，bit 为 1 时输出低电平。
 *
 * @param  channel 通道号，0~7 有效。
 * @return true 表示通道号有效且已写地址线；false 表示通道号非法。
 */
bool GraySensor_DriverSelectChannel(uint8_t channel)
{
    /* 非阻塞采样进行中不能切换模拟通道，否则 ADC 结果会和通道号不匹配。 */
    if (s_gray_sample_busy == true)
    {
        /* 当前采样尚未完成，拒绝切换通道。 */
        return false;
    }

    /* 通道号必须在 8 路范围内。 */
    if (channel >= GRAY_SENSOR_CHANNEL_COUNT)
    {
        /* 非法通道不能选通。 */
        return false;
    }

    /* 地址 bit0：参考例程 Switch_Address_0(!(i & 0x01))。 */
    if ((channel & 0x01U) == 0U)
    {
        /* bit 为 0 时输出高电平。 */
        DL_GPIO_setPins(GRAY_ADDR_PORT_PORT, GRAY_ADDR_PORT_GRAY_AD0_PIN);
    }
    else
    {
        /* bit 为 1 时输出低电平。 */
        DL_GPIO_clearPins(GRAY_ADDR_PORT_PORT, GRAY_ADDR_PORT_GRAY_AD0_PIN);
    }

    /* 地址 bit1：参考例程 Switch_Address_1(!(i & 0x02))。 */
    if ((channel & 0x02U) == 0U)
    {
        /* bit 为 0 时输出高电平。 */
        DL_GPIO_setPins(GRAY_ADDR_PORT_PORT, GRAY_ADDR_PORT_GRAY_AD1_PIN);
    }
    else
    {
        /* bit 为 1 时输出低电平。 */
        DL_GPIO_clearPins(GRAY_ADDR_PORT_PORT, GRAY_ADDR_PORT_GRAY_AD1_PIN);
    }

    /* 地址 bit2：参考例程 Switch_Address_2(!(i & 0x04))。 */
    if ((channel & 0x04U) == 0U)
    {
        /* bit 为 0 时输出高电平。 */
        DL_GPIO_setPins(GRAY_ADDR_PORT_PORT, GRAY_ADDR_PORT_GRAY_AD2_PIN);
    }
    else
    {
        /* bit 为 1 时输出低电平。 */
        DL_GPIO_clearPins(GRAY_ADDR_PORT_PORT, GRAY_ADDR_PORT_GRAY_AD2_PIN);
    }

    /* 等待模拟通道切换后的输出稳定。 */
    GraySensor_DriverDelayLoops(GRAY_SENSOR_SETTLE_LOOP_COUNT);
    /* 返回选通成功。 */
    return true;
}

/**
 * @brief  读取当前已选通灰度通道的一次 ADC 原始值。
 *
 * @note   本函数不切换地址线，只触发一次 ADC/DMA 单点采样。
 *         App 层可先调用 GraySensor_DriverSelectChannel()，再分多次调度调用本函数，
 *         从而把一整组采样拆散到多个主循环周期中。
 *
 * @param  out_value 输出单次 ADC 值。
 * @return true 表示采样成功；false 表示参数错误或 ADC/DMA 超时。
 */
bool GraySensor_DriverSampleSelected(uint16_t *out_value)
{
    /* 输出指针不能为空。 */
    if (out_value == NULL)
    {
        /* 参数错误时无法写出结果。 */
        return false;
    }

    /* 如果上层未显式初始化，先补一次驱动初始化。 */
    if (s_gray_driver_initialized == false)
    {
        /* 确保 DMA 地址和默认地址线状态有效。 */
        GraySensor_DriverInit();
    }

    /* 非阻塞采样未完成时不能启动阻塞采样，避免两条路径同时改 DMA 状态。 */
    if (s_gray_sample_busy == true)
    {
        /* 当前已有挂起采样。 */
        return false;
    }

    /* 只采样当前地址线选中的通道，不额外改变通道状态。 */
    return GraySensor_DriverSampleOnce(out_value);
}

/**
 * @brief  启动当前已选通通道的一次非阻塞 ADC 采样。
 *
 * @note   本函数只配置 DMA 并启动 ADC，不等待采样完成。调用者需要后续反复调用
 *         GraySensor_DriverPollSampleSelected() 获取结果。
 *
 * @param  无。
 * @return true 表示采样已经启动；false 表示已有采样进行中或硬件状态异常。
 */
bool GraySensor_DriverStartSampleSelected(void)
{
    /* 如果上层未显式初始化，先补一次驱动初始化。 */
    if (s_gray_driver_initialized == false)
    {
        /* 确保 DMA 地址和默认地址线状态有效。 */
        GraySensor_DriverInit();
    }

    /* 已经存在挂起采样时不能重复启动，避免覆盖 DMA 状态。 */
    if (s_gray_sample_busy == true)
    {
        /* 当前采样还没被 Poll 取走。 */
        return false;
    }

    /* 每次采样前清空旧结果，避免异常路径误用上一次 DMA 值。 */
    s_gray_adc_dma_value = 0U;
    /* 停止 DMA 通道，重新写入单次采样的源、目的和长度。 */
    DL_DMA_disableChannel(DMA, DMA_GRAY_CHAN_ID);
    /* 设置 DMA 源地址为 ADC0 MEM0 结果寄存器地址。 */
    DL_DMA_setSrcAddr(DMA, DMA_GRAY_CHAN_ID,
        DL_ADC12_getMemResultAddress(ADC_GRAY_INST, ADC_GRAY_ADCMEM_0));
    /* 设置 DMA 目的地址为本驱动的单点采样变量。 */
    DL_DMA_setDestAddr(DMA, DMA_GRAY_CHAN_ID, (uint32_t)&s_gray_adc_dma_value);
    /* 本次只搬运 1 个半字结果。 */
    DL_DMA_setTransferSize(DMA, DMA_GRAY_CHAN_ID, 1U);
    /* 清除 ADC DMA 完成标志，避免旧中断状态影响本次判断。 */
    DL_ADC12_clearInterruptStatus(ADC_GRAY_INST, DL_ADC12_INTERRUPT_DMA_DONE);
    /* 开启 DMA 通道，等待 ADC 结果触发搬运。 */
    DL_DMA_enableChannel(DMA, DMA_GRAY_CHAN_ID);
    /* 标记采样忙，再启动 ADC 转换，后续 Poll 根据该标志判断状态。 */
    s_gray_sample_busy = true;
    /* 启动一次 ADC 转换。 */
    DL_ADC12_startConversion(ADC_GRAY_INST);

    /* 非阻塞采样已经启动。 */
    return true;
}

/**
 * @brief  查询当前非阻塞 ADC 采样是否完成。
 *
 * @note   READY 时会停止 ADC/DMA、清除完成标志并写出采样值；BUSY 时不阻塞。
 *
 * @param  out_value 输出 ADC 值，READY 状态下必须有效。
 * @return 当前采样状态。
 */
gray_sensor_sample_status_t GraySensor_DriverPollSampleSelected(uint16_t *out_value)
{
    /* 没有挂起采样时返回空闲，提示上层先启动采样。 */
    if (s_gray_sample_busy == false)
    {
        /* 当前没有可查询的采样。 */
        return GRAY_SENSOR_SAMPLE_STATUS_IDLE;
    }

    /* 采样尚未完成时立即返回，不占用调度器时间。 */
    if (DL_ADC12_getRawInterruptStatus(ADC_GRAY_INST,
            DL_ADC12_INTERRUPT_DMA_DONE) == 0U)
    {
        /* DMA 完成标志未到，调用者后续周期再查。 */
        return GRAY_SENSOR_SAMPLE_STATUS_BUSY;
    }

    /* READY 状态需要有效输出指针，否则无法交付结果。 */
    if (out_value == NULL)
    {
        /* 参数错误时中止本次采样，避免 busy 永久保持。 */
        GraySensor_DriverStopSampleHardware();
        s_gray_sample_busy = false;
        DL_ADC12_clearInterruptStatus(ADC_GRAY_INST, DL_ADC12_INTERRUPT_DMA_DONE);
        return GRAY_SENSOR_SAMPLE_STATUS_ERROR;
    }

    /* 停止 ADC/DMA，让下一次采样从干净状态启动。 */
    GraySensor_DriverStopSampleHardware();
    /* 清除本次 DMA 完成标志，为下一次采样做准备。 */
    DL_ADC12_clearInterruptStatus(ADC_GRAY_INST, DL_ADC12_INTERRUPT_DMA_DONE);
    /* 采样结果已经稳定，写给调用者。 */
    *out_value = s_gray_adc_dma_value;
    /* 释放非阻塞采样忙标志。 */
    s_gray_sample_busy = false;

    /* 告诉上层采样值已输出。 */
    return GRAY_SENSOR_SAMPLE_STATUS_READY;
}

/**
 * @brief  中止当前非阻塞 ADC 采样。
 *
 * @note   上层状态机超时或复位半帧时调用，防止 ADC/DMA 保持在旧采样状态。
 *
 * @param  无。
 * @return 无。
 */
void GraySensor_DriverAbortSampleSelected(void)
{
    /* 停止当前 ADC/DMA 采样硬件路径。 */
    GraySensor_DriverStopSampleHardware();
    /* 清除完成标志，避免下一轮采样被旧状态误判为完成。 */
    DL_ADC12_clearInterruptStatus(ADC_GRAY_INST, DL_ADC12_INTERRUPT_DMA_DONE);
    /* 释放忙标志，允许上层重新启动采样。 */
    s_gray_sample_busy = false;
}

/**
 * @brief  读取单路灰度传感器 ADC 平均值。
 *
 * @note   先选通目标通道，再连续采样 GRAY_SENSOR_SAMPLE_COUNT 次并求平均。
 *
 * @param  channel   通道号，0~7 有效。
 * @param  out_value 输出 ADC 平均值。
 * @return true 表示读取成功；false 表示参数错误或 ADC/DMA 超时。
 */
bool GraySensor_DriverReadChannel(uint8_t channel, uint16_t *out_value)
{
    /* 保存多次采样的累加和。 */
    uint32_t sum = 0U;

    /* 输出指针不能为空。 */
    if (out_value == NULL)
    {
        /* 参数错误时无法写出结果。 */
        return false;
    }

    /* 如果上层未显式初始化，先补一次驱动初始化。 */
    if (s_gray_driver_initialized == false)
    {
        /* 确保 DMA 地址和默认地址线状态有效。 */
        GraySensor_DriverInit();
    }

    /* 选通目标通道，通道非法时直接失败。 */
    if (GraySensor_DriverSelectChannel(channel) == false)
    {
        /* 非法通道号。 */
        return false;
    }

    /* 对同一路做多次采样平均，降低单次 ADC 抖动影响。 */
    for (uint8_t i = 0U; i < GRAY_SENSOR_SAMPLE_COUNT; i++)
    {
        /* 保存本次单点采样结果。 */
        uint16_t sample = 0U;

        /* 任意一次采样失败则整路读取失败，避免输出半可信数据。 */
        if (GraySensor_DriverSampleOnce(&sample) == false)
        {
            /* ADC/DMA 超时或参数异常。 */
            return false;
        }

        /* 累加本次采样值，后续统一求平均。 */
        sum += sample;
    }

    /* 输出整数平均值。 */
    *out_value = (uint16_t)(sum / GRAY_SENSOR_SAMPLE_COUNT);
    /* 读取成功。 */
    return true;
}

/**
 * @brief  读取全部 8 路灰度传感器值。
 *
 * @note   为了匹配参考例程 Direction=1 的方向，物理通道 i 的值写入 out_values[7-i]。
 *
 * @param  out_values 输出数组。
 * @param  max_count  输出数组最多可容纳元素数量，至少需要 8。
 * @return true 表示 8 路全部读取成功；false 表示参数错误或任一路采样失败。
 */
bool GraySensor_DriverReadAll(uint16_t *out_values, uint8_t max_count)
{
    /* 输出数组不能为空且容量必须能容纳 8 路。 */
    if ((out_values == NULL) || (max_count < GRAY_SENSOR_CHANNEL_COUNT))
    {
        /* 参数无效时不能采样。 */
        return false;
    }

    /* 逐个物理通道读取，并按参考例程 Direction=1 反向排列输出。 */
    for (uint8_t channel = 0U; channel < GRAY_SENSOR_CHANNEL_COUNT; channel++)
    {
        /* 保存当前物理通道采样结果。 */
        uint16_t value = 0U;
        /* 输出索引按 7-channel 反向映射。 */
        uint8_t output_index = (uint8_t)((GRAY_SENSOR_CHANNEL_COUNT - 1U) - channel);

        /* 读取当前物理通道，失败时中止整组读取。 */
        if (GraySensor_DriverReadChannel(channel, &value) == false)
        {
            /* 任一路失败都认为整组快照无效。 */
            return false;
        }

        /* 写入反向后的逻辑通道位置。 */
        out_values[output_index] = value;
    }

    /* 8 路全部采样成功。 */
    return true;
}
