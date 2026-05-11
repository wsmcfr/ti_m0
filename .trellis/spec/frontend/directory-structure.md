# Frontend Directory Structure

> Current no-frontend status and placement rules for any future host UI.

---

## Overview

There is currently no `src/`, `components/`, `pages/`, `hooks/`, or frontend
package in this repository. Do not add one as part of normal firmware work.

Existing user-facing pieces are firmware serial outputs:

- `User/App/uart_app.c`: `my_printf()` sends PC-facing text on UART0.
- `User/App/gyro_app.c`: `Gyro_AppPrintLatestIfDue()` emits
  `GYRO,wz=...,yaw=...` telemetry.
- `README.md`: documents the firmware project and configured build output name.

---

## Current Layout

```text
.
|-- User/App/uart_app.c          # PC serial output function, not a frontend UI
|-- User/App/gyro_app.c          # Serial telemetry producer
|-- tests/gyro_protocol_test.c   # Host-side C test, not a UI test suite
|-- docs/plans/                  # Planning documents
`-- README.md                    # Project overview
```

There is no current frontend app directory.

---

## Future Host Tool Placement

If the user explicitly asks for a PC-side serial monitor, dashboard, or
configuration tool, create it outside the firmware runtime directories. Prefer a
clearly named folder such as:

```text
tools/serial-monitor/
```

or:

```text
host-tools/<tool-name>/
```

The future host tool must keep its package manager files, source tree, tests,
and build artifacts inside that folder so it does not pollute the MCU firmware
root.

---

## Module Organization

For future host UI work:

- Keep serial parsing logic separate from visual components.
- Treat firmware UART lines as an external protocol contract.
- Keep generated assets and frontend build outputs out of firmware build
  folders such as `gcc/`, `ticlang/`, `iar/`, and `keil/`.
- Do not import or rewrite embedded C files from the UI. Share protocol details
  through documented constants or generated fixtures only if a task creates
  that bridge.

---

## Naming Conventions

- Future host tool folders should use lower kebab case:
  `tools/serial-monitor`.
- UI source files should follow the framework chosen for that future task, but
  must not reuse firmware names in a way that confuses host and target code.
- Protocol-related frontend modules should reference the firmware signal name
  directly, for example `gyroTelemetry`, because the current serial line prefix
  is `GYRO`.

---

## Anti-Patterns

- Do not add `package.json`, `vite.config.*`, `src/`, or `node_modules/` at the
  firmware root unless the user explicitly requests a frontend app.
- Do not put frontend code in `User/`.
- Do not change firmware serial output formats casually to suit a UI. Update the
  firmware contract, tests, and frontend parser together.
- Do not describe firmware UART text output as a web API.
