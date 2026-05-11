# Component Guidelines

> Current component status and future UI component rules.

---

## Overview

There are no frontend components in the current repository. The firmware
"interface" is UART text, not a component tree.

If the user later requests a host UI, components should be built around actual
serial-monitoring workflows: connect/disconnect, port settings, live telemetry,
command buttons, and diagnostics. Do not create a marketing landing page for an
engineering tool.

---

## Current Runtime Interface

Current UI-facing firmware output examples:

- `User/App/uart_app.c`: startup text through `my_printf()`.
- `User/App/gyro_app.c`: `GYRO,wz=...,yaw=...` telemetry line.
- `User/Driver/uart_driver.h`: `uart_driver_stats_t` fields that could become
  diagnostics if surfaced through a future command protocol.

Any future component that displays firmware data must treat those outputs as
external data, not as local mock-only content.

---

## Future Component Structure

If a frontend app is explicitly added, prefer small operational components:

- `SerialConnectionPanel`: port, baud rate, connect/disconnect state.
- `GyroTelemetryTable` or `GyroLiveReadout`: parsed Wz/Yaw values and freshness.
- `GyroCommandPanel`: buttons for unlock, save, yaw zero, output rate, and
  calibration workflows.
- `UartDiagnosticsPanel`: packet counts, overflow counts, timeout counts, if the
  firmware exposes them.

Components should receive parsed data and callbacks as props. Serial-port I/O
should live in a service or hook, not directly inside display components.

---

## Props Conventions

For future TypeScript UI work:

- Define explicit prop types/interfaces next to the component or in a local
  feature `types.ts`.
- Use domain names from firmware contracts: `angularVelocityZDps`, `yawZDeg`,
  `lastUpdateMs`, `parsedPacketCount`.
- Include validity flags where firmware has validity flags, for example
  `hasAngularVelocityZ` and `hasYawZ`.
- Do not use `any` for parsed serial payloads.

---

## Styling Patterns

No styling system exists today. If one is introduced:

- Keep the UI dense and operational; this is a firmware tool, not a landing
  page.
- Use clear status indicators for serial connection, stale data, and failed
  commands.
- Do not place engineering controls inside decorative marketing cards.
- Keep controls usable at desktop widths first, because serial tools are
  normally operated during bench testing.

---

## Accessibility

Future host UI controls must have accessible labels, visible focus states, and
keyboard-usable command buttons. Destructive or persistent commands such as
restore factory settings, save config, and calibration should be clearly
distinguishable and confirmable.

---

## Common Mistakes

- Building UI components before defining the serial data contract.
- Hard-coding mock telemetry instead of parsing real `GYRO,...` lines.
- Mixing serial I/O side effects into display components.
- Hiding risky firmware commands behind ambiguous labels.
