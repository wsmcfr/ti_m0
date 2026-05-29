/*
 * Copyright (c) 2021, Texas Instruments Incorporated
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

#include "ti_msp_dl_config.h"
#include "scheduler.h"

int main(void)
{
    /*
     * 初始化 SysConfig 生成的底层配置。
     * 这里会完成电源、GPIO、系统时钟和 1ms SysTick 配置。
     */
    /* 调用 SysConfig 生成的初始化入口，让底层硬件先进入可用状态。 */
    SYSCFG_DL_init();

    /*
     * 初始化 User 分层模块。
     * 当前包括 LED App/Driver 和调度器任务时间基准。
     */
    /* 初始化用户层应用和调度器，为后续主循环任务扫描做准备。 */
    System_Init();

    /* 进入裸机主循环，程序上电后会一直停留在这个循环里运行任务。 */
    while (1)
    {
        /*
         * 主循环只运行调度器扫描。
         * 具体业务放到 User/App 层任务中，避免 main() 直接耦合外设细节。
         */
        /*
         * 扫描任务表并执行到期任务。
         * 如果本轮没有任务到期，就进入 WFI 等待下一次 SysTick 或外设中断唤醒，
         * 降低主循环空转功耗，同时不改变调度器对各任务 deadline 的判定。
         */
        if (Scheduler_Run() == false)
        {
            /* 无任务可运行时等待中断，SysTick/UART 等中断到来后会自动继续扫描。 */
            __WFI();
        }
    }
}

void SysTick_Handler(void)
{
    /*
     * SysTick 由 SysConfig 配置为 1ms 中断。
     * ISR 中只递增 tick，保持中断路径短且确定。
     */
    /* 每进入一次 1ms SysTick 中断，就把调度器毫秒计数加 1。 */
    Scheduler_TickInc();
}
