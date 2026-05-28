/**
 * @file    uart_app.c
 * @brief   UART 应用层实现，提供 printf 重定向和多路串口的业务级收发接口。
 *
 * @details UART0 连接电脑，主要用于日志和上位机输出；
 *          UART1 连接陀螺仪，主要用于接收姿态数据和发送配置命令；
 *          UART3 连接电机驱动板，主要用于发送 Modbus RTU 命令和接收编码器返回帧。
 */

#include "uart_app.h"

#include <stdarg.h>
#include <stdio.h>

/* 电脑发来的数据如果暂时不处理，也要定期取走，避免 Driver 层 ready 缓冲一直被覆盖。 */
static uint8_t s_pc_rx_discard_buffer[UART_DRIVER_RX_DMA_BUFFER_SIZE];

/**
 * @brief  初始化 UART 应用层。
 *
 * @note   这里调用 Driver 层启动各路 UART RX DMA，并输出一条启动日志。
 *         启动日志使用 DMA 批量发送，如果电脑端未连接不会影响 MCU 后续运行。
 *
 * @param  无。
 * @return 无。
 */
void Uart_AppInit(void)
{
    /* 初始化 UART 驱动层，启动各路 UART 的 DMA 接收和中断处理。 */
    Uart_DriverInit();
    /* 输出启动日志；强制转 void 是因为这里只做提示，不让日志失败影响启动流程。 */
    (void)my_printf("\r\n[UART] UART0=PC, UART1=GYRO, UART3=MOTOR DMA ready\r\n");
}

/**
 * @brief  UART 应用层周期任务。
 *
 * @note   当前电脑串口暂不做命令解析。任务只把 PC RX 的空闲包取走，
 *         防止用户误发数据后 Driver 层持续记录 ready 覆盖。
 *
 * @param  无。
 * @return 无。
 */
void Uart_AppTask(void)
{
    /* 读取并丢弃 PC 串口已切出的数据包，防止未处理命令长期占用 ready 缓冲。 */
    (void)Uart_AppReadPcPacket(s_pc_rx_discard_buffer,
        (uint16_t)sizeof(s_pc_rx_discard_buffer));
}

/**
 * @brief  通过电脑串口 UART0 发送一段数据。
 *
 * @note   该接口内部走 TX DMA，并在 DMA_DONE_TX + EOT_DONE 后返回。
 *
 * @param  data   待发送数据地址。
 * @param  length 待发送字节数。
 * @return true 表示发送完成；false 表示参数错误或发送超时。
 */
bool Uart_AppSendToPc(const uint8_t *data, uint16_t length)
{
    /* 把电脑串口发送请求转交给驱动层 UART0/PC 端口。 */
    return Uart_DriverWrite(UART_DRIVER_PORT_PC, data, length);
}

/**
 * @brief  通过陀螺仪串口 UART1 发送一段数据。
 *
 * @note   陀螺仪配置命令都是 5 字节，本接口仍保留 length 参数，方便后续扩展。
 *
 * @param  data   待发送数据地址。
 * @param  length 待发送字节数。
 * @return true 表示发送完成；false 表示参数错误或发送超时。
 */
bool Uart_AppSendToGyro(const uint8_t *data, uint16_t length)
{
    /* 把陀螺仪串口发送请求转交给驱动层 UART1/GYRO 端口。 */
    return Uart_DriverWrite(UART_DRIVER_PORT_GYRO, data, length);
}

/**
 * @brief  通过电机串口 UART3 发送一段数据。
 *
 * @note   电机驱动板使用 Modbus RTU 帧，本接口只负责字节发送，不解释协议含义。
 *
 * @param  data   待发送数据地址。
 * @param  length 待发送字节数。
 * @return true 表示发送完成；false 表示参数错误或发送超时。
 */
bool Uart_AppSendToMotor(const uint8_t *data, uint16_t length)
{
    /* 把电机串口发送请求转交给驱动层 UART3/MOTOR 端口。 */
    return Uart_DriverWrite(UART_DRIVER_PORT_MOTOR, data, length);
}

/**
 * @brief  读取电脑串口 UART0 的一包接收数据。
 *
 * @note   “一包”由 UART RX_TIMEOUT 空闲中断或 DMA 满缓冲切分。
 *
 * @param  out_data   输出缓冲。
 * @param  max_length 输出缓冲最大长度。
 * @return 实际读取字节数；0 表示暂无数据。
 */
uint16_t Uart_AppReadPcPacket(uint8_t *out_data, uint16_t max_length)
{
    /* 从驱动层读取 PC 串口的一包数据，并返回实际复制长度。 */
    return Uart_DriverReadPacket(UART_DRIVER_PORT_PC, out_data, max_length);
}

/**
 * @brief  读取陀螺仪串口 UART1 的一包接收数据。
 *
 * @note   陀螺仪可能连续输出 5 字节帧，一包里可能包含 1 个或多个完整帧，
 *         也可能包含半帧；上层需要使用流式解析器继续拼帧。
 *
 * @param  out_data   输出缓冲。
 * @param  max_length 输出缓冲最大长度。
 * @return 实际读取字节数；0 表示暂无数据。
 */
uint16_t Uart_AppReadGyroPacket(uint8_t *out_data, uint16_t max_length)
{
    /* 从驱动层读取陀螺仪串口的一包数据，并返回实际复制长度。 */
    return Uart_DriverReadPacket(UART_DRIVER_PORT_GYRO, out_data, max_length);
}

/**
 * @brief  读取电机串口 UART3 的一包接收数据。
 *
 * @note   “一包”由 UART RX_TIMEOUT 空闲中断或 DMA 满缓冲切分。
 *         App 层需要按 Modbus RTU 协议校验 CRC 后再使用数据。
 *
 * @param  out_data   输出缓冲。
 * @param  max_length 输出缓冲最大长度。
 * @return 实际读取字节数；0 表示暂无数据。
 */
uint16_t Uart_AppReadMotorPacket(uint8_t *out_data, uint16_t max_length)
{
    /* 从驱动层读取电机串口的一包数据，并返回实际复制长度。 */
    return Uart_DriverReadPacket(UART_DRIVER_PORT_MOTOR, out_data, max_length);
}

/**
 * @brief  读取 UART 驱动统计。
 *
 * @note   这是对 Driver 层统计接口的应用层转发，方便业务代码不直接包含底层细节。
 *
 * @param  port      串口逻辑编号。
 * @param  out_stats 输出统计结构体。
 * @return 无。
 */
void Uart_AppGetStats(uart_driver_port_t port, uart_driver_stats_t *out_stats)
{
    /* 直接转发统计查询请求，让业务层无需包含驱动内部上下文。 */
    Uart_DriverGetStats(port, out_stats);
}

/**
 * @brief  格式化字符串并通过电脑串口 UART0 DMA 输出。
 *
 * @note   该函数类似 printf，但不是逐字节输出，而是先 vsnprintf 到静态缓冲，
 *         再用 TX DMA 一次性发送。返回值是实际尝试发送的字节数。
 *
 * @param  format printf 风格格式字符串。
 * @param  ...    可变参数。
 * @return 成功时返回发送字节数；格式化失败或 DMA 发送失败时返回负数。
 */
int my_printf(const char *format, ...)
{
    /* 静态输出缓冲避免在栈上分配较大数组，同时适合裸机环境。 */
    static char print_buffer[UART_APP_PRINTF_BUFFER_SIZE];
    /* 保存可变参数列表，供 vsnprintf 读取。 */
    va_list args;
    /* 保存格式化后的字符串长度或错误码。 */
    int length;

    /* 格式字符串为空时无法格式化，直接返回错误。 */
    if (format == NULL)
    {
        /* 返回负数表示格式化/发送失败。 */
        return -1;
    }

    /* 初始化可变参数读取状态，从 format 后面的第一个参数开始。 */
    va_start(args, format);
    /* 将格式化结果写入静态缓冲，并限制最大写入长度防止越界。 */
    length = vsnprintf(print_buffer, sizeof(print_buffer), format, args);
    /* 结束可变参数读取，释放 va_list 相关状态。 */
    va_end(args);

    /* vsnprintf 返回负数表示格式化失败。 */
    if (length < 0)
    {
        /* 格式化失败时返回错误。 */
        return -1;
    }

    /* 如果返回长度大于等于缓冲区大小，说明输出被截断。 */
    if ((uint32_t)length >= sizeof(print_buffer))
    {
        /*
         * vsnprintf 返回“本来需要的长度”。当字符串被截断时，只发送缓冲里已有内容，
         * 并预留最后的 '\0'，避免把未定义内存发出去。
         */
        /* 将实际发送长度限制为缓冲区有效字符容量，不包含末尾字符串结束符。 */
        length = (int)sizeof(print_buffer) - 1;
    }

    /* 通过 PC 串口 DMA 发送格式化后的字符串。 */
    if (Uart_AppSendToPc((const uint8_t *)print_buffer, (uint16_t)length) == false)
    {
        /* DMA 发送失败时返回负数，提示调用者日志未成功发出。 */
        return -1;
    }

    /* 返回本次实际尝试发送的字符数。 */
    return length;
}

#if !defined(__MICROLIB)
#if defined(__ARMCLIB_VERSION) && (__ARMCLIB_VERSION <= 6000000)
/**
 * @brief  Arm Compiler 5 非 MicroLIB 模式下的 FILE 占位结构。
 *
 * @note   标准库只需要该结构存在即可完成 fputc 重定向。
 */
struct __FILE
{
    int handle;
};
#endif

/* 标准输出文件对象，供 printf/fputc 重定向使用。 */
FILE __stdout;

/**
 * @brief  禁用半主机退出。
 *
 * @note   如果不定义该函数，部分标准库配置会尝试使用 semihosting 退出程序，
 *         在无调试主机环境下可能导致程序卡住。
 *
 * @param  x 退出码，本工程不使用。
 * @return 无。
 */
void _sys_exit(int x)
{
    /* 标记退出码参数已被有意忽略，避免编译器未使用参数告警。 */
    (void)x;
}
#endif

/**
 * @brief  printf 单字符重定向入口。
 *
 * @note   标准 printf 会反复调用 fputc，因此这里使用阻塞单字节发送；
 *         如果要输出较长日志，优先使用 my_printf() 走 DMA 批量发送。
 *
 * @param  ch     待输出字符。
 * @param  stream 标准库文件流，本工程只处理 stdout，参数不需要使用。
 * @return 原样返回 ch，符合 fputc 约定。
 */
int fputc(int ch, FILE *stream)
{
    /* 本工程不区分 stdout/stderr 具体流对象，显式忽略该参数。 */
    (void)stream;

    /* 当标准库输出换行时，额外补一个回车，兼容常见串口终端 CRLF 换行。 */
    if (ch == '\n')
    {
        /* 先发送 '\r'，让串口终端光标回到行首。 */
        (void)Uart_DriverWriteByteBlocking(UART_DRIVER_PORT_PC, (uint8_t)'\r');
    }

    /* 发送当前字符本身到 PC 串口。 */
    (void)Uart_DriverWriteByteBlocking(UART_DRIVER_PORT_PC, (uint8_t)ch);
    /* 按 fputc 约定返回原字符，表示标准库层本次写入完成。 */
    return ch;
}
