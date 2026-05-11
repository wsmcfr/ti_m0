# Firmware Error Handling

> How errors are represented, propagated, and recovered in the bare-metal C
> firmware.

---

## Overview

This project uses C return values, defensive guards, counters, and quick
recovery paths. There are no exceptions and no centralized error middleware.

The normal conventions are:

- `bool` return values for operation success/failure, especially command send
  flows such as `Gyro_AppSendRawCommand()` and `Uart_DriverWrite()`.
- `0` length for "no packet" or invalid read requests, as in
  `Uart_DriverReadPacket()`.
- Typed result enums for protocol parsing, as in
  `gyro_protocol_parse_result_t`.
- Diagnostic counters for hardware communication issues, as in
  `uart_driver_stats_t`.
- Null-pointer and bounds checks before dereferencing caller-provided buffers.

---

## Error Types

| Pattern | Example | Meaning |
|---------|---------|---------|
| Boolean status | `Uart_DriverWrite(...) -> bool` | `true` means the operation completed; `false` means parameters, port, or timeout failed |
| Length result | `Uart_DriverReadPacket(...) -> uint16_t` | `0` means no packet or invalid read request |
| Parse enum | `GYRO_PROTOCOL_PARSE_BAD_CHECKSUM` | Exact frame parsing failure reason |
| Stats struct | `uart_driver_stats_t` | Runtime diagnostics for RX overflow, RX errors, TX packets, and TX timeouts |
| Has flags | `gyro_app_data_t.has_yaw_z` | Field-level validity for partial sensor samples |

Do not collapse these into a single global error flag. The current code keeps
errors local to the module that can recover or expose useful diagnostics.

---

## Error Handling Patterns

### Defensive API guards

Public and static helpers validate pointers and sizes before use. Examples:

- `GyroProtocol_ParserInit()` returns immediately on a null parser.
- `GyroProtocol_ParseFrame()` returns `GYRO_PROTOCOL_PARSE_NULL` or
  `GYRO_PROTOCOL_PARSE_SHORT` before reading frame bytes.
- `Uart_DriverReadPacket()` returns `0U` if the port, buffer, or capacity is
  invalid.

### Bounded blocking waits

Blocking transmit code must include a timeout guard. `Uart_DriverWrite()` uses
`UART_DRIVER_TX_WAIT_BASE` and `UART_DRIVER_TX_WAIT_PER_BYTE` to avoid an
infinite wait if DMA or UART completion interrupts fail.

### ISR recovery

UART interrupt handling is centralized in `Uart_DriverHandleIrq()`. It loops
over pending IIDX events, captures RX packets on timeout/full-buffer events,
sets TX completion flags on TX events, and clears bad RX data before restarting
RX DMA on error events.

### Cross-layer failure propagation

App-layer command functions stop their sequence when a lower-level call fails.
For example, `Gyro_AppYawZero()` returns `false` if unlock, yaw-zero command, or
save fails. Do not continue a configuration sequence after a failed prerequisite.

---

## Boundary Contracts

| Boundary | Contract | Failure behavior |
|----------|----------|------------------|
| Driver -> App RX | `Uart_DriverReadPacket()` copies at most `max_length` bytes | Returns `0U` if no complete packet or invalid arguments |
| App -> Driver TX | `Uart_AppSendToPc()` and `Uart_AppSendToGyro()` forward to DMA TX | Return `false` on invalid input or timeout |
| Protocol -> App | `GyroProtocol_FeedBytes()` updates sample flags only for parsed frames | Returns `false` when no valid frame is found |
| Gyro App -> Other modules | `Gyro_AppGetLatest()` copies latest snapshot | Returns `false` if output pointer is null |

---

## Common Mistakes

- Ignoring return values from configuration commands when the next command
  depends on the previous one.
- Adding unbounded `while` waits around hardware flags. Every blocking hardware
  wait needs a timeout or a clearly documented reason.
- Logging from ISR paths. Capture state or increment counters instead.
- Treating `0` from `Uart_DriverReadPacket()` as an error only. It also means
  "no packet available this scheduler cycle."
- Clearing parser state on every scheduler tick. `gyro_protocol_parser_t` must
  preserve half-frame state across DMA idle packets.
