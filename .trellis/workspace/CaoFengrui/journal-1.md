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
