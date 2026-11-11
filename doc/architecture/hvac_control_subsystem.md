# Architecture of the Industrial HVAC Control Subsystem (Air Conditioning Sequencer)

- **Status**: Official Architecture Specification & Implementation Guide
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Environment**: Zephyr RTOS (West T2 Workspace)
- **Layer**: Domain (`app/src/domain/hvac/`), Interfaces (`app/include/interfaces/`), Application (`app/src/application/active_objects/ao_hvac.c`), Infrastructure (`app/src/infrastructure/adapters/`)
- **Reference Project**: `brightc_firmware` (`/media/jeffry/jqb/brightc_/brightc_firmware`)
- **Language**: English (per project convention)

---

## 1. Executive Summary & Design Principles

The **Industrial HVAC Control Subsystem** coordinates and sequences up to **8 industrial air conditioning (AC) units** organized across up to **4 independent groups** in mission-critical telecommunications facilities and data centers. 

Its primary objective is maintaining strict thermal stability while:
1. **Preventing Inrush Overload**: Staggering compressor turn-on transients.
2. **Equalizing Compressor Wear**: Rotating duty cycles evenly across active and standby units.
3. **Providing Multi-Tier Fault Resilience**: Automatically recovering from transient pressure lockouts, isolating damaged units, and escalating to mechanical bypass upon multi-unit degradation or sensor loss.
4. **Supporting Modular Hardware Deployments**: Driving physical outputs via on-board GPIO/SPI expanders (MCP23S17) or remote RS-485 Modbus RTU relay modules without modifying domain logic.

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                         APPLICATION & DOMAIN LAYERS                         │
 │                                                                             │
 │   ┌─────────────────────────┐             ┌─────────────────────────────┐   │
 │   │    hvac_group_entity    │ 1 ─────── * │      hvac_unit_entity       │   │
 │   │ (FSM Grupo, Decisiones, │             │  (FSM Unidad, Alarma P,     │   │
 │   │  Rotación N+M, Bypass)  │             │   Sub-FSM Fan Purge Delay)  │   │
 │   └─────────────────────────┘             └──────────────┬──────────────┘   │
 └──────────────────────▲───────────────────────────────────┼─────────────────┘
                        │ evaluates metrics                 │ generates tri-state
                        │                                   ▼ (AA, SM, G)
 ┌──────────────────────┴──────────────────────────────────────────────────────┐
 │                      ACTIVE OBJECT COORDINATOR (ao_hvac)                    │
 │         - Canonical 5-phase lifecycle: init, start, stop, wait, deinit      │
 │         - Periodic tick (1000 ms), command queue, Zbus event publishing     │
 └──────────────────────▲───────────────────────────────────┬──────────────────┘
                        │ calls validated wrappers          │ calls validated wrappers
                        ▼                                   ▼
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                       PORTS (app/include/interfaces/)                       │
 │      - i_hvac_unit.h: Tri-State actuation (relay_aa, relay_sm, relay_g)     │
 │      - i_digital_input.h: Physical alarms & safety interlocks               │
 │      - i_sensor_matrix.h: Aggregated temperature acquisition                │
 └──────────────────────▲───────────────────────────────────▲──────────────────┘
                        │ implements v-table                │ implements v-table
 ┌──────────────────────┴──────────────────────┐ ┌──────────┴─────────────────┐
 │          adapter_hvac_local_gpio            │ │   adapter_hvac_remote_modbus│
 │  (Units 1..4: MCP23S17 SPI / STM32 GPIO)    │ │   (Units 5..8: Modbus RTU)  │
 └──────────────────────▲──────────────────────┘ └──────────▲──────────────────┘
                        │ injected device pointers          │ injected port
 ┌──────────────────────┴───────────────────────────────────┴──────────────────┐
 │                     COMPOSITION ROOT (composition_root.c)                   │
 │      - Immutable hardware routing table in hardware_pin_map.h               │
 │      - Operation mode profile selection (system_operation_mode_t)           │
 └─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Architectural Improvements over Legacy `brightc_firmware`

Based on deep architectural inspection of the reference project (`/media/jeffry/jqb/brightc_/brightc_firmware`), this specification solves 5 critical architectural bottlenecks:

| Aspect | Legacy Implementation (`brightc_firmware`) | Enhanced Standard (`BrightC-telemetry`) | Architectural Benefit |
| :--- | :--- | :--- | :--- |
| **Rotation Scheme & Scalability** | Rigid enum: `ROTATION_SCHEME_3`, `2_1`, `3_1`, `1_2`. Max 4 units per group. | **Generic $N+M$ Redundancy**: `active_turn_count` ($N$) configurable from 1 to 8 units. | Supports arbitrary group sizing (e.g., 1 single room with 8 units in 7+1 or 6+2, or 2 groups of 4 in 3+1). |
| **Telemetry vs. Control Separation** | Contactor feedback inputs (`fb_compressor`, `fb_fan`) entangled inside control structures. | **Single Responsibility Principle (SRP)**: Control FSM only consumes safety alarms; feedback belongs to Telemetry. | Decoupled domain models. Contactor feedback monitored even during Bypass mode. |
| **Contactor Discrepancy Monitoring** | Open-loop relay commanding with no verification of contactor physical state. | **Command-vs-Feedback Discrepancy Engine**: Telemetry raises Zbus discrepancy alarms if feedback fails within 10s. | Prevents silent room overheating caused by tripped breakers or burnt contactor coils. |
| **System Operation Profiles** | Monolithic execution: control and telemetry always running simultaneously. | **Declarative Operation Profiles**: 1) Telemetry Only (Rev F), 2) Control Only, 3) Hybrid Full. | Cleanly selectable via EEPROM configuration. Zero CPU/memory overhead for unused features. |
| **Authoritative Hardware Mapping & Storage Optimization** | Redundant hardware routing stored in EEPROM (`hvac_unit_hw_config_t units[8]`, 288 B) duplicating firmware definitions. | **Immutable `hardware_pin_map.h` as Single Truth**: Hardware pinouts removed from EEPROM (`config_hvac_t` reduced from ~476 B to ~188 B). | Eliminates EEPROM corruption risk on pin routing, frees ~288 B in non-volatile storage, and deterministically binds pins per profile. |

---

## 3. System Operation Profiles (`system_operation_mode_t`)

The BrightC Controller V3 firmware supports three distinct operational modes, selectable via EEPROM configuration (`config_types.h`):

```c
typedef enum {
    /** 
     * @brief Profile 1: Passive Audit & Telemetry (No Actuation).
     * Relays are disengaged (relay_sm = 0 / Bypass). Executes Mathematical Model Rev F,
     * logs runtime hours from contactor feedback, stores to MicroSD, pushes to MQTT.
     */
    SYSTEM_MODE_TELEMETRY_ONLY = 0,

    /** 
     * @brief Profile 2: Autonomous HVAC Controller (No Advanced Math).
     * Active Object ao_hvac drives relays according to group FSM, N+M duty rotation,
     * and pressure alarms. Psychrometric enthalpy and COP calculations are disabled.
     */
    SYSTEM_MODE_HVAC_CONTROL_ONLY,

    /** 
     * @brief Profile 3: Hybrid Closed-Loop Control + Predictive Telemetry.
     * Full concurrency: ao_hvac controls the equipment, while ao_telemetry monitors
     * actual energy/temperature metrics, evaluates COP degradation, and feeds early
     * warnings back into the control scheduler.
     */
    SYSTEM_MODE_HYBRID_FULL
} system_operation_mode_t;
```

### 3.1 Operational Profiles Hardware Mapping Strategy (`hardware_pin_map.h`)

Physical channel and terminal block assignments are defined deterministically in `app/include/common/hardware_pin_map.h`. The active operation profile governs physical channel interpretation without needing external runtime reconfiguration:

#### 1. Profile `HVAC_MODE_TELEMETRY_ONLY` (Audit & Passive Diagnostics):
- **Monitored Capacity**: Up to 8 AC units ($AC_1 \dots AC_8$).
- **Actuation**: Zero physical relays commanded; all relay lines remain disengaged (`relay_sm = 0` / mechanical bypass).
- **Auxiliary Feedback per Unit**: Exactly 2 dry-contact inputs per unit (1 + 1):
  * Compressor Run feedback (`fb_compressor`)
  * Evaporator Fan Run feedback (`fb_fan`)
- **Total Feedback Channels**: 16 digital inputs ($8 \times 2 = 16$).
- **100% On-Board Local Inputs**: Uses exclusively local optocoupled inputs on the BrightC V3 PCB (`ALM_IN_1` through `ALM_IN_16`), mapped correlatively:

| Unit | Compressor Run Input | Evaporator Fan Run Input | Hardware Device & Pin Index |
| :--- | :--- | :--- | :--- |
| **AC 1** | `ALM_IN_1` | `ALM_IN_2` | `&mcp23s17_0`, Pins 0 & 1 |
| **AC 2** | `ALM_IN_3` | `ALM_IN_4` | `&mcp23s17_0`, Pins 2 & 3 |
| **AC 3** | `ALM_IN_5` | `ALM_IN_6` | `&mcp23s17_0`, Pins 4 & 5 |
| **AC 4** | `ALM_IN_7` | `ALM_IN_8` | `&mcp23s17_0`, Pins 6 & 7 |
| **AC 5** | `ALM_IN_9` | `ALM_IN_10` | `&mcp23s17_0`, Pins 8 & 9 |
| **AC 6** | `ALM_IN_11` | `ALM_IN_12` | `&mcp23s17_0`, Pins 10 & 11 |
| **AC 7** | `ALM_IN_13` | `ALM_IN_14` | `&mcp23s17_1`, Pins 0 & 1 |
| **AC 8** | `ALM_IN_15` | `ALM_IN_16` | `&mcp23s17_1`, Pins 2 & 3 |
| **Safety** | `ALM_IN_17` (Smoke/Fire), `ALM_IN_18` (Door), `ALM_IN_19` (Water Leak), `ALM_IN_20` (DPS/Surge), `ALM_IN_21` (Phase) | `&mcp23s17_1`, Pins 4..8 |

> [!IMPORTANT]
> **Zero External Modbus I/O Dependency**: In `HVAC_MODE_TELEMETRY_ONLY`, all 8 AC units plus facility alarms are monitored **100% locally on the controller PCB**. No external RS-485 Modbus digital input module (`MBSL32DI`) or relay module is required.

#### 2. Profiles `HVAC_MODE_HYBRID_FULL` & `HVAC_MODE_HVAC_CONTROL_ONLY`:
Designed for sites requiring autonomous lead-lag rotation, smart-mode contactor driving, and active safety trips:
- **Maximum Capacity**: Up to 8 AC units (4 units local + 4 units remote via Modbus expansion).
- **Units 1..4 (Local Actuation & Safety)**:
  * Actuation via on-board relays on `&mcp23s17_2` (`relay_aa`, `relay_sm`, `relay_g`).
  * 4 dedicated safety alarm cutouts per unit (`alarm_hp`, `alarm_lp`, `alarm_flow`, `alarm_volt`) wired directly to local inputs `ALM_IN_1..16`:
    - AC 1: `ALM_IN_1..4`
    - AC 2: `ALM_IN_5..8`
    - AC 3: `ALM_IN_9..12`
    - AC 4: `ALM_IN_13..16`
- **Units 5..8 (Remote Actuation & Safety, configured when `total_units > 4`)**:
  * Actuation via remote Modbus relay module (e.g., Waveshare 32-channel Modbus RTU relay module, Slave Addr 2).
  * 4 dedicated safety alarm cutouts per unit (`alarm_hp`, `alarm_lp`, `alarm_flow`, `alarm_volt`) wired to remote Modbus `MBSL32DI` (Slave Addr 4), channels 0..15:
    - AC 5: `MBSL32DI` channels 0..3
    - AC 6: `MBSL32DI` channels 4..7
    - AC 7: `MBSL32DI` channels 8..11
    - AC 8: `MBSL32DI` channels 12..15
- **Telemetry Feedback for All Units 1..8 in Hybrid Mode**:
  * Evaporator Fan Run feedbacks (`fb_fan`): Remote Modbus `MBSL32DI` channels 16..23 (Fans 1..8).
  * Compressor Run feedbacks (`fb_compressor`): Remote Modbus `MBSL32DI` channels 24..31 (Compressors 1..8).

---

## 4. Domain Layer: Individual AC Unit Entity (`hvac_unit_entity`)

Each physical air conditioner is encapsulated by an isolated entity instance (`hvac_unit_entity_t`).

### 4.1 Tri-State Control Command (`hvac_tri_state_t`)
To interface with standard industrial air conditioning wiring, every unit produces three physical relay command signals:

```c
typedef struct {
    bool relay_aa; /**< Compressor contactor: Smart Mode ON (true) / OFF (false). */
    bool relay_sm; /**< Mode selector: Smart Controller in charge (true) / Bypass (false). */
    bool relay_g;  /**< Fan cut relay (NC contact in series with G): Fan Enabled/ON (false) / Fan Cut/OFF (true). */
} hvac_tri_state_t;
```

### 4.2 Unit Finite State Machine (FSM)

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                           UNIT FSM STATE TRANSITIONS                        │
 └─────────────────────────────────────────────────────────────────────────────┘

       ┌──────────────────┐
       │ UNIT_STATE_INIT  │
       └────────┬─────────┘
                │
                ▼
       ┌──────────────────┐
  ┌───►│  UNIT_STATE_OFF  │◄─────────────────────────────┐
  │    └────────┬─────────┘                              │
  │             │ Request Start                          │
  │             │ (with delay > 0)                       │
  │             ▼                                        │
  │    ┌──────────────────┐                              │
  │    │  STARTUP_DELAY   │                              │
  │    └────────┬─────────┘                              │
  │             │ Delay timer == 0                       │
  │             ▼                                        │ Request Stop
  │    ┌──────────────────┐                              │ (or T <= T_min)
  │    │  UNIT_STATE_ON   ├──────────────────────────────┘
  │    └────────┬─────────┘
  │             │ Critical Alarm (High/Low Press, Phase, Supply)
  │             ▼
  │    ┌──────────────────┐
  │    │  BLOCKED_ALARM   │  Retry timer (180s) expired
  │    │  (Lockout 180s)  ├──────────────────────────────┐
  │    └────────┬─────────┘  (alarms cleared)            │
  │             │                                        │
  │             │ Accumulated >= max_allowed_faults (3)  │
  │             ▼                                        │
  │    ┌──────────────────┐                              │
  │    │ UNIT_STATE_FAULT │ (Permanently evicted from    │
  │    │  (Evicted Unit)  │  rotation until manual reset)│
  │    └──────────────────┘                              │
  │                                                      │
  │    ┌──────────────────┐                              │
  │    │UNIT_STATE_BYPASS ├──────────────────────────────┘
  │    │  (relay_sm = 0)  │ When bypass cleared
  └────┴──────────────────┘
```

- **`BLOCKED_ALARM`**: Triggered immediately if high-pressure, low-pressure, supply airflow, or voltage faults trip while the compressor is commanded ON. The compressor is shut down, `fault_counter` increments, and a 3-minute lockout (`pressure_retry_s = 180s`) begins. If alarms clear after 180s, the unit returns to `UNIT_STATE_OFF`.
- **`UNIT_STATE_FAULT`**: If `fault_counter >= max_allowed_faults` (default 3), the unit is permanently disqualified from cooling rotation to protect hardware from repeated cycling under fault conditions.

### 4.3 Fan Control & Post-Purge Sub-FSM (`hvac_fan_substate_t`)
The `G` relay uses a **Normally Closed (NC)** contact in series with the thermostat's fan line (`G`): `relay_g = 0` (`LOW`) keeps the fan enabled/running, while `relay_g = 1` (`HIGH`) energizes the relay to cut/turn off the fan.
- **Default (`enable_fan_delay = 0`, Fan Control OFF)**: `DISABLED` — `relay_g = 0` at all times in Smart/Bypass modes (fan always enabled), except during `FIRE` where `relay_g = 1` cuts the fan.
- **Configured (`enable_fan_delay = 1`, Fan Control ON — turn off fan when AC is off)**:
  1. `COMPRESSOR_RUNNING`: While compressor is active (`RUNNING`), `relay_g = 0` (fan ON).
  2. `WAIT_DELAY`: When compressor turns OFF (`RUNNING -> STOPPING`), countdown timer `fan_delay_timer_s` runs (default 30s) with `relay_g = 0` (fan remains ON for post-cooling coil purge).
  3. `SMART_IDLE_ON`: Upon timer expiration (or when idle in `OFF`, `STARTING`, `BLOCKED_ALARM`, `FAULT`), `relay_g = 1` energizes the `G` relay to turn OFF the fan while the compressor is off.
  4. `BYPASS`: `relay_g = 0` immediately (thermostat takes over).
  5. `FIRE`: `relay_g = 1` immediately (cuts fan regardless of `enable_fan_delay`).

---

## 5. Domain Layer: HVAC Group Aggregate Root (`hvac_group_entity`)

A group coordinates $N+M$ units serving a common thermal zone.

### 5.1 Multi-Sensor Aggregation Metrics
Each group computes aggregated metrics from assigned temperature probes (`group_sensor_mask`):
- `highest_temp`: The ceiling temperature across all healthy probes. Governs all cooling start and boost triggers to eliminate hot spots.
- `valid_sensors`: Count of active sensors with `QUALITY_VALID`. If `valid_sensors == 0`, the group automatically trips `BYPASS_CAUSE_SENSOR_FAULT`.

### 5.2 Group FSM & Temperature Thresholds

```text
 Temperature (°C)
      ▲
      │
      ├─ >= T_bypass (default 32.0 °C) ──► Triggers BYPASS_CAUSE_HIGH_TEMP (relay_sm = 0)
      │
      ├─ >= T_alarm  (default 30.0 °C) ──► GROUP_STATE_COOL_BOOST (All N+M units turn ON)
      │
      ├─ >= T_max    (default 28.0 °C) ──► GROUP_STATE_COOLING (N active turn units turn ON)
      │
      ├─ <= T_min    (default 26.0 °C) ──► GROUP_STATE_IDLE (All units turn OFF; clears bypass)
      ▼
```

### 5.3 Generic $N+M$ Duty Rotation Algorithm
Instead of hardcoded presets, each group specifies:
- `active_turn_count` ($N$): Number of units running during normal cooling (1 to `unit_count`).
- `standby_units` ($M = 	ext{unit\_count} - N$): Units resting in reserve.

#### Ring-Buffer Dispatch Logic
Units are addressed circularly using `current_turn_offset`:
$$	ext{unit\_idx} = (	ext{current\_turn\_offset} + i) \pmod{	ext{unit\_count}}$$
- For $i < 	ext{active\_turn\_count}$: Unit receives start command with staggered delay:
  $$	ext{delay\_s} = i 	imes 	ext{delay\_on\_s}$$
- For $i \ge 	ext{active\_turn\_count}$: Unit receives stop command with fan purge delay.

#### Duty Shift Rotation
Periodically (every $K$ hours of runtime or upon manual command), the turn pointer rotates:
$$	ext{current\_turn\_offset} = (	ext{current\_turn\_offset} + 1) \pmod{	ext{unit\_count}}$$
This shifts the active duty window, ensuring identical runtime hours across all compressors.

### 5.4 Staggered Anti-Inrush Delay (`delay_on_s`)
Starting multiple compressors simultaneously causes severe voltage sags. The staggered delay ensures each compressor turns on sequentially:
- Unit 0 in turn window: starts at $t = 0	ext{ s}$.
- Unit 1 in turn window: starts at $t = 20	ext{ s}$.
- Unit 2 in turn window: starts at $t = 40	ext{ s}$.

### 5.5 Two-Stage Cooling Timeout & Rescue Mechanism
If a group remains in `COOLING` without achieving $T \le T_{min}$ before `cooling_timeout_s` expires:
1. **First Timeout Attempt (`cooling_timeout_count == 1`)**:
   - Increments `fault_counter` on all currently active turn units (they failed to meet thermal demand).
   - **Rescue Boost**: Forces all $N+M$ units in the group to turn ON immediately (`turn_count = unit_count`).
   - Resets `cooling_timer_s` for a second attempt.
2. **Second Timeout Attempt (`cooling_timeout_count == 2`)**:
   - If the room fails to cool even with 100% of units running, trips `BYPASS_CAUSE_COOLING_TIMEOUT` and transfers control to mechanical wall thermostats.

---

## 6. Safety Invariants & Multi-Cause Bypass Matrix

The subsystem enforces strict hierarchical priority:
$$\mathbf{FIRE} > \mathbf{BYPASS} > \mathbf{MANUAL} > \mathbf{SMART}$$

### 6.1 Fire Emergency Interlock (`DI_17`)
- Physical dry contact connected to fire detection system.
- When active: forces `GROUP_STATE_FIRE`.
- **Action**: Immediately disables all compressors and fan purge relays (`relay_aa = 0`, `relay_g = 0`, fan delay canceled) to avoid oxygenating a potential electrical fire.

### 6.2 Bypass Causes Bitmask (`bypass_causes_mask`)
Any non-zero bit immediately engages `GROUP_STATE_BYPASS` and drops `relay_sm = 0`:

```c
typedef enum {
    BYPASS_CAUSE_NONE            = 0,
    BYPASS_CAUSE_PHYSICAL_SWITCH = BIT(0), /**< Front-panel maintenance toggle switch (PD11). */
    BYPASS_CAUSE_HIGH_TEMP       = BIT(1), /**< Room temp >= T_bypass (clears only when T <= T_min). */
    BYPASS_CAUSE_COOLING_TIMEOUT = BIT(2), /**< Failed 2 successive cooling attempts. */
    BYPASS_CAUSE_ALL_FAULTS      = BIT(3), /**< All units in group reached max fault count (3). */
    BYPASS_CAUSE_SENSOR_FAULT    = BIT(4), /**< 0 valid sensors reporting in group. */
    BYPASS_CAUSE_REMOTE_COMMAND  = BIT(5), /**< Forced bypass via HMI / Modbus / SNMP / Web. */
    BYPASS_CAUSE_DISCREPANCY     = BIT(6), /**< Contactor feedback failed to close after commanding. */
    BYPASS_CAUSE_IO_FAULT        = BIT(7)  /**< Relay output adapter cannot be driven. */
} bypass_cause_flag_t;
```

#### 6.2.1 Control-Authority Loss (`BYPASS_CAUSE_IO_FAULT`)
`ao_hvac_step()` tracks a per-unit counter of **consecutive** `i_hvac_unit_set_tri_state()` failures
(`actuator_consecutive_failures[]`). Any successful actuation resets that unit's counter.

- When a unit belonging to a group reaches `AO_HVAC_ACTUATOR_FAULT_THRESHOLD` (3) consecutive
  failures, the group asserts `BYPASS_CAUSE_IO_FAULT` and drops into `GROUP_STATE_BYPASS`
  (reusing the existing `BYPASS_ENTERED` event).
- The cause is **self-clearing**: as soon as every unit of the group actuates successfully again,
  the flag is released and the group leaves BYPASS through the normal bypass-clear hysteresis.
- Rationale: a controller that cannot drive its relay outputs has lost control authority. Reporting
  unit states as if the commands had reached the hardware would be a silent unsafe condition.

Staleness of the digital-input feed is handled in the same protective direction: a remote safety
channel whose last acquisition failed (`di_snapshot_msg_t.stale_mask`) is consumed by
`on_hvac_di_feed_cb()` as **asserted**, so HP/LP/FLOW/VOLT lockouts engage instead of the unit being
started blind. This applies only to per-unit mechanical alarms, never to the FIRE contact nor to the
bypass maintenance switch (a missing expander must not latch a fire shutdown).

---

## 7. Ports & Adapters (Hexagonal Architecture) Specification

### 7.1 Actuator Port Contract (`app/include/interfaces/i_hvac_unit.h`)
Following Rule §4.1.D:

```c
typedef struct {
    int (*set_tri_state)(void *impl, uint8_t unit_id, hvac_tri_state_t tri);
    int (*get_tri_state)(void *impl, uint8_t unit_id, hvac_tri_state_t *out_tri);
} i_hvac_unit_api_t;

typedef struct {
    const i_hvac_unit_api_t *api;
    void *impl;
} i_hvac_unit_t;

static inline bool i_hvac_unit_is_bound(const i_hvac_unit_t *port)
{
    return port != NULL && port->api != NULL && port->impl != NULL &&
           port->api->set_tri_state != NULL && port->api->get_tri_state != NULL;
}

static inline int i_hvac_unit_set_tri_state(const i_hvac_unit_t *port, uint8_t unit_id, hvac_tri_state_t tri)
{
    if (!i_hvac_unit_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->set_tri_state(port->impl, unit_id, tri);
}
```

### 7.2 Authoritative Hardware Routing & EEPROM Storage Optimization

In earlier revisions, the hardware endpoint routing array (`hvac_unit_hw_config_t units[8]`, 288 bytes) was stored redundantly inside `config_hvac_t` in the EEPROM storage domain (`CONFIG_DOMAIN_HVAC`). 

Under the enhanced architecture:
1. **Single Authoritative Source of Truth**: Physical pinout, expander indices, and Modbus slave/channel assignments are declared immutably at compile time in `app/include/common/hardware_pin_map.h` (`k_brightc_hvac_hw_table` and `k_brightc_system_di_table`). Physical hardware wiring does not mutate during runtime and must not depend on mutable non-volatile storage.
2. **Elimination of EEPROM Divergence**: Removing hardware pinouts from EEPROM prevents accidental corruption or invalid pin bindings caused by incomplete storage writes or EEPROM degradation.
3. **Significant EEPROM Footprint Reduction**: `config_hvac_t` shrinks from ~476 bytes down to **~188 bytes**, freeing **288 bytes** of non-volatile storage per ping-pong slot in `AT24C256C`.

```c
typedef enum {
    HVAC_IO_BACKEND_NONE = 0,
    HVAC_IO_BACKEND_LOCAL_GPIO,       /**< STM32 direct GPIO */
    HVAC_IO_BACKEND_LOCAL_MCP23S17,   /**< On-board SPI expander (0 or 1) */
    HVAC_IO_BACKEND_REMOTE_MODBUS     /**< Remote RS-485 Modbus RTU Slave (e.g. Waveshare) */
} hvac_io_backend_t;

typedef struct {
    hvac_io_backend_t backend;
    uint8_t           device_id;      /**< Expander index (0/1) or Modbus Slave Address */
    uint16_t          channel;        /**< GPIO pin, expander pin (0..15), or Modbus coil/reg */
} hvac_io_endpoint_t;

/**
 * @brief Immutable compile-time hardware descriptor (declared in hardware_pin_map.h).
 * Zero bytes allocated in persistent EEPROM!
 */
typedef struct {
    /* Actuation Endpoints (Outputs) */
    hvac_io_endpoint_t relay_aa;      /**< Compressor contactor relay */
    hvac_io_endpoint_t relay_sm;      /**< Smart mode / bypass selector relay */
    hvac_io_endpoint_t relay_g;       /**< Fan cut / forced ventilation relay */

    /* Safety Interlocks (Inputs) */
    hvac_io_endpoint_t alarm_hp;      /**< High-pressure switch */
    hvac_io_endpoint_t alarm_lp;      /**< Low-pressure switch */
    hvac_io_endpoint_t alarm_flow;    /**< Airflow / water flow switch */
    hvac_io_endpoint_t alarm_volt;    /**< Phase / voltage monitor */

    /* Operational Feedback (Inputs) */
    hvac_io_endpoint_t fb_compressor; /**< Auxiliary contact from compressor contactor */
    hvac_io_endpoint_t fb_fan;        /**< Auxiliary contact from fan contactor */
} hvac_unit_hw_config_t;

/**
 * @brief Persistent HVAC Configuration Structure in EEPROM (~188 bytes).
 * Sized for CONFIG_DOMAIN_HVAC without redundant hardware pinout arrays.
 */
typedef struct {
    uint8_t               operation_mode;            /**< Profile: TELEMETRY_ONLY, HVAC_CONTROL_ONLY, HYBRID_FULL */
    uint8_t               total_units;               /**< Total configured units (1..8) */
    uint8_t               total_groups;              /**< Total configured groups (1..4) */
    uint8_t               bypass_alarm_contact_type; /**< Alarm 1 contact polarity (NC=0, NO=1) */
    hvac_io_endpoint_t    fire_interlock;            /**< Emergency fire shutdown dry contact */
    hvac_io_endpoint_t    bypass_switch;             /**< Front-panel physical bypass switch */
    config_hvac_group_t   groups[HVAC_MAX_GROUPS];   /**< 4 Groups x 40 B = 160 B setpoints & parameters */
    uint8_t               alarm_buzzer_muted;        /**< 1 = Buzzer muted, 0 = audible */
    uint8_t               reserved[15];              /**< Future expansion padding */
} config_hvac_t;

_Static_assert(sizeof(config_hvac_t) <= 200U, "config_hvac_t exceeds optimized EEPROM budget of 200 bytes");
```

---

## 8. Active Object: `ao_hvac`

The coordination thread is an Active Object adhering to the project's 5-phase lifecycle:
- **Thread Parameters**: Stack size `4096` bytes (file scope defined via `K_THREAD_STACK_DEFINE`), Priority `5` (preemptive).
- **Execution Interval**: 1000 ms periodic tick.
- **Subscriptions**:
  - `chan_sensor_matrix`: Receives fresh, calibrated temperature readings.
  - `chan_digital_inputs`: Receives debounced alarm states, fire contact DI_17, and bypass switch.
  - `chan_config_state`: Updates setpoints and rotation parameters dynamically on commit.
- **Publishing**:
  - `chan_hvac_events`: Broadcasts group state changes, unit transitions, boost activations, and bypass causes.
  - `chan_hvac_status`: Broadcasts periodic snapshot for HMI display and Modbus holding registers.

---

## 9. Verification & Test Plan (TDD)

1. **Unit Testing on `native_sim` (`tests/unit/hvac/`)**:
   - `test_unit_staggered_startup`: Verify that delay countdown prevents inrush overlap.
   - `test_unit_pressure_alarm_lockout`: Verify 180s retry lockout and permanent eviction after 3 faults.
   - `test_group_generic_n_plus_m_rotation`: Verify 7+1, 6+2, 4+1, 3+1, 2+1, and 1+1 rotation patterns.
   - `test_group_cooling_timeout_and_boost_rescue`: Verify first timeout forces all units ON, second timeout engages bypass.
   - `test_group_fire_emergency_priority`: Verify DI_17 aborts all fan and compressor relays instantaneously.
   - `test_bypass_hysteresis`: Verify $T_{bypass}$ trips bypass and does NOT clear until $T \le T_{min}$.
2. **Integration Testing**:
   - Verify zero static mutable state.
   - Verify MPU stack protection compliance on Cortex-M7.
   - Cross-compile for `brightc-controllerv3`.
