# State Management

> Current firmware state and future host UI state rules.

---

## Overview

There is no frontend state library in this repository. Current runtime state is
embedded C state in firmware:

- `uwTick` in `User/scheduler.c` is the 1 ms scheduler clock.
- Static App-layer state such as `s_gyro_latest` and `s_gyro_last_print_ms` in
  `User/App/gyro_app.c` caches current sensor data and print timing.
- Static Driver-layer state such as `s_uart_contexts` in
  `User/Driver/uart_driver.c` tracks UART DMA buffers, flags, and counters.
- Protocol parser state such as `gyro_protocol_parser_t` preserves partial
  frames across UART idle packets.

Frontend state rules apply only if a host UI is explicitly added later.

---

## Current State Categories

| State | Owner | Notes |
|-------|-------|-------|
| Scheduler tick | `User/scheduler.c` | Incremented from SysTick ISR through `Scheduler_TickInc()` |
| Latest gyroscope data | `User/App/gyro_app.c` | Exposed through `Gyro_AppGetLatest()`, `Gyro_AppGetYaw()`, and `Gyro_AppGetGyroZ()` |
| UART RX/TX flags and counters | `User/Driver/uart_driver.c` | Protected where needed with UART IRQ disable/enable |
| Protocol partial frame buffer | `gyro_protocol_parser_t` | Must persist across calls to `GyroProtocol_FeedBytes()` |

Do not move this firmware state into a frontend concept unless creating a real
host application.

---

## Future Host UI State

If a host UI is added, separate state into:

- Connection state: selected port, baud rate, connected/disconnected, errors.
- Stream state: raw lines, parse errors, stale-data detection, last packet time.
- Domain state: latest Wz/Yaw values, validity flags, diagnostics.
- Command state: in-flight command, last result, confirmation state for
  persistent actions.

Keep global state minimal. Use local component state for UI-only controls and a
single serial session store/service for shared connection state.

---

## Server State

There is no server state. Do not add server-state tooling for the current
firmware project. Serial telemetry is live stream state and should be handled as
such.

---

## Derived State

Future host tools may derive:

- Data freshness from `now - lastUpdateMs`.
- Display text from numeric telemetry.
- Warning states from UART overflow or timeout counters if firmware exposes
  them.

Do not store derived state separately unless there is a measured performance or
workflow reason.

---

## Common Mistakes

- Resetting stream parser state whenever UI state updates.
- Treating missing telemetry as zero instead of using firmware-style validity
  flags.
- Storing persistent gyroscope settings in a UI without confirming they were
  actually saved on the device.
- Adding global state libraries before the host tool has real shared state.
