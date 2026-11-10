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
| **Phase 2: Unit & Integration Tests**| `tests/unit/alarm/` | 100% | 17/17 unit tests passing on `native_sim` (Entity, Aggregate, Evaluators, Detectors, AO lifecycle, restart, active duration, dynamic registration, AC summary aggregation) |

---

## 2. Test & Verification Summary

- **Unit Test Suite (`tests/unit/alarm`)**: 17/17 tests PASS on `native_sim` (0 failures).
  - `alarm_domain_suite`: 6/6 tests PASS.
  - `alarm_ao_suite`: 5/5 tests PASS (Lifecycle, Dispatch & Query, Active Duration, Dynamic Registration, Restart & Input Validation).
  - `alarm_detectors_suite`: 6/6 tests PASS (AC Contacts DI mapping, EEPROM HVAC config integration, AC summary fault aggregation, HVAC Bypass & Lockout, Group Temp range snapshot & sensor fault, Phase voltage & meter comm fault).
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

- **2026-11-10 (Test Suite Enhancement & Invariant Hardening)**:
  1. *Temperature Detector Test Ranges*: Fixed `test_detector_temp_snapshot_and_fault` in `test_alarm_detectors.c` by updating parameter from bitmask to `group_sensor_range_t ranges[HVAC_MAX_GROUPS]`.
  2. *AC Summary Fault Aggregation Test*: Added `test_detector_ac_summary_fault_aggregation` verifying multi-source aggregation across AC1 HP DI trips and HVAC lockout state transitions.
  3. *Dynamic Registration Test*: Added `test_ao_alarm_manager_dynamic_registration` verifying runtime descriptor addition with `ALARM_SOURCE_CUSTOM` and subsequent condition posting.
  4. *Active Duration Test*: Added `test_ao_alarm_manager_active_duration` verifying millisecond duration while active and 0ms when cleared.
  5. *Restart & Input Validation Test*: Added `test_ao_alarm_manager_restart_and_input_validation` testing error return codes (`-EINVAL`, `-ENOENT`) and thread stop/restart lifecycle.
  6. *Thread Lifecycle Hardening*: Hardened `ao_alarm_manager_wait_stopped` and `ao_alarm_manager_deinit` with clean `k_thread_join` synchronization. 16/16 alarm unit tests pass 100%.

- **2026-11-10 (Persistent EEPROM Configuration Integration for Thermal Groups)**:
  1. *EEPROM HVAC Config Binding*: Removed hardcoded default sensor ranges in `composition_root.c`. The temperature detector now loads group sensor ranges (`sensor_start`, `sensor_end`) and high temperature thresholds (`temp_alarm`) directly from the persistent HVAC configuration stored in EEPROM (`self->hvac.config`).
  2. *Independent Per-Group Thermal Evaluation*: Extended `adapter_alarm_detector_temp` with `adapter_temp_group_config_t` and `eval_low/eval_high[HVAC_MAX_GROUPS][16]`, enabling independent sensor ranges, trip thresholds, and hysteresis per room/group.
  3. *Unconfigured Group Isolation*: Groups not enabled in EEPROM (`total_groups` or unassigned sensors) are cleanly disabled, preventing false temperature alarms or spurious sensor fault assertions.
  4. *Runtime Dynamic Reconfiguration*: Added `adapter_alarm_detector_temp_set_group_from_hvac()` and `adapter_alarm_detector_temp_apply_hvac_config()`, hooked into `app_container_set_hvac_group_config()` and `app_container_set_hvac_config()` to immediately update alarm detection thresholds whenever configuration changes at runtime.
  5. *Initialization Order Optimization*: Reordered subsystem initialization in `composition_root.c` so `hvac` (which loads EEPROM configuration) initializes before `alarm`.
  6. *Unit Test Verification*: Added `test_detector_temp_eeprom_hvac_config_integration` in `test_alarm_detectors.c`. Total 17/17 alarm tests pass 100%. Target STM32H743 builds cleanly (ROM 16.34%, RAM 37.55%).

- **2026-11-10 (Elimination of Literal Thresholds & Configurable `temp_low_alarm_c` in EEPROM)**:
  1. *Per-Group Low Temp Alarm Persistence*: Added `int8_t temp_low_alarm_c` to `config_hvac_group_t` (in place of `reserved[1]`), preserving the 40-byte binary layout and allowing persistent low temperature alarm thresholds per room/group (default 15°C, range -50..50°C).
  2. *Clean Zero-Literal Initialization*: Refactored `adapter_alarm_detector_temp_init(&det_temp, &ao)` to accept purely context and manager handle, eliminating all hardcoded literals (`15.0f`, `28.0f`, `1.0f`) from `composition_root.c`.
  3. *Dynamic Group Binding*: High temperature alarm threshold is mapped 1:1 to `temp_alarm` (temp boost setpoint) from each group's persistent EEPROM configuration, while low temperature alarm threshold is mapped to `temp_low_alarm_c`.
  4. *Test Verification*: Validated in `test_alarm_detectors.c` (17/17 PASS), all regression suites pass (100%), target firmware STM32H743 builds cleanly (ROM 16.34%, RAM 37.55%).

- **2026-11-10 (Shell CLI Support & Persistent Alarm Buzzer Mute)**:
  1. *Persistent Alarm Buzzer Mute in EEPROM*:
     - In `config_hvac_t`: Replaced `reserved[16]` with `uint8_t alarm_buzzer_muted;` and `uint8_t reserved[15];`, preserving exact binary layout.
     - Added validation check `cfg->alarm_buzzer_muted > 1U` in `config_hvac_is_valid()`.
     - Explicitly initialized `.alarm_buzzer_muted = 0U` in all default profiles (`k_default_hvac_config_hybrid_4units`, `k_default_hvac_config_control_only_8units`, `k_default_hvac_config_telemetry_only`).
  2. *System Indicator Active Object Integration*:
     - Added `bool buzzer_muted;` to `ao_system_indicator_t` and public APIs `ao_system_indicator_set_buzzer_muted()` and `ao_system_indicator_is_buzzer_muted()`.
     - In `ao_system_indicator_step()`, silenced buzzer when muted for `BUZZER_PATTERN_ALARM_CONTINUOUS` and `BUZZER_PATTERN_ALARM_INTERMITTENT`, keeping beeps and chime functional.
     - Added unit test `test_ao_indicator_alarm_buzzer_mute` in `tests/unit/indicators/src/test_system_indicators.c` (9/9 PASS).
  3. *Composition Root & DI Wiring*:
     - Implemented `app_container_set_alarm_buzzer_mute()` and `app_container_is_alarm_buzzer_muted()` updating RAM config, indicators AO, and marking `CONFIG_DOMAIN_HVAC` dirty for EEPROM persistence.
     - In `composition_root_init_alarm()`, restored persisted buzzer mute setting into indicators AO upon boot.
  4. *Clean Architecture Application Facade*:
     - Added DTOs `app_facade_alarm_status_t` and `app_facade_alarm_item_t`.
     - Implemented facade service functions: `app_facade_alarm_get_status()`, `app_facade_alarm_get_active_list()`, `app_facade_alarm_get_catalog()`, `app_facade_alarm_get_snapshot()`, `app_facade_alarm_ack()`, `app_facade_alarm_set_buzzer_mute()`, and `app_facade_alarm_is_buzzer_muted()`.
  5. *Zephyr Shell CLI Presentation*:
     - Implemented `alarm` subcommands in `shell_brightc.c`: `status`, `list`/`active`, `catalog`, `snapshot <id>`, `ack <id>`, `mute [on|off|toggle]`.
     - Interacts strictly with `app_facade.h` adhering to Clean Architecture.
  6. *Target Compilation & Verification*:
     - Target STM32H743 builds cleanly: Flash 347,584 B (16.57%), RAM 196,892 B (37.55%).
     - All unit tests pass: `test-alarm` (17/17), `test-ind` (9/9), `test-hvac` (59/59). All architectural invariants (1, 2, 3, 5, 6) verified with 0 violations.
