# Hook Guidelines

> Current hook status and future stateful logic rules for host tools.

---

## Overview

There are no React hooks, Vue composables, or frontend data-fetching utilities in
the current firmware repository.

If a future host UI is added, hooks/composables should isolate serial-port
side effects, protocol parsing, and command sequencing from presentational
components.

---

## Future Custom Hook Patterns

For React-style tools, likely hooks would be:

- `useSerialConnection`: open/close the selected serial port and expose
  connection status.
- `useGyroTelemetry`: parse firmware `GYRO,wz=...,yaw=...` lines into typed
  telemetry state.
- `useGyroCommands`: send command bytes or higher-level command requests if the
  firmware exposes a PC command protocol.

For other frameworks, use equivalent composables/services with the same
separation of concerns.

---

## Data Fetching

Current firmware does not expose HTTP or server data. A future UI will likely
consume serial streams, not fetch from a server.

Rules:

- Do not add React Query, SWR, or HTTP client libraries unless a real HTTP API
  exists.
- Treat serial input as an event stream with reconnect, stale-data, and parse
  error states.
- Keep parsing deterministic and testable with captured line fixtures.
- Preserve firmware timing constraints. Do not flood UART0 with commands faster
  than firmware and the external module can safely process.

---

## Naming Conventions

- React hooks must start with `use`.
- Use firmware domain names in hook names: `useGyroTelemetry`,
  `useUartDiagnostics`, `useSerialConnection`.
- Parser helpers that are not hooks should not use the `use` prefix.
- Command hooks must distinguish transient commands from persistent operations
  such as save, restore, or calibration.

---

## Common Mistakes

- Creating frontend hooks for firmware-only changes.
- Treating UART telemetry like HTTP request/response data.
- Retrying persistent gyroscope writes automatically without user intent.
- Reinitializing parsers on every render and losing partial serial data.
