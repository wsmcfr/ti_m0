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

## Executable Runtime Contracts

### Scheduler Deadline Contract

Scope / trigger: use this contract when changing `User/scheduler.c`,
`User/scheduler.h`, scheduler task intervals, or task runtime diagnostics.

Signatures:

```c
bool Scheduler_Run(void);
bool Scheduler_GetTaskStats(scheduler_task_id_t task_id,
    scheduler_task_stats_t *out_stats);
void Scheduler_ClearTaskStats(scheduler_task_id_t task_id);
```

Contracts:

- A task whose `deadline_ms` has passed is executed at most once during a single
  `Scheduler_Run()` scan.
- `Scheduler_Run()` returns `true` when at least one task ran in the scan and
  returns `false` when no task was due; `empty.c` may use `false` to enter WFI.
- Low-priority or I/O-heavy tasks may use `initial_offset_ms` in the task table
  to avoid concentrating UART, OLED, and LED work on the same millisecond.
- After a task returns, the next deadline is scheduled from the task end tick:
  `deadline_ms = Scheduler_GetTick() + interval_time_ms`.
- The scheduler must not replay every missed historical period after a long
  blocking operation. Historical lateness is diagnostic data, not work debt.
- `scheduler_task_stats_t.last_lateness_ms` records
  `now_time - deadline_ms` for the most recent run.
- `scheduler_task_stats_t.max_lateness_ms` records the maximum observed
  lateness.
- `scheduler_task_stats_t.missed_deadline_count` accumulates complete missed
  periods: `lateness_ms / interval_time_ms`.
- `Scheduler_ClearTaskStats()` must clear runtime, overrun, lateness, and missed
  deadline counters together.

Validation and error matrix:

| Case | Required behavior | Test point |
|------|-------------------|------------|
| Task starts exactly at deadline | `last_lateness_ms == 0` | scheduler host test |
| Low-priority tasks have offsets | UART/LED/OLED do not all run on the same 100 ms boundary | scheduler host test |
| No task is due | `Scheduler_Run()` returns `false`, main loop may call `__WFI()` | scheduler host test / hardware smoke test |
| Task runs longer than its interval | `overrun_count++`, next immediate scan does not rerun it | scheduler host test |
| Tick jumps far past deadline | task runs once, missed periods are counted | scheduler host test |
| Invalid task ID or null stats pointer | `Scheduler_GetTaskStats()` returns `false` | scheduler host test when expanded |

Good/base/bad cases:

- Good: a 1 ms task delayed until 150 ms runs once and records lateness/missed
  periods.
- Good: UART, OLED, and LED use explicit initial offsets so display/log/heartbeat
  work does not pile up on the same tick boundary.
- Base: a task with no lateness keeps lateness counters at zero.
- Bad: a `while` loop advances `deadline_ms` one period at a time and lets a
  high-frequency task immediately run again before lower-priority tasks can
  progress.
- Bad: ignoring the `Scheduler_Run()` return value in the main loop and keeping
  the CPU spinning at full speed when no task is due.

Wrong vs correct:

```c
/* Wrong: replays every historical period and can starve lower-priority tasks. */
while (deadline_is_in_the_past) {
    task->deadline_ms += task->interval_time_ms;
}

/* Correct: run once, record lateness, and schedule the next future deadline. */
task->deadline_ms = Scheduler_GetTick() + task->interval_time_ms;

/* Correct: idle main loop can sleep until the next SysTick or UART interrupt. */
if (Scheduler_Run() == false) {
    __WFI();
}
```

Tests required:

```powershell
gcc -std=c99 -Wall -Wextra -Werror -DSCHEDULER_HOST_TEST -IUser -IUser/App -IUser/Driver tests/scheduler_test.c User/scheduler.c -o tests/scheduler_test.exe
.\tests\scheduler_test.exe
```

Assertions must cover task order, initial deadline offsets, no immediate rerun
after overrun, the `Scheduler_Run()` boolean return, `missed_deadline_count`,
`last_lateness_ms`, `max_lateness_ms`, runtime stats, and clearing all stats
fields.

### Non-Blocking Gray ADC Sampling Contract

Scope / trigger: use this contract when changing
`User/Driver/gray_sensor_driver.*` or the runtime portion of
`User/App/line_track_app.c`.

Signatures:

```c
bool GraySensor_DriverStartSampleSelected(void);
gray_sensor_sample_status_t GraySensor_DriverPollSampleSelected(uint16_t *out_value);
void GraySensor_DriverAbortSampleSelected(void);
bool GraySensor_DriverSelectChannel(uint8_t channel);
```

Contracts:

- Scheduler tasks must use Start/Poll/Abort for periodic gray sensor sampling;
  they must not busy-wait for ADC/DMA completion.
- `GraySensor_DriverStartSampleSelected()` starts one sample on the currently
  selected channel and returns without waiting.
- `GraySensor_DriverPollSampleSelected()` returns `BUSY` without blocking until
  the ADC/DMA completion flag is present.
- `READY` writes exactly one ADC sample to `out_value`, stops ADC/DMA, clears
  the DMA-done flag, and releases the busy state.
- `Abort` stops ADC/DMA, clears the DMA-done flag, and releases the busy state.
- `GraySensor_DriverSelectChannel()` must reject channel changes while a
  non-blocking sample is busy so channel selection and ADC result cannot drift.
- The App layer owns frame assembly, channel averaging, timeout policy, and
  snapshot publishing.

Validation and error matrix:

| Case | Required behavior | Owner |
|------|-------------------|-------|
| Start while idle | DMA/ADC start, status becomes busy | Driver |
| Poll before completion | returns `GRAY_SENSOR_SAMPLE_STATUS_BUSY` | Driver |
| Poll after completion with null output | aborts sample and returns `ERROR` | Driver |
| Channel change while busy | returns `false` | Driver |
| App timeout | aborts sample and discards half-frame | App |
| Complete 8-channel frame | publishes one coherent snapshot | App |

Good/base/bad cases:

- Good: `LineTrack_AppTask()` starts or polls at most one ADC sample per call.
- Base: existing pure calibration/normalization/position logic stays host
  testable with `LINE_TRACK_HOST_TEST`.
- Bad: reading all 8 channels in one scheduler task with synchronous ADC waits.

Wrong vs correct:

```c
/* Wrong: a periodic task waits inside the ADC loop. */
while (adc_dma_done == 0U) {
    wait_count--;
}

/* Correct: start now, return, and poll in a later scheduler call. */
(void)GraySensor_DriverStartSampleSelected();
status = GraySensor_DriverPollSampleSelected(&sample);
```

Tests required:

```powershell
gcc -std=c99 -Wall -Wextra -Werror -DLINE_TRACK_HOST_TEST -IUser/App tests/gray_sensor_logic_test.c User/App/line_track_app.c -o tests/gray_sensor_logic_test.exe
.\tests\gray_sensor_logic_test.exe
```

Hardware validation should confirm that `LineTrack_AppGetSnapshot()` becomes
valid after complete frames, ADC timeout does not freeze the scheduler, and
`Scheduler_GetTaskStats(SCHEDULER_TASK_ID_LINE_TRACK, ...)` does not show
repeated overruns during normal sampling.

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
