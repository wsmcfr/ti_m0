# Firmware Logging Guidelines

> UART text output and telemetry conventions for the MSPM0G3507 firmware.

---

## Overview

There is no logging library. Runtime text output goes through UART0/PC using
`my_printf()` in `User/App/uart_app.c`. UART1/GYRO is reserved for the gyroscope
module and should not be used for human-readable logs.

Logging must be sparse because UART0 is configured for 115200 baud and TX uses a
blocking DMA completion wait. High-rate telemetry must be throttled.

---

## Log Levels

The firmware does not implement formal log levels. Use lightweight tags instead:

| Tag style | Purpose | Examples |
|-----------|---------|----------|
| `[UART] ...` | One-time UART initialization or UART app status | `Uart_AppInit()` |
| `[GYRO] ...` | One-time gyroscope protocol/application status | `Gyro_AppInit()` |
| `GYRO,...` | Machine-readable sensor telemetry | `Gyro_AppPrintLatestIfDue()` |

If a future task needs warning/error text, use short tagged lines such as
`[UART][WARN] ...`, but prefer counters for high-frequency or ISR-detected
events.

---

## Structured Output

Current structured telemetry is CSV-style text:

```text
GYRO,wz=<deg_per_second>,yaw=<degrees>\r\n
```

`Gyro_AppPrintLatestIfDue()` converts floats to milli-units before formatting so
the firmware does not depend on floating-point `printf` support. Follow this
pattern for future sensor output.

Telemetry rules:

- Prefix each machine-readable line with a stable token such as `GYRO`.
- Use `\r\n` line endings for serial terminals.
- Keep numeric formats stable once external tools may parse them.
- Throttle periodic output. The current gyroscope print interval is
  `GYRO_APP_PRINT_INTERVAL_MS`.

---

## What To Log

- One-time startup readiness messages from App-layer init functions:
  `Uart_AppInit()` and `Gyro_AppInit()`.
- Low-rate sensor summaries intended for a PC serial assistant or future host
  tool, such as the `GYRO,wz=...,yaw=...` line.
- Manual command workflow results only when the user-facing task needs it.
  Return values still remain the primary error contract.

---

## What Not To Log

- Do not call `my_printf()` from interrupt handlers such as
  `UART_PC_INST_IRQHandler()` or `UART_GYRO_INST_IRQHandler()`.
- Do not log every received UART byte or every parsed gyroscope frame at high
  output rates.
- Do not use floating-point `%f` formatting in firmware logs. Use fixed-point
  formatting like `Gyro_AppFloatToMilli()`.
- Do not log from Driver-layer hot paths unless explicitly debugging a hardware
  issue and the output is temporary.
- Do not send PC/debug logs over UART1, because UART1 is the gyroscope protocol
  link.

---

## Real Examples

- `User/App/uart_app.c`: `my_printf()` formats into a static buffer and sends
  through UART0 DMA.
- `User/App/gyro_app.c`: `Gyro_AppPrintLatestIfDue()` throttles sensor output
  and avoids float `printf`.
- `User/Driver/uart_driver.c`: error events increment counters instead of
  printing from ISR context.
