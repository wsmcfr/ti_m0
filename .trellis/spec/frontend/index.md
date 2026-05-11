# Frontend Development Guidelines

> Current status and future rules for host-side UI or serial tools.

---

## Overview

This repository currently has no frontend application. It is an MSPM0G3507
embedded firmware project. The only user-facing runtime output is UART0 serial
text from firmware, for example the `GYRO,wz=...,yaw=...` telemetry line in
`User/App/gyro_app.c`.

The Trellis `frontend` layer is kept because the project was initialized as
`fullstack`, but future AI agents must not create a React/Vue/web application
unless the user explicitly asks for a PC-side serial tool, dashboard, or UI.

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Current no-frontend layout and future host-tool placement | Filled |
| [Component Guidelines](./component-guidelines.md) | Rules for future UI components and current serial output boundary | Filled |
| [Hook Guidelines](./hook-guidelines.md) | Future hook/composable constraints for serial tools | Filled |
| [State Management](./state-management.md) | Firmware runtime state vs future host UI state | Filled |
| [Quality Guidelines](./quality-guidelines.md) | Frontend-specific quality gates if a UI is later added | Filled |
| [Type Safety](./type-safety.md) | Type and protocol contract rules for future host tools | Filled |

---

## Pre-Development Checklist

Before doing anything in this layer:

1. Confirm the task is actually frontend/host-tool work. Firmware-only changes
   should use `.trellis/spec/backend/`.
2. If adding a PC UI, define the serial protocol contract first. Current
   firmware output examples live in `User/App/gyro_app.c`.
3. Read `directory-structure.md`, `state-management.md`, and
   `type-safety.md` before creating files.
4. Do not add package managers, web build systems, or UI folders to the firmware
   root unless the user explicitly requests the host application.

---

**Language**: Trellis guideline documentation is written in English. Source code
comments in this repository must follow the project `AGENTS.md` requirement and
be written in Chinese.
