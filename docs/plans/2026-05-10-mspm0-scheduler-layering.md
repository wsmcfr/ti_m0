# MSPM0 Scheduler Layering Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将当前 MSPM0G3507 空工程改为 `User/App` 与 `User/Driver` 分层结构，并用 `scheduler.c/.h` 接管主循环周期任务。

**Architecture:** `empty.c` 只保留板级初始化、系统初始化和主循环调度。`User/scheduler.*` 负责毫秒 tick、任务表、系统级模块初始化和周期轮询。`User/App` 放业务任务，`User/Driver` 放硬件封装，当前先以蓝色 LED 为最小可验证任务。

**Tech Stack:** TI MSPM0 SDK 2.04.00.06、MSPM0G3507、DriverLib、SysConfig、Keil ARMCLANG/GCC/TICLANG/IAR 工程文件。

---

### Task 1: 建立分层目录与调度接口

**Files:**
- Create: `User/scheduler.h`
- Create: `User/scheduler.c`
- Create: `User/App/led_app.h`
- Create: `User/App/led_app.c`
- Create: `User/Driver/led_driver.h`
- Create: `User/Driver/led_driver.c`

**Step 1: 写最小接口**

定义 `Scheduler_TickInc()`、`Scheduler_Run()`、`System_Init()`，并提供 `Led_AppTask()` 作为 100ms 周期任务。

**Step 2: 编写最小实现**

`SysTick_Handler()` 只调用 `Scheduler_TickInc()`，调度器通过 `uwTick` 判断任务间隔，LED App 调用 Driver 层翻转蓝灯。

### Task 2: 改造入口

**Files:**
- Modify: `empty.c`

**Step 1: 移除阻塞延时闪灯**

删除 `delay_ms()` 与 `delay_times` 轮询，避免主循环被阻塞。

**Step 2: 接入调度框架**

`main()` 调用 `SYSCFG_DL_init()`、`System_Init()`，然后在 `while(1)` 中持续调用 `Scheduler_Run()`。

### Task 3: 接入构建系统

**Files:**
- Modify: `keil/empty_LP_MSPM0G3507_nortos_keil.uvprojx`
- Modify: `gcc/makefile`
- Modify: `ticlang/makefile`
- Modify: `iar/makefile`

**Step 1: 添加 include path**

加入 `../User`、`../User/App`、`../User/Driver`。

**Step 2: 添加源文件**

加入 `scheduler.c`、`led_app.c`、`led_driver.c` 到各工具链构建输入。

### Task 4: 验证

**Commands:**
- `make -C gcc clean all`
- `make -C ticlang clean all`
- 如命令行工具链不可用，记录实际错误；Keil 工程至少确保 XML 已加入源文件和包含路径。

