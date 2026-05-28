# MSPM0 Motor Gray OLED Key Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add motor UART protocol/app, gray tracking sensor, OLED, and key modules to the MSPM0G3507 firmware.

**Architecture:** Hardware register access stays in `User/Driver/`; business behavior and scheduler tasks stay in `User/App/`; pure Modbus/gray math logic is kept hardware-free where practical for host-side tests. SysConfig is the source of truth for UART3, I2C1, ADC0/DMA, gray address GPIO, and key GPIO.

**Tech Stack:** TI MSPM0 DriverLib/SysConfig, C99 bare-metal firmware, cooperative scheduler, host-side GCC tests for pure logic.

---

### Task 1: Hardware Resource Contract

**Files:**
- Modify: `empty.syscfg`
- Regenerate: `ti_msp_dl_config.c`
- Regenerate: `ti_msp_dl_config.h`

**Steps:**
1. Add UART3 as `UART_MOTOR`, 115200 baud, RX/TX DMA and RX timeout interrupt on `PB3/PB2`.
2. Add I2C1 as `I2C_OLED` on `PA30/PA29`.
3. Add ADC0 as `ADC_GRAY` on `PA27`, with DMA channel not conflicting with UART DMA channels.
4. Add gray address GPIO `PA15/PA14/PA13`.
5. Add key GPIO `PA7/PB10/PB11/PB14` with pull-up input.
6. Regenerate generated config through the project SysConfig flow.
7. Verify generated headers expose all expected symbols.

### Task 2: Host Tests For Pure Logic

**Files:**
- Create: `tests/motor_protocol_test.c`
- Create: `tests/gray_sensor_logic_test.c`

**Steps:**
1. Write failing tests for Modbus CRC, motor speed frame construction, PID frame construction, and encoder response parsing.
2. Write failing tests for gray calibration normalization, binary conversion, and line position error calculation.
3. Run host tests and confirm failures are due to missing functions.

### Task 3: Motor Protocol And App

**Files:**
- Create: `User/App/motor_protocol.h`
- Create: `User/App/motor_protocol.c`
- Create: `User/App/motor_app.h`
- Create: `User/App/motor_app.c`
- Modify: `User/App/uart_app.h`
- Modify: `User/App/uart_app.c`
- Modify: `User/Driver/uart_driver.h`
- Modify: `User/Driver/uart_driver.c`

**Steps:**
1. Extend UART logical port table with `UART_DRIVER_PORT_MOTOR`.
2. Add motor UART app send/read helpers.
3. Implement pure Modbus CRC, frame builders, and encoder parser.
4. Implement motor app init/task and public speed/PID/read APIs.
5. Run motor protocol host test and verify pass.

### Task 4: Gray Sensor Driver And Line Tracking App

**Files:**
- Create: `User/Driver/gray_sensor_driver.h`
- Create: `User/Driver/gray_sensor_driver.c`
- Create: `User/App/line_track_app.h`
- Create: `User/App/line_track_app.c`

**Steps:**
1. Implement ADC DMA setup/read helper for selected gray channel samples.
2. Implement address-line channel selection for 8-channel gray sensor.
3. Implement calibration, normalization, binary output, and line error calculation.
4. Run gray logic host test and verify pass.

### Task 5: OLED Driver And App

**Files:**
- Create: `User/Driver/oled_driver.h`
- Create: `User/Driver/oled_driver.c`
- Create: `User/App/oled_app.h`
- Create: `User/App/oled_app.c`

**Steps:**
1. Port SSD1306 command/data writes from HAL I2C to MSPM0 DriverLib I2C1.
2. Keep OLED drawing APIs bounded for 128x64 page addressing.
3. Add a low-rate OLED app task showing motor/gray/key summaries without high-frequency I2C traffic.

### Task 6: Key Driver And App

**Files:**
- Create: `User/Driver/key_driver.h`
- Create: `User/Driver/key_driver.c`
- Create: `User/App/key_app.h`
- Create: `User/App/key_app.c`

**Steps:**
1. Read four active-low key GPIOs.
2. Implement debounce and down/up/hold event tracking.
3. Expose lightweight getters for other app modules.

### Task 7: Scheduler And Build Integration

**Files:**
- Modify: `User/scheduler.c`
- Modify: `gcc/makefile`
- Modify: `ticlang/makefile`
- Modify: `iar/makefile`
- Modify: `keil/empty_LP_MSPM0G3507_nortos_keil.uvprojx`

**Steps:**
1. Register key, line tracking, motor, and OLED app init/task calls.
2. Add all new `.c` files to active makefiles and Keil project.
3. Run host tests.
4. Run target build if toolchain is available.
5. Inspect `git diff --check` and changed file list.
