

## project structure
- /BrightC-telemetry/doc/STRUCTURE.md

## documents
- All documents generated in English.

## Environment & Toolchain
- **Topology**: West T2 workspace. Workspace root contains `.west/`, `zephyr/`, `modules/`, and manifest application repo `BrightC-telemetry/`.
- **Python / West venv**: Activate `/home/jeffry/.zephyr-venv/bin/activate` or invoke `/home/jeffry/.zephyr-venv/bin/west` directly.
- **Zephyr SDK**: `/home/jeffry/zephyr-sdk-1.0.1` (set `export ZEPHYR_SDK_INSTALL_DIR=/home/jeffry/zephyr-sdk-1.0.1` if not inherited).
- **Working directory**: Execute build and test commands from `BrightC-telemetry/` (e.g., `cd BrightC-telemetry`).

## Build Commands
- **Target board**: `brightc-controllerv3` (STM32H743ZIT6, Cortex-M7 @ 480 MHz).
- **Firmware build**:
  ```bash
  cd BrightC-telemetry
  west build -b brightc-controllerv3 app
  ```
- **Primary build output** (matches `.vscode` compile commands path):
  ```bash
  west build -b brightc-controllerv3 app -d app/build/primary
  ```
- **Debug build** (verbose logs and assertions enabled):
  ```bash
  west build -b brightc-controllerv3 app -- -DEXTRA_CONF_FILE=debug.conf
  ```

## Testing & Verification
- **Run focused test suite immediately** (POSIX `native_sim`, executes binary via `-t run`):
  ```bash
  cd BrightC-telemetry
  west build -b native_sim tests/unit/modbus -t run
  west build -b native_sim tests/unit/lib -t run
  ```
  *(Note: Initial CMake configuration takes ~45-90s to evaluate DTS/Kconfig trees; subsequent runs rebuild in seconds).*
- **Run test suites via Twister**:
  ```bash
  west twister -T tests/unit/modbus -p native_sim -v --inline-logs
  ```
- **Run with code coverage (LCOV/gcov)**:
  ```bash
  west twister -T tests/unit -p native_sim --coverage -v
  ```
- **Domain purity check** (must return 0 matches; domain code has zero Zephyr dependencies):
  ```bash
  grep -rn "<zephyr/" app/src/domain/
  ```
- **Port encapsulation check** (must return 0 matches; application code never calls vtable directly):
  ```bash
  grep -rn "api->" app/src | grep -v "infrastructure/adapters/" | grep -v "infrastructure/di/"
  ```
- **Commit formatting**: CI enforces DCO sign-off. Always use `git commit -s`.

## Architecture & Code Invariants
- **Clean Architecture layers**:
  - `app/src/domain/`: Pure ISO C (C99/C11). Only standard libc headers (`stdint.h`, `stdbool.h`, `stddef.h`, `errno.h`). Absolutely NO `<zephyr/...>` or MCU register headers.
  - `app/include/interfaces/i_<name>.h`: Abstract ports. Every port must define: (1) `i_<name>_api_t` v-table, (2) `i_<name>_t { const i_<name>_api_t *api; void *impl; }` handle, and (3) `static inline` validated wrapper functions (`i_<name>_<op>()`) that check handles before dispatching.
  - `app/src/infrastructure/adapters/`: Hardware adapters implementing port v-tables using Zephyr drivers.
  - `app/src/infrastructure/di/composition_root.c`: The ONLY file permitted to invoke `DEVICE_DT_GET()` and bind port handles (`.api`, `.impl`).
  - `app/src/main.c`: Contains the only mutable static application object (`static app_container_t g_app`). All other components receive dependencies by pointer.
- **MPU Stack Guard Invariant**:
  - NEVER embed `k_thread_stack_t` inside structs in `.bss` (like `app_container_t`).
  - On Cortex-M7 with MPU stack protection (`CONFIG_HW_STACK_PROTECTION` / `CONFIG_MPU_STACK_GUARD`), embedding stacks inside structs breaks MPU hardware alignment and triggers fatal `MPU FAULT (Instruction Access Violation)`.
  - Always define thread stacks at file scope using `K_THREAD_STACK_DEFINE` or `K_THREAD_STACK_ARRAY_DEFINE`.
- **Zero dynamic memory**: No `malloc` or `k_malloc` in the execution loop. Use static allocations, fixed message queues (`k_msgq`), or static slabs.
- **Modbus / RS-485 quirks**:
  - RS485-1 (USART2) and RS485-2 (USART6) are half-duplex; each is exclusively owned by its Active Object thread (`ao_modbus`).
  - Never call `uart_configure()` with `FLOW_CTRL_NONE` on a bus with hardware Driver Enable (`de-mode = "hardware"`).
  - Runtime failover between RS485-1 and RS485-2 is supported via `i_slave_registry_set_bus()`.
- **Naming & Language**: Identifiers and code comments are in English; architecture documentation in `doc/` is in Spanish.

## Governance & Safety Invariants
- **Physical flashing**: NEVER run `west flash --runner openocd` on physical hardware without explicit human confirmation.
- **Critical files**: Halt and request human approval before modifying linker scripts (`*.ld`), reset assembly (`startup_*.s`), or CI workflows (`.github/workflows/`).
