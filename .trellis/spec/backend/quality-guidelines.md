# Firmware Quality Guidelines

> Code quality standards for MSPM0G3507 firmware work.

---

## Overview

This project is C99 bare-metal firmware built on TI DriverLib and SysConfig.
Quality means preserving deterministic scheduler behavior, keeping hardware
access in driver wrappers, making protocol logic testable on the host, and
keeping generated configuration in sync with hand-written code.

Source comments must follow `AGENTS.md`: Chinese comments are required for
new code, including function headers, core structures, key variables, and
non-obvious branches.

---

## Forbidden Patterns

- No dynamic allocation (`malloc`, `free`) in firmware runtime code unless a
  future design explicitly approves it.
- No business logic in ISR handlers. ISRs should capture bytes/events, update
  flags/counters, restart hardware, and return.
- No long blocking delays inside periodic scheduler tasks. Blocking command
  flows must be explicit APIs like `Gyro_AppAutoBiasBlocking()`.
- No direct edits to generated `ti_msp_dl_config.*` files.
- No duplicate protocol constants across App, Driver, and tests. Keep protocol
  command bytes in `gyro_protocol.*`.
- No unbounded waits for DMA/UART/hardware flags.
- No high-rate logging or float `printf` usage in firmware loops.

---

## Required Patterns

- Use unsigned tick-difference checks for elapsed time:
  `(uint32_t)(now - last) >= interval`. `Scheduler_Run()` and
  `Gyro_AppDelayMs()` show this pattern.
- Keep periodic tasks short and non-blocking. Add new periodic work through the
  `Scheduler_Task` table in `User/scheduler.c`.
- Guard public APIs against null pointers and invalid lengths before accessing
  caller memory.
- Keep hardware symbols and DriverLib calls in `User/Driver/` unless the file is
  generated or the task has an explicit reason.
- Use App-layer APIs to compose driver and protocol behavior.
- Keep pure protocol logic independent of MCU registers so it can be tested with
  a desktop compiler.
- Update all build metadata when adding or renaming source files.
- Preserve existing function/file naming conventions and Chinese source
  comments.

---

## Testing Requirements

At minimum, run the host-side protocol test after changing
`User/App/gyro_protocol.*` or gyroscope command behavior:

```powershell
gcc -IUser/App tests/gyro_protocol_test.c User/App/gyro_protocol.c -o tests/gyro_protocol_test.exe
.\tests\gyro_protocol_test.exe
```

When the embedded toolchain is available, also build the target project:

```powershell
make -C gcc clean all
```

Known local limitation: this machine may not have `make` installed. If so,
report that the target build could not be run and still run any host-side tests
that do not require the TI toolchain.

Recommended test expansion:

- Add host-side tests for any pure parser, encoder, checksum, or state machine.
- For DriverLib-dependent code, document the manual hardware test case and the
  expected UART/GPIO behavior.
- For scheduler changes, test tick wrap behavior through unsigned differences
  when practical.

---

## Code Review Checklist

- Does `empty.c` remain limited to init, main loop, and SysTick handoff?
- Are new periodic tasks registered in `User/scheduler.c` with a justified
  interval?
- Are DriverLib and NVIC/DMA operations isolated in Driver-layer files?
- Are all buffer lengths checked before `memcpy`, DMA setup, or parser access?
- Does every blocking loop have a bounded timeout or a documented manual
  command reason?
- Are UART RX/TX shared buffers protected against ISR/main-loop races, as
  `Uart_DriverReadPacket()` does by disabling the specific UART IRQ?
- Are generated SysConfig values, makefiles, and tests synchronized?
- Do new source comments explain function purpose, key parameters, return
  values, boundary cases, and resource/state changes in Chinese?

---

## Real Examples

- `User/scheduler.c`: task table, 1 ms tick usage, unsigned overflow-safe timing.
- `User/Driver/uart_driver.c`: bounded DMA waits, UART IRQ loop, RX ready buffer
  protection, and diagnostic counters.
- `User/App/gyro_protocol.c` plus `tests/gyro_protocol_test.c`: pure protocol
  code with host-side tests.
