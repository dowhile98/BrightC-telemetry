# Developer & Agent Guidelines: BrightC-telemetry

Welcome to **BrightC-telemetry**, an industrial-grade IoT telemetry and edge-control firmware engineered for the **DAECGE BrightC Controller V3** (STM32H743ZIT6, Cortex-M7 @ 480 MHz) powered by **Zephyr RTOS**.

This document defines the authoritative engineering rules, architectural invariants, SOLID design principles, Clean Architecture layering, TDD practices, and reference guides that **MUST be strictly followed by all developers and AI agents**.

---

## 1. Environment & Toolchain Reference

- **Topology**: West T2 Workspace.
  - Workspace root: contains `.west/`, `zephyr/`, `modules/`, and application repository `BrightC-telemetry/`.

- **Execution Directory**:
  - All build, test, and audit commands must be executed from `BrightC-telemetry/` (e.g., `cd BrightC-telemetry`).

---

## 2. Architecture & Design Documentation Index

All architectural guidelines and subsystem specifications are maintained in `doc/architecture/` (written in English per project standards). Review the corresponding document before modifying any subsystem:

| Document | Scope & Authority |
| :--- | :--- |
| [`doc/STRUCTURE.md`](doc/STRUCTURE.md) | High-level repository layout, component classification, layer boundaries. |
| [`doc/architecture/active_object_architecture.md`](doc/architecture/active_object_architecture.md) | **Active Object Pattern**: 5-phase lifecycle, MPU stack guard invariants, zero static mutable state. |
| [`doc/architecture/dependency_injection_container_architecture.md`](doc/architecture/dependency_injection_container_architecture.md) | **Composition Root & DI Container**: Subsystem vertical grouping, 2-phase lifecycle (`init` & `wire`), DIP getters. |
| [`doc/architecture/ports_and_adapters_architecture.md`](doc/architecture/ports_and_adapters_architecture.md) | **Hexagonal Architecture (Ports & Adapters)**: Fat-pointer handles, ISP segregation, connectivity FSM, hardware injection rules. |
| [`doc/architecture/modbus_subsystem.md`](doc/architecture/modbus_subsystem.md) | Dual RS485 half-duplex buses, slave registries, nanoMODBUS adapters, failover orchestration. |
| [`doc/architecture/network_subsystem.md`](doc/architecture/network_subsystem.md) | STM32 RMII Ethernet, ESP-Hosted SPI Wi-Fi, DHCP/Static IP, Wi-Fi control plane, Zbus publishers. |
| [`doc/architecture/storage_subsystem.md`](doc/architecture/storage_subsystem.md) | Microconf EEPROM adapter, NVS flash partitions, configuration serialization and atomic commits. |
| [`doc/architecture/digital_input_subsystem.md`](doc/architecture/digital_input_subsystem.md) | MCP23S17 SPI expanders, on-chip GPIOs, debouncing filter, input state aggregator. |
| [`doc/architecture/system_indicators.md`](doc/architecture/system_indicators.md) | Diagnostic status LEDs, PWM buzzer melodies, fault pattern state machine. |
| [`doc/architecture/sensor_subsystem.md`](doc/architecture/sensor_subsystem.md) | Energy monitoring (JSY-MK-339), RTD temperature banks (CWT-TM-16PT), digital input modules (MBSL32DI). |
| [`doc/architecture/telemetry_store_forward_subsystem.md`](doc/architecture/telemetry_store_forward_subsystem.md) | SD card batching, offline spooling, store-and-forward telemetry pipeline. |
| [`doc/architecture/selftest_subsystem.md`](doc/architecture/selftest_subsystem.md) | Power-on self-test (POST), runtime peripheral sanity checks. |
| [`doc/architecture/mcuboot_external_flash_ota_architecture.md`](doc/architecture/mcuboot_external_flash_ota_architecture.md) | MCUboot dual-slot bootloader, external QSPI flash staging, dual-bank rollback. |

---

## 3. Implementation Traceability & Progress Maintenance (`doc/progress/`)

To ensure rigorous project tracking and auditability, **all developers and AI agents MUST maintain concise progress documents** under `doc/progress/`:

1. **Master Matrix**: [`doc/progress/README.md`](doc/progress/README.md) provides an executive overview of all subsystems with their current completion percentage, codebase coverage, and last update timestamp.
2. **Subsystem Tracking Files**: Every subsystem has a dedicated, concise tracking file:
   - [`doc/progress/core_architecture_progress.md`](doc/progress/core_architecture_progress.md): Active Objects, DI container, Ports & Adapters.
   - [`doc/progress/modbus_progress.md`](doc/progress/modbus_progress.md): Industrial Modbus subsystem.
   - [`doc/progress/digital_inputs_progress.md`](doc/progress/digital_inputs_progress.md): Digital inputs and contact alarms.
   - [`doc/progress/network_progress.md`](doc/progress/network_progress.md): Network link and Wi-Fi subsystem.
   - [`doc/progress/storage_progress.md`](doc/progress/storage_progress.md): Persistent storage subsystem.
   - [`doc/progress/system_indicators_progress.md`](doc/progress/system_indicators_progress.md): LED and Buzzer indicators.
   - [`doc/progress/sensors_progress.md`](doc/progress/sensors_progress.md): Sensor matrix and Mathematical Model Rev F.
   - [`doc/progress/sd_card_filesystem_progress.md`](doc/progress/sd_card_filesystem_progress.md): MicroSD and FatFS filesystem.
   - [`doc/progress/telemetry_store_forward_progress.md`](doc/progress/telemetry_store_forward_progress.md): Store & forward spooler.
   - [`doc/progress/selftest_progress.md`](doc/progress/selftest_progress.md): Post-boot self-test and diagnostics.
   - [`doc/progress/ota_mcuboot_progress.md`](doc/progress/ota_mcuboot_progress.md): Firmware OTA and MCUboot.
3. **Mandatory Update Rule**:
   - Whenever any component is added, modified, or refactored—**even if the subsystem is already at 100%**—the developer or AI agent **MUST update the corresponding file in `doc/progress/`**.
   - Record the changes concisely under the subsystem's `Changelog & Refactoring History` section with date, modified files, and summary.
   - If completion status changes, update the status percentage both in the subsystem file and in `doc/progress/README.md`.
4. **Conciseness Invariant**: Progress files must be structured and brief (~40–80 lines: status header, component table, pending checklist, changelog bullets). Avoid verbose conversational narratives.

---

## 4. SOLID Principles in Embedded C

Every module in BrightC-telemetry must strictly uphold the SOLID design principles:

### 3.1 Single Responsibility Principle (SRP)
- **Domain Layer (`app/src/domain/`)**: ONLY contains pure algorithms, mathematical models, codecs, and state transitions. No RTOS calls, no hardware registers.
- **Ports (`app/include/interfaces/`)**: ONLY define pure abstract contracts (`i_<name>_api_t`, `i_<name>_t`, validated wrappers).
- **Adapters (`app/src/infrastructure/adapters/`)**: ONLY adapt a specific hardware peripheral, bus, or RTOS driver to satisfy an interface port contract.
- **Active Objects (`app/src/application/active_objects/`)**: ONLY coordinate event queues, execute thread loops, and publish state changes to Zbus.
- **Composition Root (`app/src/infrastructure/di/composition_root.c`)**: ONLY instantiates concrete adapters, probes hardware devicetree, wires dependencies, and exposes DIP getters.

### 3.2 Open/Closed Principle (OCP)
- Modules are **open for extension, closed for modification**.
- Adding a new sensor, relay controller, storage medium, or network interface is accomplished by adding a new adapter implementing an existing port `i_*.h`, **without altering existing Active Objects or domain logic**.

### 3.3 Liskov Substitution Principle (LSP)
- Any concrete adapter implementing port `i_<name>_api_t` (as well as in-memory test fakes in `tests/fakes/`) must be 100% interchangeable without changing the correctness of the consumer.
- All implementations must respect contract pre-conditions (defensive NULL handling, returning negative POSIX errno on failure, consistent return semantics).

### 3.4 Interface Segregation Principle (ISP)
- Clients must not be forced to depend on interfaces they do not use.
- **Example**: Network link management is segregated into:
  - `i_network_t`: Generic link lifecycle (`init`, `start`, `stop`, `get_config`, `set_config`, `get_status`) implemented by both Ethernet and Wi-Fi.
  - `i_wifi_control_t`: Dedicated wireless control (`scan`, `connect_ap`, `disconnect`, `set_mode`) implemented exclusively by Wi-Fi adapters.
- Methods never accept redundant discriminator arguments (e.g., `network_interface_t iface`) when the port handle already encapsulates the target adapter instance.

### 3.5 Dependency Inversion Principle (DIP)
- High-level modules (Active Objects, use cases) **MUST NEVER** depend on low-level modules (Zephyr drivers, hardware adapters). Both must depend on abstract ports (`app/include/interfaces/i_*.h`).
- Concrete dependencies are resolved and injected exclusively at startup by `composition_root.c`.

---

## 5. Fundamental Architectural Invariants (Non-Negotiable)

Any PR or commit violating any of the following invariants will be rejected by CI and architectural review:

### Invariant 1: Rule §4.1.D Port Contract & Fat-Pointer Encapsulation
Every port header (`app/include/interfaces/i_<name>.h`) MUST declare:
1. `i_<name>_api_t`: The operation function pointer table (v-table).
2. `i_<name>_t`: The fat-pointer handle combining `{ const i_<name>_api_t *api; void *impl; }`.
3. `i_<name>_is_bound(const i_<name>_t *port)`: Safety validation predicate verifying that both `api`, `impl`, and mandatory function pointers are non-NULL.
4. `static inline i_<name>_<op>()`: Public type-safe wrappers performing defensive checks (handle validation, non-NULL buffers) before dispatching to `api-><op>()`.
> **STRICT RULE**: Application, Active Object, and domain code **MUST NEVER** dereference `port->api->...` directly. Direct v-table access is strictly forbidden outside the adapter implementing the port and the composition root.
> Audit check:
> ```bash
> grep -rn "api->" app/src | grep -v "infrastructure/adapters/" | grep -v "infrastructure/di/"
> # MUST return 0 matches
> ```

### Invariant 2: Hardware Device Injection Isolation
Adapters (`app/src/infrastructure/adapters/`) **MUST NEVER** call `DEVICE_DT_GET()` or probe devicetree directly.
- All Zephyr device pointers (`const struct device *`) MUST be obtained in `composition_root.c` and passed explicitly to `adapter_<name>_init()`.
- This ensures adapters can be 100% unit tested on `native_sim` using mock/stub devices without physical DTS binding.
> Audit check:
> ```bash
> grep -rn "DEVICE_DT_GET" app/src/infrastructure/adapters/
> # MUST return 0 matches
> ```

### Invariant 3: Pure Domain Layer
Headers in `app/src/domain/` and `app/include/common/` MUST NOT include any `<zephyr/...>` headers or vendor HAL/register headers.
- Permitted dependencies: standard libc headers (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<errno.h>`, `<string.h>`).
> Audit check:
> ```bash
> grep -rn "<zephyr/" app/src/domain/
> # MUST return 0 matches
> ```

### Invariant 4: MPU Stack Guard Invariant (Cortex-M7 STM32H743)
On Cortex-M7 with MPU stack protection (`CONFIG_HW_STACK_PROTECTION` and `CONFIG_MPU_STACK_GUARD`), stack buffers require strict hardware alignment (32-byte MPU subregion alignment).
- **NEVER** embed `k_thread_stack_t` inside structs in `.bss` (such as `app_container_t` or Active Object structs). Doing so corrupts MPU alignment and triggers fatal `MPU FAULT (Instruction Access Violation)`.
- Always define thread stacks at file scope using `K_THREAD_STACK_DEFINE` or `K_THREAD_STACK_ARRAY_DEFINE` in `composition_root.c` or test runners.

### Invariant 5: Zero Dynamic Memory Allocation
- `malloc`, `free`, `k_malloc`, `k_free`, and `k_heap` are **strictly forbidden** in runtime loops.
- All memory (Active Object contexts, command queues, bouncing buffers, network frames) MUST be allocated statically at initialization or embedded within context structures.

### Invariant 6: Zero File-Scope Mutable State
- No module may define mutable `static` variables at file scope (e.g., `static uint8_t s_buffer[2048]`).
- All state must reside inside instance context structures (`ao_<name>_t`, `adapter_<name>_t`, `app_container_t`) passed by pointer.
- The ONLY allowed mutable static application context is `static app_container_t g_app;` in `app/src/main.c`.

### Invariant 7: Active Object Canonical 5-Phase Lifecycle
Every Active Object (`ao_*`) must implement the canonical lifecycle:
1. `ao_<name>_init(ao_<name>_t *self, ...)`: Zeroes memory, copies configs/ports, creates thread in gated/dormant state.
2. `ao_<name>_start(ao_<name>_t *self)`: Posts start event, sets `is_running = true`, transitions thread to inner loop.
3. `ao_<name>_stop(ao_<name>_t *self)`: Posts stop command, requests loop exit.
4. `ao_<name>_wait_stopped(ao_<name>_t *self, k_timeout_t timeout)`: Blocks on `stopped_sem` until thread cleanly exits inner loop.
5. `ao_<name>_deinit(ao_<name>_t *self)`: Terminates thread, releases synchronization primitives.

---

## 6. Test-Driven Development (TDD) Workflow

Every new feature, bugfix, or refactoring MUST follow the **Red-Green-Refactor** workflow:

### 5.1 Test Levels & Frameworks
1. **Unit Tests (Zephyr Ztest on `native_sim`)**:
   - Fast execution on host Linux.
   - Uses in-memory fakes (`tests/fakes/`) to simulate hardware (GPIO, SPI, Modbus, EEPROM, Network).
   - Located in `tests/unit/<subsystem>/`.
2. **Integration Tests (Ztest & Twister)**:
   - Automated testing of multi-AO synchronization, Zbus event routing, and DI container wiring.
3. **Hardware-in-the-Loop (HIL)**:
   - Physical execution on `brightc-controllerv3` using ST-Link / OpenOCD.

### 5.2 Build & Execution Commands

#### Run Unit Tests on `native_sim`
Use dedicated build directories `-d build/<suite>` to prevent CMake cache conflicts when switching suites:
```bash
# Network test suite
west build -b native_sim tests/unit/network -d build/test-network -t run

# Modbus test suite (10 suites, 99 tests)
west build -b native_sim tests/unit/modbus -d build/test-modbus -t run

# Digital Inputs test suite
west build -b native_sim tests/unit/digital_input -d build/test-di -t run

# System Indicators test suite
west build -b native_sim tests/unit/indicators -d build/test-ind -t run

# Storage test suite
west build -b native_sim tests/unit/storage -d build/test-storage -t run
```

#### Run All Tests via Twister
```bash
west twister -T tests/unit -p native_sim -v --inline-logs
```

#### Measure Code Coverage (LCOV)
```bash
west twister -T tests/unit -p native_sim --coverage -v
# View HTML report generated under twister-out/coverage/index.html
```

#### Target Firmware Build (Physical Board)
```bash
# Primary target build
west build -p -b brightc-controllerv3 app -d build/primary

# Verify memory footprint:
# Flash must remain < 50% (2 MB flash)
# RAM must remain < 50% (512 KB AXI SRAM)
```

---

## 7. Coding Standards & Conventions

1. **Language & Standards**:
   - C Standard: **ISO C99 / C11**.
   - Identifiers, types, and comments: **English**.
   - Architecture documentation in `doc/`: **English** (user-facing explanations in chat may be in Spanish).
2. **Naming Conventions**:
   - Interfaces (Ports): `app/include/interfaces/i_<concept>.h`, v-table `i_<concept>_api_t`, handle `i_<concept>_t`, wrappers `i_<concept>_<op>()`.
   - Adapters: `app/src/infrastructure/adapters/adapter_<peripheral>_<bus>.c`, context `adapter_<peripheral>_<bus>_t`, port factory `adapter_<peripheral>_<bus>_port()`.
   - Active Objects: `app/src/application/active_objects/ao_<name>.c`, context `ao_<name>_t`, thread stack `AO_<NAME>_STACK_SIZE`, queue depth `AO_<NAME>_QUEUE_DEPTH`.
   - Pure Types: `app/include/common/<subsystem>_types.h`.
3. **Error Handling**:
   - All public functions return `int` with standard negative POSIX errno (`-EINVAL`, `-ENODEV`, `-ENOSYS`, `-ETIMEDOUT`, `-EALREADY`, `-EBUSY`, `-ENOMEM`).
   - Success is indicated by `0`.
   - Functions never panic or call `k_panic()` unless a fatal hardware initialization invariant fails in `main.c`.
4. **Git Commit Format**:
   - Conventional Commits: `feat(modbus): ...`, `fix(network): ...`, `refactor(di): ...`, `test(storage): ...`.
   - DCO Sign-off required: Always use `git commit -s`.

---

## 8. Safety & Governance Invariants

AI agents and automated tools MUST halt and request explicit human confirmation before:
- Flashing physical hardware via OpenOCD, J-Link, or ST-Link (`west flash`).
- Modifying linker scripts (`*.ld`), memory region mappings, or reset startup assembly (`startup_*.s`).
- Modifying CI/CD pipelines (`.github/workflows/`).
- Running `git push` to remote repositories.
