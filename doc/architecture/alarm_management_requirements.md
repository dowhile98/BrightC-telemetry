# Alarm Management Subsystem: Requirements Specification

- **Document Identifier**: `doc/architecture/alarm_management_requirements.md`
- **Subsystem**: Centralized Alarm Management Subsystem (`alarm_manager`)
- **Target Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Framework**: Zephyr RTOS (West T2 Workspace)
- **Author**: Lead Embedded Firmware Architect
- **Status**: Stage 1 Requirements Specification (Approved for Review)

---

## 1. Scope & Overview

This document specifies the authoritative functional and non-functional requirements for the **Alarm Management Subsystem** of the BrightC Controller V3 firmware.

The subsystem serves as the single source of truth for the health, fault, and operational alarm state of the equipment. It is strictly responsible for:
1. Managing the full lifecycle of all system, physical, and virtual alarms.
2. Maintaining current alarm status, activation timestamps, trigger metrics, and contributing sources.
3. Detecting clean single-shot state transitions (`ON_PRESS` on activation, `ON_RELEASE` on recovery).
4. Publishing transition events asynchronously to decoupled consumers (System Indicators, Persistent Logger, SNMP Traps, HMI, Web Server, Modbus Server).
5. Providing an extensible, open-ended registration architecture allowing future alarms to be introduced without modifying the core engine.

---

## 2. Functional Requirements (FR)

### 2.1 Lifecycle & State Management
- **FR-01 (Canonical States)**: Every alarm managed by the subsystem shall exist in one of the following states:
  - `ALARM_STATE_INACTIVE`: Quiescent condition; no alarm active.
  - `ALARM_STATE_ACTIVE`: Alarm condition is asserted and active.
  - `ALARM_STATE_ACKNOWLEDGED` *(Optional per alarm descriptor)*: Condition remains asserted, but an operator has acknowledged the condition.
  - `ALARM_STATE_LATCHED` *(Optional per alarm descriptor)*: Condition has cleared, but the alarm remains held active until an explicit operator reset is performed.
- **FR-02 (Single-Shot Transitions)**:
  - The subsystem shall emit an `ON_PRESS` (`ALARM_EVENT_ACTIVATED`) transition event **if and only if** the alarm transitions from `INACTIVE` to `ACTIVE`.
  - The subsystem shall emit an `ON_RELEASE` (`ALARM_EVENT_CLEARED`) transition event **if and only if** the alarm transitions from `ACTIVE` (or `ACKNOWLEDGED`/`LATCHED`) to `INACTIVE`.
  - Persistent assertion of an already-active alarm shall **never** generate duplicate activation events.
  - Persistent quiescence of an inactive alarm shall **never** generate duplicate clearance events.
- **FR-03 (Transition Timestamps & Metrics)**:
  - On every transition to `ACTIVE`, the subsystem shall record the acquisition timestamp (`timestamp_ms`), increment the cumulative trigger counter (`trigger_count`), and record the triggering metric value (`trigger_value`).
  - On every transition to `INACTIVE`, the subsystem shall record the clearance timestamp (`cleared_timestamp_ms`) and calculate the total active duration.
- **FR-04 (Operator Acknowledgment)**:
  - For alarms configured with `is_ack_supported = true`, receiving an acknowledge command while in `ACTIVE` state shall transition the alarm to `ACKNOWLEDGED` and emit an `ALARM_EVENT_ACKNOWLEDGED` event.
  - Acknowledging an alarm shall **not** deactivate the alarm if the underlying physical condition remains asserted.
- **FR-05 (Alarm Latching)**:
  - For alarms configured with `is_latch_enabled = true`, when the physical condition returns to normal while in `ACTIVE` or `ACKNOWLEDGED` state, the alarm shall transition to `ALARM_STATE_LATCHED` and remain held until an explicit `alarm_manager_reset_latch()` command is received.

### 2.2 Multi-Source Aggregated Alarms
- **FR-06 (Aggregated Alarm Entity)**:
  - The subsystem shall support **Aggregated Alarms** where an alarm condition depends on multiple discrete sources (e.g., $N$ temperature probes assigned to an HVAC group, or multiple phase voltages).
  - An Aggregated Alarm shall maintain an internal **contributing source bitmask** (`contributing_sources_mask`).
- **FR-07 (Aggregation Semantics)**:
  - **Activation (OR Logic)**: If the aggregated alarm is currently `INACTIVE`, and **any** assigned valid source enters alarm condition, the source bit is set in the mask, and the aggregate alarm transitions to `ACTIVE` (emitting an `ON_PRESS` event).
  - **Contribution Tracking**: If additional assigned sources enter alarm condition while the aggregate alarm is already `ACTIVE`, their bits are set in the mask. No redundant `ON_PRESS` events are emitted, but a source-change event or snapshot update is recorded.
  - **Deactivation (AND-All Clear Logic)**: When a contributing source recovers, its bit is cleared from the mask. The aggregated alarm shall remain `ACTIVE` as long as at least one source remains in alarm. The aggregated alarm shall transition to `INACTIVE` (emitting an `ON_RELEASE` event) **if and only if all contributing sources have cleared** (`contributing_sources_mask == 0`).
  - **Zero Artificial Flapping**: The recovery of one source and simultaneous failure of another source within the same aggregate group shall not trigger an artificial `CLEARED` followed by `ACTIVATED` sequence.

### 2.3 Environmental & Thermodynamic Alarms
- **FR-08 (Group Low Temperature Alarm)**:
  - An alarm shall be triggered when the temperature of **any valid assigned sensor** in an HVAC group falls below the configured low temperature threshold ($T < T_{low\_alarm}$).
  - Default threshold: $15.0^\circ\text{C}$ (configurable via EEPROM/NVS).
  - Recovery condition with hysteresis: The alarm shall clear **if and only if all assigned sensors** rise above the recovery threshold ($T \ge T_{low\_alarm} + Hyst_{low}$).
  - Default hysteresis: $1.0^\circ\text{C}$ (configurable).
- **FR-09 (Group High Temperature Alarm)**:
  - An alarm shall be triggered when the temperature of **any valid assigned sensor** in an HVAC group exceeds the configured high temperature alarm threshold ($T > T_{high\_alarm}$).
  - Default threshold: $28.0^\circ\text{C}$ (configurable).
  - Recovery condition with hysteresis: The alarm shall clear **if and only if all assigned sensors** fall below the recovery threshold ($T \le T_{high\_alarm} - Hyst_{high}$).
  - Default hysteresis: $1.0^\circ\text{C}$ (configurable).
- **FR-10 (Sensor Validity Isolation)**:
  - An invalid, faulted, or disconnected sensor shall **never** be used to evaluate low or high temperature conditions.
  - Loss of a sensor reading shall **not** be interpreted as normal temperature and shall **not** clear an existing thermal alarm.
  - Disconnected sensors shall be evaluated exclusively by the **Sensor Fault / Disconnection** alarm rules.

### 2.4 Electrical Power Quality Alarms (3-Phase Line Voltage)
- **FR-11 (3-Phase Under-Voltage Alarm)**:
  - An under-voltage alarm shall trigger if the measured RMS voltage on **any active AC line** ($V_a, V_b, V_c$) falls below the under-voltage trip threshold:
    $$V_{trip\_low} = V_{nominal} \times (1 - \Delta V_{low}\%) = 220\text{ V} \times (1 - 0.08) = 202.4\text{ V}$$
  - The alarm shall clear when all active line voltages recover above:
    $$V_{clear\_low} = V_{trip\_low} + V_{hyst\_low}$$
- **FR-12 (3-Phase Over-Voltage Alarm)**:
  - An over-voltage alarm shall trigger if the measured RMS voltage on **any active AC line** ($V_a, V_b, V_c$) exceeds the over-voltage trip threshold:
    $$V_{trip\_high} = V_{nominal} \times (1 + \Delta V_{high}\%) = 220\text{ V} \times (1 + 0.08) = 237.6\text{ V}$$
  - The alarm shall clear when all active line voltages fall below:
    $$V_{clear\_high} = V_{trip\_high} - V_{hyst\_high}$$
- **FR-13 (Phase Measurement Invalidation)**:
  - If communication with the electrical energy meter (JSY-MK-339) is lost (`is_valid == false`), the line voltage alarms shall transition to an unverified state or hold previous state with a Comm Fault alarm, preventing false voltage trips.

### 2.5 HVAC Control & Actuator Alarms
- **FR-14 (HVAC System Bypass Alarm)**:
  - An alarm shall be asserted whenever the HVAC control subsystem enters `GROUP_STATE_BYPASS`, regardless of whether the cause was manual bypass switch activation, critical temperature threshold breach ($T \ge T_{bypass}$), or complete HVAC unit cascade lockout.
  - The alarm shall clear when the HVAC subsystem cleanly exits Bypass mode back to `AUTO` or `SMART` mode.
- **FR-15 (Air Conditioner Discrete Contact Alarms)**:
  - For each configured AC unit (units 1 to 4, expandable to 8), the subsystem shall maintain independent, discrete alarms for:
    1. `ALARM_ID_ACx_HIGH_PRESSURE`: Mechanical high pressure cutout contact closed.
    2. `ALARM_ID_ACx_LOW_PRESSURE`: Mechanical low pressure cutout contact closed.
    3. `ALARM_ID_ACx_AIRFLOW_FAULT`: Supply air differential pressure or flow switch fault.
    4. `ALARM_ID_ACx_VOLTAGE_FAULT`: AC unit internal phase/voltage protection trip.
- **FR-16 (AC Unit Consolidated Fault Alarm)**:
  - In addition to individual contact alarms, the subsystem shall maintain an aggregate `ALARM_ID_ACx_SUMMARY_FAULT` for each AC unit, asserting whenever any of its 4 contacts are active or when the unit is in safety lockout.

### 2.6 Facility & Safety Contact Alarms
- **FR-17 (Smoke & Fire Alarm)**:
  - Asserted immediately upon confirmation of the facility fire/smoke interlock contact (`DI_ROLE_SMOKE_FIRE`).
  - Severity: `ALARM_SEVERITY_FATAL`.
- **FR-18 (Containment Door Interlock Alarm)**:
  - Asserted upon opening of the cold/hot aisle containment door contact (`DI_ROLE_DOOR_OPEN`).
  - Severity: `ALARM_SEVERITY_INFO` / `WARNING`.
- **FR-19 (Water Leak Detection Alarm)**:
  - Asserted upon detection of liquid under raised floors (`DI_ROLE_WATER_LEAK`).
  - Severity: `ALARM_SEVERITY_CRITICAL`.

### 2.7 Hardware & Infrastructure Alarms
- **FR-20 (Ethernet Link Disconnected)**:
  - Triggered when the physical RMII Ethernet PHY link is down.
- **FR-21 (MicroSD Storage Disconnected / Fault)**:
  - Triggered when the MicroSD card detect switch opens or when the filesystem fails to mount/write.
- **FR-22 (RS-485 Modbus Bus Offline)**:
  - Triggered when repeated Modbus communication timeouts occur with critical telemetry slaves (CWT temperature bank, JSY-MK-339 meter, or MBSL32DI expander).

### 2.8 Extensibility & Registration Architecture
- **FR-23 (Open-Ended Registration)**:
  - The alarm management core shall **not** hardcode a fixed, non-modifiable switch-case or static array of size 32.
  - The subsystem shall provide an alarm registration API (`alarm_manager_register_descriptor()`) accepting an immutable descriptor (`alarm_descriptor_t`) defining:
    - Unique 16-bit Alarm ID.
    - String identifier and human-readable description.
    - Category / Source (`ALARM_SOURCE_DIGITAL_INPUT`, `SENSOR`, `HVAC`, `ELECTRICAL`, `SYSTEM`).
    - Default Severity (`INFO`, `WARNING`, `CRITICAL`, `FATAL`).
    - Priority Level (`LOW`, `MEDIUM`, `HIGH`, `EMERGENCY`).
    - Flags: `is_latch_supported`, `is_ack_supported`, `is_aggregated`.
- **FR-24 (Bounded Static Allocation)**:
  - The maximum number of manageable alarms shall be configurable at compile time via Kconfig (`CONFIG_ALARM_MANAGER_MAX_ALARMS`, default: 96).

### 2.9 Distribution & Integration Contracts
- **FR-25 (System Indicators Notification)**:
  - The subsystem shall publish every state transition to `chan_system_alarm` using the normalized `alarm_msg_t` contract:
    - `alarm_id`: ID of the alarm.
    - `level`: Highest mapped severity level (`ALARM_LEVEL_INFO`, `WARNING`, `CRITICAL`).
    - `is_active`: `true` on activation, `false` on clear.
    - `timestamp_ms`: Transition uptime.
  - The system indicators active object (`ao_system_indicator`) shall adjust LED blinking and buzzer beeping based on the highest active alarm level without blocking the alarm manager.
- **FR-26 (Persistent Logging)**:
  - Every `ON_PRESS` and `ON_RELEASE` transition shall be published to the logging pipeline to record timestamp, alarm ID, event name, trigger value, and contributing source details into non-volatile storage.
- **FR-27 (SNMP Traps)**:
  - The SNMP trap generator shall subscribe to alarm transitions and evaluate enterprise-specific trap dispatching asynchronously, completely decoupled from the alarm processing thread.
- **FR-28 (HMI & Web Server Query API)**:
  - The subsystem shall provide a thread-safe query interface (`i_alarm_query_t`) allowing HMI (DWIN) and Web Server to fetch:
    - Total count of currently active alarms.
    - Highest active severity level.
    - Bitmask / array of active alarm IDs.
    - Detailed snapshot of any alarm by ID.
- **FR-29 (Modbus Server Register Mapping)**:
  - The subsystem shall expose a compact binary bitmask and discrete register table for SCADA/BMS clients reading Modbus Discrete Inputs (FC 02) or Input Registers (FC 04).

---

## 3. Non-Functional Requirements (NFR)

- **NFR-01 (Zero Dynamic Memory Allocation)**:
  - `malloc`, `free`, `k_malloc`, `k_free`, and heap memory pools are strictly forbidden. All contexts, tables, queues, and descriptors must be allocated statically at initialization.
- **NFR-02 (Zero File-Scope Mutable State)**:
  - No file-scope static mutable variables (`static uint8_t s_var`). All state must reside in instance context structures (`ao_alarm_manager_t`, `alarm_entity_t`, etc.) passed by pointer.
- **NFR-03 (MPU Stack Guard Compliance)**:
  - Thread stacks must be defined strictly using `K_THREAD_STACK_DEFINE` at file scope in `composition_root.c` with 32-byte MPU alignment, never embedded inside `.bss` structs.
- **NFR-04 (Pure Domain Isolation)**:
  - Domain algorithms (FSM transitions, hysteresis calculations, multi-source aggregation) in `app/src/domain/` must contain zero Zephyr RTOS headers and zero hardware registers (ISO C99 standard library only).
- **NFR-05 (Deterministic Execution & Bounded Latency)**:
  - Alarm condition evaluation and transition processing must complete within $< 50\,\mu\text{s}$ per alarm evaluation on Cortex-M7 @ 480 MHz.
- **NFR-06 (Defensive Interface Contracts)**:
  - All public API wrappers must perform defensive checks (NULL handle validation, range checking) and return standard negative POSIX errno (`-EINVAL`, `-ENODEV`, `-ENOSPC`).
- **NFR-07 (Host Testability / 100% Mockable)**:
  - All domain and application logic must compile and execute on `native_sim` host builds with 100% test coverage using Ztest and in-memory fakes.

---

## 4. Initial Alarm Catalog

| Alarm ID | Identifier Name | Source Category | Severity | Priority | Description / Trigger Condition | Recovery Condition |
|:---|:---|:---|:---|:---|:---|:---|
| **0x0001** | `ALARM_AC1_HIGH_PRESSURE` | Digital Input | CRITICAL | HIGH | AC Unit 1 high pressure switch tripped | Contact opens / clears |
| **0x0002** | `ALARM_AC1_LOW_PRESSURE` | Digital Input | WARNING | MEDIUM | AC Unit 1 low pressure switch tripped | Contact opens / clears |
| **0x0003** | `ALARM_AC1_AIRFLOW_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 1 supply air/fan airflow failure | Contact opens / clears |
| **0x0004** | `ALARM_AC1_VOLTAGE_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 1 internal phase/voltage protector | Contact opens / clears |
| **0x0005** | `ALARM_AC2_HIGH_PRESSURE` | Digital Input | CRITICAL | HIGH | AC Unit 2 high pressure switch tripped | Contact opens / clears |
| **0x0006** | `ALARM_AC2_LOW_PRESSURE` | Digital Input | WARNING | MEDIUM | AC Unit 2 low pressure switch tripped | Contact opens / clears |
| **0x0007** | `ALARM_AC2_AIRFLOW_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 2 supply air/fan airflow failure | Contact opens / clears |
| **0x0008** | `ALARM_AC2_VOLTAGE_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 2 internal phase/voltage protector | Contact opens / clears |
| **0x0009** | `ALARM_AC3_HIGH_PRESSURE` | Digital Input | CRITICAL | HIGH | AC Unit 3 high pressure switch tripped | Contact opens / clears |
| **0x000A** | `ALARM_AC3_LOW_PRESSURE` | Digital Input | WARNING | MEDIUM | AC Unit 3 low pressure switch tripped | Contact opens / clears |
| **0x000B** | `ALARM_AC3_AIRFLOW_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 3 supply air/fan airflow failure | Contact opens / clears |
| **0x000C** | `ALARM_AC3_VOLTAGE_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 3 internal phase/voltage protector | Contact opens / clears |
| **0x000D** | `ALARM_AC4_HIGH_PRESSURE` | Digital Input | CRITICAL | HIGH | AC Unit 4 high pressure switch tripped | Contact opens / clears |
| **0x000E** | `ALARM_AC4_LOW_PRESSURE` | Digital Input | WARNING | MEDIUM | AC Unit 4 low pressure switch tripped | Contact opens / clears |
| **0x000F** | `ALARM_AC4_AIRFLOW_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 4 supply air/fan airflow failure | Contact opens / clears |
| **0x0010** | `ALARM_AC4_VOLTAGE_FAULT` | Digital Input | CRITICAL | HIGH | AC Unit 4 internal phase/voltage protector | Contact opens / clears |
| **0x0011** | `ALARM_FACILITY_FIRE` | Digital Input | FATAL | EMERGENCY | Facility Smoke / Fire alarm interlock active | Contact clears (Manual Ack) |
| **0x0012** | `ALARM_FACILITY_DOOR_OPEN` | Digital Input | INFO | LOW | Containment aisle door contact open | Contact closes |
| **0x0013** | `ALARM_FACILITY_WATER_LEAK`| Digital Input | CRITICAL | HIGH | Raised floor water inundation cable active | Water cleared |
| **0x0014** | `ALARM_FACILITY_SURGE_PROT`| Digital Input | WARNING | MEDIUM | SPD / Lightning arrestor cartridge tripped | Cartridge replaced |
| **0x0015** | `ALARM_FACILITY_PHASE_MON` | Digital Input | CRITICAL | HIGH | External main 3-phase sequence/loss relay | Power restored |
| **0x0020** | `ALARM_GROUP1_TEMP_LOW` | Analog/Sensor | WARNING | MEDIUM | Any group 1 sensor $< T_{low}$ ($15.0^\circ\text{C}$) | All sensors $\ge T_{low} + Hyst$ |
| **0x0021** | `ALARM_GROUP1_TEMP_HIGH` | Analog/Sensor | CRITICAL | HIGH | Any group 1 sensor $> T_{alarm}$ ($28.0^\circ\text{C}$) | All sensors $\le T_{alarm} - Hyst$ |
| **0x0022** | `ALARM_GROUP1_TEMP_BOOST` | Analog/Sensor | CRITICAL | HIGH | Temperature in boost condition ($T \ge T_{alarm}$) | All units recover room |
| **0x0023** | `ALARM_GROUP2_TEMP_LOW` | Analog/Sensor | WARNING | MEDIUM | Any group 2 sensor $< T_{low}$ | All sensors $\ge T_{low} + Hyst$ |
| **0x0024** | `ALARM_GROUP2_TEMP_HIGH` | Analog/Sensor | CRITICAL | HIGH | Any group 2 sensor $> T_{alarm}$ | All sensors $\le T_{alarm} - Hyst$ |
| **0x0030** | `ALARM_LINE_UNDER_VOLTAGE` | Electrical | CRITICAL | HIGH | Any 3-phase line $< 220\text{V} - 8\%$ ($202.4\text{V}$) | All lines $\ge 202.4\text{V} + 2\text{V}$ |
| **0x0031** | `ALARM_LINE_OVER_VOLTAGE` | Electrical | CRITICAL | HIGH | Any 3-phase line $> 220\text{V} + 8\%$ ($237.6\text{V}$) | All lines $\le 237.6\text{V} - 2\text{V}$ |
| **0x0040** | `ALARM_HVAC_BYPASS_ACTIVE` | HVAC/Control | CRITICAL | HIGH | HVAC system entered Bypass operation | System returns to Auto/Smart |
| **0x0041** | `ALARM_HVAC_UNIT1_LOCKOUT` | HVAC/Control | CRITICAL | HIGH | AC Unit 1 in safety lockout / fault | Unit fault cleared / reset |
| **0x0042** | `ALARM_HVAC_UNIT2_LOCKOUT` | HVAC/Control | CRITICAL | HIGH | AC Unit 2 in safety lockout / fault | Unit fault cleared / reset |
| **0x0043** | `ALARM_HVAC_UNIT3_LOCKOUT` | HVAC/Control | CRITICAL | HIGH | AC Unit 3 in safety lockout / fault | Unit fault cleared / reset |
| **0x0044** | `ALARM_HVAC_UNIT4_LOCKOUT` | HVAC/Control | CRITICAL | HIGH | AC Unit 4 in safety lockout / fault | Unit fault cleared / reset |
| **0x0050** | `ALARM_SENSOR_GROUP1_FAULT` | Sensor/HW | WARNING | MEDIUM | Assigned control sensor disconnected/fault | Sensor communication restored |
| **0x0051** | `ALARM_SENSOR_GROUP2_FAULT` | Sensor/HW | WARNING | MEDIUM | Assigned control sensor disconnected/fault | Sensor communication restored |
| **0x0060** | `ALARM_INFRA_ETHERNET_DOWN` | System | INFO | LOW | RMII Ethernet physical link down | Ethernet link up |
| **0x0061** | `ALARM_INFRA_SD_CARD_FAULT`| System | WARNING | MEDIUM | MicroSD unmounted or write failure | SD card mounted / verified |
| **0x0062** | `ALARM_INFRA_MODBUS_FAULT` | System | WARNING | MEDIUM | Repeated communication error on RS-485 bus | Modbus communication restored |
