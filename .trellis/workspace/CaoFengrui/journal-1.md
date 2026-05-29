# Journal - CaoFengrui (Part 1)

> AI development session journal
> Started: 2026-05-11

---



## Session 1: Initialize Trellis workflow guidelines

**Date**: 2026-05-11
**Task**: Initialize Trellis workflow guidelines
**Branch**: `main`

### Summary

Completed Trellis bootstrap for the MSPM0G3507 firmware project: filled backend firmware guidelines, documented current no-frontend state for future host tools, archived the bootstrap task, ran Trellis context validation and gyro protocol host test, committed and pushed the Trellis workflow setup to origin/main.

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `1a81dc6` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 2: PCB pin layout and gray sensor planning

**Date**: 2026-05-29
**Task**: PCB pin layout and gray sensor planning
**Branch**: `main`

### Summary

(Add summary)

### Main Changes

| Area | Details |
|------|---------|
| PCB pin documentation | Created `docs/m0g3507-pcb-pin-connections.md` for the Tianmengxing M0G3507 carrier board, documenting current EasyEDA PCB nets, module interfaces, power path, headers, occupied MCU pins, and routing notes. |
| Existing module pins | Recorded gyro UART on `A08/A09`, motor UART on `B02/B03`, OLED I2C on `A29/A30`, and keys on `A07/B10/B11/B14` with the note that `B14(K3)` should be renamed to `B14(K4)`. |
| Gray line sensor research | Compared the GanWei no-MCU grayscale module examples and auxiliary board files. Identified two valid modes: `CLK+DAT` serial digital read using `PB9/PB8`, and `AD0/AD1/AD2 + OUT` analog-address read. |
| Final gray module pinout | Documented the final user-selected analog-address mapping: `AD0 -> PA15`, `AD1 -> PA14`, `AD2 -> PA13`, `OUT -> PA27/A0_0`, plus `3V3/GND`. Clarified that `PA27` is ADC0 analog input `A0_0`, not `ADC27`. |
| GitHub upload | Committed and pushed the PCB pin connection document to `origin/main` as `eedbc2d docs: add M0G3507 PCB pin connection notes`. |
| Local ignore cleanup | Updated `.gitignore` locally to ignore `Gerber_PCB1_2026-05-28.zip` and `电机/`; this `.gitignore` change is still uncommitted at the time of recording. |

**Updated Files**:
- `docs/m0g3507-pcb-pin-connections.md`
- `.gitignore` (local uncommitted ignore rule update)


### Git Commits

| Hash | Message |
|------|---------|
| `eedbc2d` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 3: 集成电机循迹OLED按键底层模块

**Date**: 2026-05-29
**Task**: 集成电机循迹OLED按键底层模块
**Branch**: `main`

### Summary

(Add summary)

### Main Changes

| 项目 | 记录 |
|---|---|
| 本次工作 | 添加电机串口驱动与应用、灰度循迹驱动与算法、SSD1306 OLED 驱动与状态显示、按键驱动与消抖事件，并接入调度器 |
| 硬件配置 | 更新 `empty.syscfg` 和 `ti_msp_dl_config.c/h`，新增 UART3 电机、I2C1 OLED、ADC0+DMA_CH6 灰度采样、灰度地址 GPIO、按键 GPIO |
| 构建工程 | 同步 `gcc/makefile`、`ticlang/makefile`、`iar/makefile`、Keil `.uvprojx/.uvoptx` 文件列表 |
| 测试证据 | `motor_protocol_test: all tests passed`；`gray_sensor_logic_test: all tests passed`；用户提供 Keil ARMCLANG 构建结果 `.axf` 为 `0 Error(s), 0 Warning(s)` |
| 未验证项 | 本次没有做板上验证；OLED 亮屏、按键触发、灰度 8 路采样、电机 UART3 实机收发仍需上板确认 |
| 备注 | 根目录未跟踪的 `device.opt`、`device.lds.genlibs`、`device_linker.lds` 是 SysConfig 临时生成产物，未纳入代码提交 |


### Git Commits

| Hash | Message |
|------|---------|
| `ccaaf0e` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 4: MSPM0G3507 调度与非阻塞优化

**Date**: 2026-05-29
**Task**: MSPM0G3507 调度与非阻塞优化
**Branch**: `main`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 会话主题 | MSPM0G3507 固件调度与非阻塞优化 |
| 主提交 | `8cb895b feat: optimize scheduler responsiveness` |
| 推送状态 | 已推送到 `origin/main`，远端指向 `8cb895b08dec709955e16dcd8d07e75b85c6389d` |
| 调度优化 | 将灰度循迹读取改为 1ms 切片状态机，`LineTrack_AppTask` 从 20ms 周期调整为 1ms，降低单次任务占用时间 |
| UART 优化 | 增加 `Uart_DriverTryWrite()` 和 `my_printf_try()` 非阻塞发送路径，忙时返回失败而不是阻塞等待 |
| 日志优化 | 陀螺仪周期日志改为 UART 忙则跳过，避免遥测输出拖慢主循环调度 |
| OLED 优化 | 增加 I2C 失败退避；OLED 初始化失败后不继续清屏，未接屏时减少反复超时等待 |
| 测试覆盖 | 新增 `tests/oled_app_format_test.c`，覆盖 OLED 文本格式化逻辑 |
| 验证结果 | `gyro_protocol_test`、`motor_protocol_test`、`gray_sensor_logic_test`、`oled_app_format_test` 通过；Keil Build 为 0 Error(s)、0 Warning(s) |
| 保留事项 | 本地仍有 `ti_msp_dl_config.c` 的 SysConfig 生成文件空白差异，未纳入功能提交 |

**经验记录**：
- 在裸机协作式调度中，耗时外设访问应优先拆成小步状态机，避免一个任务独占主循环。
- 串口日志必须允许忙时跳过或限频，调试输出不能成为实时路径上的阻塞点。
- OLED/I2C 等可拔插外设应有失败退避和初始化失败短路，避免硬件缺失时持续拖慢系统。
- SysConfig 生成文件应尽量避免手工修改；纯空白 churn 不应混入功能提交。


### Git Commits

| Hash | Message |
|------|---------|
| `8cb895b` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 5: Scheduler deadline stats optimization

**Date**: 2026-05-29
**Task**: Scheduler deadline stats optimization
**Branch**: `main`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 调度器优先级 | 将任务顺序调整为巡线、陀螺仪、电机、按键、UART、OLED、LED，优先保障关键实时任务。 |
| deadline 调度 | 将任务周期基准从 last_time 改为 deadline_ms，任务耗时不会直接重置下一次周期起点，减少长期漂移。 |
| 运行统计 | 新增 scheduler_task_id_t、scheduler_task_stats_t、Scheduler_GetTaskStats()、Scheduler_ClearTaskStats()，可读取 run_count、overrun_count、max_runtime_ms、last_runtime_ms、last_start_ms。 |
| 测试 | 新增 tests/scheduler_test.c，覆盖任务顺序、deadline 追赶和耗时/超期统计；并复跑现有陀螺仪、电机、灰度、OLED host 测试。 |

**验证结果**:
- scheduler tests passed
- gyro_protocol_test: all tests passed
- motor_protocol_test: all tests passed
- gray_sensor_logic_test: all tests passed
- oled_app_format_test: all tests passed
- `make -C gcc clean all` 未运行成功：本机未安装 make。

**Updated Files**:
- `User/scheduler.c`
- `User/scheduler.h`
- `tests/scheduler_test.c`


### Git Commits

| Hash | Message |
|------|---------|
| `2d814eb` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 6: Add selected motor control APIs

**Date**: 2026-05-29
**Task**: Add selected motor control APIs
**Branch**: `main`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 电机控制接口 | 为 `motor_app` 增加 `Motor_AppSetSpeed()`、`Motor_AppSetSpeed2()`、`Motor_AppStop()`，支持按 0~3 电机编号控制单路/两路/停止。 |
| 协议策略 | 保持底层 Modbus RTU 四路速度帧不变，App 层把单路/两路操作整理成完整四路速度快照发送。 |
| 两路场景 | `Motor_AppSetSpeed2()` 会把未指定的两路主动写 0，避免只用两路电机时残留旧速度。 |
| 测试 | 新增 `tests/motor_app_test.c`，使用 UART/tick 桩函数验证发送帧、状态缓存、非法索引拒绝和 CRC。 |
| GitHub | 已推送到 `origin/main`。 |

**验证**:
- `gcc -std=c99 -Wall -Wextra -Werror -IUser/App -IUser/Driver -IUser tests/motor_app_test.c User/App/motor_app.c User/App/motor_protocol.c -o tests/motor_app_test.exe; .\\tests\\motor_app_test.exe`
- `gcc -std=c99 -Wall -Wextra -Werror -IUser/App tests/motor_protocol_test.c User/App/motor_protocol.c -o tests/motor_protocol_test.exe; .\\tests\\motor_protocol_test.exe`
- `gcc -std=c99 -Wall -Wextra -Werror -IUser/App -IUser/Driver -IUser tests/oled_app_format_test.c User/App/oled_app.c -o tests/oled_app_format_test.exe; .\\tests\\oled_app_format_test.exe`
- `git diff --check`

**限制**:
- `make -C gcc clean all` 未运行成功，本机缺少 `make` 命令。


### Git Commits

| Hash | Message |
|------|---------|
| `40f3798` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
