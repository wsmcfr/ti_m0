/**
 * @file    uart_driver.c
 * @brief   MSPM0G3507 UART0/UART1/UART3 DMA + 空闲中断驱动实现。
 *
 * @details 各路串口都使用 RX DMA 持续搬运数据。UART 接收超时中断
 *          DL_UART_MAIN_IIDX_RX_TIMEOUT_ERROR 被当作“空闲中断”使用：
 *          当一段数据结束后，驱动停止当前 RX DMA，计算已经接收的字节数，
 *          复制到 ready 缓冲区给 App 层读取，然后立即重启 RX DMA。
 */

#include "uart_driver.h"

#include <string.h>

#include "ti_msp_dl_config.h"

/* TX 等待保护计数。该值不是精确定时，只用于避免硬件异常时永久卡死。 */
#define UART_DRIVER_TX_WAIT_BASE        (800000UL)

/* 每发送 1 字节额外增加的等待计数，用于覆盖 115200bps 下较长字符串的发送时间。 */
#define UART_DRIVER_TX_WAIT_PER_BYTE    (8000UL)

/**
 * @brief  单路 UART 的硬件描述和运行状态。
 *
 * @note   uart/rx_dma_chan/tx_dma_chan/irqn 是固定硬件资源；
 *         rx_dma_buffer 是 DMA 正在写入的缓冲；
 *         rx_ready_buffer 是中断中切出来、等待 App 层读取的一包数据。
 */
typedef struct
{
    UART_Regs *uart;                               /* UART 寄存器基地址。 */
    IRQn_Type irqn;                                /* 对应 NVIC 中断号，用于临界区保护。 */
    uint8_t rx_dma_chan;                           /* RX DMA 通道号。 */
    uint8_t tx_dma_chan;                           /* TX DMA 通道号。 */
    uint8_t rx_dma_buffer[UART_DRIVER_RX_DMA_BUFFER_SIZE];   /* DMA 接收工作缓冲。 */
    uint8_t rx_ready_buffer[UART_DRIVER_RX_DMA_BUFFER_SIZE]; /* 已切包、待上层读取的缓冲。 */
    volatile uint16_t rx_ready_length;             /* rx_ready_buffer 当前有效字节数。 */
    volatile bool rx_ready;                        /* true 表示上层还没有取走当前接收包。 */
    volatile bool tx_busy;                         /* true 表示该串口正在进行一次 DMA 发送。 */
    volatile bool tx_dma_done;                     /* UART TX DMA 搬运完成标志。 */
    volatile bool tx_eot_done;                     /* UART 移位寄存器真正发送完毕标志。 */
    volatile uint32_t rx_packet_count;             /* 成功切出的 RX 包数量。 */
    volatile uint32_t rx_overflow_count;           /* ready 包被新包覆盖的次数。 */
    volatile uint32_t rx_error_count;              /* 接收错误或非法访问计数。 */
    volatile uint32_t tx_packet_count;             /* 成功完成的 TX 包数量。 */
    volatile uint32_t tx_timeout_count;            /* TX 等待超时计数。 */
    volatile uint32_t tx_reject_count;             /* 非阻塞发送被拒绝次数，用于观察日志或命令丢弃情况。 */
} uart_driver_context_t;

/* 三路串口的硬件资源表，和 empty.syscfg 里的 UART_PC/UART_GYRO/UART_MOTOR、DMA 通道保持一致。 */
static uart_driver_context_t s_uart_contexts[UART_DRIVER_PORT_COUNT] =
{
    {
        .uart = UART_PC_INST,
        .irqn = UART_PC_INST_INT_IRQN,
        .rx_dma_chan = DMA_PC_RX_CHAN_ID,
        .tx_dma_chan = DMA_PC_TX_CHAN_ID,
    },
    {
        .uart = UART_GYRO_INST,
        .irqn = UART_GYRO_INST_INT_IRQN,
        .rx_dma_chan = DMA_GYRO_RX_CHAN_ID,
        .tx_dma_chan = DMA_GYRO_TX_CHAN_ID,
    },
    {
        .uart = UART_MOTOR_INST,
        .irqn = UART_MOTOR_INST_INT_IRQN,
        .rx_dma_chan = DMA_MOTOR_RX_CHAN_ID,
        .tx_dma_chan = DMA_MOTOR_TX_CHAN_ID,
    },
};

/* 标记 Uart_DriverInit() 是否已经执行，给 fputc 这类早期调用提供兜底判断。 */
static bool s_uart_driver_initialized = false;

static uart_driver_context_t *Uart_DriverGetContext(uart_driver_port_t port);
static void Uart_DriverStartRxDma(uart_driver_context_t *ctx);
static bool Uart_DriverStartTxDma(uart_driver_context_t *ctx, const uint8_t *data,
    uint16_t length);
static void Uart_DriverPublishRxPacket(uart_driver_context_t *ctx, uint16_t length);
static void Uart_DriverCaptureRxByIdle(uart_driver_context_t *ctx);
static void Uart_DriverHandleIrq(uart_driver_context_t *ctx);

/**
 * @brief  根据逻辑端口获取 UART 运行上下文。
 *
 * @note   所有公开接口先通过本函数检查端口范围，避免数组越界访问硬件表。
 *
 * @param  port 串口逻辑编号。
 * @return 有效端口返回上下文指针，非法端口返回 NULL。
 */
static uart_driver_context_t *Uart_DriverGetContext(uart_driver_port_t port)
{
    /* 判断传入端口号是否超出上下文数组范围，防止后续数组越界。 */
    if ((uint32_t)port >= (uint32_t)UART_DRIVER_PORT_COUNT)
    {
        /* 非法端口没有对应硬件资源，返回 NULL 让调用者走错误处理。 */
        return NULL;
    }

    /* 端口合法时返回该端口对应的运行上下文地址。 */
    return &s_uart_contexts[(uint32_t)port];
}

/**
 * @brief  启动或重启某一路串口的 RX DMA。
 *
 * @note   每次空闲中断切包后都要重新写入源地址、目标地址和传输长度。
 *         RX 源地址固定为 UART RXDATA，目标地址是当前串口的 rx_dma_buffer。
 *
 * @param  ctx 串口运行上下文。
 * @return 无。
 */
static void Uart_DriverStartRxDma(uart_driver_context_t *ctx)
{
    /* 调用者传入空上下文时直接返回，避免访问无效指针。 */
    if (ctx == NULL)
    {
        /* 空指针没有可配置的 DMA 通道，直接结束函数。 */
        return;
    }

    /* 先关通道再改地址和长度，避免 DMA 正在搬运时改寄存器造成不可预期结果。 */
    /* 关闭当前 RX DMA 通道，确保后续重新配置地址和长度时通道处于停止状态。 */
    DL_DMA_disableChannel(DMA, ctx->rx_dma_chan);
    /* 设置 DMA 源地址为当前 UART 的 RXDATA 寄存器，串口收到的数据从这里读出。 */
    DL_DMA_setSrcAddr(DMA, ctx->rx_dma_chan, (uint32_t)(&ctx->uart->RXDATA));
    /* 设置 DMA 目的地址为本串口的接收工作缓冲区起始位置。 */
    DL_DMA_setDestAddr(DMA, ctx->rx_dma_chan, (uint32_t)&ctx->rx_dma_buffer[0]);
    /* 设置本轮 DMA 最多接收的字节数，达到该长度会触发 DMA_DONE_RX。 */
    DL_DMA_setTransferSize(DMA, ctx->rx_dma_chan, UART_DRIVER_RX_DMA_BUFFER_SIZE);
    /* 重新打开 RX DMA 通道，使 UART 后续收到的字节继续搬入缓冲区。 */
    DL_DMA_enableChannel(DMA, ctx->rx_dma_chan);
}

/**
 * @brief  启动某一路 UART 的 TX DMA 发送。
 *
 * @note   调用者必须保证 ctx、data 和 length 已经通过检查，且当前端口不忙。
 *         本函数只负责配置硬件并置位运行状态，不等待 DMA_DONE_TX 或 EOT_DONE。
 *
 * @param  ctx    串口运行上下文。
 * @param  data   待发送数据缓冲。
 * @param  length 待发送字节数。
 * @return true 表示 DMA 已经启动；false 表示参数无效。
 */
static bool Uart_DriverStartTxDma(uart_driver_context_t *ctx, const uint8_t *data,
    uint16_t length)
{
    /* 检查关键参数，避免后续写 DMA 寄存器时访问无效地址。 */
    if ((ctx == NULL) || (data == NULL) || (length == 0U))
    {
        /* 参数无效时不能启动发送。 */
        return false;
    }

    /* 标记当前端口进入发送忙状态，直到 EOT 中断确认线端发送完成。 */
    ctx->tx_busy = true;
    /* 清除本次发送的 DMA 搬运完成标志，等待 UART DMA_DONE_TX 中断重新置位。 */
    ctx->tx_dma_done = false;
    /* 清除本次发送的线端完成标志，等待 UART EOT_DONE 中断重新置位。 */
    ctx->tx_eot_done = false;

    /* 停止 TX DMA 通道，确保后续重新配置源、目的和长度安全。 */
    DL_DMA_disableChannel(DMA, ctx->tx_dma_chan);
    /* 清除 UART 中可能残留的 TX DMA/EOT 中断状态，避免误判本次发送已完成。 */
    DL_UART_Main_clearInterruptStatus(ctx->uart,
        DL_UART_MAIN_INTERRUPT_DMA_DONE_TX | DL_UART_MAIN_INTERRUPT_EOT_DONE);

    /* 设置 TX DMA 源地址为调用者提供的数据缓冲区。 */
    DL_DMA_setSrcAddr(DMA, ctx->tx_dma_chan, (uint32_t)data);
    /* 设置 TX DMA 目的地址为当前 UART 的 TXDATA 寄存器。 */
    DL_DMA_setDestAddr(DMA, ctx->tx_dma_chan, (uint32_t)(&ctx->uart->TXDATA));
    /* 设置 TX DMA 本次需要搬运的字节数。 */
    DL_DMA_setTransferSize(DMA, ctx->tx_dma_chan, length);
    /* 启动 TX DMA，让数据按 UART TX 触发节奏写入发送 FIFO。 */
    DL_DMA_enableChannel(DMA, ctx->tx_dma_chan);

    /* DMA 已经启动，后续完成状态由 UART 中断维护。 */
    return true;
}

/**
 * @brief  把本次 RX DMA 收到的数据发布给 App 层。
 *
 * @note   当前实现保留“最新一包”。如果 App 层没有及时读取上一包，
 *         新包会覆盖旧包并增加 rx_overflow_count，便于调试发现任务周期过慢。
 *
 * @param  ctx    串口运行上下文。
 * @param  length 本次接收到的字节数。
 * @return 无。
 */
static void Uart_DriverPublishRxPacket(uart_driver_context_t *ctx, uint16_t length)
{
    /* 没有上下文或本次没有收到数据时，不发布空包给上层。 */
    if ((ctx == NULL) || (length == 0U))
    {
        /* 无有效数据可复制，直接返回。 */
        return;
    }

    /* 防御性限制发布长度，避免异常长度超过 ready 缓冲区容量。 */
    if (length > UART_DRIVER_RX_DMA_BUFFER_SIZE)
    {
        /* 长度异常时截断到缓冲区最大容量，保护后续 memcpy 边界。 */
        length = UART_DRIVER_RX_DMA_BUFFER_SIZE;
    }

    /* 如果上一包还没被 App 层读取，本次发布会覆盖旧数据。 */
    if (ctx->rx_ready == true)
    {
        /* 上层未读取时覆盖旧包。这样不会阻塞中断，但会记录丢包风险。 */
        ctx->rx_overflow_count++;
    }

    /* 把 DMA 工作缓冲中的本包数据复制到 ready 缓冲，隔离后续 DMA 写入。 */
    memcpy(ctx->rx_ready_buffer, ctx->rx_dma_buffer, length);
    /* 记录 ready 缓冲中的有效数据长度，供 App 层读取。 */
    ctx->rx_ready_length = length;
    /* 标记当前已有完整接收包，提示 App 层可以取走。 */
    ctx->rx_ready = true;
    /* 成功发布一包后累加接收包计数，用于调试统计。 */
    ctx->rx_packet_count++;
}

/**
 * @brief  在 RX 空闲或 RX DMA 满缓冲时切出一包数据。
 *
 * @note   DMASZ 会随着 DMA 搬运递减，因此已收字节数 = 缓冲长度 - 剩余长度。
 *         停止 DMA 后再额外读取 RX FIFO，是为了兜住极端情况下尚未被 DMA 搬走的尾字节。
 *
 * @param  ctx 串口运行上下文。
 * @return 无。
 */
static void Uart_DriverCaptureRxByIdle(uart_driver_context_t *ctx)
{
    /* 保存 DMA 当前剩余传输数量，用来反推已接收字节数。 */
    uint16_t remaining;
    /* 保存本次最终捕获到的字节数，包括 DMA 已搬运和 FIFO 尾字节。 */
    uint16_t received;

    /* 调用者传入空上下文时直接返回，避免访问 DMA 和 UART 寄存器。 */
    if (ctx == NULL)
    {
        /* 空上下文无法切包，直接结束函数。 */
        return;
    }

    /* 先停止 RX DMA，固定 DMASZ 和缓冲内容，避免计算长度时继续变化。 */
    DL_DMA_disableChannel(DMA, ctx->rx_dma_chan);

    /* 读取 DMA 剩余传输数量，正常情况下该值会从缓冲长度递减。 */
    remaining = DL_DMA_getTransferSize(DMA, ctx->rx_dma_chan);
    /* 检查剩余数量是否异常大于缓冲长度，避免无符号减法下溢。 */
    if (remaining > UART_DRIVER_RX_DMA_BUFFER_SIZE)
    {
        /* 理论上不会出现，做保护是为了避免异常寄存器值导致长度下溢。 */
        remaining = UART_DRIVER_RX_DMA_BUFFER_SIZE;
    }

    /* 用缓冲总长度减去剩余数量，得到 DMA 已经搬运进内存的字节数。 */
    received = (uint16_t)(UART_DRIVER_RX_DMA_BUFFER_SIZE - remaining);

    /* 当 UART FIFO 里还有 DMA 未及时搬走的尾字节，且缓冲仍有空间时继续读取。 */
    while ((DL_UART_Main_isRXFIFOEmpty(ctx->uart) == false) &&
           (received < UART_DRIVER_RX_DMA_BUFFER_SIZE))
    {
        /* 从 UART RX FIFO 读出 1 字节，补到 DMA 接收缓冲尾部。 */
        ctx->rx_dma_buffer[received] = DL_UART_Main_receiveData(ctx->uart);
        /* 已接收长度增加 1，下一次写入缓冲的下一个位置。 */
        received++;
    }

    /* 将本次捕获到的数据发布到 ready 缓冲，供 App 层读取。 */
    Uart_DriverPublishRxPacket(ctx, received);
    /* 重新启动 RX DMA，保证下一帧数据可以继续接收。 */
    Uart_DriverStartRxDma(ctx);
}

/**
 * @brief  统一处理某一路 UART 的中断事件。
 *
 * @note   IIDX 每次只返回一个最高优先级中断，所以这里循环读取，
 *         直到没有待处理中断，避免 RX_TIMEOUT 和 DMA_DONE_TX 同时到来时漏处理。
 *
 * @param  ctx 串口运行上下文。
 * @return 无。
 */
static void Uart_DriverHandleIrq(uart_driver_context_t *ctx)
{
    /* 控制 IIDX 轮询循环是否继续，true 表示继续读取待处理中断。 */
    bool keep_checking = true;

    /* 中断入口传入空上下文时直接返回，避免访问非法寄存器。 */
    if (ctx == NULL)
    {
        /* 没有有效上下文就没有可处理的串口事件。 */
        return;
    }

    /* 循环读取 UART 待处理中断，直到硬件报告没有更多事件。 */
    while (keep_checking == true)
    {
        /* 读取当前最高优先级 UART 中断原因，并按原因分支处理。 */
        switch (DL_UART_Main_getPendingInterrupt(ctx->uart))
        {
            case DL_UART_MAIN_IIDX_RX_TIMEOUT_ERROR:
                /*
                 * 接收超时表示总线进入空闲状态。此时把 DMA 已收数据切包，
                 * 再立即重启 RX DMA，下一帧数据就能继续无缝接收。
                 */
                /* 将空闲前累积的 UART 字节切成一包发布给 App 层。 */
                Uart_DriverCaptureRxByIdle(ctx);
                /* 当前事件处理完成，继续检查是否还有其它待处理中断。 */
                break;

            case DL_UART_MAIN_IIDX_DMA_DONE_RX:
                /*
                 * DMA 缓冲满 128 字节时也要切包，否则 DMASZ 归零后 DMA 停止，
                 * 后续字节会堆在 UART FIFO 甚至溢出。
                 */
                /* DMA 缓冲满时立即切包，避免后续字节因 DMA 停止而丢失。 */
                Uart_DriverCaptureRxByIdle(ctx);
                /* 当前事件处理完成，继续检查后续中断。 */
                break;

            case DL_UART_MAIN_IIDX_DMA_DONE_TX:
                /* DMA 已经把内存数据搬入 UART TX FIFO，但线上的最后一位可能还没发完。 */
                /* 标记 TX DMA 搬运完成，解除发送函数的第一阶段等待。 */
                ctx->tx_dma_done = true;
                /* 当前事件处理完成，继续检查是否还有 EOT 等事件。 */
                break;

            case DL_UART_MAIN_IIDX_EOT_DONE:
                /* EOT 表示 UART 移位寄存器发送完毕，可以安全复用发送缓冲。 */
                /* 标记串口线上的最后一位也已发送完成，解除发送函数的第二阶段等待。 */
                ctx->tx_eot_done = true;
                /* 非阻塞发送依赖中断释放忙标志，避免下一包一直被判定为忙。 */
                if (ctx->tx_busy == true)
                {
                    /* 本包线端已经真正发送完成，允许后续发送复用同一 DMA 通道。 */
                    ctx->tx_busy = false;
                    /* 成功完成一包发送，累加发送包计数。 */
                    ctx->tx_packet_count++;
                }
                /* 当前事件处理完成，继续检查是否还有其它中断。 */
                break;

            case DL_UART_MAIN_IIDX_OVERRUN_ERROR:
            case DL_UART_MAIN_IIDX_BREAK_ERROR:
            case DL_UART_MAIN_IIDX_PARITY_ERROR:
            case DL_UART_MAIN_IIDX_FRAMING_ERROR:
            case DL_UART_MAIN_IIDX_NOISE_ERROR:
                /*
                 * 错误中断通常意味着帧格式或接线/波特率异常。
                 * 读取并清空 FIFO 后重启 RX DMA，让后续正常数据可以重新同步。
                 */
                /* 记录接收错误次数，便于联调时判断通信质量。 */
                ctx->rx_error_count++;
                /* 清空 RX FIFO 中残留的错误数据，避免污染下一帧。 */
                while (DL_UART_Main_isRXFIFOEmpty(ctx->uart) == false)
                {
                    /* 读出并丢弃 1 字节错误数据，只为推进 FIFO。 */
                    (void)DL_UART_Main_receiveData(ctx->uart);
                }
                /* 错误恢复后重启 RX DMA，等待下一段正常数据。 */
                Uart_DriverStartRxDma(ctx);
                /* 当前错误事件处理完成，继续检查是否还有待处理中断。 */
                break;

            case DL_UART_MAIN_IIDX_NO_INTERRUPT:
            default:
                /* 没有更多 UART 中断，结束本轮 IIDX 轮询循环。 */
                keep_checking = false;
                /* 跳出 switch，while 会因为 keep_checking=false 结束。 */
                break;
        }
    }
}

/**
 * @brief  初始化 UART DMA 驱动。
 *
 * @note   SYSCFG_DL_init() 已经完成 UART、GPIO、DMA 通道基础配置。
 *         本函数只补充 DMA 源/目的地址、启动 RX DMA，并打开各路 UART NVIC 中断。
 *
 * @param  无。
 * @return 无。
 */
void Uart_DriverInit(void)
{
    /* 遍历所有 UART 端口上下文，逐个恢复状态并启动 RX DMA。 */
    for (uint32_t i = 0U; i < (uint32_t)UART_DRIVER_PORT_COUNT; i++)
    {
        /* 取得当前端口上下文，后续配置该端口的状态和硬件中断。 */
        uart_driver_context_t *ctx = &s_uart_contexts[i];

        /* 清空 ready 包长度，表示初始化时没有待上层读取的数据。 */
        ctx->rx_ready_length = 0U;
        /* 清除 RX ready 标志，避免上电后误读旧缓冲。 */
        ctx->rx_ready = false;
        /* 清除发送忙标志，允许第一次发送立即开始。 */
        ctx->tx_busy = false;
        /* 清除 TX DMA 完成标志，等待真正发送时由中断置位。 */
        ctx->tx_dma_done = false;
        /* 清除 UART 发送完成标志，等待真正发送时由 EOT 中断置位。 */
        ctx->tx_eot_done = false;
        /* 清零接收包计数，统计从本次初始化后重新开始。 */
        ctx->rx_packet_count = 0U;
        /* 清零接收覆盖计数，便于判断初始化后的真实丢包情况。 */
        ctx->rx_overflow_count = 0U;
        /* 清零接收错误计数，便于后续联调排查。 */
        ctx->rx_error_count = 0U;
        /* 清零发送包计数，统计从本次初始化后重新开始。 */
        ctx->tx_packet_count = 0U;
        /* 清零发送超时计数，便于后续判断硬件或中断是否异常。 */
        ctx->tx_timeout_count = 0U;
        /* 清零非阻塞发送拒绝计数，便于观察本次运行后的真实丢弃情况。 */
        ctx->tx_reject_count = 0U;

        /*
         * RX timeout 是本工程的“串口空闲中断”来源。
         * empty.syscfg 已设置 rxTimeoutValue=15，这里再次写入同一值，
         * 是为了防止后续重新生成配置时遗漏该项导致空闲中断失效。
         */
        /* 配置 UART 接收超时时间，作为本工程的数据帧空闲切包依据。 */
        DL_UART_Main_setRXInterruptTimeout(ctx->uart, 15U);
        /* 启动当前端口的 RX DMA，让接收数据自动搬入内存缓冲。 */
        Uart_DriverStartRxDma(ctx);

        /* 清除 NVIC 中可能残留的挂起中断，避免初始化后立刻误进 ISR。 */
        NVIC_ClearPendingIRQ(ctx->irqn);
        /* 打开当前 UART 的 NVIC 中断，使 RX timeout、DMA done、EOT 能被处理。 */
        NVIC_EnableIRQ(ctx->irqn);
    }

    /* 标记驱动已经初始化，供后续早期输出或调试判断使用。 */
    s_uart_driver_initialized = true;
}

/**
 * @brief  使用 TX DMA 发送一段数据。
 *
 * @note   该函数是阻塞式发送：先等待 DMA_DONE_TX，再等待 EOT_DONE。
 *         这样函数返回后，调用者传入的缓冲区可以立即复用，适合 App 层发送命令和日志。
 *
 * @param  port   串口逻辑编号。
 * @param  data   待发送数据地址。
 * @param  length 待发送字节数，0 表示不发送但返回成功。
 * @return true 表示 DMA 和 UART 发送完成；false 表示参数错误或等待超时。
 */
bool Uart_DriverWrite(uart_driver_port_t port, const uint8_t *data, uint16_t length)
{
    /* 根据逻辑端口查找硬件上下文，后续通过该上下文访问 UART 和 DMA。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);
    /* 发送等待循环计数器，用于避免硬件异常时阻塞不返回。 */
    uint32_t wait_count;

    /* 检查端口是否合法，并且在需要发送数据时检查数据指针是否有效。 */
    if ((ctx == NULL) || ((data == NULL) && (length > 0U)))
    {
        /* 参数错误时不访问硬件，直接返回发送失败。 */
        return false;
    }

    /* 长度为 0 表示没有实际数据要发送，按空发送成功处理。 */
    if (length == 0U)
    {
        /* 空数据不需要占用 DMA 或 UART，直接返回成功。 */
        return true;
    }

    /* 设置等待已有发送完成的保护计数，避免 tx_busy 长期不清导致死等。 */
    wait_count = UART_DRIVER_TX_WAIT_BASE;
    /* 当当前端口还在发送且等待计数未耗尽时，持续轮询等待。 */
    while ((ctx->tx_busy == true) && (wait_count > 0UL))
    {
        /* 每轮等待消耗 1 个计数，作为简单超时保护。 */
        wait_count--;
    }

    /* 如果等待结束后仍然忙，说明前一次发送可能卡住或硬件异常。 */
    if (ctx->tx_busy == true)
    {
        /* 记录发送超时次数，便于后续定位问题。 */
        ctx->tx_timeout_count++;
        /* 无法启动本次发送，返回失败。 */
        return false;
    }

    /* 启动 TX DMA；阻塞式接口随后会等待 DMA 和线端完成。 */
    if (Uart_DriverStartTxDma(ctx, data, length) == false)
    {
        /* 正常参数路径不应失败，失败时按发送异常处理。 */
        ctx->tx_timeout_count++;
        /* 返回发送失败。 */
        return false;
    }

    /* 按发送长度扩大等待上限，覆盖较长数据在低波特率下的耗时。 */
    wait_count = UART_DRIVER_TX_WAIT_BASE + ((uint32_t)length * UART_DRIVER_TX_WAIT_PER_BYTE);
    /* 等待 TX DMA 搬运完成标志，由 UART DMA_DONE_TX 中断置位。 */
    while ((ctx->tx_dma_done == false) && (wait_count > 0UL))
    {
        /* 每轮等待消耗 1 个计数，避免中断异常时永久阻塞。 */
        wait_count--;
    }

    /* 如果 TX DMA 未在保护计数内完成，则认为本次发送失败。 */
    if (ctx->tx_dma_done == false)
    {
        /* 关闭 TX DMA 通道，停止可能还未完成的异常搬运。 */
        DL_DMA_disableChannel(DMA, ctx->tx_dma_chan);
        /* 释放发送忙标志，避免后续发送一直被阻塞。 */
        ctx->tx_busy = false;
        /* 清除异常发送残留标志，避免下次发送被旧状态影响。 */
        ctx->tx_dma_done = false;
        /* 清除异常发送的线端完成标志。 */
        ctx->tx_eot_done = false;
        /* 记录超时统计，便于调试。 */
        ctx->tx_timeout_count++;
        /* 返回发送失败。 */
        return false;
    }

    /* 重新设置等待上限，用于等待 UART 移位寄存器真正发送完最后一位。 */
    wait_count = UART_DRIVER_TX_WAIT_BASE + ((uint32_t)length * UART_DRIVER_TX_WAIT_PER_BYTE);
    /* 等待 EOT 完成标志，由 UART EOT_DONE 中断置位。 */
    while ((ctx->tx_eot_done == false) && (wait_count > 0UL))
    {
        /* 每轮等待消耗 1 个计数，避免硬件异常时永久阻塞。 */
        wait_count--;
    }

    /* 如果 EOT 未在保护计数内到达，说明线端发送完成状态异常。 */
    if (ctx->tx_eot_done == false)
    {
        /* 释放发送忙标志，允许后续尝试重新发送。 */
        ctx->tx_busy = false;
        /* 清除异常发送残留标志，避免后续统计或状态判断混淆。 */
        ctx->tx_dma_done = false;
        /* 清除线端完成标志，本次发送实际未确认完成。 */
        ctx->tx_eot_done = false;
        /* 记录发送超时统计。 */
        ctx->tx_timeout_count++;
        /* 返回发送失败。 */
        return false;
    }

    /*
     * DMA 和 UART 线端都完成后释放发送忙状态。
     * 如果 EOT 中断已经先释放过，这里再次写 false 是幂等的。
     */
    ctx->tx_busy = false;
    /* 通知调用者本次阻塞式 DMA 发送成功完成。 */
    return true;
}

/**
 * @brief  尝试使用 TX DMA 发送一段数据，若端口忙则立即返回。
 *
 * @note   本函数不会等待 DMA_DONE_TX 或 EOT_DONE，适合周期任务里的日志输出。
 *         调用者必须保证 data 指向的缓冲在 EOT_DONE 中断前不会被改写或释放。
 *
 * @param  port   串口逻辑编号。
 * @param  data   待发送数据地址。
 * @param  length 待发送字节数，0 表示不发送但返回成功。
 * @return true 表示本次发送已经启动或为空发送；false 表示参数错误或端口正忙。
 */
bool Uart_DriverTryWrite(uart_driver_port_t port, const uint8_t *data, uint16_t length)
{
    /* 根据逻辑端口查找硬件上下文，后续通过该上下文访问 UART 和 DMA。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);

    /* 检查端口是否合法，并且在需要发送数据时检查数据指针是否有效。 */
    if ((ctx == NULL) || ((data == NULL) && (length > 0U)))
    {
        /* 非阻塞接口不能等待或修复参数错误，直接返回失败。 */
        return false;
    }

    /* 长度为 0 表示没有实际数据要发送，按空发送成功处理。 */
    if (length == 0U)
    {
        /* 空数据不占用 DMA 或 UART。 */
        return true;
    }

    /* 端口忙时立即拒绝本次发送，保证周期任务不会卡住调度器。 */
    if (ctx->tx_busy == true)
    {
        /* 记录拒绝次数，便于判断日志频率是否过高。 */
        ctx->tx_reject_count++;
        /* 告诉调用者本次没有启动发送。 */
        return false;
    }

    /* 启动 DMA 发送，不等待完成。 */
    if (Uart_DriverStartTxDma(ctx, data, length) == false)
    {
        /* 记录拒绝次数，便于调试非法调用或异常路径。 */
        ctx->tx_reject_count++;
        /* 启动失败。 */
        return false;
    }

    /* 发送已经启动，完成后由 UART 中断释放 busy 并累加 tx_packet_count。 */
    return true;
}

/**
 * @brief  查询某一路 UART TX DMA 是否正在发送。
 *
 * @note   非阻塞日志在改写静态缓冲前调用本函数，避免 DMA 尚未完成时覆盖发送内容。
 *
 * @param  port 串口逻辑编号。
 * @return true 表示端口正在发送或端口非法；false 表示当前可尝试启动新发送。
 */
bool Uart_DriverIsTxBusy(uart_driver_port_t port)
{
    /* 根据逻辑端口查找硬件上下文。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);

    /* 非法端口按忙处理，防止上层误以为空闲后继续写入。 */
    if (ctx == NULL)
    {
        /* 没有有效上下文，保守返回忙。 */
        return true;
    }

    /* 返回当前发送忙标志。 */
    return ctx->tx_busy;
}

/**
 * @brief  阻塞发送 1 个字节。
 *
 * @note   标准库 printf 的 fputc 会逐字节调用，若每个字节都启动 DMA 会非常低效。
 *         因此 fputc 使用本函数做单字节兜底；批量日志请使用 my_printf()，它会走 TX DMA。
 *
 * @param  port 串口逻辑编号。
 * @param  data 待发送字节。
 * @return true 表示写入 UART 成功；false 表示端口非法。
 */
bool Uart_DriverWriteByteBlocking(uart_driver_port_t port, uint8_t data)
{
    /* 根据逻辑端口查找 UART 上下文。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);

    /* 端口非法时不能访问 UART 寄存器。 */
    if (ctx == NULL)
    {
        /* 返回 false 告诉调用者单字节发送失败。 */
        return false;
    }

    /* 显式引用初始化标志，保留后续扩展早期输出保护的入口，同时避免未使用告警。 */
    (void)s_uart_driver_initialized;
    /* 调用 DriverLib 阻塞式发送 1 字节，常用于 fputc 的逐字符输出。 */
    DL_UART_Main_transmitDataBlocking(ctx->uart, data);
    /* 单字节已经写入发送流程，返回成功。 */
    return true;
}

/**
 * @brief  读取一包由空闲中断切分出的接收数据。
 *
 * @note   读取时短暂关闭对应 UART 的 NVIC 中断，防止 ISR 正在覆盖 ready 缓冲。
 *         如果 out_data 太小，只复制 max_length 字节，并丢弃本包剩余数据。
 *
 * @param  port       串口逻辑编号。
 * @param  out_data   上层接收缓冲。
 * @param  max_length 上层缓冲最大长度。
 * @return 实际复制到 out_data 的字节数；0 表示当前没有完整接收包。
 */
uint16_t Uart_DriverReadPacket(uart_driver_port_t port, uint8_t *out_data, uint16_t max_length)
{
    /* 根据端口查找 UART 上下文。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);
    /* 保存本次实际要复制给上层的字节数。 */
    uint16_t copy_length;

    /* 检查上下文、输出缓冲和缓冲容量，任何无效参数都不能读取。 */
    if ((ctx == NULL) || (out_data == NULL) || (max_length == 0U))
    {
        /* 参数非法或没有可写空间时返回 0，表示没有读到数据。 */
        return 0U;
    }

    /* 关闭对应 UART 中断，防止 ISR 在复制 ready 缓冲时改写同一数据。 */
    NVIC_DisableIRQ(ctx->irqn);

    /* 如果当前没有完整接收包，立即恢复中断并返回 0。 */
    if (ctx->rx_ready == false)
    {
        /* 恢复 UART 中断，保持接收功能继续工作。 */
        NVIC_EnableIRQ(ctx->irqn);
        /* 告诉调用者当前没有可读取的完整包。 */
        return 0U;
    }

    /* 默认复制 ready 包的全部有效长度。 */
    copy_length = ctx->rx_ready_length;
    /* 如果上层缓冲比 ready 包小，只复制缓冲能容纳的部分。 */
    if (copy_length > max_length)
    {
        /* 截断复制长度，保护 out_data 不越界。 */
        copy_length = max_length;
    }

    /* 把 ready 缓冲中的数据复制到调用者提供的输出缓冲。 */
    memcpy(out_data, ctx->rx_ready_buffer, copy_length);
    /* 清除 ready 标志，表示当前包已经被上层取走或丢弃剩余部分。 */
    ctx->rx_ready = false;
    /* 清空 ready 长度，避免后续误读旧长度。 */
    ctx->rx_ready_length = 0U;

    /* 复制完成后恢复 UART 中断，让 ISR 可以继续发布新包。 */
    NVIC_EnableIRQ(ctx->irqn);

    /* 返回本次实际复制给上层的字节数。 */
    return copy_length;
}

/**
 * @brief  查询当前是否有待读取接收包。
 *
 * @note   该函数只读长度，不清除 ready 标志，适合调试或上层决定是否分配处理时间。
 *
 * @param  port 串口逻辑编号。
 * @return 待读取字节数；0 表示没有待处理数据。
 */
uint16_t Uart_DriverGetPendingLength(uart_driver_port_t port)
{
    /* 根据端口查找 UART 上下文。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);

    /* 端口非法或当前没有 ready 包时，返回 0 表示无待处理数据。 */
    if ((ctx == NULL) || (ctx->rx_ready == false))
    {
        /* 没有可读数据长度。 */
        return 0U;
    }

    /* 返回当前 ready 包的有效字节数，但不清除 ready 标志。 */
    return ctx->rx_ready_length;
}

/**
 * @brief  清除某一路串口已经切包但尚未处理的 RX 数据。
 *
 * @note   该函数不会停止正在运行的 RX DMA，只丢弃 ready 缓冲中的旧包。
 *
 * @param  port 串口逻辑编号。
 * @return 无。
 */
void Uart_DriverClearRx(uart_driver_port_t port)
{
    /* 根据端口查找 UART 上下文。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);

    /* 端口非法时没有可清除的 ready 缓冲。 */
    if (ctx == NULL)
    {
        /* 无有效上下文，直接结束。 */
        return;
    }

    /* 临时关闭对应 UART 中断，防止 ISR 正在发布新 ready 包。 */
    NVIC_DisableIRQ(ctx->irqn);
    /* 清除 ready 标志，丢弃当前待上层读取的接收包。 */
    ctx->rx_ready = false;
    /* 清空 ready 长度，避免上层看到旧数据长度。 */
    ctx->rx_ready_length = 0U;
    /* 恢复 UART 中断，让后续接收继续工作。 */
    NVIC_EnableIRQ(ctx->irqn);
}

/**
 * @brief  读取某一路串口的运行统计。
 *
 * @note   统计值用于联调，不参与主业务流程；读取时不清零。
 *
 * @param  port      串口逻辑编号。
 * @param  out_stats 输出统计结构体。
 * @return 无。
 */
void Uart_DriverGetStats(uart_driver_port_t port, uart_driver_stats_t *out_stats)
{
    /* 根据端口查找 UART 上下文。 */
    uart_driver_context_t *ctx = Uart_DriverGetContext(port);

    /* 端口非法或输出结构为空时无法写出统计数据。 */
    if ((ctx == NULL) || (out_stats == NULL))
    {
        /* 参数无效时直接返回，不修改调用者内存。 */
        return;
    }

    /* 输出接收成功切包次数。 */
    out_stats->rx_packet_count = ctx->rx_packet_count;
    /* 输出 ready 包被覆盖次数。 */
    out_stats->rx_overflow_count = ctx->rx_overflow_count;
    /* 输出接收错误中断次数。 */
    out_stats->rx_error_count = ctx->rx_error_count;
    /* 输出发送成功包数。 */
    out_stats->tx_packet_count = ctx->tx_packet_count;
    /* 输出发送等待超时次数。 */
    out_stats->tx_timeout_count = ctx->tx_timeout_count;
    /* 输出非阻塞发送被拒绝次数。 */
    out_stats->tx_reject_count = ctx->tx_reject_count;
}

/**
 * @brief  UART0 中断服务函数。
 *
 * @note   UART0 在本工程中命名为 UART_PC，用于连接电脑端串口。
 *
 * @param  无。
 * @return 无。
 */
void UART_PC_INST_IRQHandler(void)
{
    /* 将 UART0/PC 的中断交给统一处理函数，传入 PC 端口上下文。 */
    Uart_DriverHandleIrq(&s_uart_contexts[UART_DRIVER_PORT_PC]);
}

/**
 * @brief  UART1 中断服务函数。
 *
 * @note   UART1 在本工程中命名为 UART_GYRO，用于连接陀螺仪模块。
 *
 * @param  无。
 * @return 无。
 */
void UART_GYRO_INST_IRQHandler(void)
{
    /* 将 UART1/GYRO 的中断交给统一处理函数，传入陀螺仪端口上下文。 */
    Uart_DriverHandleIrq(&s_uart_contexts[UART_DRIVER_PORT_GYRO]);
}

/**
 * @brief  UART3 中断服务函数。
 *
 * @note   UART3 在本工程中命名为 UART_MOTOR，用于连接电机驱动板。
 *
 * @param  无。
 * @return 无。
 */
void UART_MOTOR_INST_IRQHandler(void)
{
    /* 将 UART3/MOTOR 的中断交给统一处理函数，传入电机端口上下文。 */
    Uart_DriverHandleIrq(&s_uart_contexts[UART_DRIVER_PORT_MOTOR]);
}
