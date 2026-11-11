# Architecture of the Digital Input & Contact Alarm Subsystem (Digital Input Subsystem)

- **Status**: Technical Architecture Specification & Port Contract
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Physical Channels**: SPI1 MCP23S17 Expanders (4 chips, 64 GPIOs total) & External Modbus RTU Digital Input Modules
- **Architectural Pattern**: Clean Architecture (Ports & Adapters) + Hardware-Agnostic Channel Ingestion + Software Integrator Debounce Filter + Zbus Event Publishing
- **Language**: English (per project convention)

---

## 1. Executive Summary & Problem Statement

### 1.1 Purpose of the Subsystem
The BrightC Controller V3 monitors critical electromechanical contacts and industrial safety alarms in data centers and telecom facilities:
1. **Evaporator Supply Fan Auxiliary Contacts ($U_{vent\_ON}$)**: Governs airflow mass calculation ($\dot{m}_{da,total}$) in the Revision F Thermodynamic Engine (§2.3, §4.3.4), independent of compressor cycling.
2. **Compressor Contactor Auxiliary Feedback ($c$)**: Drives the Compressor Stabilization State Machine (§3) to isolate motor inrush, step load power spikes, and short-cycling events.
3. **Discrete Facility Safety Alarms**: High/Low refrigerant pressure switches, water leak detection cables, smoke/fire alarm contacts, phase failure/sequence monitors, surge protector status (DPS), and physical rack/door interlocks.

### 1.2 The Dual-Source Physical Reality
The controller acquires digital signals from two fundamentally different physical sources:

| Source Type | Physical Bus | Conditioning & Signal Nature | Debounce Requirement |
|:---|:---|:---|:---|
| **Internal Channels** | SPI1 `MCP23S17` (4 expanders, up to 64 pins) | Galvanically isolated $24\text{ V}$ to $48\text{ V DC}$ optocouplers reading mechanical relays, contactors, and microswitches. Prone to contact bounce (10 ms to 100 ms) and mechanical chatter. | **MANDATORY**: Software Integrator Debounce ($500\text{ ms}$ for contactors per Rev F §2.3; $50\text{ ms}$ for alarms). |
| **External Channels** | RS-485 Modbus RTU (e.g. Remote I/O slave) | Remote industrial microcontroller already performs local electrical filtering and digital validation before updating Modbus registers. | **BYPASS (0 ms)**: Signals arrive clean over RS-485; redundant debouncing adds unnecessary lag. |

### 1.3 The Hardware-Agnostic Invariant
Higher-level consumers—the **Thermodynamic Engine (Revision F)**, the **Alarm Engine**, **Modbus TCP Server**, and **Local HMI**—must query **semantic roles** (e.g., `DI_ROLE_FAN_1_AUX`, `DI_ROLE_COMPRESSOR_1_AUX`, `DI_ROLE_FIRE_ALARM`), completely oblivious to whether a contact is wired to internal expander `mcp23s17_0` Pin 3 or received over RS-485 from Modbus slave 3 Bit 5.

---

## 2. Design Decisions (Closed & Traceable)

| # | Decision | Technical Justification |
|---|----------|-------------------------|
| **D1** | **Semantic Role-Based Channel Addressing** (`di_role_t`) | Decouples domain logic from physical board wiring. The application asks "is Fan 1 running?", not "is Pin PA0 high?". Channel routing is configured via mapping tables stored in non-volatile memory. |
| **D2** | **Software Integrator Debounce Filter (`debounce_filter_t`)** | An accumulation integrator: increments `stable_counter_ms += scan_period_ms` while input remains identical to the previous sample; any transient glitch instantly resets the counter to 0. A state transition commits **only** after uninterrupted stability for $T_{debounce}$. |
| **D3** | **Zero-Debounce Bypass for Remote Modbus Inputs (`debounce_ms = 0`)** | Modbus inputs are already sanitized at the remote module. Setting `debounce_ms = 0` bypasses the integrator timer, transitioning the stable state in the exact same scan cycle with zero CPU delay. |
| **D4** | **Electrical Polarity Normalization** (`active_level` + `contact_type`) | Translates field wiring configurations (`ACTIVE_HIGH` vs `ACTIVE_LOW`, `NORMALLY_OPEN` vs `NORMALLY_CLOSED`) into a normalized domain boolean: `is_active` (`true` = asserted/alarmed/on, `false` = normal/off). |
| **D5** | **Zephyr Upstream MCP23S17 Driver Exploitation (`gpio-controller`)** | The devicetree defines `mcp23s17_0..3` with `compatible = "microchip,mcp23s17"; gpio-controller;`. Zephyr's upstream driver natively handles SPI transactions. Using `gpio_port_get_raw()` reads all 16 pins of an expander in a single 2-byte SPI burst, reducing bus overhead by 93% compared to individual pin reads. |
| **D6** | **Periodic Active Object Scanning (`ao_digital_input` @ 50 Hz)** | Sampled every $20\text{ ms}$ ($50\text{ Hz}$). Requires 25 consecutive identical samples for compressor/fan contactors ($500\text{ ms}$ stability per Rev F §2.3) or 3 samples ($60\text{ ms}$) for fast alarms. |
| **D7** | **Zbus Event Notification (`chan_digital_input_state`)** | Confirmed state transitions publish immediately to Zbus. Subsystems (`ao_system_indicator`, alarm logger, Modbus TCP server) react without polling. |
| **D8** | **Zero Dynamic Allocation & MPU Compliance** | Static context tables allocated by value inside `app_container_t`. Thread stacks defined at file scope (`K_THREAD_STACK_DEFINE`). Zero `malloc`. |

---

## 3. Layered Clean Architecture & Data Flow

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                         DOMAIN & APPLICATION CONSUMERS                      │
 │  - Mathematical Model Engine (Revision F: checks U_vent_ON and Compressors) │
 │  - System Alarm Coordinator  (triggers sirens, buzzer, and safe state)      │
 │  - ao_system_indicator       (visual LED blink codes on alarm assertion)    │
 │  - Modbus TCP Server         (Discrete Inputs 10001..10032 for SCADA/BMS)   │
 └──────────────────────────────────────▲──────────────────────────────────────┘
                                        │ Zbus: chan_digital_input_state
                                        │ / i_digital_input port queries
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │             DIGITAL INPUT ACTIVE OBJECT (ao_digital_input.c)                │
 │                                                                             │
 │  - 50 Hz Periodic Scan Timer (20 ms interval)                               │
 │  - Iterates over Logical Channel Table (di_channel_config_t)                 │
 │  - Dispatches raw acquisition to corresponding hardware adapter             │
 │  - Evaluates Polarity Inversion -> Software Integrator Debounce             │
 │  - Emits Zbus events upon confirmed state transitions                       │
 └──────────────────────┬───────────────────────────────┬──────────────────────┘
                        │                               │
                        ▼ (Source: INTERNAL_SPI)        ▼ (Source: EXTERNAL_MODBUS)
 ┌──────────────────────────────────────┐     ┌────────────────────────────────┐
 │     ADAPTER: adapter_di_mcp23s17     │     │   ADAPTER: adapter_di_modbus   │
 │  - Binds to Zephyr gpio_port_get_raw │     │  - Reads non-blocking cached   │
 │  - Fast 16-bit SPI burst per chip    │     │    slave registry table        │
 │  - Raw noisy contact signal          │     │  - Clean digitized remote bits │
 └──────────────────────┬───────────────┘     └────────────────┬───────────────┘
                        │                                      │
                        ▼                                      ▼
 ┌──────────────────────────────────────┐     ┌────────────────────────────────┐
 │        PHYSICAL HARDWARE: SPI1       │     │    PHYSICAL HARDWARE: RS-485   │
 │  - 4x MCP23S17 on SPI1 (64 GPIOs)    │     │  - Remote Modbus RTU I/O Slave │
 │  - 24V-48V Optocoupled Field Inputs  │     │  - Field Sensor Terminal Block │
 └──────────────────────────────────────┘     └────────────────────────────────┘
```

---

## 4. Domain Level — Data Types (Pure ISO C99)

File: `app/include/common/digital_input_types.h`

```c
#ifndef APP_COMMON_DIGITAL_INPUT_TYPES_H_
#define APP_COMMON_DIGITAL_INPUT_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum supported logical digital input channels in the system. */
#define DI_CHANNEL_MAX 64U

/** Default contactor debounce time in milliseconds (Revision F §2.3). */
#define DI_DEBOUNCE_CONTACTOR_MS 500U

/** Default alarm contact debounce time in milliseconds. */
#define DI_DEBOUNCE_ALARM_MS 60U

/**
 * @brief Semantic roles for digital inputs in BrightC V3.
 */
typedef enum {
    /* HVAC Fan & Compressor Feedback (Revision F Model) */
    DI_ROLE_FAN_1_AUX = 0,           /**< Evaporator Fan 1 Run Feedback (U_vent_ON) */
    DI_ROLE_FAN_2_AUX,               /**< Evaporator Fan 2 Run Feedback (U_vent_ON) */
    DI_ROLE_FAN_3_AUX,               /**< Evaporator Fan 3 Run Feedback (U_vent_ON) */
    DI_ROLE_FAN_4_AUX,               /**< Evaporator Fan 4 Run Feedback (U_vent_ON) */
    DI_ROLE_COMPRESSOR_1_AUX,        /**< Compressor 1 Run Auxiliary Contact (c1) */
    DI_ROLE_COMPRESSOR_2_AUX,        /**< Compressor 2 Run Auxiliary Contact (c2) */
    DI_ROLE_COMPRESSOR_3_AUX,        /**< Compressor 3 Run Auxiliary Contact (c3) */
    DI_ROLE_COMPRESSOR_4_AUX,        /**< Compressor 4 Run Auxiliary Contact (c4) */

    /* HVAC Safety Interlocks */
    DI_ROLE_HIGH_PRESSURE_1,         /**< High Pressure Cutout Switch Circuit 1 */
    DI_ROLE_HIGH_PRESSURE_2,         /**< High Pressure Cutout Switch Circuit 2 */
    DI_ROLE_LOW_PRESSURE_1,          /**< Low Pressure Cutout Switch Circuit 1 */
    DI_ROLE_LOW_PRESSURE_2,          /**< Low Pressure Cutout Switch Circuit 2 */
    DI_ROLE_THERMAL_OVERLOAD_FAN,    /**< Evaporator Fan Motor Overload Trip */
    DI_ROLE_THERMAL_OVERLOAD_COMP,   /**< Compressor Motor Overload Trip */

    /* Facility & Environmental Alarms */
    DI_ROLE_WATER_LEAK,              /**< Raised Floor Water Inundation Sensor */
    DI_ROLE_SMOKE_FIRE,              /**< Facility Smoke / Fire Alarm Interlock */
    DI_ROLE_PHASE_MONITOR,           /**< 3-Phase Power Sequence / Loss Relay */
    DI_ROLE_SURGE_PROTECTOR,         /**< Lightning / Surge Protective Device (DPS) */
    DI_ROLE_DOOR_OPEN,               /**< Containment Aisle Door Contact */

    /* Generic User & Expansion Inputs */
    DI_ROLE_USER_1,
    DI_ROLE_USER_2,
    DI_ROLE_USER_3,
    DI_ROLE_USER_4,

    DI_ROLE_MAX
} di_role_t;

/**
 * @brief Physical hardware source for a digital input channel.
 */
typedef enum {
    DI_SOURCE_INTERNAL_SPI = 0,      /**< On-board MCP23S17 SPI expander pin */
    DI_SOURCE_EXTERNAL_MODBUS,       /**< Remote Modbus RTU I/O module register bit */
    DI_SOURCE_MOCK                   /**< Software simulated input for unit testing */
} di_source_type_t;

/**
 * @brief Electrical signal assertion level.
 */
typedef enum {
    DI_ACTIVE_HIGH = 0,              /**< Voltage present (High logic) asserts input */
    DI_ACTIVE_LOW                    /**< Voltage absent / GND (Low logic) asserts input */
} di_active_level_t;

/**
 * @brief Physical mechanical contact configuration.
 */
typedef enum {
    DI_CONTACT_NORMALLY_OPEN = 0,    /**< NO: Contact closes when event occurs */
    DI_CONTACT_NORMALLY_CLOSED       /**< NC: Contact opens when event occurs (fail-safe) */
} di_contact_type_t;

/**
 * @brief Normalized logical digital input state.
 */
typedef enum {
    DI_STATE_INACTIVE = 0,           /**< Normal operating condition / Contact de-asserted */
    DI_STATE_ACTIVE = 1              /**< Active condition / Alarm asserted / Contactor closed */
} di_state_t;

/*============================================================================*
 * SOFTWARE INTEGRATOR DEBOUNCE FILTER
 *============================================================================*/

/**
 * @brief Pure C99 Software Integrator Debounce Filter State.
 */
typedef struct {
    bool     stable_state;           /**< Confirmed debounced output state */
    bool     raw_last_state;         /**< Preceding raw sample level */
    uint32_t stable_counter_ms;      /**< Accumulated continuous stability in ms */
    uint16_t debounce_ms;            /**< Required stable duration (0 = immediate bypass) */
} debounce_filter_t;

/**
 * @brief Initialize a debounce filter instance.
 */
static inline void debounce_filter_init(debounce_filter_t *filter, uint16_t debounce_ms, bool initial_state)
{
    filter->debounce_ms = debounce_ms;
    filter->stable_state = initial_state;
    filter->raw_last_state = initial_state;
    filter->stable_counter_ms = 0U;
}

/**
 * @brief Update the debounce filter with a newly acquired raw sample.
 *
 * @param[in,out] filter          Pointer to filter state structure.
 * @param[in]     raw_state       Latest raw digital sample.
 * @param[in]     scan_period_ms  Time elapsed since previous sample (e.g. 20 ms).
 * @param[out]    out_stable      Receives current confirmed stable state (may be NULL).
 * @return true if an output state transition was confirmed on this cycle; false otherwise.
 */
static inline bool debounce_filter_update(debounce_filter_t *filter,
                                          bool raw_state,
                                          uint32_t scan_period_ms,
                                          bool *out_stable)
{
    /* Zero-Debounce Bypass (e.g. pre-filtered Modbus inputs) */
    if (filter->debounce_ms == 0U) {
        if (filter->stable_state != raw_state) {
            filter->stable_state = raw_state;
            filter->raw_last_state = raw_state;
            if (out_stable != NULL) {
                *out_stable = filter->stable_state;
            }
            return true;
        }
        if (out_stable != NULL) {
            *out_stable = filter->stable_state;
        }
        return false;
    }

    /* Integrator Accumulation */
    if (raw_state == filter->raw_last_state) {
        filter->stable_counter_ms += scan_period_ms;
    } else {
        filter->raw_last_state = raw_state;
        filter->stable_counter_ms = 0U; /* Glitch detected: reset accumulator */
    }

    /* Threshold Evaluation */
    if (filter->stable_counter_ms >= (uint32_t)filter->debounce_ms) {
        if (filter->stable_state != raw_state) {
            filter->stable_state = raw_state;
            if (out_stable != NULL) {
                *out_stable = filter->stable_state;
            }
            return true;
        }
    }

    if (out_stable != NULL) {
        *out_stable = filter->stable_state;
    }
    return false;
}

/*============================================================================*
 * LOGICAL CHANNEL CONFIGURATION & STATUS
 *============================================================================*/

/**
 * @brief Static configuration of one logical digital input channel.
 */
typedef struct {
    di_role_t         role;          /**< Semantic role */
    di_source_type_t  source_type;   /**< INTERNAL_SPI, EXTERNAL_MODBUS, MOCK */
    uint8_t           source_dev_id; /**< Chip index (0..3 for MCP23S17) or Modbus slave ID */
    uint8_t           pin_index;     /**< Expander pin (0..15) or Modbus bit offset */
    di_active_level_t active_level;  /**< ACTIVE_HIGH or ACTIVE_LOW */
    di_contact_type_t contact_type;  /**< NORMALLY_OPEN or NORMALLY_CLOSED */
    uint16_t          debounce_ms;   /**< Stability duration in ms (0 = bypass) */
    bool              enabled;       /**< Channel active flag */
} di_channel_config_t;

/**
 * @brief Runtime status and diagnostic metrics for one channel.
 */
typedef struct {
    di_role_t role;                  /**< Semantic role */
    bool      raw_level;             /**< Most recent un-debounced pin level */
    bool      is_active;             /**< Confirmed normalized active state */
    bool      is_stale;              /**< Last acquisition failed (value retained) */
    uint32_t  active_duration_s;     /**< Seconds spent continuously in active state */
    uint32_t  transition_count;      /**< Lifetime state change counter */
    uint64_t  last_transition_ms;    /**< Uptime timestamp of last state change */
} di_channel_status_t;

/**
 * @brief Zbus Event payload emitted on state transition.
 */
typedef struct {
    di_role_t  role;                 /**< Channel that transitioned */
    di_state_t new_state;            /**< New confirmed state (ACTIVE / INACTIVE) */
    di_state_t prev_state;           /**< Previous confirmed state */
    uint64_t   timestamp_ms;         /**< Kernel uptime when confirmed */
} di_transition_event_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_DIGITAL_INPUT_TYPES_H_ */
```

---

## 5. Port Interface Contract — `i_digital_input.h`

Adheres to Section 4.1.D of `doc/STRUCTURE.md`:

```c
#ifndef APP_INTERFACES_I_DIGITAL_INPUT_H_
#define APP_INTERFACES_I_DIGITAL_INPUT_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/digital_input_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1. V-TABLE */
typedef struct {
    int (*init)(void *impl, const di_channel_config_t *configs, size_t count);
    int (*scan)(void *impl, uint32_t elapsed_ms);
    int (*get_state)(void *impl, di_role_t role, bool *out_is_active);
    int (*get_raw_level)(void *impl, di_role_t role, bool *out_raw_level);
    int (*get_status)(void *impl, di_role_t role, di_channel_status_t *out_status);
    int (*get_active_mask)(void *impl, uint64_t *out_mask);
} i_digital_input_api_t;

/* 2. HANDLE */
typedef struct {
    const i_digital_input_api_t *api;
    void *impl;
} i_digital_input_t;

/* 3. VALIDATED WRAPPERS */
static inline bool i_digital_input_is_bound(const i_digital_input_t *port)
{
    return port != NULL && port->api != NULL && port->impl != NULL;
}

static inline int i_digital_input_init(const i_digital_input_t *port,
                                      const di_channel_config_t *configs,
                                      size_t count)
{
    if (!i_digital_input_is_bound(port) || configs == NULL || count == 0U || count > DI_CHANNEL_MAX) {
        return -EINVAL;
    }
    return port->api->init(port->impl, configs, count);
}

static inline int i_digital_input_scan(const i_digital_input_t *port, uint32_t elapsed_ms)
{
    if (!i_digital_input_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->scan(port->impl, elapsed_ms);
}

static inline int i_digital_input_get_state(const i_digital_input_t *port, di_role_t role, bool *out_is_active)
{
    if (!i_digital_input_is_bound(port) || (unsigned int)role >= (unsigned int)DI_ROLE_MAX || out_is_active == NULL) {
        return -EINVAL;
    }
    return port->api->get_state(port->impl, role, out_is_active);
}

static inline int i_digital_input_get_raw_level(const i_digital_input_t *port, di_role_t role, bool *out_raw)
{
    if (!i_digital_input_is_bound(port) || (unsigned int)role >= (unsigned int)DI_ROLE_MAX || out_raw == NULL) {
        return -EINVAL;
    }
    return port->api->get_raw_level(port->impl, role, out_raw);
}

static inline int i_digital_input_get_status(const i_digital_input_t *port, di_role_t role, di_channel_status_t *out_status)
{
    if (!i_digital_input_is_bound(port) || (unsigned int)role >= (unsigned int)DI_ROLE_MAX || out_status == NULL) {
        return -EINVAL;
    }
    return port->api->get_status(port->impl, role, out_status);
}

static inline int i_digital_input_get_active_mask(const i_digital_input_t *port, uint64_t *out_mask)
{
    if (!i_digital_input_is_bound(port) || out_mask == NULL) {
        return -EINVAL;
    }
    return port->api->get_active_mask(port->impl, out_mask);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_DIGITAL_INPUT_H_ */
```

---

## 6. Physical Hardware Adapters

### 6.1 Internal SPI Adapter (`adapter_di_mcp23s17.c`)
- **Devicetree Nodes**: `mcp23s17_0`, `mcp23s17_1`, `mcp23s17_2`, `mcp23s17_3` on `&spi1`.
- **Driver Integration**: Uses upstream Zephyr `CONFIG_GPIO_MCP23S17=y`.
- **Fast Burst Optimization**:
  At each 20 ms scan tick, the adapter executes `gpio_port_get_raw(dev, &port_value)` for Port A and Port B.
  A single 16-bit register fetch captures all 16 pins simultaneously over SPI. The 4 chips are scanned in $< 50\,\mu\text{s}$ total CPU time.

### 6.2 External Modbus RTU Adapter (`adapter_di_modbus.c`)
- **Integration**: Queries the already-running `ao_modbus` and `i_slave_registry_t`.
- **Zero Bus Latency**: Does **not** perform blocking RS-485 transactions during the scan cycle. Instead, it reads the latest cached coil/discrete inputs updated by the background Modbus polling loop.
- **Bypass Configuration**: In `di_channel_config_t`, all Modbus channels set `debounce_ms = 0`, ensuring that new Modbus data updates the logical state immediately without adding artificial filtering delay.
- **Data Freshness (staleness)**: Because the cached snapshot is refreshed asynchronously, an unreachable `MBSL32DI` would otherwise keep reporting its last bitmap forever. `adapter_di_gpio_read()` therefore rejects remote samples whose age exceeds `ADAPTER_DI_MODBUS_STALE_TIMEOUT_MS` (3000 ms, overridable at runtime with
  `adapter_di_gpio_set_modbus_stale_timeout_ms()`, `0` disables the check) returning `-ENODATA`.
  `adapter_di_gpio_set_modbus_raw()` stamps `modbus_last_update_ms` on every accepted poll.

### 6.3 Acquisition-Failure Propagation (Staleness Contract)
When `raw_reader()` returns an error, `di_aggregator_scan()` **retains** the last confirmed value
(so the debounce integrator is not perturbed by a transient poll failure) but marks the channel
`di_channel_status_t.is_stale = true`. The flag clears on the next successful acquisition.

- `di_aggregator_pack_snapshot()` exports the aggregate as `di_snapshot_msg_t.stale_mask`.
- `di_snapshot_role_active_or_stale()` is the consumer-side helper: it resolves a role and returns
  `is_active || stale`, i.e. **unknown is reported as asserted**.
- Consumers must fail in the protective direction. `lis_hvac_di_feed` uses this helper for the
  per-unit HP/LP/FLOW/VOLT lockouts so a unit is never started blind when its remote safety channel
  cannot be read. The FIRE contact and the front-panel bypass switch deliberately do **not** use it:
  a missing expander must not latch a fire shutdown or force maintenance bypass.

---

## 7. Polarity & Contact Normalization Engine

Before feeding the raw sample into `debounce_filter_update()`, electrical characteristics are resolved into an asserted state:

```c
bool is_raw_asserted = (cfg->active_level == DI_ACTIVE_HIGH) ? raw_pin_level : !raw_pin_level;

if (cfg->contact_type == DI_CONTACT_NORMALLY_CLOSED) {
    /* In a Normally Closed safety circuit, contact opening indicates alarm/event */
    is_raw_asserted = !is_raw_asserted;
}

/* Feed into integrator filter */
bool transitioned = debounce_filter_update(&chan->filter, is_raw_asserted, scan_period_ms, &chan->stable_active);

if (transitioned) {
    chan->transition_count++;
    chan->last_transition_ms = k_uptime_get();
    
    /* Publish event to Zbus */
    di_transition_event_t evt = {
        .role = cfg->role,
        .new_state = chan->stable_active ? DI_STATE_ACTIVE : DI_STATE_INACTIVE,
        .prev_state = chan->stable_active ? DI_STATE_INACTIVE : DI_STATE_ACTIVE,
        .timestamp_ms = chan->last_transition_ms,
    };
    zbus_chan_pub(&chan_digital_input_state, &evt, K_MSEC(10));
}
```

---

## 8. Integration with Upstream Subsystems

1. **Thermodynamic Engine (Revision F)**:
   - When evaluating dry air mass flow ($\dot{m}_{da,total}$):
     ```c
     bool fan_on = false;
     i_digital_input_get_state(&container->digital_inputs, DI_ROLE_FAN_1_AUX, &fan_on);
     if (fan_on) {
         u_vent_mask |= (1 << 0);
     }
     ```
   - Decoupled from compressor states; evaluates airflow accurately even during thermostat off-cycles.
2. **Compressor Stabilization State Machine (§3)**:
   - Continuously evaluates `DI_ROLE_COMPRESSOR_1_AUX` and `DI_ROLE_COMPRESSOR_2_AUX` transitions to drive `INRUSH_WAIT`, `STEP_LOAD_CAPTURE`, and `STEADY_STATE`.
3. **System Indicator Active Object (`ao_system_indicator`)**:
   - Subscribes to `chan_digital_input_state`.
   - Critical alarms (`DI_ROLE_HIGH_PRESSURE`, `DI_ROLE_SMOKE_FIRE`, `DI_ROLE_WATER_LEAK`) immediately trigger `SYSTEM_HEALTH_FAULT` and rapid LED blink/buzzer alarm patterns.
4. **Modbus TCP Server (SCADA Egress)**:
   - Exposes discrete inputs at Modbus addresses `10001` to `10032`, matching Huawei NetEco and industrial BMS registers.

---

## 9. Verification & Unit Testing Strategy

1. **Pure Host Tests (`tests/unit/digital_input/test_debounce_filter.c`)**:
   - Verify integrator behavior: single-cycle glitch rejection, continuous stability requirement, transition confirmation.
   - Verify bypass behavior: `debounce_ms = 0` transitions on the exact same cycle.
2. **Polarity Normalization Tests**:
   - Verify all 4 permutations: (ACTIVE_HIGH / NO), (ACTIVE_HIGH / NC), (ACTIVE_LOW / NO), (ACTIVE_LOW / NC).
3. **Agility / Multi-Source Tests (`test_digital_input_aggregator.c`)**:
   - Configure Channel 1 as `INTERNAL_SPI` (debounce 500 ms) and Channel 2 as `EXTERNAL_MODBUS` (debounce 0 ms).
   - Feed simultaneous square-wave noise into Channel 1 and clean pulse into Channel 2.
   - Assert Channel 2 updates instantly while Channel 1 filters out noise.
