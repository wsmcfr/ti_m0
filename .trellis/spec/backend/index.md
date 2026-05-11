# Firmware Development Guidelines

> Project-specific rules for the MSPM0G3507 bare-metal firmware.

---

## Overview

This repository is a TI MSPM0G3507 DriverLib/SysConfig firmware project, not a
web backend. In Trellis, the `backend` layer maps to the firmware runtime:
startup code, the cooperative scheduler, application modules, driver wrappers,
protocol parsing, and host-side unit tests.

The current firmware is organized around:

- `empty.c`: SysConfig initialization, `System_Init()`, and the forever loop.
- `User/scheduler.*`: 1 ms tick and cooperative periodic task dispatch.
- `User/App/*`: application-level LED, UART, and gyroscope behavior.
- `User/Driver/*`: hardware-facing GPIO/UART/DMA wrappers.
- `empty.syscfg` and `ti_msp_dl_config.*`: SysConfig-owned hardware contract.
- `tests/gyro_protocol_test.c`: host-side tests for pure protocol logic.

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Firmware module organization and file layout | Filled |
| [Database Guidelines](./database-guidelines.md) | Embedded persistence and generated configuration rules | Filled |
| [Error Handling](./error-handling.md) | C return-status, recovery, ISR, and stats patterns | Filled |
| [Quality Guidelines](./quality-guidelines.md) | Firmware review standards and tests | Filled |
| [Logging Guidelines](./logging-guidelines.md) | UART logging, telemetry, and throttling rules | Filled |

---

## Pre-Development Checklist

Before changing firmware code, read the files that match the work:

| Work type | Must read |
|-----------|-----------|
| New module, file move, build file change | `directory-structure.md`, `quality-guidelines.md` |
| SysConfig, peripheral, DMA, UART, or generated config change | `database-guidelines.md`, `error-handling.md`, `quality-guidelines.md` |
| Driver or ISR work | `error-handling.md`, `logging-guidelines.md`, `quality-guidelines.md` |
| Protocol parsing or command generation | `directory-structure.md`, `error-handling.md`, `quality-guidelines.md` |
| UART text output or telemetry | `logging-guidelines.md`, `quality-guidelines.md` |

Always also read `.trellis/spec/guides/index.md` for cross-layer and reuse
checks.

---

**Language**: Trellis guideline documentation is written in English. Source code
comments in this repository must follow the project `AGENTS.md` requirement and
be written in Chinese.
