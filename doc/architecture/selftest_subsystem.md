# Architecture of the Post-Boot Self-Test & Diagnostic Subsystem (Self-Test Subsystem)

- **Status**: Technical Architecture Specification & Port Contract
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Execution Environment**: Zephyr RTOS (West T2 Topology)
- **Architectural Pattern**: Clean Architecture (Ports & Adapters) + Declarative Self-Test Registry + Zbus Event Publishing
- **Language**: English (per project convention)

---

## 1. Executive Overview & Problem Statement

### 1.1 The Need for an Extensible Self-Test Subsystem
The BrightC Controller V3 is an industrial IoT edge device responsible for critical telemetry, energy metering, HVAC thermal efficiency calculation (Revision F), and multi-channel relay actuation. Because it operates unmonitored in server rooms and data centers, discovering hardware, bus, or sensor degradation during runtime or post-boot is vital.

The Self-Test Subsystem solves three key operational challenges:
1. **Deterministic Boot-Gate Verification (Fail-Safe Boot)**: Before energizing actuator coils or trusting sensor telemetry, the controller must verify internal voltage rails ($V_{REFINT}$), external non-volatile memory (I2C2 EEPROM), and core peripheral communications.
2. **Partial-Failure Resilience & Graceful Degradation**: If an external Modbus energy meter is unplugged or Wi-Fi fails to associate, the controller must **not** crash or enter an infinite reset loop. Instead, it must classify the failure as `DEGRADED`, isolate the affected subsystem, and alert operators through physical indicators (LED/Buzzer), telemetry events, and diagnostic registers.
3. **Open-Closed Extensibility**: As new hardware components (e.g. differential pressure sensors, 4G cellular modems, expansion boards on SPI1) are added to the platform, developers must be able to register new self-test checks **without modifying the central self-test execution engine**.

### 1.2 Boundary Definition: What this Subsystem Does and Does NOT Do
- **Does**:
  - Perform non-destructive physical hardware probes.
  - Coordinate synchronous POST gates (prior to Active Object start) and asynchronous peripheral acceptance checks.
  - Compile a standardized diagnostic report (`selftest_report_t`) containing individual results, execution times, error codes, and a bitmask matrix.
  - Publish overall health states (`SYSTEM_HEALTH_HEALTHY`, `SYSTEM_HEALTH_DEGRADED`, `SYSTEM_HEALTH_FAULT`) on `chan_system_health`.
  - Expose on-demand test execution via Zephyr Shell and diagnostic registers via Modbus/SNMP.
- **Does NOT**:
  - Perform destructive operations (it must never erase active EEPROM configuration slots or blindly cycle high-voltage AC relays during boot).
  - Replace runtime control loops or safety interlocks.
  - Maintain its own thread or allocate dynamic heap memory. The execution engine is purely declarative and driven synchronously during bootstrap or on-demand from caller threads (Shell / Composition Root).

---

## 2. Design Decisions (Closed & Traceable)

| # | Architectural Decision | Technical Justification |
|---|------------------------|-------------------------|
| **D1** | **Two-Stage Test Lifecycle** (`STAGE_SYNC_POST` + `STAGE_ASYNC_ACCEPTANCE`) | Hardware checks that take milliseconds (CPU, internal voltages, EEPROM I2C handshake) must pass synchronously before threads launch. Slow peripheral checks (Ethernet link negotiation, ESP32 co-processor boot, Modbus multi-drop polling) must not block system bootstrap. |
| **D2** | **Declarative Test Descriptor Registry** (`selftest_item_t`) | Implements the Open-Closed Principle (OCP). Each peripheral or adapter declares a static test descriptor containing metadata, severity, timeout, execution function pointer, and context pointer. |
| **D3** | **Severity Triad** (`CRITICAL`, `DEGRADED`, `INFO`) | Prevents binary all-or-nothing boots. A `CRITICAL` failure halts the system into a Safe State; a `DEGRADED` failure transitions the system to degraded monitoring without halting core tasks. |
| **D4** | **Non-Destructive Probe Invariant** | POST must be safe for physical hardware. Modbus relay tests must perform read-only register checks (FC01/FC03) instead of toggling coils. EEPROM tests must verify ping-pong header CRCs rather than overwriting operational slots. |
| **D5** | **Zbus Health State Coupling** | Self-test results publish directly to `chan_system_health`, allowing `ao_system_indicator` to immediately project status onto the physical user LED and buzzer without coupling to low-level hardware drivers. |
| **D6** | **Bounded Per-Test Execution Timeouts** | Any test executing against an external bus (I2C, SPI, UART/RS-485) must enforce a hard software deadline (timeout) to prevent a deadlocked peripheral from permanently hanging boot execution. |
| **D7** | **Zero Dynamic Allocation (Static Value Semantics)** | The engine, descriptor tables, and report buffers reside strictly in ROM/Flash (`const` descriptors) or static RAM structures inside `app_container_t`. Strict compliance with ARM Cortex-M7 MPU rules. |

---

## 3. Layered Architecture (Clean Architecture)

```
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                       APPLICATION CONSUMERS & EGRESS                        │
 │  - ao_system_indicator   (LED blink codes & Buzzer audio patterns)         │
 │  - Zephyr Shell CLI      (Interactive command `selftest [run|status|list]`) │
 │  - Modbus TCP/RTU Server (Diagnostics Input Registers 30001..30010)        │
 │  - ao_storage            (Logs fatal POST errors to persistent memory)      │
 └──────────────────────────────────────▲──────────────────────────────────────┘
                                        │ Zbus: chan_system_health
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │                     APPLICATION LAYER: SELF-TEST RUNNER                     │
 │  - Executes registered test stages with deterministic timeout guards        │
 │  - Aggregates results into immutable selftest_report_t                     │
 │  - Computes global system_health_t status                                   │
 └──────────────────────────────────────▲──────────────────────────────────────┘
                                        │ Consumes abstract port contract
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │                        PORT / CONTRACT: i_selftest.h                        │
 │  - V-table: init, run_stage, run_by_id, get_report                         │
 │  - Handle: i_selftest_t { api, impl }                                       │
 │  - static inline validated wrappers                                         │
 └──────────────────────────────────────▲──────────────────────────────────────┘
                                        │ Implemented by engine & registry
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │                   INFRASTRUCTURE: DECLARATIVE REGISTRY                      │
 │  Table of selftest_item_t descriptors provided by concrete adapters:        │
 │  [ selftest_mcu ] [ selftest_eeprom ] [ selftest_eth ] [ selftest_modbus ]  │
 └──────────────────────────────────────▲──────────────────────────────────────┘
                                        │ Direct non-destructive driver queries
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │                          PHYSICAL HARDWARE TARGET                           │
 │  - STM32H743 VREFINT / Temp  - AT24C256C (I2C2)   - LAN8742A RMII PHY       │
 │  - ESP32 (SPI4 ESP-Hosted)   - RS485-1 (USART2)   - RS485-2 (USART6)        │
 │  - CWT-TM16PT (16-ch RTD)    - JSY-MK-339 Energy  - MCP23S17 / Waveshare    │
 └─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Domain Level — Data Types (Pure ISO C99)

File: `app/include/common/selftest_types.h`

```c
#ifndef APP_COMMON_SELFTEST_TYPES_H_
#define APP_COMMON_SELFTEST_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum character length for test item name. */
#define SELFTEST_NAME_MAX_LEN 24U

/** Maximum diagnostic message string length. */
#define SELFTEST_DIAG_MSG_MAX_LEN 48U

/** Maximum number of self-tests supported by the static report bitmask. */
#define SELFTEST_MAX_ITEMS 32U

/**
 * @brief Self-test lifecycle execution stages.
 */
typedef enum {
    SELFTEST_STAGE_SYNC_POST = 0,    /**< Synchronous POST gates before threads start */
    SELFTEST_STAGE_ASYNC_ACCEPTANCE, /**< Asynchronous checks after network/buses launch */
    SELFTEST_STAGE_PERIODIC,         /**< Continuous runtime BIST checks */
    SELFTEST_STAGE_ON_DEMAND_ONLY,   /**< Manual diagnostic tests (e.g. interactive relay click) */
    SELFTEST_STAGE_MAX
} selftest_stage_t;

/**
 * @brief Severity classification of a self-test failure.
 */
typedef enum {
    SELFTEST_SEVERITY_CRITICAL = 0, /**< Failure halts system into safe state */
    SELFTEST_SEVERITY_DEGRADED,     /**< Failure allows operational boot in degraded mode */
    SELFTEST_SEVERITY_INFO          /**< Informational/advisory check (does not degrade health) */
} selftest_severity_t;

/**
 * @brief Outcome of a single test execution.
 */
typedef enum {
    SELFTEST_RESULT_PASS = 0,       /**< Test succeeded within limits */
    SELFTEST_RESULT_WARN,           /**< Test passed with non-fatal warning */
    SELFTEST_RESULT_FAIL,           /**< Hardware or communication fault detected */
    SELFTEST_RESULT_SKIPPED,        /**< Test was bypassed (e.g. disabled peripheral) */
    SELFTEST_RESULT_TIMEOUT         /**< Execution deadline exceeded */
} selftest_result_t;

/**
 * @brief Global system health status (aligned with system_indicators.md).
 */
typedef enum {
    SYSTEM_HEALTH_HEALTHY = 0,      /**< All critical and degraded tests passed */
    SYSTEM_HEALTH_DEGRADED,         /**< One or more DEGRADED tests failed */
    SYSTEM_HEALTH_FAULT             /**< At least one CRITICAL test failed (Safe State) */
} system_health_t;

/**
 * @brief Test execution function prototype.
 *
 * @param[in]  ctx          Opaque peripheral context pointer.
 * @param[out] diag_msg     Destination buffer for short diagnostic details.
 * @param[in]  diag_msg_len Size of diag_msg buffer.
 * @return selftest_result_t result code.
 */
typedef selftest_result_t (*selftest_exec_fn_t)(void *ctx, char *diag_msg, size_t diag_msg_len);

/**
 * @brief Declarative Self-Test Item Descriptor.
 */
typedef struct {
    uint16_t             id;                          /**< Unique item ID (0x0100..0xFFFF) */
    char                 name[SELFTEST_NAME_MAX_LEN]; /**< Human-readable identifier */
    selftest_stage_t     stage;                       /**< Execution lifecycle stage */
    selftest_severity_t  severity;                    /**< Impact on global system health */
    uint32_t             timeout_ms;                  /**< Maximum allotted run duration */
    selftest_exec_fn_t   execute;                     /**< Implementation function */
    void                *ctx;                         /**< Injected context (adapter or handle) */
} selftest_item_t;

/**
 * @brief Immutable post-run diagnostic report.
 */
typedef struct {
    system_health_t  overall_health;   /**< Final aggregated health state */
    uint16_t         total_tests;      /**< Number of tests executed */
    uint16_t         passed_tests;     /**< Count of tests returning PASS or WARN */
    uint16_t         failed_tests;     /**< Count of tests returning FAIL or TIMEOUT */
    uint16_t         skipped_tests;    /**< Count of tests skipped */
    uint32_t         passed_mask;      /**< Bitmask of passed test indices (0..31) */
    uint32_t         failed_mask;      /**< Bitmask of failed test indices (0..31) */
    uint32_t         critical_mask;    /**< Bitmask of failed tests with CRITICAL severity */
    uint32_t         total_duration_ms;/**< Cumulative duration in milliseconds */
    uint16_t         first_fault_id;   /**< ID of the first failing test (0 if all passed) */
} selftest_report_t;

/*============================================================================*
 * INLINED VALIDATORS (PURE PREDICATES)
 *============================================================================*/

static inline bool selftest_stage_is_valid(selftest_stage_t stage)
{
    return (unsigned int)stage < (unsigned int)SELFTEST_STAGE_MAX;
}

static inline bool selftest_severity_is_valid(selftest_severity_t sev)
{
    return sev == SELFTEST_SEVERITY_CRITICAL ||
           sev == SELFTEST_SEVERITY_DEGRADED ||
           sev == SELFTEST_SEVERITY_INFO;
}

static inline bool selftest_result_is_valid(selftest_result_t res)
{
    return res >= SELFTEST_RESULT_PASS && res <= SELFTEST_RESULT_TIMEOUT;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_SELFTEST_TYPES_H_ */
```

---

## 5. Port Contract — `i_selftest.h`

Following Section 4.1.D of `doc/STRUCTURE.md`:

```c
#ifndef APP_INTERFACES_I_SELFTEST_H_
#define APP_INTERFACES_I_SELFTEST_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/selftest_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1. V-TABLE */
typedef struct {
    int (*init)(void *impl, const selftest_item_t *items, size_t item_count);
    int (*run_stage)(void *impl, selftest_stage_t stage, selftest_report_t *out_report);
    int (*run_test_by_id)(void *impl, uint16_t test_id, selftest_result_t *out_result,
                          char *diag_msg, size_t diag_len);
    int (*get_report)(void *impl, selftest_report_t *out_report);
} i_selftest_api_t;

/* 2. HANDLE */
typedef struct {
    const i_selftest_api_t *api;
    void *impl;
} i_selftest_t;

/* 3. VALIDATED WRAPPERS */
static inline bool i_selftest_is_bound(const i_selftest_t *st)
{
    return st != NULL && st->api != NULL && st->impl != NULL;
}

static inline int i_selftest_init(const i_selftest_t *st, const selftest_item_t *items, size_t count)
{
    if (!i_selftest_is_bound(st) || items == NULL || count == 0U || count > SELFTEST_MAX_ITEMS) {
        return -EINVAL;
    }
    if (st->api->init == NULL) {
        return -ENOSYS;
    }
    return st->api->init(st->impl, items, count);
}

static inline int i_selftest_run_stage(const i_selftest_t *st, selftest_stage_t stage, selftest_report_t *out_report)
{
    if (!i_selftest_is_bound(st) || !selftest_stage_is_valid(stage)) {
        return -EINVAL;
    }
    if (st->api->run_stage == NULL) {
        return -ENOSYS;
    }
    return st->api->run_stage(st->impl, stage, out_report);
}

static inline int i_selftest_run_test_by_id(const i_selftest_t *st, uint16_t test_id,
                                            selftest_result_t *out_result, char *diag_msg, size_t diag_len)
{
    if (!i_selftest_is_bound(st) || test_id == 0U || out_result == NULL) {
        return -EINVAL;
    }
    if (st->api->run_test_by_id == NULL) {
        return -ENOSYS;
    }
    return st->api->run_test_by_id(st->impl, test_id, out_result, diag_msg, diag_len);
}

static inline int i_selftest_get_report(const i_selftest_t *st, selftest_report_t *out_report)
{
    if (!i_selftest_is_bound(st) || out_report == NULL) {
        return -EINVAL;
    }
    if (st->api->get_report == NULL) {
        return -ENOSYS;
    }
    return st->api->get_report(st->impl, out_report);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_SELFTEST_H_ */
```

---

## 6. Target Hardware Self-Test Catalog (BrightC Controller V3)

The baseline configuration registers the following concrete diagnostic probes across the physical hardware:

| Item ID | Test Name | Target Hardware / Subsystem | Stage | Severity | Max Timeout | Test Probe Description |
|:-------:|:----------|:----------------------------|:-----:|:--------:|:-----------:|:-----------------------|
| `0x0001` | `MCU_VREFINT` | STM32H743 Internal ADC3 Channel | `SYNC_POST` | `CRITICAL` | 50 ms | Converts internal $V_{REFINT}$ (nominal ~1.21 V). Verifies analog supply rail $V_{DDA}$ is between 3.10 V and 3.50 V. |
| `0x0002` | `MCU_TEMPSENSOR`| STM32H743 Die Temperature Sensor | `SYNC_POST` | `DEGRADED` | 50 ms | Converts internal temperature sensor. Verifies core die temperature is within industrial limits ($-40^\circ	ext{C} \le T \le +105^\circ	ext{C}$). |
| `0x0101` | `EEPROM_I2C2` | AT24C256C (I2C2 @ `0x50`) | `SYNC_POST` | `CRITICAL` | 100 ms | Performs 0-byte I2C addressing poll. Validates that the external EEPROM acknowledges on I2C2 and bus is healthy. |
| `0x0102` | `STORAGE_INTEGRITY`| `microconf` Dual-Slot Table | `SYNC_POST` | `CRITICAL` | 150 ms | Inspects ping-pong headers of `CONFIG_DOMAIN_DEVICE` and `CONFIG_DOMAIN_MODBUS`. Validates active slot CRC32 matches stored CRC. |
| `0x0201` | `ETH_PHY_LAN8742`| LAN8742A RMII PHY (MDIO/MDC) | `ASYNC` | `DEGRADED` | 200 ms | Reads PHY Identifier Registers 1 & 2 (`PHYIDR1=0x0007`, `PHYIDR2=0xC13X`) over MDIO. Confirms PHY silicon is alive. |
| `0x0202` | `WIFI_ESP_HOSTED`| ESP32 Coprocessor via SPI4 | `ASYNC` | `DEGRADED` | 500 ms | Probes ESP-Hosted-MCU control channel. Confirms SPI4 link negotiation, sync handshake, and co-processor firmware ready. |
| `0x0301` | `RS485_1_TRANSCEIVER`| RS-485 Port 1 (USART2, DE=PD4) | `ASYNC` | `DEGRADED` | 100 ms | Asserts DE pin, verifies GPIO drive state, checks UART baud rate configuration ($9600..115200$). |
| `0x0302` | `RS485_2_TRANSCEIVER`| RS-485 Port 2 (USART6, DE=PG8) | `ASYNC` | `DEGRADED` | 100 ms | Asserts DE pin, verifies GPIO drive state, checks UART baud rate configuration. |
| `0x0303` | `CWT_TM16PT_RTD` | CWT-TM16PT 16-Channel RTD Unit | `ASYNC` | `DEGRADED` | 400 ms | Issues Modbus FC04 read to Input Register `0x0000` (Channel 1). Verifies unit response and non-fault reading. |
| `0x0304` | `JSY_MK339_METER`| JSY-MK-339 3-Phase Energy Meter | `ASYNC` | `DEGRADED` | 400 ms | Issues Modbus FC03 read to Phase A Voltage (`0x0048`). Verifies slave address ACK and frame validity. |
| `0x0305` | `RELAY_WAVESHARE32`| Waveshare Modbus RTU 32-Ch Relay| `ASYNC` | `DEGRADED` | 300 ms | Issues Modbus FC01 read to Coil status (`0x0000`). Read-only check; does not disturb active relay states. |
| `0x0401` | `EXPANDER_MCP23S17`| MCP23S17 on SPI1 (MultiSPI) | `ASYNC` | `DEGRADED` | 100 ms | Reads register `IOCON` (`0x0A`) over SPI1. Validates SPI bus integrity and I/O expander presence. |

---

## 7. Execution Engine & Integration in Boot Cycle

### 7.1 Boot Execution Flow in `composition_root.c`

```text
composition_root_init()
 ├── 1. Init EEPROM driver & Storage Adapter (adapter_storage_microconf)
 ├── 2. Register static k_selftest_items[] table into selftest_engine
 ├── 3. Execute STAGE_SYNC_POST:
 │       rc = i_selftest_run_stage(&container->selftest, SELFTEST_STAGE_SYNC_POST, &report);
 │       if (report.overall_health == SYSTEM_HEALTH_FAULT) {
 │           LOG_ERR("POST FATAL ERROR: Item 0x%04X failed. Entering Safe State.", report.first_fault_id);
 │           composition_root_enter_safe_state(report.first_fault_id);
 │           return -EIO; /* Halts execution; watchdog reset or visual blink code */
 │       }
 ├── 4. Load persisted configurations (network, modbus slaves, calibration)
 ├── 5. Initialize Active Objects (ao_modbus, ao_network, ao_storage, ao_system_indicator)
 └── 6. Start Active Object threads
```

### 7.2 Asynchronous Acceptance Test Execution
Once operational threads start, a lightweight one-shot work item (`k_work`) or the Network/System orchestrator executes `SELFTEST_STAGE_ASYNC_ACCEPTANCE`:
1. Gathers dynamic status from physical links (Ethernet link carrier, Wi-Fi association, Modbus initial slave polling).
2. Computes the complete `selftest_report_t`.
3. If all tests pass $ightarrow$ publishes `SYSTEM_HEALTH_HEALTHY` to `chan_system_health`.
4. If one or more `DEGRADED` tests fail $ightarrow$ publishes `SYSTEM_HEALTH_DEGRADED` along with the first faulted ID.

---

## 8. Failure Handling Policies (Safe State vs. Degraded Operation)

### 8.1 Critical Failure Handling (`SYSTEM_HEALTH_FAULT`)
When a test marked `SELFTEST_SEVERITY_CRITICAL` fails during `STAGE_SYNC_POST` (e.g., $V_{DDA}$ is out of spec, or external EEPROM is non-responsive):
1. **Actuator Inactivation**: All onboard and external relay drivers are forced to `DE-ENERGIZED` state.
2. **Thread Launch Abort**: Operational threads (`ao_modbus`, `ao_network`) are **not** started.
3. **Indicator Blink Code**: `user_led` flashes a distinctive Morse/Blink code identifying the failed subsystem (e.g. 1 flash = MCU/ADC, 2 flashes = EEPROM/I2C2), while buzzer emits an audible warning beep.
4. **Diagnostic Shell Preservation**: If UART console is functional, Zephyr Shell remains active to permit technician diagnostics.

### 8.2 Degraded Failure Handling (`SYSTEM_HEALTH_DEGRADED`)
When a test marked `SELFTEST_SEVERITY_DEGRADED` fails (e.g. CWT-TM16PT external temperature module disconnected):
1. The system continues bootstrapping all remaining operational subsystems.
2. The sensor matrix mapper automatically marks affected semantic channels as `SENSOR_STATUS_OFFLINE`.
3. The thermal calculation engine locks out compressor efficiency estimations until mandatory sensor data is restored.
4. Physical LED switches to slow amber blinking (`LED_STATE_BLINK_SLOW`).
5. Modbus Diagnostic Register `30002` sets the corresponding bit high to alert the remote SCADA/BMS.

---

## 9. Diagnostic Interfaces & Egress

### 9.1 Zephyr Shell Commands (`selftest`)
Registered via `SHELL_STATIC_SUBCMD_SET_CREATE`:
- `selftest list`: Displays all registered tests, ID, stage, severity, and timeout.
- `selftest status`: Prints the last `selftest_report_t` summary (total, passed, failed, duration, fault bitmask).
- `selftest run [all | <id>]`: Triggers on-demand execution of the full suite or an individual test item and prints execution time + diagnostic output string.

Example Shell Output:
```text
uart:~$ selftest status
================ BrightC V3 Self-Test Report ================
Overall Health    : DEGRADED
Executed Tests    : 12
Passed            : 11
Failed            : 1 (0x0304 - JSY_MK339_METER)
Duration          : 324 ms
Failed Bitmask    : 0x00000400
Critical Faults   : None
Last Diag Message : Slave Addr 2 timeout after 400 ms
=============================================================
```

### 9.2 Industrial Modbus Diagnostics Mapping
Read-only Input Registers (`30001` to `30008`):

| Address | Description | Format | Values |
|:-------:|:------------|:------:|:-------|
| `30001` | Global System Health Status | uint16 | `0` = Healthy, `1` = Degraded, `2` = Critical Fault |
| `30002` | Failed Test Bitmask (Bits 0..15) | uint16 | Bit 0: MCU, Bit 1: Temp, Bit 2: EEPROM, Bit 3: Microconf, Bit 4: Eth, Bit 5: WiFi, Bit 6: RS485-1, Bit 7: RS485-2, Bit 8: CWT, Bit 9: JSY, Bit 10: Relays, Bit 11: MCP23S17 |
| `30003` | Failed Test Bitmask (Bits 16..31)| uint16 | Reserved for future test expansions |
| `30004` | First Failing Test ID | uint16 | Hex ID of the first failing component (e.g. `0x0304`) |
| `30005` | Total Tests Executed | uint16 | Count of tests evaluated during last run |
| `30006` | Total Tests Failed | uint16 | Count of failures recorded |
| `30007` | Execution Duration (ms) | uint16 | Cumulative runtime of self-test suite |

---

## 10. Developer Extensibility Guide: Adding a New Test

To add a new test for a newly introduced peripheral (e.g. an ADS1115 external ADC or a 4G modem):

1. **Define Test ID & Handler**:
   In the new adapter's directory (e.g. `app/src/infrastructure/adapters/adapter_adc_ads1115.c`), write a non-destructive probe function:
   ```c
   static selftest_result_t selftest_ads1115_probe(void *ctx, char *diag, size_t diag_len) {
       adapter_adc_t *dev = (adapter_adc_t *)ctx;
       if (!adapter_adc_is_ready(dev)) {
           snprintf(diag, diag_len, "ADS1115 I2C NACK at 0x48");
           return SELFTEST_RESULT_FAIL;
       }
       snprintf(diag, diag_len, "ADS1115 OK");
       return SELFTEST_RESULT_PASS;
   }
   ```
2. **Declare Descriptor in `composition_root.c`**:
   Add the item to the declarative `k_selftest_items[]` table:
   ```c
   {
       .id = 0x0501,
       .name = "ADC_EXT_ADS1115",
       .stage = SELFTEST_STAGE_ASYNC_ACCEPTANCE,
       .severity = SELFTEST_SEVERITY_DEGRADED,
       .timeout_ms = 100U,
       .execute = selftest_ads1115_probe,
       .ctx = &self->ads1115_adapter,
   },
   ```
3. **No Runner Changes Required**:
   The engine automatically runs the new probe during its assigned stage, computes timeouts, updates the bitmasks, reports to the shell, and publishes the global health state.
