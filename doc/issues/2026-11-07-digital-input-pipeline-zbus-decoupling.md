# Issue Analysis: Digital Input Pipeline & Zbus Reactive Decoupling

- **Date:** 2026-11-07
- **Target Platform:** DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Author:** Embedded Technical Lead & Architecture Team
- **Status:** APPROVED & IMPLEMENTATION IN PROGRESS
- **Reference Spec:** [`doc/architecture/digital_input_subsystem.md`](../architecture/digital_input_subsystem.md)

---

## 1. Executive Summary & Problem Statement

The BrightC Controller V3 monitors critical electromechanical contacts and industrial safety alarms in data centers and telecom facilities:
1. **Evaporator Supply Fan Auxiliary Contacts ($U_{vent\_ON}$)** and **Compressor Contactor Auxiliary Feedback ($c$)** for the Revision F Thermodynamic Engine.
2. **Facility & Unit Alarms**: Smoke/Fire interlock (`DI_ROLE_SMOKE_FIRE`), High/Low pressure cutouts (`DI_ROLE_UNIT_x_ALARM_HP/LP`), water leakage, airflow loss, and voltage monitoring.

### Current Deficiencies:
1. **Zbus State Channel Without Observers**:
   - `ao_digital_input` publishes transitions to `chan_digital_input_state`, but this channel is configured with `ZBUS_OBSERVERS_EMPTY`.
2. **Missing Consolidated State Channel (Snapshot)**:
   - `chan_digital_input_state` only broadcasts transient edge events (`di_transition_event_t`). Any consumer needing the consolidated state of all 64 channels (active mask, raw mask, active durations) must poll the aggregator struct directly.
3. **HVAC Safety Interlock Disconnection**:
   - `ao_hvac` provides `ao_hvac_set_fire()` and `ao_hvac_set_unit_alarms()`, but no reactive bridge connects the Digital Input subsystem to `ao_hvac`. Physical fire or pressure trips do not reach the HVAC controller.
4. **Presentation Coupling**:
   - `app_facade_di_get_status()` directly accesses `&s_app->di.aggregator` fields instead of reading an immutable, thread-safe Zbus channel.

---

## 2. Target Reactive Architecture

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                           PHYSICAL HARDWARE INGESTION                       │
 │  - SPI1 MCP23S17 (chips 0 & 1, 24 channels) via gpio_port_get_raw() burst  │
 │  - On-chip STM32 GPIOs (User button, manual switch bypass)                  │
 │  - Modbus RTU MBSL32DI (32 remote digital inputs over RS-485)               │
 └──────────────────────────────────────┬──────────────────────────────────────┘
                                        │ adapter_di_gpio_read()
                                        ▼
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                   DOMAIN LAYER: digital_input_aggregator.c                  │
 │  - Integrator Debounce: 500 ms (contactors), 60 ms (alarms), 0 ms (Modbus) │
 │  - Polarity Normalization: active_level + contact_type -> is_active         │
 │  - Transition detection & active duration tracking (active_duration_s)      │
 └──────────────────────────────────────┬──────────────────────────────────────┘
                                        │ di_aggregator_scan()
                                        ▼
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                ACTIVE OBJECT: ao_digital_input.c (50 Hz / 20 ms)            │
 └───────────────────────┬───────────────────────────────┬─────────────────────┘
                         │ publishes                     │ publishes
                         ▼                               ▼
             ┌────────────────────────┐      ┌────────────────────────┐
             │chan_digital_input_state│      │chan_digital_input_snap │
             │(di_transition_event_t) │      │  (di_snapshot_msg_t)   │
             └───────────┬────────────┘      └───────────┬────────────┘
                         │                               │
                         │                               ├─────────────────────────────┐
                         ▼                               ▼                             ▼
                 ┌───────────────┐               ┌───────────────┐             ┌───────────────┐
                 │ Event Logger  │               │lis_hvac_di_fe │             │  App Facade   │
                 │   & Buzzer    │               │  (Listener)   │             │  & Shell CLI  │
                 └───────────────┘               └───────┬───────┘             └───────────────┘
                                                         │
                                        ┌────────────────┴────────────────┐
                                        │ ao_hvac_set_fire()              │
                                        │ ao_hvac_set_unit_alarms()       │
                                        ▼                                 ▼
                                ┌───────────────┐                 ┌───────────────┐
                                │ Fire Shutdown │                 │ Unit Lockout  │
                                └───────────────┘                 └───────────────┘
```

---

## 3. Zbus Channel Specifications

### 3.1 `chan_digital_input_snapshot`
- **Channel Name:** `chan_digital_input_snapshot`
- **Payload Type:** `di_snapshot_msg_t`
- **Header:** `app/src/application/zbus/digital_input_channels.h`
- **Definition:** `app/src/application/zbus/digital_input_channels.c`

```c
typedef struct {
    uint64_t  active_mask;                        /**< 64-bit mask of normalized active channels */
    uint64_t  raw_mask;                           /**< 64-bit mask of raw physical pin levels */
    uint8_t   channel_count;                      /**< Number of configured channels in snapshot */
    di_role_t roles[DI_CHANNEL_MAX];              /**< Semantic role assigned to each channel */
    bool      is_active[DI_CHANNEL_MAX];          /**< True if channel is asserted/alarmed */
    uint32_t  active_duration_s[DI_CHANNEL_MAX];  /**< Elapsed seconds in active state */
    uint64_t  timestamp_ms;                       /**< Acquisition timestamp */
    uint32_t  sequence;                           /**< Monotonically increasing sequence number */
} di_snapshot_msg_t;
```

### 3.2 `chan_digital_input_state`
- **Channel Name:** `chan_digital_input_state`
- **Payload Type:** `di_transition_event_t` (role, new_state, prev_state, timestamp_ms).

---

## 4. Consumer Wiring: `lis_hvac_di_feed`

In `composition_root.c`, a synchronous listener `lis_hvac_di_feed` observes `chan_digital_input_snapshot`:
1. **Fire Alarm Interlock**:
   - Inspects `snap->roles` for `DI_ROLE_SMOKE_FIRE`.
   - Dispatches `ao_hvac_set_fire(&self->hvac.ao, is_fire_active)`.
2. **Unit Mechanical Alarms (Units 0..7)**:
   - For each unit $u \in [0, 7]$, checks corresponding roles `DI_ROLE_UNIT_u_ALARM_HP`, `LP`, `FLOW`, `VOLT`.
   - Dispatches `ao_hvac_set_unit_alarms(&self->hvac.ao, u, hp, lp, flow, volt)`.

---

## 5. Implementation Tasks

- [ ] **1. Extend `digital_input_channels.h/.c`**:
  - Define `di_snapshot_msg_t`.
  - Declare and define `chan_digital_input_snapshot` with observer `lis_hvac_di_feed`.
- [ ] **2. Domain Snapshot Packing Helper**:
  - Implement `di_aggregator_pack_snapshot()` in `digital_input_aggregator.h/.c`.
- [ ] **3. Active Object Publishing**:
  - In `ao_digital_input_step()`, pack snapshot and publish to `chan_digital_input_snapshot`.
  - Add sequence tracking to `ao_digital_input_t`.
- [ ] **4. Composition Root Wiring**:
  - Implement `lis_hvac_di_feed` listener forwarding fire & unit alarms to `ao_hvac`.
- [ ] **5. Facade & Shell Decoupling**:
  - Update `app_facade_di_get_status()` & `app_facade_di_get_raw()` to read atomically from `chan_digital_input_snapshot`.
  - Add `app_facade_di_get_snapshot()`.
- [ ] **6. Verification & Invariants**:
  - Run all unit tests via Twister.
  - Verify zero invariant violations.
  - Cross-compile primary firmware target (`brightc-controllerv3`).
