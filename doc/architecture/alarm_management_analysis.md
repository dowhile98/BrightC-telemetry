# Alarm Management Subsystem: Comparative Analysis & Legacy Audit

- **Document Identifier**: `doc/architecture/alarm_management_analysis.md`
- **Subsystem**: Centralized Alarm Management Subsystem (`alarm_manager`)
- **Target Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Framework**: Zephyr RTOS (West T2 Workspace)
- **Author**: Lead Embedded Firmware Architect
- **Status**: Stage 1 Analysis & Architectural Proposal (Approved for Review)

---

## 1. Executive Summary & Audit Objectives

The BrightC Controller V3 is an industrial IoT edge telemetry and mission-critical HVAC controller deployed in data centers, telecom base stations, and central offices. A core capability is the **centralized management of system, electrical, environmental, and electro-mechanical alarms**.

The goal of this Stage 1 analysis is to rigorously audit two reference codebases:
1. **Reference Project 1 (Stable Legacy)**:
   `/media/jeffry/jqb/GitHub/BrightC-V3/Firmware/BrightC_ETH_CycloneTCP_FileX`
   *(STM32Cube + Azure RTOS ThreadX + CycloneTCP + FileX)*
2. **Reference Project 2 (Initial Zephyr Prototype)**:
   `/media/jeffry/jqb/brightc_/brightc_firmware`
   *(Early Zephyr RTOS migration attempt with partial DDD layering)*

This audit reconstructs the complete functional behavior of all alarms in production, identifies gaps, flaws, and lost features, and establishes the architectural foundation for a robust, modular, decoupled, and extensible **Alarm Management Subsystem** in `BrightC-telemetry`.

---

## 2. In-Depth Audit of Reference Project 1 (Stable Legacy)

### 2.1 Overview & Execution Model
Reference Project 1 is based on Azure RTOS (ThreadX) running on STM32H743:
- **Central Alarm Thread**: `brightc_alarm_manager_thread_entry` in `BrightC/App/Src/brightc_alarm_thread_process_entry.c`.
- **Inter-Thread Communication**: A single ThreadX message queue (`brightc_system.queue`) carrying messages of type `Brightc_alarm_queue_t` (`uint16_t alarm; uint8_t state;`).
- **Alarm Storage**: A single 32-bit unsigned integer bitmask (`brightc_system.alarms`) representing active alarms (`bit = 1` for active, `bit = 0` for cleared).
- **Physical Output Signaling**: Direct SPI expander writes (`brightc_alarm_set_io()`) to drive output relays (e.g., `BRIGHTC_ALARM1` to `BRIGHTC_ALARM6`).
- **Acoustic Signaling**: Polled by a separate thread `brightc_buzzer_thread_entry` which checks `if ((brightc_system.alarms != 0) && (brightc.conf.sound == 1))` and emits a 1500 Hz intermittent double-beep.
- **External Exposure**: Direct bitmask reading in HTTP REST callbacks (`/api/alarms_status`), Modbus TCP register polling, and synchronous SNMP trap dispatching (`brightc_alarm_send_trap()`).

### 2.2 Functional Alarm Flows in Reference Project 1

#### A. Air Conditioner Contact Alarms (4 Contacts per AC x 4 Units = 16 Alarms)
In `brightc_io_manager_thread_entry.cpp`, two MCP23S17 SPI expanders (`IO1` and `IO2`) are polled every 500 ms using the lightweight button library `lwbtn`:
- **AC Unit 1 (AA1)**:
  - Input 0 (`GPA0`, IO1): High Pressure Switch (`BRIGHTC_ALARM_AA1_HIGH_PRESSURE`).
  - Input 1 (`GPA1`, IO1): Low Pressure Switch (`BRIGHTC_ALARM_AA1_LOW_PRESSURE`).
  - Input 2 (`GPA2`, IO1): Airflow / Supply Fault (`BRIGHTC_ALARM_AA1_SUPPLY`).
  - Input 3 (`GPA3`, IO1): Phase / Voltage Protection (`BRIGHTC_ALARM_AA1_HIGH_LOW_VOLTAGE`).
- **AC Unit 2 (AA2)**: Inputs 4..7 (`GPA4..GPA7`, IO1).
- **AC Unit 3 (AA3)**: Inputs 8..11 (`GPB0..GPB3`, IO1).
- **AC Unit 4 (AA4)**: Inputs 12..15 (`GPA0..GPA3`, IO2).

**Operational Flow**:
1. When a contact closes (`LWBTN_EVT_ONPRESS`): calls `brightc_alarm_set(alarm_id, BRIGHTC_ALARM_ENABLE)`.
2. When a contact opens (`LWBTN_EVT_ONRELEASE`): calls `brightc_alarm_set(alarm_id, BRIGHTC_ALARM_DISABLE)`.
3. In `brightc_alarm_manager_thread_entry`:
   - Sets/clears bit in `brightc_system.alarms |= (1 << msg.alarm)`.
   - Writes a text log into memory/SD via `brightc_log_write()`.
   - Directly mutates the target AC unit struct: `hvac[i].alarmHP = msg.state`, etc.
   - Sets or unsets ThreadX event flags: `tx_event_flags_set(&hvac[i].events, BRIGHTC_HVAC_PRESSURE_ALARM, ...)`.
   - Sets HMI update event flag: `tx_event_flags_set(&brightc_system.events, SYSTEM_HMI_UPDATE, TX_OR)`.
   - Synchronously constructs and sends an SNMP v2c trap via `brightc_alarm_send_trap(msg.alarm, msg.state)`.

#### B. Facility Contact Alarms (Inputs 17 to 24)
- **Input 16 (IN17)**: Smoke / Fire Alarm (`BRIGHTC_ALARM_FIRE`). When active, it forces all HVAC units into emergency stop or emergency ventilation depending on configuration (`brightc.conf.fan`).
- **Input 17 (IN18)**: Door Open Alarm (`BRIGHTC_ALARM_DOOR`).
- **Inputs 18 to 23 (IN19..IN24)**: Generic digital inputs (`BRIGHTC_ALARM_GENERIC_19..24`).
- **MicroSD Card Detection**: Microswitch on GPIO pin polled by `brightcGpioIn` (input 2). On press, sends `BRIGHTC_ALARM_SD_DISCONECTED`.
- **Manual Bypass Switch**: Polled by `brightcGpioIn` (input 0). On press, posts bypass command to HVAC thread.

#### C. High Temperature Alarm
- Evaluated in `brightc_adc_thread_entry.c`:
  - Periodically calls `brightc_hvac_temp_alarm(&brightc)`.
  - Condition: iterates over all configured RTD probes (`sensor.T[i]`); if **any** sensor $T_i > T_{alarm}$, it returns 1.
  - State tracking: relies on a single variable `brightc.high_temp`.
  - On activation: sets `brightc.high_temp = 1`, calls `brightc_alarm_set(BRIGHTC_ALARM_HIGH_TEMP, BRIGHTC_ALARM_ENABLE)`, activates relay `BRIGHTC_ALARM2` via `brightc_alarm_set_io()`, and logs event.
  - On deactivation: sets `brightc.high_temp = 0`, calls `brightc_alarm_set(BRIGHTC_ALARM_HIGH_TEMP, BRIGHTC_ALARM_DISABLE)`, clears relay.
  - **Critical Flaw**: **Zero hysteresis**! When temperature hovers around $T_{alarm}$, the alarm constantly toggles between enable and disable, flooding logs and SNMP traps.

#### D. Low Temperature Alarm
- **Finding**: **Completely non-existent** in Reference Project 1! The firmware had no low-temperature protection or alarm.

#### E. Electrical Line Voltage Alarms
- **Finding**: Reference Project 1 **did not implement AC line voltage monitoring** (no high/low grid voltage alarm from an electrical meter). The only voltage alarm was the binary contact input from each AC unit's internal phase protector (`alarmVoltaje` on inputs 3, 7, 11, 15).

#### F. Bypass Alarm
- Generated in `brightc_hvac_bypass_thread_entry.c`:
  - When the system transitions into Bypass mode (due to manual switch, temperature exceeding $T_{bypass}$, or HVAC group failure), it calls `brightc_alarm_set(BRIGHTC_ALARM_BYPASS, BRIGHTC_ALARM_ENABLE)`.
  - Deactivates when the bypass condition is released.

#### G. Sensor Disconnection Alarm
- Evaluated in `brightc_hvac_control_thread_entry.c`:
  - Calls `brightc_alarm_set(BRIGHTC_ALARM_SENSOR_DISCONECTED, obj->sensor_status ? BRIGHTC_ALARM_DISABLE : BRIGHTC_ALARM_ENABLE)`.

### 2.3 Architectural & Design Flaws in Reference Project 1

1. **Monolithic God Thread**:
   `brightc_alarm_manager_thread_entry` is tightly coupled to every subsystem. It directly accesses `hvac[i]` structs, `brightc` global variables, ThreadX mutexes, ThreadX event flags, SNMP context, and DWIN HMI event flags inside a single 400-line `switch-case` statement.
2. **32-Alarm Hard Ceiling**:
   Because `brightc_system.alarms` is a `uint32_t` bitmask, the system cannot exceed 32 alarms without breaking binary compatibility with HMI, Web Server, and SNMP.
3. **Queue Head-of-Line Blocking**:
   `tx_queue_send(&brightc_system.queue, &msg, pdMS_TO_TICKS(200))` blocks the sender if the queue is full. Furthermore, `brightc_alarm_send_trap()` performs network I/O synchronously inside the alarm manager thread loop, stalling alarm processing if network transmission lags.
4. **No Alarm History or Timestamps**:
   Individual alarms store neither activation timestamp, duration, nor triggering value. Only the latest trap event holds an RTC timestamp.
5. **No Group Source Traceability**:
   For `BRIGHTC_ALARM_HIGH_TEMP`, the system cannot report which specific sensor probe or probes caused the alarm.
6. **Modbus Server Index Bug**:
   In `brightc_modbus_tcp.c` (lines 304–356), register assignments for unit 0 incorrectly indexed `hvac[1]`, `hvac[2]`, and `hvac[3]` due to copy-paste errors.

---

## 3. In-Depth Audit of Reference Project 2 (Initial Zephyr Prototype)

### 3.1 Overview & Execution Model
Reference Project 2 attempted to structure the firmware using Clean Architecture principles on Zephyr RTOS:
- **Domain Entities**: `alarm_entity.h`/`.c` implements a state machine with single-shot edge triggering (`INACTIVE` $ightarrow$ `ACTIVE` $ightarrow$ `ACKNOWLEDGED` $ightarrow$ `LATCHED`).
- **Domain Value Object**: `alarm_event_t` in `alarm_value.h` captures snapshot data (`alarm_id`, `source`, `severity`, `priority`, `previous_state`, `new_state`, `timestamp_us`, `trigger_value`).
- **Application Layer**: `process_alarm_use_case.c` evaluates conditions and publishes transitions via an output port.
- **Event Bus**: Zephyr Zbus used for decoupled message distribution.
- **Observers**:
  - `zbus_digital_in_alarm_observer.c`
  - `zbus_sensor_alarm_observer.c`
  - `zbus_alarm_buzzer_observer.c`
  - `zbus_alarm_logger.c`
  - `zbus_hvac_alarm_bridge.c`
- **Cache / Repository**: `alarm_repository.c` holds a RAM cache (`g_alarm_cache[ALARM_ID_MAX]`) protected by a mutex for PULL queries by HMI and Modbus.

### 3.2 Evaluation of Reference Project 2 Modules

#### A. Alarm Entity (`alarm_entity.c`)
- **Strengths**:
  - Pure C (C99/C11) with zero Zephyr or hardware dependencies.
  - Strict single-shot edge triggering: only emits an event when state changes (`prev_state != new_state`).
  - Supports optional latching (`is_latch_enabled`) and operator acknowledgment (`is_ack_required`).
  - Tracks trigger counter (`trigger_count`) and timestamps (`last_triggered_us`, `last_cleared_us`).
- **Weaknesses**:
  - Lacks multi-source aggregation support. Each entity is strictly 1:1 with an `alarm_id`. It cannot handle an alarm fed by $N$ independent sensor probes where any 1 probe triggers the alarm, but all probes must recover before the alarm clears.

#### B. Digital Input Observer (`zbus_digital_in_alarm_observer.c`)
- Translates `digital_input_event_t` directly to `process_alarm_uc_evaluate(..., ALARM_ID_DIGITAL_IN_1 + input_id, is_active, 0.0f, timestamp)`.
- Maps 24 digital inputs directly to alarms.

#### C. Sensor Alarm Observer (`zbus_sensor_alarm_observer.c`)
- **Broken Hysteresis Algorithm**:
  ```c
  bool is_low = any_below && !all_above_hyst;
  ```
  If threshold is 15.0°C and hysteresis is 1.0°C:
  - If $T = 14.5^\circ	ext{C}$: `any_below = true`, `all_above_hyst = false` $ightarrow$ `is_low = true`. (Correct)
  - If $T$ rises to $15.5^\circ	ext{C}$: `any_below = false` (since $15.5 \ge 15.0$).
  - `is_low` immediately calculates to `false && !all_above_hyst = false`!
  - **The alarm clears at 15.0°C instead of 16.0°C! The hysteresis had zero effect on recovery!**
- **Missing High Temperature Alarm**:
  The observer only evaluated low temperature; high temperature was completely omitted.
- **File-Scope Static Mutable State**:
  Defines `g_temp_values[]`, `g_temp_initialized[]`, and `g_alarm_uc` at file scope, violating Clean Architecture Invariant 6.

#### D. Buzzer Observer (`zbus_alarm_buzzer_observer.c`)
- Listens on Zbus, counts active alarms via `alarm_repository_get_active_count()`, and uses deferred `k_work` to call `play_buzzer_pattern_uc` or `stop_buzzer_pattern_uc`.
- **Race Condition**: Shared file-scope variables without queue buffering cause intermediate events to be lost if multiple alarms activate simultaneously.

#### E. HVAC Alarm Bridge (`zbus_hvac_alarm_bridge.c`)
- Listens to HVAC events and only logs warnings. It **does not generate formal alarm entities** for Bypass or HVAC failures.

---

## 4. Comprehensive Cross-Project Comparison Matrix

| Feature / Dimension | Reference Project 1 (Stable Legacy) | Reference Project 2 (Initial Zephyr) | Gap / Defect Identified | Recommendation for BrightC-telemetry |
|:---|:---|:---|:---|:---|
| **Architectural Model** | Monolithic procedural thread (`TX_THREAD`) with direct global mutation | Hexagonal / Clean Architecture prototype with Zbus | Ref 1 is heavily coupled; Ref 2 has broken observers and file-scope globals | Implement Clean Architecture Active Object (`ao_alarm_manager`) with canonical 5-phase lifecycle |
| **Alarm State Representation** | Flat 32-bit bitmask (`uint32_t alarms`) | FSM entity per ID (`INACTIVE`, `ACTIVE`, `ACK`, `LATCHED`) | Ref 1 capped at 32 alarms; Ref 2 hardcoded to 60 IDs | Dynamic/extensible registration model supporting scalable descriptor table |
| **AC Contacts (4 per AC)** | 16 discrete inputs directly mapped to `hvac[i]` flags | 24 generic inputs mapped to `ALARM_ID_DIGITAL_IN_x` | Ref 2 lost semantic AC contact roles (`HP`, `LP`, `FLOW`, `VOLT`) | Restore semantic roles via `di_role_t` and map directly to AC unit instances |
| **Low Temperature Alarm** | Not implemented | Implemented globally across sensors | Ref 2 hysteresis calculation is mathematically defective | Implement proper FSM hysteresis with memory of active state and group sensor filtering |
| **High Temperature Alarm** | Any probe $> T_{alarm}$; zero hysteresis | Not implemented | Ref 1 flaps around setpoint; Ref 2 omitted it entirely | Implement group-scoped evaluation with configurable hysteresis ($T_{alarm} - Hyst$) |
| **3-Phase Line Voltage Alarms** | Not implemented (only contact inputs) | Not implemented | Neither project monitors AC line voltage thresholds | Ingest `energy_reading_t`, monitor Over/Under voltage on lines with hysteresis |
| **Bypass Alarm** | Triggered directly in bypass thread | Only logged in bridge; no alarm event | Bypass state not tracked as a formal alarm entity in Zephyr | Trigger `ALARM_ID_HVAC_BYPASS` whenever HVAC group state transitions to/from BYPASS |
| **AC Unit Lockout / Failure** | Direct flag `BRIGHTC_HVAC_PRESSURE_ALARM` | Not implemented | No aggregate alarm for total unit lockout | Aggregate unit contact alarms into `ALARM_ID_HVAC_UNIT_FAULT` |
| **Sensor Disconnection** | Binary check on RTD status | Evaluated per sensor via bitmask | Ref 2 did not aggregate into group telemetry | Per-sensor fault alarms + Group sensor degradation alarm |
| **Infrastructure Alarms** | Ethernet, SD card | Ethernet, SD, RTC battery, Modbus | Ref 1 uses global variables; Ref 2 not connected to drivers | Event-driven detection from network and storage active objects |
| **Distribution Mechanism** | Synchronous queue + direct function calls | Zephyr Zbus event channel | Ref 1 blocks thread; Ref 2 worker can drop burst events | Asynchronous Zbus event publishing + thread-safe snapshot cache (`i_alarm_query_t`) |
| **Buzzer Integration** | Separate thread polling `alarms != 0` | Zbus observer controlling buzzer use case | Ref 1 lacks priority tones; Ref 2 drops bursts | Feed `ao_system_indicator` via `chan_system_alarm` using prioritized severity levels |
| **External Interfaces (HMI/Web/Modbus)** | Direct bitmask reads & index bugs | Mutex-protected repository cache | Ref 1 has copy-paste bugs; Ref 2 lacks bitmask & snapshot serialization | Expose standardized fat-pointer query interface `i_alarm_query_t` |

---

## 5. Classification of Findings

1. **Existing and Correct (Preserve in Design)**:
   - 4 contact inputs per AC unit: High Pressure (HP), Low Pressure (LP), Airflow/Supply (FLOW), Voltage/Phase Monitor (VOLT).
   - Dedicated physical inputs for Fire Alarm (`BRIGHTC_ALARM_FIRE`) and Door Contact (`BRIGHTC_ALARM_DOOR`).
   - Single-shot edge-triggering semantics (emit exactly 1 event on activation, exactly 1 on clear).
   - Acoustic alarm beeping whenever active alarms exist (unless muted by configuration).
   - SNMP trap generation with alarm ID, description, timestamp, and active/cleared status.
   - HMI visual alarm indicators per AC unit contact.

2. **Existing Needing Improvements**:
   - High temperature alarm: needs configurable hysteresis ($T_{alarm} - Hyst$) to eliminate flapping.
   - Bypass alarm: needs formal entity status, logging, and integration into indicators.
   - Acoustic signaling: needs multi-level severity escalation (Critical vs Warning vs Info) instead of a single generic beep.
   - Alarm repository: must eliminate file-scope static mutable state and comply with DI container architecture.

3. **Functionality Lost During Migration**:
   - Mapping of the 16 AC contacts to specific AC unit control logic (`hvac[i]`).
   - DWIN HMI icon registers mapping (0x10F1 to 0x1100).
   - Modbus TCP/RTU Discrete Inputs mapping for alarm status.
   - Web Server `/api/alarms_status` JSON output.

4. **Incomplete Functionality**:
   - Low temperature alarm: hysteresis formula in Ref 2 is broken.
   - Sensor fault alarms: Ref 2 defines IDs but does not evaluate them across all bus concentrators.

5. **Architectural Deficiencies in Legacy Code**:
   - Monolithic switch-case in Ref 1 coupling all subsystems.
   - Hardcoded 32-bit integer bitmask limiting alarm count.
   - Synchronous network I/O (SNMP traps) executed inside the alarm processing loop.
   - Unprotected shared global variables (`brightc.network_alarm`, `brightc.sd_alarm`).

6. **New Required Functionality**:
   - 3-Phase AC Line Voltage monitoring (High Voltage $> 220	ext{V} + 8\%$, Low Voltage $< 220	ext{V} - 8\%$) with hysteresis.
   - Multi-source aggregated alarms (Group Low Temp, Group High Temp, Group Sensor Degraded).
   - Extensible registration engine allowing modules to register alarms without modifying core headers.
   - Thread-safe snapshot and query port (`i_alarm_query_t`) for decoupled consumers.

---

## 6. Conclusion & Transition to Architecture

The audit clearly indicates that while Reference Project 1 proved the required operational features in production, its monolithic, tightly-coupled implementation cannot scale. Conversely, Reference Project 2 introduced valuable Clean Architecture concepts (pure domain entities, Zbus decoupling), but suffered from flawed evaluation algorithms, static mutable state, and incomplete integration.

The new **Alarm Management Subsystem** in `BrightC-telemetry` will combine the functional completeness of Reference Project 1 with a strictly decoupled, testable, and extensible Clean Architecture design based on Zephyr RTOS Active Objects.
