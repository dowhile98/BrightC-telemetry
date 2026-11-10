# Alarm Management Subsystem Progress

- **Status**: **COMPLETE (100%)**
- **Architecture References**:
  - Analysis: [`doc/architecture/alarm_management_analysis.md`](../architecture/alarm_management_analysis.md)
  - Requirements: [`doc/architecture/alarm_management_requirements.md`](../architecture/alarm_management_requirements.md)
  - Architecture: [`doc/architecture/alarm_management_architecture.md`](../architecture/alarm_management_architecture.md)
  - Test Plan: [`doc/architecture/alarm_management_test_plan.md`](../architecture/alarm_management_test_plan.md)
  - Task Spec: [`doc/tasks/1.alarms`](../tasks/1.alarms)

---

## 1. Subsystem Roadmap & Phase Deliverables

| Phase / Component | Deliverable | Status | Description |
| :--- | :--- | :---: | :--- |
| **Phase 1: Analysis & Requirements** | `alarm_management_analysis.md`<br>`alarm_management_requirements.md` | 100% | Legacy audit of Ref 1 & Ref 2, functional gap matrix, initial catalog (71 alarms), and FSM requirements |
| **Phase 1: Architecture & Design** | `alarm_management_architecture.md`<br>`alarm_management_test_plan.md` | 100% | Clean Architecture, Active Object model, 7 Mermaid diagrams, multi-source aggregation, Zbus & query ports |
| **Phase 2: Domain Layer** | `alarm_entity.c`<br>`alarm_aggregate.c`<br>`alarm_evaluator.c`<br>`alarm_registry.c` | 100% | Pure ISO C99 domain state machines, multi-source bitmasks, low/high temp & 3-phase volt evaluators with hysteresis, extensible static catalog (71 default alarms, up to 4 Groups / Salas & 8 AC Units) |
| **Phase 2: Detectors & Adapters** | `adapter_alarm_detector_di.c`<br>`adapter_alarm_detector_temp.c`<br>`adapter_alarm_detector_volt.c`<br>`adapter_alarm_detector_hvac.c` | 100% | Ingestion adapters mapping DI contacts (AC1..AC8 + facility), temperature snapshots across 4 groups, JSY-MK-339 3-phase readings with meter comm fault, and HVAC bypass/lockouts |
| **Phase 2: Active Object & Zbus** | `ao_alarm_manager.c`<br>`alarm_channels.c` | 100% | Canonical 5-phase Active Object coordinator, Zbus publishing (`chan_alarm_transition`, `chan_system_alarm`), edge-transition filtering, and thread-safe snapshot cache |
| **Phase 2: Composition Root & DI** | `composition_root.c`<br>`composition_root.h` | 100% | Subsystem wiring, file-scope MPU stack guard, Zbus listeners (`lis_alarm_di_feed`, `lis_alarm_sensor_feed`, `lis_alarm_hvac_feed`, `lis_indicator_alarm_feed`), DIP query port getters |
| **Phase 2: Unit & Integration Tests**| `tests/unit/alarm/` | 100% | 12/12 unit tests passing on `native_sim` (Entity, Aggregate, Evaluators, Detectors, AO lifecycle, dispatch & query) |

---

## 2. Test & Verification Summary

- **Unit Test Suite (`tests/unit/alarm`)**: 12/12 tests PASS on `native_sim` (0 failures).
  - `alarm_domain_suite`: 6/6 tests PASS.
  - `alarm_ao_suite`: 2/2 tests PASS.
  - `alarm_detectors_suite`: 4/4 tests PASS (covers 8 AC contacts, 4 Groups temperature & bypass, worst-case phase voltage, meter comm fault).
- **Subsystem Regression Tests**: All existing test suites pass with zero regressions:
  - `tests/unit/digital_input`: 17/17 tests PASS (100%).
  - `tests/unit/indicators`: 8/8 tests PASS (100%).
  - `tests/unit/hvac`: 59/59 tests PASS (100%).
  - `tests/unit/sensor`: 51/51 tests PASS (100%).
- **Target Firmware Cross-Compilation (`brightc-controllerv3`, STM32H743)**:
  - ROM (Flash): 340,204 B (16.22% of 2 MB - budget < 50%).
  - RAM (AXI SRAM): 195,484 B (37.29% of 512 KB - budget < 50%).
  - Zero linker errors, zero warnings.
- **Architectural Invariant Audits**:
  - Direct v-table access (`api->`): 0 violations outside adapters/di.
  - Devicetree direct access (`DEVICE_DT_GET`): 0 violations in adapters.
  - Domain RTOS leak (`<zephyr/` in `domain/alarm/`): 0 violations.

---

## 3. Changelog & Refactoring History

- **2026-11-10 (Phase 1)**: Completed Stage 1 architectural discovery, legacy project comparison, requirements definition, architecture specification with 7 editable Mermaid diagrams, and comprehensive test plan.
- **2026-11-10 (Phase 2)**: Completed full Phase 2 implementation following Clean Architecture, Ports & Adapters, and TDD:
  - Added pure domain entities (`alarm_entity`, `alarm_aggregate`, `alarm_evaluator`, `alarm_registry`).
  - Added port contract [`i_alarm_query.h`](../../app/include/interfaces/i_alarm_query.h) and domain types [`alarm_types.h`](../../app/include/common/alarm_types.h).
  - Added Active Object [`ao_alarm_manager`](../../app/src/application/active_objects/ao_alarm_manager.c) and Zbus channel [`chan_alarm_transition`](../../app/src/application/zbus/alarm_channels.c).
  - Added ingestion detector adapters for DI contacts, group temperatures, 3-phase line voltage, and HVAC bypass/lockouts.
  - Integrated into composition root with DIP getters, thread stack guard, and Zbus listener orchestration.
  - Cross-compiled firmware for STM32H743 target board and verified 100% test pass.
- **2026-11-10 (Post-Review Hardening & 4-Group / 8-Unit Extension)**:
  1. *Disconnected Probes Fix*: Invalid/disconnected sensors freeze thermal conditions (no false clearance), asserting `ALARM_ID_SENSOR_GROUP1..4_FAULT`.
  2. *HVAC 8-Unit Complete Support*: Extended catalog to AC1..AC8 contacts (32 alarms) and Unit 1..8 lockouts (`ALARM_ID_HVAC_UNIT1..8_LOCKOUT`). Added DI role mappings for units 4..7 in `adapter_alarm_detector_di.c` and 8-unit lockout monitoring in `adapter_alarm_detector_hvac.c`.
  3. *Full 4-Group Thermal & Bypass Support (`HVAC_MAX_GROUPS = 4U`)*:
     - Added `ALARM_ID_GROUP1..4_TEMP_LOW`, `TEMP_HIGH`, `TEMP_BOOST`.
     - Added `ALARM_ID_HVAC_G1..4_BYPASS_ACTIVE` and `ALARM_ID_SENSOR_GROUP1..4_FAULT`.
     - Registered all descriptors in `alarm_registry.c` (71 default catalog alarms).
     - Generalized `adapter_alarm_detector_temp` to accept `const uint16_t *group_masks` array for up to 4 groups.
     - Generalized `adapter_alarm_detector_hvac` to evaluate bypass across all 4 groups.
  4. *Queue Saturation Defense*: Transition-driven posting filter in all detector adapters (only post on state change), increased `AO_ALARM_MANAGER_QUEUE_DEPTH` to 64, and added queue-overflow warning logs in `ao_alarm_manager.c`.
  5. *Formal Kconfig*: Added `BrightC Alarm Management Subsystem` menu in `app/Kconfig` defining `CONFIG_ALARM_MANAGER_MAX_ALARMS` (default 128), `CONFIG_AO_ALARM_MANAGER_QUEUE_DEPTH` (default 64), and `CONFIG_AO_ALARM_MANAGER_STACK_SIZE` (default 2048).
  6. *3-Phase Voltage Phase Accuracy*: Evaluates worst-case phase voltage (`min_v` on under-voltage, `max_v` on over-voltage) for trigger reporting in `adapter_alarm_detector_volt.c`.
  7. *Energy Meter Communication Alarm*: Detects `!reading->is_valid` and asserts `ALARM_ID_ENERGY_METER_COMM_FAULT`.
  8. *Regression & Target Verification*: 12/12 alarm unit tests PASS, 135+ tests pass across all suites, STM32H743 target firmware builds cleanly (ROM 16.22%, RAM 37.29%).
- **2026-11-10 (Review Verification & Comprehensive Corrections)**:
  1. *Stack Size Kconfig Linkage*: Replaced literal 2048 in `composition_root.c` with `CONFIG_AO_ALARM_MANAGER_STACK_SIZE` (declared in `app/Kconfig` and backed by default in `ao_alarm_manager.h`).
  2. *Zbus Static Container Pointer Invariant Justification*: Documented the exact architectural rationale of `s_active_container` in `composition_root.c`. Zephyr Zbus listeners statically define C callback signatures without `user_data` context; `s_active_container` serves as a read reference to the instantiated container allocated in `main.c`, containing zero mutable file-scope state.
  3. *Cooling Boost Alarm Generation*: Added active generation of `ALARM_ID_GROUP1..4_TEMP_BOOST` in `adapter_alarm_detector_hvac.c` when groups transition to `GROUP_STATE_COOL_BOOST`, and cleared upon exit.
  4. *Modbus Infrastructure Alarm Integration*:
     - Added `ao_modbus_bus_fault_cb_t` callback to `ao_modbus.h` and `.c`.
     - Tracked consecutive poll failures against `MODBUS_CONSECUTIVE_FAILS_THRESHOLD` (3), dispatching bus fault state changes to `on_modbus_bus_fault` in `composition_root.c` to assert/clear `ALARM_ID_INFRA_MODBUS_FAULT`.
     - Propagated `is_valid = false` reading from `ao_modbus.c` to `adapter_alarm_detector_volt` when JSY-MK-339 poll fails to reliably trigger `ALARM_ID_ENERGY_METER_COMM_FAULT`.
     - Corrected RS485-2 bus assignment for MBSL32DI and Waveshare Relay in `modbus_device_table.h`.
  5. *Full Regression & Target Compilation*: Verified all 6 suites PASS (alarm, di, ind, hvac, sensor, modbus) and primary target `brightc-controllerv3` builds cleanly (16.25% Flash, 37.30% RAM).
