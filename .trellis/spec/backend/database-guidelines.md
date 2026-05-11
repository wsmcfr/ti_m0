# Embedded Persistence And Configuration Guidelines

> This firmware has no database. Treat this file as the rule set for persistent
> configuration, generated hardware configuration, and nonvolatile state.

---

## Overview

There is no ORM, SQL database, migration system, filesystem, or server-side
state in this project. Persistent behavior currently comes from:

- SysConfig hardware configuration in `empty.syscfg`.
- Generated DriverLib symbols in `ti_msp_dl_config.c` and
  `ti_msp_dl_config.h`.
- Toolchain build metadata in `gcc/`, `ticlang/`, `iar/`, and `keil/`.
- External gyroscope module settings written through 5-byte UART commands in
  `User/App/gyro_protocol.c` and sequenced by `User/App/gyro_app.c`.

If a future task requires MCU flash/EEPROM-style state, create an explicit
driver/app contract first. Do not improvise ad hoc global persistence.

---

## Configuration Sources Of Truth

| Concern | Source of truth | Consumers |
|---------|-----------------|-----------|
| UART0/UART1 pins, baud rates, FIFO, DMA channels, RX timeout | `empty.syscfg` | `ti_msp_dl_config.*`, `User/Driver/uart_driver.c` |
| UART DMA RX buffer size | `UART_DRIVER_RX_DMA_BUFFER_SIZE` in `User/Driver/uart_driver.h` plus SysConfig transfer size 128 | UART driver, UART app, gyro app |
| Build output name | `NAME = M0G3507_Project` in makefiles and IDE project metadata | `gcc/makefile`, `ticlang/makefile`, `iar/makefile`, project files |
| Gyroscope command bytes | `User/App/gyro_protocol.c` and `User/App/gyro_protocol.h` | `User/App/gyro_app.c`, `tests/gyro_protocol_test.c` |

When changing any value above, search the repository for all copies first.
Several contracts are mirrored across generated config, makefiles, tests, and
headers.

---

## Persistence Patterns

- Firmware startup should not write sensor configuration or nonvolatile state by
  default. `Gyro_AppInit()` deliberately only initializes parser/state and logs
  readiness.
- External gyroscope settings require explicit user-level calls such as
  `Gyro_AppSetOutputRate()`, `Gyro_AppSetBaud()`, `Gyro_AppYawZero()`, and
  `Gyro_AppSaveConfig()`.
- Configuration writes follow the module manual's sequence:
  unlock, wait at least 100 ms, write command, optionally wait and save.
- Long calibration operations are explicit blocking APIs, for example
  `Gyro_AppAutoBiasBlocking()`, and must not be called from high-frequency
  scheduler tasks.
- Runtime counters such as `uart_driver_stats_t` are diagnostic state only; they
  are not persistent storage.

---

## Migration Rules

There are no database migrations. Use these firmware migration rules instead:

1. Hardware/peripheral migration starts in `empty.syscfg`.
2. Regenerate `ti_msp_dl_config.*` with the TI SysConfig flow.
3. Update Driver-layer resource tables that consume generated symbols, such as
   `s_uart_contexts` in `User/Driver/uart_driver.c`.
4. Update matching constants, comments, tests, and README documentation.
5. Verify host-side pure logic tests and, when the toolchain is available, the
   MCU build.

For new source files, update all active build systems. `gcc/makefile` already
lists `USER_C_FILES` and `OBJECTS`; new `.c` files must be added there.

---

## Naming Conventions

- Generated SysConfig names use upper snake case from `empty.syscfg`, for
  example `UART_PC`, `UART_GYRO`, `DMA_PC_RX`, and `LED_PORT`.
- Firmware constants use a module prefix and uppercase names, for example
  `GYRO_REG_BAUD`, `GYRO_APP_COMMAND_DELAY_MS`, and
  `UART_DRIVER_TX_WAIT_BASE`.
- External protocol values belong in the protocol module, not in App or Driver
  files.
- Runtime state that should reset on boot remains in static RAM with an `s_`
  prefix. Do not make it persistent unless the task explicitly requires it.

---

## Common Mistakes

- Editing `ti_msp_dl_config.*` directly. These files are generated and will be
  overwritten.
- Changing `UART_DRIVER_RX_DMA_BUFFER_SIZE` without checking
  `empty.syscfg` DMA transfer sizes for both UART RX channels.
- Saving gyroscope settings on every boot. That can cause unnecessary writes to
  the module's nonvolatile memory and makes startup behavior surprising.
- Introducing file/database terminology or dependencies into the firmware.
  There is no filesystem or database runtime on this target.
- Updating only one makefile or project file after adding a module.
