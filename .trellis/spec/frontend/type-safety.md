# Type Safety

> Type and protocol contract rules for future host tools.

---

## Overview

The current project uses C types in firmware, not TypeScript. Important firmware
contracts include:

- `gyro_protocol_parse_result_t` for parse outcomes.
- `gyro_protocol_sample_t` for frame-level parsed values and validity flags.
- `gyro_app_data_t` for latest application-level sensor data.
- `uart_driver_port_t` and `uart_driver_stats_t` for UART driver contracts.

If a future frontend/host tool is added, its types must mirror the firmware
serial contract explicitly instead of relying on untyped string handling.

---

## Type Organization

For future TypeScript host tools:

- Define serial protocol types in a protocol-focused module, for example
  `serial/types.ts` or `features/gyro/types.ts`.
- Keep UI prop types close to components when they are component-specific.
- Use firmware-derived names where helpful, converted to idiomatic TypeScript:
  `angularVelocityZDps`, `yawZDeg`, `rawAngularVelocityZ`, `rawYawZ`,
  `lastUpdateMs`, `parsedPacketCount`.
- Represent validity flags explicitly:
  `hasAngularVelocityZ`, `hasYawZ`.

---

## Runtime Validation

Current firmware telemetry is text. A future parser must validate before updating
UI state:

- Confirm the line prefix, for example `GYRO`.
- Confirm expected key/value fields exist.
- Reject or mark invalid values that fail numeric parsing.
- Preserve the previous valid value only when the UI clearly indicates data
  freshness/staleness.

If a validation library is introduced, keep it inside the host tool folder and
use it only where runtime input crosses the serial boundary.

---

## Common Patterns

Future host parser tests should include:

- A valid `GYRO,wz=...,yaw=...` line.
- Negative values.
- Missing fields.
- Non-numeric fields.
- Extra fields for forward compatibility.

These cases match the firmware's existing emphasis on explicit parse results in
`GyroProtocol_ParseFrame()` and host-side tests in `tests/gyro_protocol_test.c`.

---

## Forbidden Patterns

- Do not use `any` for serial payloads.
- Do not cast unvalidated serial strings to typed telemetry objects.
- Do not assume missing fields are zero. Use validity flags or parse errors.
- Do not duplicate firmware command bytes in UI code without a documented
  contract and tests.
