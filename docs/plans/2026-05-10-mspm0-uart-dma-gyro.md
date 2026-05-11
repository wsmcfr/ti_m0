# MSPM0 UART DMA Gyro Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在天猛星 MSPM0G3507 工程中配置两路 UART DMA，并用 UART1 接收陀螺仪、UART0 将解析后的数据和 printf 输出发送到电脑。

**Architecture:** SysConfig 负责 UART0/UART1、DMA 通道和引脚复用。`User/Driver/uart_driver.*` 只封装硬件 DMA 收发和 RX timeout 空闲中断事件。`User/App/gyro_protocol.*` 负责纯协议解析与配置命令构造，`User/App/uart_app.*` 负责业务任务、printf 重定向和电脑输出。

**Tech Stack:** MSPM0G3507、TI DriverLib、SysConfig、Keil ARMCLANG、MinGW GCC 单元测试。

---

### Task 1: 协议测试

**Files:**
- Create: `tests/gyro_protocol_test.c`

**Steps:**
- 写角速度帧、角度帧、校验错误、配置命令构造测试。
- 先运行 `gcc -IUser/App tests/gyro_protocol_test.c User/App/gyro_protocol.c -o tests/gyro_protocol_test.exe`，预期因协议库不存在失败。

### Task 2: 陀螺仪协议库

**Files:**
- Create: `User/App/gyro_protocol.h`
- Create: `User/App/gyro_protocol.c`

**Steps:**
- 实现 `GyroProtocol_Checksum()`、`GyroProtocol_ParseFrame()`、`GyroProtocol_FeedBytes()`。
- 实现解锁、保存、重启、恢复出厂、Z 轴归零、自动零偏、手动标定、设置波特率、设置输出速率等命令构造函数。
- 跑通 `tests/gyro_protocol_test.exe`。

### Task 3: SysConfig UART DMA

**Files:**
- Modify: `empty.syscfg`

**Steps:**
- 增加 `UART0`：`PA10=TX`、`PA11=RX`、115200、RX DMA CH0、TX DMA CH1、RX_TIMEOUT_ERROR 中断。
- 增加 `UART1`：`PA8=TX`、`PA9=RX`、115200、RX DMA CH2、TX DMA CH3、RX_TIMEOUT_ERROR 中断。

### Task 4: UART 驱动与应用层

**Files:**
- Create: `User/Driver/uart_driver.h`
- Create: `User/Driver/uart_driver.c`
- Create: `User/App/uart_app.h`
- Create: `User/App/uart_app.c`

**Steps:**
- Driver 层实现两路 RX DMA 启动、空闲中断取长度、TX DMA 非阻塞发送。
- App 层实现 `Uart_Init()`、`Uart_Task()`、`my_printf()`、`fputc()`。
- UART1 的陀螺仪数据进入协议解析器，UART0 输出 `yaw` 和 `wz`。

### Task 5: 工程接入与验证

**Files:**
- Modify: `User/scheduler.c`
- Modify: `keil/empty_LP_MSPM0G3507_nortos_keil.uvprojx`
- Modify: `gcc/makefile`
- Modify: `ticlang/makefile`
- Modify: `iar/makefile`

**Steps:**
- 调度器加入 `Uart_Task()`。
- 各工程加入新增源文件。
- 运行 SysConfig 生成配置，再用 ARMCLANG 做语法验证，最后让用户在 Keil Rebuild。

