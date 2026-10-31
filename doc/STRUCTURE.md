# Software Architecture and Project Structure BrightC Telemetry
**Master Guide for Clean Architecture, TDD and Zephyr RTOS**  
*Platform: DAECGE BrightC Controller V3 (STM32H743ZIT6 LQFP144)*  
*Execution Environment: Zephyr RTOS (West T2 Topology)*

---

## 1. Architectural Vision and Guiding Principles

The **BrightC Telemetry** firmware is designed under the principles of **Clean Architecture (Ports and Adapters / Hexagonal Architecture)** and **Test-Driven Development (TDD)**, leveraging the capabilities of the **Zephyr RTOS** ecosystem.

```
 +-----------------------------------------------------------------------+
 |                     PRESENTATION & CLI (Shell)                        |
 |         (HMI DWIN UART3, Zephyr Shell UART1, HTTP/SNMP Handlers)      |
 +-----------------------------------+-----------------------------------+
                                      |
                                      v
 +-----------------------------------------------------------------------+
 |                        APPLICATION LAYER                              |
 |       (Active Objects, Use Cases, Orchestration, Zephyr Zbus, SMF)    |
 +------------------+---------------------------------+------------------+
                     |                                 |
                     v                                 v
 +------------------------------------+   +------------------------------+
 |          DOMAIN LAYER              |   |      PORTS / INTERFACES      |
 | (Pure C99/C11 Logic, Algorithms,   |   | (Abstract contracts in C:    |
 |  Models, Pure State Machines)      |   |  i_modbus.h, i_storage.h,    |
 |    *ZERO Zephyr dependencies*      |   |  i_sensor.h, i_network.h)    |
 +------------------------------------+   +--------------+---------------+
                                                          ^
                                                          | (Implements)
 +-------------------------------------------------------+---------------+
 |                      INFRASTRUCTURE LAYER                               |
 | (Zephyr Adapters: GPIO, NVS/Settings, Ethernet LAN8742, WiFi ESP32)      |
 |       (Composition Root / Dependency Injection: composition_root.c)      |
 +---------------------------------------------------------------------------+
                                      |
                                      v
 +-----------------------------------------------------------------------+
 |                HARDWARE & DEVICETREE (DTS / Overlays)                 |
 |            (boards/daecge/brightc-controllerv3, app/boards/)          |
 +-----------------------------------------------------------------------+
```

### 1.1 Fundamental Invariants

1. **Unidirectional Dependency Rule**: Inner layers never know outer layers. The Domain does not know about Zephyr, FreeRTOS, STM32, or any physical hardware.
2. **Domain Purity**: Every file in `domain/` compiles with standard C (`stdint.h`, `stdbool.h`, `stddef.h`). It is **strictly prohibited** to include `#include <zephyr/...>` or silicon registers in the Domain.
3. **Dependency Inversion (IoC)**: Software interacts with the outside world through abstract interfaces (*ports* in `include/interfaces/`). Infrastructure implements those ports (*adapters*).
4. **Devicetree as Single Source of Truth for Hardware**: There are no `bsp.c` files or CubeMX calls. All pin configuration (pinctrl), clocks, and buses reside in `.dts` and `.overlay` files.
5. **Immediate Testability (TDD)**: All business and application logic can be tested in milliseconds on the host machine (Host PC or `native_sim`) without connecting the physical board.
6. **Zero Malloc in the Execution Loop**: Static memory, preallocated pools (`k_mem_slab`, static `k_heap`) and strongly typed channels at compile time.
7. **Stack Definition and MPU Protection**: Zephyr thread stacks (`k_thread_stack_t`) **MUST NEVER** be embedded inside structures (`struct` in `.bss`), such as the container `app_container_t`. In microcontrollers with Cortex-M MPU (Cortex-M7 STM32H7) and `CONFIG_HW_STACK_PROTECTION` / `CONFIG_MPU_STACK_GUARD`, Zephyr requires stacks to reside in hardware-aligned dedicated sections with their respective MPU guards. Embedding a stack inside a struct misaligns memory and corrupts MPU regions, triggering critical hardware faults such as `MPU FAULT (Instruction Access Violation)`. It is **mandatory** to define stacks at file scope using `K_THREAD_STACK_DEFINE` or `K_THREAD_STACK_ARRAY_DEFINE`.

---

## 2. Canonical Project Structure (West T2 Topology)

The Zephyr workspace organizes the project through the **West T2 topology**, where `BrightC-telemetry` is the application and manifest repository.

```text
BrightC_TelemetryWorkspace/                  # Root of the West Workspace
├── .west/                                   # Local west metadata
├── zephyr/                                  # Zephyr RTOS source tree (submodule/upstream)
├── modules/                                 # External modules (nanoMODBUS, microconf, hal_stm32, etc.)
│   └── lib/
│       ├── nanoMODBUS/
│       ├── microconf/
│       └── zephyr-snmp/
│
└── BrightC-telemetry/                        # Main Application Repository (This Repo)
    ├── west.yml                             # West manifest (pinning dependencies and versions)
    ├── CMakeLists.txt                       # Zephyr module-level CMake
    ├── Kconfig                              # Zephyr module-level Kconfig
    ├── zephyr/
    │   └── module.yml                       # Module declaration (dts_root, board_root)
    │
    ├── boards/                              # Custom board definitions (Hardware Base)
    │   └── daecge/
    │       └── brightc-controllerv3/        # BrightC BaseBoard V3 (STM32H743ZIT6)
    │           ├── board.cmake              # Flash configuration (openocd / runner)
    │           ├── board.yml                # Board metadata
    │           ├── brightc-controllerv3.dts # Complete Devicetree (pinctrl, clocks, buses, PHY)
    │           ├── brightc-controllerv3.yaml# Capabilities (RAM, Flash, peripherals)
    │           ├── brightc-controllerv3_defconfig # Default board configuration
    │           ├── Kconfig.brightc-controllerv3
    │           └── support/
    │               └── openocd.cfg          # OpenOCD configuration (ST-Link / SWD)
    │
    ├── dts/                                 # Project Devicetree extensions
    │   └── bindings/                        # YAML bindings for proprietary devices
    │
    ├── drivers/                             # Out-of-tree Zephyr Drivers
    │   ├── CMakeLists.txt
    │   ├── Kconfig
    │   └── sensor/                          # Drivers implementing native Zephyr sensor API
    │
    ├── lib/                                 # Cross-cutting libraries and utilities
    │   ├── CMakeLists.txt
    │   └── Kconfig
    │
    ├── app/                                 # Main Firmware Application
    │   ├── CMakeLists.txt                   # Application CMake (target 'app')
    │   ├── Kconfig                          # Application Kconfig menu
    │   ├── prj.conf                         # Base Kconfig common to production
    │   ├── debug.conf                       # Debug Kconfig overlay (verbose logs, asserts)
    │   ├── VERSION                          # Semantic firmware version
    │   │
    │   ├── boards/                          # Application-specific hardware overlays
    │   │   ├── brightc-controllerv3.overlay # Assignment of app functions to DTS nodes
    │   │   ├── brightc-controllerv3.conf    # V3-specific Kconfig options
    │   │   └── native_sim.overlay           # Peripheral simulation (emulated UARTs, etc.)
    │   │
    │   ├── include/                         # Ports and Interfaces (Pure Contracts)
    │   │   ├── interfaces/                  # Abstract interfaces (V-Tables / C contracts)
    │   │   │   ├── i_telemetry_port.h       # Send/receive telemetry contract
    │   │   │   ├── i_modbus_client.h        # Modbus transport contract (FC03, FC06, etc.)
    │   │   │   ├── i_slave_registry.h       # Slave manager and scheduling contract
    │   │   │   ├── i_config_storage.h       # Modular EEPROM/NVS persistence contract
    │   │   │   ├── i_network.h              # Network interface contract (Ethernet / WiFi)
    │   │   │   ├── i_led.h                  # User LED contract
    │   │   │   ├── i_buzzer.h               # Buzzer signaling contract
    │   │   │   ├── i_relay.h                # Discrete output / relay control contract
    │   │   │   ├── i_sensor_port.h          # Analog measurement acquisition contract
    │   │   │   ├── i_temperature_bank.h     # Multi-channel temperature read contract
    │   │   │   └── i_display_port.h         # DWIN HMI interface contract
    │   │   └── common/                      # Agnostic primitive data types
    │   │       ├── config_types.h           # Persistent configuration domains and events
    │   │       ├── indicator_types.h        # Indicator types (LED, buzzer, alarms)
    │   │       ├── modbus_types.h           # Bus structs, descriptors, results
    │   │       ├── telemetry_types.h        # Measurement, alarm, timestamp structs
    │   │       └── error_codes.h            # Unified error codes
    │   │
    │   └── src/                             # Application source code
    │       ├── domain/                      # 100% PURE: Business logic (No <zephyr/...>)
    │       │   ├── models/                  # Entities (TelemetryFrame, AlarmState, DeviceProfile)
    │       │   ├── services/                # Business rules (AlarmEvaluator, CalibrationEngine)
    │       │   └── sm/                      # Pure state machines (TelemetryFSM)
    │       │
    │       ├── application/                 # Use cases and Orchestration (Uses Zephyr Zbus / RTOS)
    │       │   ├── active_objects/          # Asynchronous execution threads (Actors)
    │       │   │   ├── ao_telemetry.c       # Coordinator thread for acquisition and sampling
    │       │   │   ├── ao_modbus.c          # Modbus service thread (RS485-1 / RS485-2)
    │       │   │   ├── ao_storage.c         # Async persistence coordinator thread EEPROM
    │       │   │   ├── ao_network.c         # Connectivity thread (Ethernet / WiFi ESP32)
    │       │   │   ├── ao_system_indicator.c# Indicators thread (LED / Buzzer)
    │       │   │   └── ao_hmi.c             # DWIN LCD HMI thread
    │       │   ├── zbus/                    # Zephyr Zbus event channels
    │       │   │   ├── telemetry_channels.h # zbus_channel definition (samples, alarms)
    │       │   │   ├── telemetry_channels.c # ZBUS_CHAN_DEFINE instantiation
    │       │   │   ├── indicator_channels.h # Indicator event channels
    │       │   │   └── indicator_channels.c # ZBUS_CHAN_DEFINE instantiation for indicators
    │       │   └── use_cases/               # Flows (ProcessTelemetrySample, PublishMqttReport)
    │       │
    │       ├── infrastructure/              # Adapters to Zephyr and composition
    │       │   ├── adapters/                # Port implementations using Zephyr APIs
    │       │   │   ├── adapter_modbus_nmbs.c      # nanoMODBUS over Zephyr UART/RS485
    │       │   │   ├── adapter_storage_microconf.c# Persistence microconf over EEPROM AT24C256C
    │       │   │   ├── adapter_sensor_zephyr.c    # Sensor reading using <zephyr/drivers/sensor.h>
    │       │   │   ├── adapter_relay_gpio.c       # Relay handling using <zephyr/drivers/gpio.h>
    │       │   │   ├── adapter_wifi_esp_hosted.c  # ESP-Hosted network driver (WiFi / BLE)
    │       │   │   ├── adapter_led_gpio.c         # User LED control via GPIO
    │       │   │   ├── adapter_buzzer_pwm.c       # Buzzer control via PWM
    │       │   │   └── adapter_display_dwin.c     # DWIN serial parser over Zephyr UART3
    │       │   └── di/                      # Composition Root (Dependency Injection)
    │       │       ├── composition_root.h   # Startup API of the dependency graph
    │       │       └── composition_root.c   # DEVICE_DT_GET and port-adapter binding
    │       │
    │       ├── presentation/                # User Interface / HMI
    │       │   ├── dwin/                    # Model-View-Presenter (MVP) for DWIN LCD
    │       │   │   ├── dwin_presenter.c     # Variable presentation logic
    │       │   │   └── dwin_views.c         # Screen frame formatting
    │       │   └── shell/                   # Zephyr CLI commands (Diagnostics and Calibration)
    │       │       └── telemetry_shell.c    # SHELL_CMD_REGISTER commands for UART1 terminal
    │       │
    │       └── main.c                       # Application entry point
    │
    ├── tests/                               # Automated Test Suites (TDD)
    │   ├── testcase.yaml                    # Global descriptor for 'twister' runner
    │   ├── unit/                            # Isolated Unit Tests (Run in native_sim)
    │   │   ├── domain/                      # Business logic tests (100% mocked)
    │   │   │   ├── test_alarm_evaluator.c
    │   │   │   └── test_telemetry_fsm.c
    │   │   └── application/                 # Use case and Zbus tests
    │   │       └── test_process_sample.c
    │   ├── fakes/                           # Fakes and In-Memory Implementations for tests
    │   │   ├── include/
    │   │   │   ├── fake_storage_port.h
    │   │   │   ├── fake_sensor_port.h
    │   │   │   └── fake_relay_port.h
    │   │   └── src/
    │   │       ├── fake_storage_port.c
    │   │       ├── fake_sensor_port.c
    │   │       └── fake_relay_port.c
    │   ├── integration/                     # Integration tests in native_sim / Emulators
    │   │   └── test_modbus_loopback.c
    │   └── hil/                             # Tests on Real Hardware (BrightC Controller V3)
    │       └── test_adc_accuracy.c
    │
    └── doc/                                 # Architecture and Reference Documentation
        ├── STRUCTURE.md                     # This master architecture document
        ├── architecture/                    # Master technical specifications per subsystem
        │   ├── modbus_subsystem.md          # Complete Modbus subsystem architecture
        │   ├── network_subsystem.md         # Network subsystem architecture (Ethernet / WiFi)
        │   ├── storage_subsystem.md         # EEPROM persistence subsystem architecture
        │   ├── sensor_subsystem.md          # Sensor ingestion & hardware-agnostic routing architecture
        │   └── system_indicators.md         # System indicators architecture (LED / Buzzer)
        ├── brightcV3.md                     # HW schematic and pinctrl reference
        └── Doxyfile                         # API documentation generator
```

---

## 3. Component Classification Matrix (Decision Tree)

When you need to create a new file or functionality, answer the following questions in order:

| Design Question | Architectural Classification | Location | Invariants and Restrictions |
| :--- | :--- | :--- | :--- |
| **Does it contain business rules, alarm calculations, mathematical conversions, or telemetry states?** | **Domain** | `app/src/domain/` | C99/C11 pure. Only libc headers (`stdint.h`). **PROHIBITED** to include `<zephyr/...>` or microcontroller registers. |
| **Does it define the abstract contract of a service to decouple the domain from HW or OS?** | **Port / Interface** | `app/include/interfaces/` | Header-only (`i_*.h`). Function pointer structures (v-tables) or typed C APIs. |
| **Is it an asynchronous continuous process, an actor processing messages from a queue or channel?** | **Application / Active Object** | `app/src/application/active_objects/` | Owns its own thread/stack or workqueue; communicates exclusively via **Zbus** or message queues. |
| **Does it implement a port interacting with Zephyr drivers, RTOS, or external libraries?** | **Adapter (Infrastructure)** | `app/src/infrastructure/adapters/` | Contains `#include <zephyr/drivers/...>`. Translates native Zephyr types to Domain types. |
| **Where are Devicetree devices obtained and connected to adapters?** | **Composition Root (DI)** | `app/src/infrastructure/di/` | Only place where `DEVICE_DT_GET()` macros are invoked to inject dependencies upward. |
| **Is it a command for the debug serial console or a graphical HMI screen?** | **Presentation / Shell** | `app/src/presentation/` or `app/src/shell/` | Uses Zephyr Shell or Model-View-Presenter (MVP) pattern. Does not manipulate hardware directly. |
| **Is it a driver for a hardware chip implementing Zephyr driver model?** | **Out-of-Tree Driver** | `drivers/<subsystem>/` | Uses `DEVICE_DT_DEFINE`, devicetree bindings (`dts/bindings/`) and standard APIs (`sensor_driver_api`, etc.). |
| **Does it configure physical pins, clocks, flash partitions, or board peripheral instances?** | **Devicetree / BSP** | `boards/` or `app/boards/` | `.dts`, `.overlay` and `.defconfig` files. Zero C code. |

---

## 4. Implementation of Key Patterns in Zephyr

### 4.1 Inversion of Control: Ports and Adapters in C

#### A. The Abstract Port (No Zephyr Dependencies)

Every port lives in a single header `app/include/interfaces/i_<name>.h` and consists of three inseparable pieces:

1. The **v-table** `i_<name>_api_t`: the function pointers implemented by the adapter.
2. The **handle** `i_<name>_t`: `const ... *api` (the v-table) plus `void *impl` (opaque adapter state).
3. One **`static inline` function per operation**, `i_<name>_<operation>()`, that validates the handle and arguments **before** dispatching through the v-table.

```c
#ifndef APP_INTERFACES_I_RELAY_H_
#define APP_INTERFACES_I_RELAY_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { RELAY_CHANNEL_MAIN_POWER = 0, RELAY_CHANNEL_AUXILIARY, RELAY_CHANNEL_MAX } relay_channel_t;
typedef enum { RELAY_STATE_OPEN = 0, RELAY_STATE_CLOSED } relay_state_t;

static inline bool relay_channel_is_valid(relay_channel_t channel)
{
    return (unsigned int) channel < (unsigned int) RELAY_CHANNEL_MAX;
}

/* 1. V-table: implementation detail, never called from the application */
typedef struct {
    int (*set_state)(void *impl, relay_channel_t channel, relay_state_t state);
    int (*get_state)(void *impl, relay_channel_t channel, relay_state_t *out_state);
} i_relay_api_t;

/* 2. Handle injected into the port consumer */
typedef struct {
    const i_relay_api_t *api;
    void *impl;
} i_relay_t;

static inline bool i_relay_is_bound(const i_relay_t *relay)
{
    return relay != NULL && relay->api != NULL && relay->impl != NULL;
}

/* 3. Validated wrappers: the only allowed path to the v-table */
static inline int i_relay_set_state(const i_relay_t *relay, relay_channel_t channel,
                                    relay_state_t state)
{
    if (!i_relay_is_bound(relay) || !relay_channel_is_valid(channel)) {
        return -EINVAL;
    }

    return relay->api->set_state(relay->impl, channel, state);
}

static inline int i_relay_get_state(const i_relay_t *relay, relay_channel_t channel,
                                    relay_state_t *out_state)
{
    if (!i_relay_is_bound(relay) || !relay_channel_is_valid(channel) || out_state == NULL) {
        return -EINVAL;
    }

    return relay->api->get_state(relay->impl, channel, out_state);
}

#endif /* APP_INTERFACES_I_RELAY_H_ */
```

The consumer writes `i_relay_set_state(&port, RELAY_CHANNEL_MAIN_POWER, RELAY_STATE_CLOSED)`; they never see the v-table and cannot forget a check.

#### B. Infrastructure Adapter (Connected to Zephyr GPIO)
`app/src/infrastructure/adapters/adapter_relay_gpio.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "interfaces/i_relay.h"

struct relay_gpio_context {
    struct gpio_dt_spec gpio_spec[RELAY_CHANNEL_MAX];
};

static int relay_gpio_set(void *impl, relay_channel_t channel, relay_state_t state)
{
    struct relay_gpio_context *ctx = impl;

    /* The wrapper already performed structural validation (handle, channel):
     * here we only check what depends on adapter state. */
    if (ctx == NULL) {
        return -EINVAL;
    }

    return gpio_pin_set_dt(&ctx->gpio_spec[channel], (int) state);
}

static int relay_gpio_get(void *impl, relay_channel_t channel, relay_state_t *out_state)
{
    struct relay_gpio_context *ctx = impl;
    int val = gpio_pin_get_dt(&ctx->gpio_spec[channel]);

    if (val < 0) {
        return val;
    }

    *out_state = val ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
    return 0;
}

static const i_relay_api_t s_relay_api = {
    .set_state = relay_gpio_set,
    .get_state = relay_gpio_get,
};

/* Port binding is the responsibility of the Composition Root, not the adapter:
 * the adapter only exposes its v-table. */
const i_relay_api_t *relay_gpio_api(void)
{
    return &s_relay_api;
}
```

#### C. Composition Root (Static Bridge between Hardware and Software)
`app/src/infrastructure/di/composition_root.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "interfaces/i_relay.h"

LOG_MODULE_REGISTER(comp_root, LOG_LEVEL_INF);

/* Structure instantiated from Devicetree and owned by the container */
static struct relay_gpio_context s_relay_hw_ctx = {
    .gpio_spec = {
        [RELAY_CHANNEL_MAIN_POWER] = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(relay_main), gpios, {0}),
        [RELAY_CHANNEL_AUXILIARY]  = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(relay_aux), gpios, {0}),
    }
};

int composition_root_init(app_container_t *self)
{
    LOG_INF("Initializing Composition Root & Peripheral Verification...");

    for (int i = 0; i < RELAY_CHANNEL_MAX; i++) {
        if (!gpio_is_ready_dt(&s_relay_hw_ctx.gpio_spec[i])) {
            LOG_ERR("Relay GPIO %d not ready in Devicetree", i);
            return -ENODEV;
        }
        gpio_pin_configure_dt(&s_relay_hw_ctx.gpio_spec[i], GPIO_OUTPUT_INACTIVE);
    }

    /* Only point in the firmware that writes the port v-table. */
    self->relay.api = relay_gpio_api();
    self->relay.impl = &s_relay_hw_ctx;

    LOG_INF("Hardware bound to ports correctly.");
    return 0;
}
```

---

#### D. General Rule for Defining Ports (Mandatory)

Every new port, and every modification to an existing one, complies with these rules. They are not style recommendations: they are the contract that allows testing the domain without hardware and prevents a `NULL` from reaching a driver.

1. **Location and purity.** A single header in `app/include/interfaces/i_<domain>.h`,
   header-only (no port `.c`). It may include `<stdint.h>`, `<stdbool.h>`,
   `<stddef.h>`, `<errno.h>` and other headers from `include/common/`. **Never**
   `<zephyr/...>`, nor MCU headers, nor nanoMODBUS.
2. **Three pieces.** The v-table `i_<domain>_api_t`, the handle
   `i_<domain>_t { const i_<domain>_api_t *api; void *impl; }` and the wrappers.
   Names are `snake_case` and the prefix matches the header name.
3. **One wrapper per operation, without exception.** Every v-table entry has its
   `static inline i_<domain>_<operation>()` in the same header. If an operation
   does not deserve a wrapper, it does not belong in the v-table.
4. **`static inline`, not an external function.** This way the port adds no
   symbols, needs no translation unit of its own, and the compiler can eliminate
   unused wrappers.
5. **`i_<domain>_is_bound()` public** and used by all wrappers: the handle is
   bound only if `api` and `impl` are not `NULL`.
6. **Two-level validation, without duplication.**
   - *Structural (wrapper)*: bound handle, non-null output pointers, ranges and
     enumerations that are part of the contract, capacities greater than zero. It
     is cheap, needs no state, and uses the pure predicates from `common/` (for
     example `modbus_bus_id_is_valid()`), never repeating them by hand.
   - *Semantic (implementation)*: Does the device exist?, is the bus full?,
     is the claim active? The wrapper cannot answer this and does not try.
7. **Uniform return.** `int` with negative errno (`-EINVAL`, `-ENOENT`, `-ENOSPC`,
   `-EADDRINUSE`) for state or configuration operations; a domain enum
   (for example `modbus_transaction_result_t`) for transaction results.
   Do not mix them in the same signature. Unbound handle or invalid argument
   returns `-EINVAL` (or the domain "locally rejected" value: `MODBUS_RESULT_INVALID`) and
   **nothing is dispatched**.
8. **Forbidden to call the v-table from the application.** `handle->api->...` only
   appears in the adapter implementing the port and in the Composition Root, which is
   the only one that writes `api`/`impl`. Quick verification before each PR:
   ```bash
   grep -rn "api->" app/src | grep -v "infrastructure/adapters/" | grep -v "infrastructure/di/"
   ```
   Must return empty (port implementations in `application/` only *create* the
   handle, they do not consume it).
9. **No global state.** The `impl` points to an instance owned by the composition
   container; the port does not allocate memory, does not create threads, and does
   not know the RTOS.
10. **Fakes implement the same v-table** and are exercised through the wrappers,
    not calling the fake's v-table: this way the test verifies the contract and not a
    copy of the contract.
11. **Doxygen documentation** in the header for the handle, each wrapper and each
    output parameter, indicating what error each rejection returns. Identifiers and
    code comments in English; architecture prose (this document) in Spanish.

Living examples of this rule: `app/include/interfaces/i_slave_registry.h` and
`app/include/interfaces/i_modbus_client.h`; their compliance is verified in
`tests/unit/modbus/src/test_interfaces.c`.

> A header `i_*.h` that only defines **shared data types** (for example
> `i_temperature_bank.h`, which describes a reading) is not a port: it has no v-table
> nor wrappers. The rule applies to any header declaring an operations v-table.

---

### 4.2 Thread Decoupling with Zephyr Zbus

In complex telemetry applications, direct coupling between threads via direct calls causes blocking, priority inversion, and spaghetti code. **Zephyr Zbus** solves this by providing an asynchronous messaging bus, with compile-time verification and zero dynamic copies.

```
 [Sensor AO] ----(publishes)---> [chan_telemetry_sample]
                                           |
                         +-----------------+-----------------+
                         |                                   |
                         v                                   v
                  [ao_modbus_sub]                     [ao_network_sub]
               (Updates Holding Regs)            (Transmits over Ethernet/MQTT)
```

`app/src/application/zbus/telemetry_channels.h`:
```c
#ifndef TELEMETRY_CHANNELS_H_
#define TELEMETRY_CHANNELS_H_

#include <zephyr/zbus/zbus.h>
#include "common/telemetry_types.h"

/* Strongly typed messages */
typedef struct {
    uint32_t timestamp_ms;
    float current_ch1_amps;
    float current_ch2_amps;
    float bus_voltage_volts;
    uint32_t status_flags;
} telemetry_sample_msg_t;

typedef struct {
    uint16_t alarm_id;
    uint8_t severity;
    bool is_active;
} telemetry_alarm_msg_t;

/* Zbus channel declarations */
ZBUS_CHAN_DECLARE(chan_telemetry_sample);
ZBUS_CHAN_DECLARE(chan_telemetry_alarm);

#endif /* TELEMETRY_CHANNELS_H_ */
```

`app/src/application/zbus/telemetry_channels.c`:
```c
#include "zbus/telemetry_channels.h"

ZBUS_CHAN_DEFINE(chan_telemetry_sample,       /* Channel name */
                 telemetry_sample_msg_t,       /* Data type */
                 NULL,                         /* Optional validator */
                 NULL,                         /* User data */
                 ZBUS_OBSERVERS(modbus_sample_sub, network_sample_sub, dwin_sample_sub),
                 ZBUS_MSG_INIT(0)              /* Initial value */
);

ZBUS_CHAN_DEFINE(chan_telemetry_alarm,
                 telemetry_alarm_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(alarm_sub),
                 ZBUS_MSG_INIT(0)
);
```

---

## 5. TDD Methodology (Test-Driven Development)

### 5.1 Red-Green-Refactor Cycle in Firmware
1. **RED**: Write a unit test in `tests/unit/domain/test_alarm_evaluator.c` that verifies a required behavior (e.g. overcurrent > 50A triggers alarm). The test **fails** at compile or run time.
2. **GREEN**: Write the minimum code in `app/src/domain/services/alarm_evaluator.c` to make the test pass.
3. **REFACTOR**: Improve design, variable names, and efficiency keeping all tests green.

### 5.2 Test Structure with In-Memory Fakes (Ztest)

A fake implements the **same v-table** as the adapter, with the same
`void *impl`, and is linked into a handle during the test. The code under test
consumes it through the port wrappers (rule §4.1.D), so the test verifies the
contract, not a copy of the contract.

`tests/fakes/src/fake_relay.c`:
```c
#include "interfaces/i_relay.h"

typedef struct {
    relay_state_t states[RELAY_CHANNEL_MAX];
    uint32_t call_count[RELAY_CHANNEL_MAX];
} fake_relay_t;

static int fake_relay_set(void *impl, relay_channel_t channel, relay_state_t state) {
    fake_relay_t *fake = impl;

    fake->states[channel] = state;
    fake->call_count[channel]++;
    return 0;
}

static int fake_relay_get(void *impl, relay_channel_t channel, relay_state_t *out_state) {
    fake_relay_t *fake = impl;

    *out_state = fake->states[channel];
    return 0;
}

static const i_relay_api_t s_fake_api = {
    .set_state = fake_relay_set,
    .get_state = fake_relay_get,
};

void fake_relay_bind(i_relay_t *out, fake_relay_t *storage) {
    out->api = &s_fake_api;
    out->impl = storage;
}

relay_state_t fake_relay_spy_state(const fake_relay_t *fake, relay_channel_t ch) {
    return fake->states[ch];
}
```

`tests/unit/domain/test_alarm_evaluator.c`:
```c
#include <zephyr/ztest.h>
#include "domain/services/alarm_evaluator.h"
#include "fake_relay.h"

static fake_relay_t s_fake;
static i_relay_t s_relay;

ZTEST_SUITE(alarm_evaluator_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(alarm_evaluator_suite, test_should_trip_relay_on_overcurrent)
{
    (void) memset(&s_fake, 0, sizeof(s_fake));
    fake_relay_bind(&s_relay, &s_fake);
    alarm_evaluator_init(&s_relay);

    /* Normal current: relay must stay open */
    telemetry_sample_t normal_sample = { .current_ch1 = 15.0f };
    alarm_evaluator_process(&normal_sample);
    zassert_equal(fake_relay_spy_state(&s_fake, RELAY_CHANNEL_MAIN_POWER), RELAY_STATE_OPEN, 
                  "Relay must not trip with normal current");

    /* Critical overcurrent (60A): relay must trip */
    telemetry_sample_t fault_sample = { .current_ch1 = 60.0f };
    alarm_evaluator_process(&fault_sample);
    zassert_equal(fake_relay_spy_state(&s_fake, RELAY_CHANNEL_MAIN_POWER), RELAY_STATE_CLOSED,
                  "Relay must trip on overcurrent");
    zassert_equal(s_fake.call_count[RELAY_CHANNEL_MAIN_POWER], 1U, "There must be a single trip");
}
```

### 5.3 Execution with Twister in `native_sim`
To run all unit tests at native CPU speed on your Linux machine without touching the microcontroller:

```bash
# Run unit test suites on native_sim
west twister -T tests/unit -p native_sim -v --inline-logs

# Run tests with LCOV/gcov coverage report
west twister -T tests/unit -p native_sim --coverage -v
```

---

## 6. Industrial Modbus Subsystem Architecture (nanoMODBUS)

> **Master Document**: For exhaustive detail on sequence diagrams, stale claim recovery, and step-by-step guide to adding new actuators (e.g. relay expanders), consult [**doc/architecture/modbus_subsystem.md**](architecture/modbus_subsystem.md).

The BrightC Controller V3 Modbus subsystem is designed for high availability, wiring fault tolerance, and total physical decoupling between RS485 ports and field devices.

```
 +-------------------------------------------------------------------------------------------------+
 |                                   APPLICATION LAYER & ZBUS                                     |
 |   [chan_temperature_bank_16ch]       [chan_energy_metrics]           [chan_modbus_health]        |
 +-----------------^-----------------------------^--------------------------------^----------------+
                    |                             |                                |
                    +-----------------------------+--------------------------------+
                                                  |
 +-----------------------------------------------+-------------------------------------------------+
 |                    SLAVE MANAGER AND BUS ROUTING                                               |
 |                    (modbus_registry.c + slave_scheduler.c)                                      |
 |                                                                                                 |
 |  - Maintains modbus_slave_descriptor_t table (serializable for microconf / EEPROM)             |
 |  - Dynamic failover: reassigns devices between RS485-1 and RS485-2 at runtime                   |
 |  - Health state machine with Offline Backoff (isolates disconnected slaves)                   |
 +-----------------------------------------------+-------------------------------------------------+
                          |                                                |
                          v                                                v
 +-----------------------------------------------+  +----------------------------------------------+
 |   RS485-1 THREAD: ao_modbus.c (bus 0)         |  |   RS485-2 THREAD: ao_modbus.c (bus 1)       |
 |  - i_modbus_client_t bus1 -> adapter_modbus_nmbs (single Zephyr include)    |  |  - i_modbus_client_t bus2 -> adapter_modbus_nmbs                              |
 |  - DE transceiver control (PD4)               |  |  - DE transceiver control (PG8)           |
 |  - Hot reconfiguration of USART2              |  |  - Hot reconfiguration of USART6          |
 |  - Polls: CWT-TM-16PT (16x PT100)             |  |  - Polls: Energy meters, Relays          |
 +-----------------------+-----------------------+  +----------------------+-----------------------+
                          |                                                 |
                          v                                                 v
           [Hardware: USART2 + DE]                         [Hardware: USART6 + DE]
```

### 6.1 Modbus Subsystem Design Principles

1. **Thread Isolation (Active Object)**:
   - RS485 is a **half-duplex** medium. Each physical port (`RS485-1` and `RS485-2`) is exclusively owned by its own thread. No external component performs direct read/write calls to the UART.
2. **Hardware Interchangeability (Hot Failover)**:
   - Field devices are not hard-coded to a fixed port. If the `RS485-1` transceiver is damaged by an electrical discharge, cables A/B are moved to `RS485-2` and:
     ```c
     (void) i_slave_registry_set_bus(&app->registry, DEV_ID_CWT_TM16PT, MODBUS_BUS_RS485_2);
     ```
   - The system relocates polling instantly without restarting or modifying firmware.
3. **Dynamic UART Reconfiguration in Zephyr**:
   - Thanks to `CONFIG_UART_USE_RUNTIME_CONFIGURE=y` and the `uart_configure()` function, baud rate and parity can be changed at runtime if field configuration changes.
4. **Protection Against Disconnected Slaves (Offline Backoff)**:
   - If a slave suffers 3 consecutive failures, it enters `MODBUS_HEALTH_OFFLINE_BACKOFF`.
   - Instead of saturating the bus by trying to read it every 1 second (which would generate 300 ms timeouts each cycle), the manager reduces its query rate to a slow probe every 10 seconds, preserving bus bandwidth for healthy devices.
5. **Preparation for Persistence with `microconf` and EEPROM**:
   - Configuration descriptors (`modbus_slave_descriptor_t`) and transport (`modbus_bus_transport_cfg_t`) are flat memory structures with fixed sizes and no pointers, ready to be serialized/deserialized in blocks.

### 6.2 CWT-TM-16PT Temperature Sensor Driver

The industrial **CWT-TM-16PT** sensor (16 PT100/PT1000 channels) is decoded in
[`cwt_tm16pt_codec.c`](app/src/domain/services/cwt_tm16pt_codec.c) (pure code, no
I/O); register reads are performed by the Active Object through port
`i_modbus_client`, never including nanoMODBUS:
- **Fast Burst Read**: Reads all 16 channels in a single 16-word request (`0x0020` to `0x002F`), requiring only ~45 ms of bus time at 9600 baud.
- **Open Probe Detection**: Detects the sentinel code `0x7FFF` (3276.7 °C) or `-32768` and classifies it automatically in the `open_probe_mask` without generating spurious values.
- **Remote Reconfiguration**: Can reprogram Slave ID, baud rate and parity by writing to register `0x0010`.

---

## 7. Persistent Storage Subsystem Architecture (microconf)

> **Master Document**: For the complete EEPROM AT24C256C partitioning map, debounce policies, and guide to register new configuration domains, consult [**doc/architecture/storage_subsystem.md**](architecture/storage_subsystem.md).

### 7.1 The Concurrency Challenge and the Active Object Solution
The external EEPROM AT24C256C (I2C2) requires between 5 ms and 10 ms per page write cycle. If a control or serial communication thread writes directly and synchronously to the EEPROM, it would freeze causing frame loss and severe jitter. Additionally, `microconf` does not have internal multi-threading synchronization primitives.

Therefore, the architecture decouples storage into three levels:
1. **RAM memory**: Subsystems read and mutate their configuration in memory instantly under internal mutexes (`modbus_registry`, `ao_network`).
2. **Zbus Notification**: When a configuration is mutated, an event is emitted on channel `chan_system_config`.
3. **Async Coordinator (`ao_storage`)**: A dedicated Active Object receives events, applies a delay window (*debounce* of 500 ms) to dampen change bursts, and atomically executes `mconf_save()` in the background.

### 7.2 Modular EEPROM Partitioning
To avoid a change in network or telemetry configuration invalidating Modbus or calibration data, the 32 KB EEPROM is divided into independent virtual regions via `mconf_zephyr_eeprom_init()`:
- `0x0000` (1 KB): Device Identity (`CONFIG_DOMAIN_DEVICE`)
- `0x0400` (2 KB): Modbus Register and Buses (`CONFIG_DOMAIN_MODBUS`)
- `0x0C00` (1 KB): Ethernet Network Parameters (`CONFIG_DOMAIN_NET_ETH`)
- `0x1000` (1 KB): Wi-Fi Network Parameters (`CONFIG_DOMAIN_NET_WIFI`)
- `0x1400` (1 KB): Calibrations and Analog Offsets (`CONFIG_DOMAIN_CALIBRATION`)
- `0x1800` (2 KB): Sensor Routing and Roles (`CONFIG_DOMAIN_SENSOR_CFG`)
- `0x2000` (2 KB): OLS Betas and Thermal Baseline (`CONFIG_DOMAIN_BASELINE`)
- `0x2800` (6 KB): Reserved for future security/SNMP extensions
- `0x4000` (16 KB): Circular audit log (*Event Log*)

---

## 8. Workflow for Adding a New Module

When the team develops a new feature (e.g., Energy reading via I2C / SPI):

1. **Define the Port in `include/interfaces/`**:  
   Create `i_energy_meter.h` following the **mandatory rule of §4.1.D**: v-table
   `i_energy_meter_api_t`, handle `i_energy_meter_t` and a validated `static inline`
   wrapper per operation. Nobody consumes the v-table directly.
2. **Create the Fake in `tests/fakes/`**:  
   Implement `fake_energy_meter.c` to record simulated reads and verify calls.
3. **Write Domain Unit Tests (TDD Red)**:  
   Create `tests/unit/domain/test_energy_service.c` testing kWh accumulation calculations and failure detection.
4. **Implement Domain Logic (TDD Green)**:  
   Implement `app/src/domain/services/energy_service.c`.
5. **Implement Hardware Adapter in `infrastructure/`**:  
   Create `app/src/infrastructure/adapters/adapter_energy_ade7953.c` calling Zephyr I2C/SPI API.
6. **Connect in Devicetree and Composition Root**:  
   Add the node in the board overlay (`app/boards/brightc-controllerv3.overlay`) and register binding in `composition_root.c`.
7. **Integrate with Zbus or Shell**:  
   Publish processed data on a Zbus channel or create an inspection command in `app/src/shell/`.

---

## 9. Code Governance and Standards

- **C Standard**: ISO C99 or C11.
- **principles**: SOLID
- **Integer Types**: Always use `<stdint.h>` (`uint8_t`, `int32_t`, etc.). Prohibited to use bare `unsigned int`, `long` or `short`.
- **Error Handling**: Always return standard negative POSIX error codes (`-EINVAL`, `-ENODEV`, `-ETIMEDOUT`, `-EIO`).
- **File Names**: Strict `snake_case` (e.g., `telemetry_manager.c`, `i_modbus_port.h`).
- **Interface Prefixes**: Every public interface file must start with prefix `i_` (e.g., `i_storage.h`).
- **Adapter Prefixes**: Every physical implementation must start with `adapter_` (e.g., `adapter_storage_nvs.c`).
- **Documentation**: Doxygen comments on all public interface functions (`@brief`, `@param`, `@return`).
- **Ports**: Every port complies with rule §4.1.D (v-table + handle + `static inline` validated wrappers). A port without validated wrappers is rejected in review.
- **v-table Access**: `handle->api->...` is only allowed in the adapter implementing the port and in the Composition Root.
- **Stack Definition**: Prohibited to embed `k_thread_stack_t` inside structs in `.bss`. Use exclusively `K_THREAD_STACK_DEFINE` or `K_THREAD_STACK_ARRAY_DEFINE` at file scope to preserve MPU alignment and stack guards.
