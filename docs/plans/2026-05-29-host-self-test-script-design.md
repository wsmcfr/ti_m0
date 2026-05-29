# Host Self-Test Script Design

## Goal

Add one PowerShell script that lets the firmware project run all host-side
checks before hardware is available.

## Scope

The script validates code that can run on the PC:

- scheduler timing behavior through `tests/scheduler_test.c`
- gyroscope protocol parsing through `tests/gyro_protocol_test.c`
- gray sensor pure logic through `tests/gray_sensor_logic_test.c`
- motor protocol building and parsing through `tests/motor_protocol_test.c`
- motor App state behavior through `tests/motor_app_test.c`
- OLED App line formatting through `tests/oled_app_format_test.c`

The script also attempts target firmware build only when `make` is available.
On this machine `make` is currently unavailable, so the target build is reported
as skipped instead of failing the host self-test by default.

## Design

Create `scripts/test_project.ps1`.

The script uses local `gcc` to compile each host test into an isolated
`tests/build/<run-id>/` directory, then runs the produced executable
immediately. Each test has an explicit name, compile command, run command, and
pass/fail result.

Script options:

- `-SkipTargetBuild`: do not attempt `make -C gcc clean all`
- `-RequireTargetBuild`: fail if target build is skipped or unavailable
- `-KeepArtifacts`: keep generated test executables under `tests/build/<run-id>/`

## Limits

This script cannot prove physical UART, I2C, ADC, DMA, OLED, key, motor, or
gyroscope wiring behavior. Those checks need the board and peripherals. When
hardware arrives, add a separate smoke-test workflow that uses serial logs,
driver counters, and visible OLED/motor/sensor behavior.

## Expected Use

```powershell
.\scripts\test_project.ps1 -SkipTargetBuild
```

For a stricter environment with `make` and the TI toolchain installed:

```powershell
.\scripts\test_project.ps1 -RequireTargetBuild
```
