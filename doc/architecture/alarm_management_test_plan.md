# Alarm Management Subsystem: Test & Verification Plan

- **Document Identifier**: `doc/architecture/alarm_management_test_plan.md`
- **Subsystem**: Centralized Alarm Management Subsystem (`alarm_manager`)
- **Target Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6) & `native_sim` (Host Linux)
- **Framework**: Zephyr Ztest + In-Memory Fakes (Clean Architecture / TDD)
- **Author**: Lead Embedded Firmware Architect
- **Status**: Stage 1 Test Plan Specification (Approved for Review)

---

## 1. Test Strategy & Philosophy

In accordance with BrightC-telemetry TDD standards (§6 of `AGENTS.md`), all domain logic, detectors, and Active Object state machines must be **100% testable on the host environment using `native_sim`** without requiring physical microcontroller hardware.

### 1.1 Test Levels
1. **Domain Unit Tests (`tests/unit/alarm/domain/`)**:
   - Pure C testing of `alarm_entity_t`, `alarm_aggregate_entity_t`, and `alarm_registry_t`.
   - Zero RTOS headers; deterministic and instantaneous execution.
2. **Detector Unit Tests (`tests/unit/alarm/detectors/`)**:
   - Verification of temperature hysteresis, 3-phase line voltage trip/recovery equations, and digital input contact mapping.
3. **Active Object & Lifecycle Tests (`tests/unit/alarm/ao/`)**:
   - Verification of canonical 5-phase lifecycle (`init`, `start`, `stop`, `wait_stopped`, `deinit`).
   - MPU stack safety, queue processing, deduplication, and non-blocking posting.
4. **Integration & Consumer Contract Tests (`tests/unit/alarm/integration/`)**:
   - Zbus message routing to `chan_alarm_transition` and `chan_system_alarm`.
   - Verification of `i_alarm_query_t` query port under concurrent access.
   - Closed-loop verification with `ao_system_indicator` (LED and Buzzer response).

---

## 2. Test Suites & Test Cases

### 2.1 Suite 1: Pure Domain Alarm Entity (`test_alarm_entity.c`)
- **TC-ENT-01: Single-Shot Activation (`ON_PRESS`)**:
  - Feed `is_condition_true = true` into an `INACTIVE` entity.
  - Assert state transitions to `ALARM_STATE_ACTIVE`.
  - Assert `ALARM_EVENT_ACTIVATED` event is emitted.
  - Assert `trigger_count == 1` and `timestamp_ms` is recorded.
- **TC-ENT-02: Duplicate Suppression on Persistent Condition**:
  - Feed `is_condition_true = true` repeatedly into an already `ACTIVE` entity.
  - Assert state remains `ALARM_STATE_ACTIVE`.
  - Assert function returns `false` (no event emitted).
  - Assert `trigger_count` remains 1.
- **TC-ENT-03: Single-Shot Clearance (`ON_RELEASE`)**:
  - Feed `is_condition_true = false` into an `ACTIVE` entity (latch disabled).
  - Assert state transitions to `ALARM_STATE_INACTIVE`.
  - Assert `ALARM_EVENT_CLEARED` event is emitted.
  - Assert `cleared_timestamp_ms` is recorded.
- **TC-ENT-04: Duplicate Suppression on Persistent Normal State**:
  - Feed `is_condition_true = false` repeatedly into an `INACTIVE` entity.
  - Assert state remains `INACTIVE` and zero events are emitted.
- **TC-ENT-05: Latch Mode Behavior**:
  - Configure entity with `is_latch_enabled = true`.
  - Activate entity (`INACTIVE` $\rightarrow$ `ACTIVE`).
  - Feed `is_condition_true = false`.
  - Assert state transitions to `ALARM_STATE_LATCHED` (not `INACTIVE`).
  - Feed `is_condition_true = true`; assert state remains `LATCHED` (no re-arming).
  - Issue `alarm_entity_reset_latch()`.
  - Assert state transitions to `ALARM_STATE_INACTIVE` with `ALARM_EVENT_CLEARED`.
- **TC-ENT-06: Operator Acknowledgment**:
  - Activate entity. Issue `alarm_entity_acknowledge()`.
  - Assert state transitions to `ALARM_STATE_ACKNOWLEDGED`.
  - Assert alarm remains active in query.
  - Feed `is_condition_true = false`; assert state transitions to `INACTIVE`.

### 2.2 Suite 2: Multi-Source Aggregated Alarm Entity (`test_alarm_aggregate.c`)
- **TC-AGG-01: First Source Triggers Aggregate Activation**:
  - Initialize aggregate with 5 temperature probes (mask `0x1F`).
  - Assert initial aggregate state is `INACTIVE`.
  - Assert probe 2 ($T = 14.2^\circ\text{C} < 15.0^\circ\text{C}$).
  - Assert aggregate transitions to `ACTIVE`, emitting single `ALARM_EVENT_ACTIVATED`.
  - Assert `contributing_sources_mask == (1 << 2)`.
- **TC-AGG-02: Second Source Joins Active Aggregate Without Duplicate Event**:
  - Assert probe 4 ($T = 13.9^\circ\text{C}$).
  - Assert aggregate remains `ACTIVE`.
  - Assert no duplicate `ACTIVATED` event is emitted.
  - Assert `contributing_sources_mask == ((1 << 2) | (1 << 4))`.
- **TC-AGG-03: First Source Recovers While Second Remains Active (Zero Flapping)**:
  - Recover probe 2 ($T = 16.5^\circ\text{C} \ge 16.0^\circ\text{C}$).
  - Assert aggregate remains `ACTIVE`.
  - Assert **no artificial CLEARED event** is emitted.
  - Assert `contributing_sources_mask == (1 << 4)`.
- **TC-AGG-04: All Sources Recover Triggering Aggregate Clearance**:
  - Recover probe 4 ($T = 16.2^\circ\text{C}$).
  - Assert `contributing_sources_mask == 0`.
  - Assert aggregate transitions to `INACTIVE`, emitting single `ALARM_EVENT_CLEARED`.

### 2.3 Suite 3: Condition Detectors (`test_alarm_detectors.c`)
- **TC-DET-01: Digital Input Contact Mapping**:
  - Inject `di_transition_event_t` with `role = DI_ROLE_UNIT_0_ALARM_HP` and `new_state = DI_STATE_ACTIVE`.
  - Assert detector dispatches condition `true` for `ALARM_AC1_HIGH_PRESSURE`.
  - Inject `new_state = DI_STATE_INACTIVE`.
  - Assert detector dispatches condition `false`.
- **TC-DET-02: Temperature Threshold & Hysteresis Calculation**:
  - Given $T_{low\_alarm} = 15.0^\circ\text{C}$, $Hyst = 1.0^\circ\text{C}$:
  - Input $15.1^\circ\text{C}$ $\rightarrow$ No alarm.
  - Input $14.9^\circ\text{C}$ $\rightarrow$ Low Temp Alarm asserts.
  - Input $15.5^\circ\text{C}$ $\rightarrow$ Low Temp Alarm **remains asserted** (within hysteresis band).
  - Input $16.0^\circ\text{C}$ $\rightarrow$ Low Temp Alarm **clears**.
- **TC-DET-03: High Temperature Threshold & Hysteresis Calculation**:
  - Given $T_{high\_alarm} = 28.0^\circ\text{C}$, $Hyst = 1.0^\circ\text{C}$:
  - Input $27.9^\circ\text{C}$ $\rightarrow$ No alarm.
  - Input $28.1^\circ\text{C}$ $\rightarrow$ High Temp Alarm asserts.
  - Input $27.5^\circ\text{C}$ $\rightarrow$ High Temp Alarm **remains asserted** (above $28.0 - 1.0 = 27.0$).
  - Input $26.9^\circ\text{C}$ $\rightarrow$ High Temp Alarm **clears**.
- **TC-DET-04: Sensor Fault & Disconnection Isolation**:
  - Inject sensor sample with `quality = QUALITY_DISCONNECTED`.
  - Assert detector asserts `ALARM_SENSOR_GROUP1_FAULT`.
  - Assert low/high temperature evaluators **ignore** the disconnected probe.
- **TC-DET-05: 3-Phase Line Voltage Over/Under Detection**:
  - Given nominal $220\text{V}$, trip band $\pm 8\%$ ($202.4\text{V}$ / $237.6\text{V}$), $Hyst = 2.0\text{V}$:
  - Phase A = $200.0\text{V}$ $\rightarrow$ Under-Voltage asserts.
  - Phase A = $203.0\text{V}$ $\rightarrow$ Under-Voltage **remains asserted** ($< 204.4\text{V}$).
  - Phase A = $205.0\text{V}$ $\rightarrow$ Under-Voltage **clears**.
  - Phase B = $239.0\text{V}$ $\rightarrow$ Over-Voltage asserts.
  - Phase B = $236.0\text{V}$ $\rightarrow$ Over-Voltage **clears** ($< 235.6\text{V}$).

### 2.4 Suite 4: Active Object Lifecycle & Concurrency (`test_ao_alarm_manager.c`)
- **TC-AO-01: Canonical 5-Phase Lifecycle**:
  - `init` $\rightarrow$ thread dormant in `.bss` / statically defined stack.
  - `start` $\rightarrow$ thread running, queue active.
  - `stop` $\rightarrow$ termination requested.
  - `wait_stopped` $\rightarrow$ thread cleanly halted.
  - `deinit` $\rightarrow$ primitives cleaned up.
- **TC-AO-02: Message Queue Deserialization & Priority**:
  - Post burst of 10 alarm condition updates.
  - Verify all 10 are processed in sequence without dropped events.
- **TC-AO-03: Non-Blocking Post Under Saturated Queue**:
  - Fill queue to capacity. Post additional event with `K_NO_WAIT`.
  - Assert post helper returns `-ENOSPC` without hanging the caller.

### 2.5 Suite 5: Consumer Integration & Query Interface (`test_alarm_integration.c`)
- **TC-INT-01: System Indicators Notification**:
  - Activate `ALARM_AC1_HIGH_PRESSURE` (Severity: `CRITICAL`).
  - Verify `chan_system_alarm` receives `alarm_msg_t` with `level = ALARM_LEVEL_CRITICAL`, `is_active = true`.
  - Verify `ao_system_indicator` activates fast LED blinking and buzzer melody.
  - Clear alarm; verify `chan_system_alarm` receives `is_active = false`.
  - Verify `ao_system_indicator` returns buzzer to silent and LED to heartbeat.
- **TC-INT-02: Query Port Interface (`i_alarm_query_t`)**:
  - With 3 alarms active (1 Warning, 2 Critical):
  - `i_alarm_query_get_active_count()` returns 3.
  - `i_alarm_query_get_highest_active_severity()` returns `ALARM_SEVERITY_CRITICAL`.
  - `i_alarm_query_is_active(id)` returns `true` for active, `false` for inactive.
  - `i_alarm_query_get_active_alarm_ids()` fills buffer with the 3 IDs.

---

## 3. Detailed Verification of Section 11 Operational Scenarios

### Scenario 1 — Activation and Recovery of Single Alarm
- **Action**: Contact for AC1 High Pressure closes.
- **Expected**:
  1. Detector posts condition `true`.
  2. `ao_alarm_manager` transitions `ALARM_AC1_HIGH_PRESSURE` from `INACTIVE` to `ACTIVE`.
  3. Single `ON_PRESS` event published to `chan_alarm_transition`.
  4. Indicator message published to `chan_system_alarm`.
  5. Contact opens; condition `false` posted.
  6. Single `ON_RELEASE` event published.
  7. Indicator message published with `is_active = false`.
- **Pass Criteria**: Exactly 1 activated event, exactly 1 cleared event, zero duplicates.

### Scenario 2 — Multiple Simultaneous Alarms
- **Action**: Simulate power disturbance causing simultaneous Line Under-Voltage, AC1 High Pressure, and Fire Alarm.
- **Expected**:
  1. Each alarm maintains distinct identity in entity table.
  2. None overwrites another.
  3. Highest severity arbitrates to `ALARM_SEVERITY_FATAL` (Fire).
  4. Clearing Line Under-Voltage leaves Fire and AC1 HP active with severity `FATAL`.
- **Pass Criteria**: Independent lifecycle per ID; correct severity escalation.

### Scenario 3 — Aggregated Temperature Alarm
- **Action**: Probe 3 drops below 15°C $\rightarrow$ Probe 5 drops below 15°C $\rightarrow$ Probe 3 recovers to 17°C $\rightarrow$ Probe 5 recovers to 17°C.
- **Expected**:
  1. Aggregate transitions to `ACTIVE` on Probe 3 drop.
  2. Probe 5 drop updates mask without emitting second `ACTIVATED` event.
  3. Probe 3 recovery updates mask without emitting `CLEARED` event.
  4. Probe 5 recovery clears mask and emits single `CLEARED` event.
- **Pass Criteria**: Exactly 1 `ACTIVATED` event at start, exactly 1 `CLEARED` event at end. Zero flapping in between.

### Scenario 4 — Sensor Disconnected
- **Action**: Probe 2 cable severed (simulated hardware fault).
- **Expected**:
  1. `ALARM_SENSOR_GROUP1_FAULT` asserts.
  2. Probe 2 excluded from Group Low/High temperature calculations.
  3. Room temperature evaluation continues using remaining valid probes (probes 1, 3, 4, 5).
  4. Cable reconnected $\rightarrow$ sensor fault alarm clears, probe rejoined to temperature calculation.
- **Pass Criteria**: Sensor disconnection does not cause false temperature trip or false clear.

### Scenario 5 — Recovery of a Lagging or Reconnecting Consumer
- **Action**: Web Server or HMI task stalls or boots up after alarms are already active.
- **Expected**:
  1. Consumer calls `i_alarm_query_get_active_alarm_ids()`.
  2. Consumer fetches complete list of active alarms and their activation timestamps directly from the thread-safe snapshot cache.
  3. Consumer state is 100% reconciled without needing past event playback.
- **Pass Criteria**: Zero dependency on lost event queue messages for static state reconstruction.

### Scenario 6 — System Boot with Existing Fault Conditions Present
- **Action**: Firmware boots while physical high-pressure contact is already closed.
- **Expected**:
  1. System initializes all entities to `ALARM_STATE_INACTIVE`.
  2. Initial scan by `ao_digital_input` detects closed contact and posts condition `true`.
  3. `ao_alarm_manager` transitions alarm from `INACTIVE` to `ACTIVE` on initial cycle.
  4. Indicators and logging reflect active alarm immediately upon boot.
- **Pass Criteria**: Faults present at power-on are immediately captured and reported.

### Scenario 7 — Dynamic Registration of a New Infrastructure Alarm
- **Action**: An expansion module registers a new alarm `ALARM_ID_EXPANSION_FAN_FAIL` via `alarm_manager_register_descriptor()`.
- **Expected**:
  1. Descriptor is stored in registry table.
  2. Condition can be updated and evaluated immediately.
  3. Events are published over standard Zbus channels.
  4. Core `ao_alarm_manager` source code requires zero modifications.
- **Pass Criteria**: Open/Closed principle verified; zero modifications to core engine.

---

## 4. Execution Commands & Acceptance Gates

### 4.1 Unit Test Execution Command
```bash
west build -b native_sim tests/unit/alarm -d build/test-alarm -t run
```

### 4.2 Code Coverage Target (LCOV)
```bash
west twister -T tests/unit/alarm -p native_sim --coverage -v
# Criteria:
# - Domain Layer (app/src/domain/alarm/): 100% line & branch coverage
# - Detectors: 100% line & branch coverage
# - Active Object: > 90% line & branch coverage
```

### 4.3 Static Analysis Gate
```bash
cppcheck --enable=all --error-exitcode=1 \
    -I app/include \
    -I app/src \
    app/src/domain/alarm/ \
    app/src/application/active_objects/ao_alarm_manager.c
```
