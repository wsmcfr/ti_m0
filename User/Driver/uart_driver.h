/**
 * @file    uart_driver.h
 * @brief   MSPM0G3507 多串口 DMA 驱动层接口。
 *
 * @details 本驱动只负责 UART0/UART1/UART3 的硬件收发、DMA 缓冲和中断事件归集。
 *          上层不直接访问 UART 寄存器，而是通过本文件读取一包空闲中断切分后的数据。
 */

#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UART DMA 接收缓冲长度。SysConfig 中 RX DMA transferSize 也配置为 128，二者需要保持一致。 */
#define UART_DRIVER_RX_DMA_BUFFER_SIZE  (128U)

/**
 * @brief  工程中使用的串口逻辑编号。
 *
 * @note   PC 串口用于连接电脑串口助手/上位机，GYRO 串口用于连接陀螺仪模块，
 *         MOTOR 串口用于连接电机驱动板。
 */
typedef enum
{
    UART_DRIVER_PORT_PC = 0,     /* UART0，PA10=TX，PA11=RX，对接电脑。 */
    UART_DRIVER_PORT_GYRO,       /* UART1，PA8=TX，PA9=RX，对接陀螺仪。 */
    UART_DRIVER_PORT_MOTOR,      /* UART3，PB2=TX，PB3=RX，对接电机驱动板。 */
    UART_DRIVER_PORT_COUNT       /* 串口数量，只用于数组长度检查。 */
} uart_driver_port_t;

/**
 * @brief  UART 驱动运行统计。
 *
 * @note   这些计数主要用于联调时判断是否丢包、是否发送超时、是否发生接收异常。
 */
typedef struct
{
    uint32_t rx_packet_count;    /* 已经由空闲中断或 RX DMA 满缓冲切分出的接收包数量。 */
    uint32_t rx_overflow_count;  /* 上层未及时取走上一包时，又收到新包的覆盖次数。 */
    uint32_t rx_error_count;     /* UART 接收异常或非法端口访问次数。 */
    uint32_t tx_packet_count;    /* 通过 DMA 成功发送完成的包数量。 */
    uint32_t tx_timeout_count;   /* 等待 DMA_DONE_TX 或 EOT_DONE 超时次数。 */
    uint32_t tx_reject_count;    /* 非阻塞发送因忙、长度过大或参数非法被拒绝的次数。 */
} uart_driver_stats_t;

void Uart_DriverInit(void);
bool Uart_DriverWrite(uart_driver_port_t port, const uint8_t *data, uint16_t length);
bool Uart_DriverTryWrite(uart_driver_port_t port, const uint8_t *data, uint16_t length);
bool Uart_DriverIsTxBusy(uart_driver_port_t port);
bool Uart_DriverWriteByteBlocking(uart_driver_port_t port, uint8_t data);
uint16_t Uart_DriverReadPacket(uart_driver_port_t port, uint8_t *out_data, uint16_t max_length);
uint16_t Uart_DriverGetPendingLength(uart_driver_port_t port);
void Uart_DriverClearRx(uart_driver_port_t port);
void Uart_DriverGetStats(uart_driver_port_t port, uart_driver_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* UART_DRIVER_H */
