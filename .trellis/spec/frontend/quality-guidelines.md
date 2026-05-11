# Frontend Quality Guidelines

> Quality standards for the currently absent frontend layer and any future host
> UI.

---

## Overview

There is no frontend build, lint, or test command today. For firmware-only work,
use the firmware quality rules in `.trellis/spec/backend/quality-guidelines.md`.

If a host UI is explicitly added later, it must come with its own lint, type
check, and tests inside the host-tool folder.

---

## Forbidden Patterns

- Do not add frontend dependencies or generated web artifacts to the firmware
  root for firmware-only tasks.
- Do not create a marketing page when the user asks for an engineering serial
  tool.
- Do not change firmware UART output format without updating parser tests and
  firmware documentation.
- Do not make UI command buttons send persistent or calibration commands without
  confirmation and clear state feedback.
- Do not hide serial parse errors; bench tools need visible diagnostics.

---

## Required Patterns

For future host UI work:

- Keep all host-tool files in a dedicated folder such as `tools/serial-monitor/`.
- Provide parser tests for real firmware serial lines.
- Provide type checks if TypeScript is used.
- Keep UI state synchronized with real serial connection state.
- Document the firmware/UI protocol contract with examples.
- Keep firmware changes and host UI changes reviewed as a cross-layer contract
  when both are touched.

---

## Testing Requirements

Current frontend tests: not applicable.

Future host UI tests should cover:

- Serial line parser fixtures, especially `GYRO,wz=...,yaw=...`.
- Connection state transitions: disconnected, connecting, connected, error.
- Command confirmation flows for save, restore, yaw zero, and calibration.
- Stale telemetry display when no new data arrives.
- Basic accessibility checks for command controls.

If firmware protocol output changes, run both firmware protocol tests and host
parser tests.

---

## Code Review Checklist

- Is this truly frontend/host-tool work, or should it be firmware-only?
- Is the host tool isolated from MCU build folders?
- Are serial protocol assumptions documented and tested?
- Are invalid, missing, stale, and negative telemetry values handled?
- Are risky device commands protected against accidental clicks?
- Are lint/type/test commands documented for the new host tool?

---

## Real Current References

- `User/App/gyro_app.c`: current UART telemetry producer.
- `User/App/uart_app.c`: UART0 text output implementation.
- `tests/gyro_protocol_test.c`: model for fixture-based protocol tests.
