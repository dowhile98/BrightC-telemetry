# Industrial HVAC Control Subsystem Progress

- **Status**: **100% (IMPLEMENTED & VERIFIED)**
- **Architecture Reference**: [`doc/architecture/hvac_control_subsystem.md`](../architecture/hvac_control_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Domain Types & Limits** | `app/include/common/hvac_types.h` | 100% | Pure C99 domain types, limits, enums, hardware routing endpoints, config structs, and pure validators |
| **Storage Configuration Domain** | `app/include/common/config_types.h` | 100% | `CONFIG_DOMAIN_HVAC` (base `0x2800`, total `2048` B, 2 ping-pong slots of `1024` B) |
| **Default Configuration Profiles** | `app/include/common/default_config_profiles.h` | 100% | Centralized immutable profiles for Hybrid 4-unit baseline, Control-only 8-unit, Telemetry-only, DI mappings, and profile switchers |
| **Actuator Port Contract** | `app/include/interfaces/i_hvac_unit.h` | 100% | Rule §4.1.D fat-pointer handle `i_hvac_unit_t`, v-table `i_hvac_unit_api_t`, validated wrappers |
| **AC Unit Domain Entity** | `app/src/domain/models/hvac_unit_entity.h`, `.c` | 100% | Pure C99/C11 FSM: staggered anti-inrush, 180s lockout, eviction after max faults, fan purge sub-FSM |
| **HVAC Group Domain Entity** | `app/src/domain/models/hvac_group_entity.h`, `.c` | 100% | Pure C99/C11 aggregate root: generic $N+M$ rotation, 2-stage cooling timeout rescue, bypass hysteresis, fire interlock |
| **Actuator Infrastructure Adapter** | `app/src/infrastructure/adapters/adapter_hvac_actuator.h`, `.c` | 100% | Implements `i_hvac_unit_api_t`: translates tri-states into physical relays across Modbus or local backends |
| **On-Board Relay GPIO Adapter** | `app/src/infrastructure/adapters/adapter_relay_gpio.h`, `.c` | 100% | Concrete adapter implementing `i_relay_t` for the 18 internal on-board relays across `mcp23s17_2` and `mcp23s17_3` |
| **Zbus Status & Event Channels** | `app/src/application/zbus/hvac_channels.h`, `.c` | 100% | Defines `chan_hvac_status` carrying snapshot message `hvac_status_msg_t` |
| **HVAC Active Object Coordinator** | `app/src/application/active_objects/ao_hvac.h`, `.c` | 100% | Canonical 5-phase lifecycle Active Object with dynamic runtime configuration update support (`ao_hvac_set_group_config`, `ao_hvac_apply_config`) |
| **Composition Root Integration** | `app/src/infrastructure/di/composition_root.h`, `.c` | 100% | Subsystem 6 integrated in `app_container_t`: boot-time storage loading with default fallback (`k_default_hvac_cfg`), debounced dirty-marking via `ao_storage` |
| **In-Memory Fakes** | `tests/fakes/include/fake_hvac_unit.h`, `.c`, `fake_relay.h`, `.c` | 100% | In-memory `i_hvac_unit_t` and `i_relay_t` implementations with call tracking and fault injection |
| **HVAC Unit Test Suite** | `tests/unit/hvac/` | 100% | **37/37 tests passing on `native_sim` across 5 test suites** (unit, group, actuator, relay GPIO, and Active Object suites) |

---

## 2. Pending Work
- [x] Implement pure domain types & values (`app/include/common/hvac_types.h`).
- [x] Implement abstract port contract `i_hvac_unit.h` adhering to Rule §4.1.D.
- [x] Implement `hvac_unit_entity.c` & `.h` with tri-state actuation, alarm lockouts, and fan purge sub-FSM.
- [x] Implement `hvac_group_entity.c` & `.h` with generic $N+M$ redundancy rotation, staggered delay, and cooling timeout.
- [x] Implement in-memory fake `fake_hvac_unit.c` & `.h` and `fake_relay.c` & `.h`.
- [x] Implement actuator adapter `adapter_hvac_actuator.c` & `.h` with local/remote backend dispatching.
- [x] Implement Zbus status channels `hvac_channels.c` & `.h`.
- [x] Implement Active Object coordinator `ao_hvac.c` & `.h` adhering to 5-phase canonical lifecycle.
- [x] Wire declarative hardware routing and operational modes in `composition_root.c` & `.h`.
- [x] Create comprehensive unit test suite in `tests/unit/hvac/` (37/37 PASS on `native_sim` across 5 test suites).
- [x] Boot-load persisted HVAC config from EEPROM with fallback defaults and debounced dirty marking.
- [x] Support zero-reboot runtime configuration updates without disturbing running state machines.
- [x] Implement multi-sensor group temperature aggregation (`hvac_compute_group_metrics` and `ao_hvac_feed_temperatures`).
- [x] Centralize immutable default storage and DI configuration profiles (`default_config_profiles.h` and `app_defaults.h`).
- [x] Phase 3: Implement Multi-Room Range Engine (`[sensor_start, sensor_end]` ranges and `sensor_metric_policy_t` MAX/AVG/MIN selection).
- [x] Phase 5: Facade & Shell Alignment (`app_facade_hvac_configure_group_sensors`, `app_facade_hvac_configure_group_units`, shell `hvac sensors` and `hvac config` subcommands, sensor range and policy display in `hvac status`).

---

## 3. Changelog & Refactoring History
- **2026-11-07**: Implemented Phase 5 (Facade & Shell Alignment for Multi-Room Range Engine):
  - `app_facade.h/.c`: Added `sensor_start`, `sensor_end`, and `metric_policy` to `groups[]` in `app_facade_hvac_status_t` and populated them in `app_facade_hvac_get_status()`. Implemented `app_facade_hvac_configure_group_sensors()` to dynamically update group sensor range and metric policy, invoke `ao_hvac_set_group_config()`, and mark `CONFIG_DOMAIN_HVAC` dirty in persistent storage. Implemented `app_facade_hvac_configure_group_units()` to update `active_turn_count` (N+M) and `unit_mask`, invoke `ao_hvac_set_group_config()`, and mark storage dirty.
  - `shell_brightc.c`: Added `hvac_metric_policy_str()` helper. Updated `cmd_hvac_status()` to display assigned sensor range and metric policy (`MAX`, `AVG`, `MIN`) per group. Added `cmd_hvac_sensors()` (`hvac sensors <grp> <start> <end> [max|avg|min]`) and `cmd_hvac_config()` (`hvac config <grp> <active_turn> [unit_mask_hex]`), and registered both in `sub_hvac` shell command set.
  - `test_ao_hvac.c`: Added `test_dynamic_group_sensor_range_and_metric_policy` verifying live dynamic range reconfiguration (1..2 -> 3..4), metric policy transitions (MAX -> AVG -> MIN), and resulting HVAC FSM actuation and unit tri-state control.
  - `test_composition_root.c`: Added `test_app_facade_hvac_configure_group_sensors_and_units` verifying facade parameter validation, group reconfiguration, and status reporting.
  - Unit Tests: **38/38 tests passing on `native_sim` across 5 HVAC test suites** (100% PASS).
- **2026-11-07**: Implemented Phase 3 (HVAC Multi-Room Range Engine):
  - `hvac_types.h`: Defined `sensor_metric_policy_t` (`SENSOR_METRIC_MAX`, `SENSOR_METRIC_AVG`, `SENSOR_METRIC_MIN`) and validator `sensor_metric_policy_is_valid()`. Updated `config_hvac_group_t` to replace `reserved[2]` with `sensor_start`, `sensor_end`, `metric_policy`, `reserved[1]` while keeping size strictly at 40 bytes. Updated `config_hvac_group_is_valid()` to validate metric policy and non-inverted sensor range.
  - `hvac_group_entity.h/.c`: Added `metric_policy`, `sensor_start`, and `sensor_end` fields to `hvac_group_entity_t`; initialized in `hvac_group_entity_init()` and copied in `hvac_group_entity_set_config()`. In `hvac_group_entity_evaluate()`, dynamically computed `ctrl_temp` (MAX, AVG, or MIN) to drive high-temperature bypass and thermal FSM transitions. Implemented pure domain function `hvac_compute_group_metrics_range()` for 64-bit valid masks and flat sensor IDs (1..59).
  - `ao_hvac.h/.c`: Added `ao_hvac_feed_sensor_snapshot()` supporting 64-bit valid masks and range-based evaluation per active group with fallback to legacy `sensor_mask`. Updated `ao_hvac_feed_temperatures()` to forward into `ao_hvac_feed_sensor_snapshot()`.
  - `app_defaults.h`: Updated default profiles `k_default_hvac_config_hybrid_4units` (`.sensor_start = 1U, .sensor_end = 5U`) and `k_default_hvac_config_control_only_8units` (`.sensor_start = 1U, .sensor_end = 8U`) with `SENSOR_METRIC_MAX`.
  - Unit Tests: Added `test_config_hvac_group_size_and_validation`, `test_hvac_compute_group_metrics_range`, and `test_metric_policy_evaluation` in `test_hvac_group.c`, and `test_multi_room_disjoint_sensor_ranges` in `test_ao_hvac.c`. Test suite passes **37/37 tests across 5 test suites (100% PASS)**.
- **2026-11-06**: Resolved audit findings P0-02 and P1-05:
  - P0-02 (Relay Change-Detection & Saturation Guard): Added state cache guard in `adapter_hvac_actuator_set_tri_state()` preventing redundant bus dispatches when commanded tri-state matches cached state. Initialized cache to `(hvac_tri_state_t)0xFF` on adapter init and invalidated on `adapter_hvac_actuator_set_hw_config()`. Handled uninitialized fallback in `adapter_hvac_actuator_get_tri_state()`.
  - P1-05 (HVAC Setpoint Validation & Modes): Enforced `heat_target_x10 < cool_target_x10` and preserved dynamic `temp_alarm` and `temp_bypass` deltas in `app_facade_hvac_set_setpoint()`. Implemented `HVAC_MODE_FAN_ONLY` in `app_facade_hvac_set_mode()` setting smart idle fan while disabling compressors. Added fan control handling to `app_facade_hvac_set_unit_manual()`.
  - Test suite status: **33/33 tests passing on `native_sim` across 5 test suites**.
- **2026-11-06**: Resolved audit findings P2-5, P2-6, P2-7:
  - P2-5 (Safe State on Stop): Implemented `ao_hvac_safe_state(ao_hvac_t *self)` de-energizing all compressor, heater, and fan stages to `HVAC_TRI_STATE_OFF` immediately on AO stop and on thread exit.
  - P2-6 (Double Wait Contract): Aligned `ao_hvac_stop()` with canonical 5-phase lifecycle by removing internal `k_sem_take(&self->stopped_sem)`, leaving synchronization ownership exclusively to `ao_hvac_wait_stopped()`.
  - P2-7 (Actuator Error Handling): Checked return status of `i_hvac_unit_set_tri_state()` in `ao_hvac_step()`. On failure, logs `LOG_ERR`, increments `self->actuator_fault_count`, and triggers unit fault increment on `hvac_unit_entity_t`.
  - Added unit tests `test_safe_state_and_lifecycle_contract` and `test_actuator_error_handling` in `test_ao_hvac.c`.
- **2026-11-06**: Internal On-Board Relay Hardware Integration:
  - Implemented `adapter_relay_gpio.h/.c` providing concrete `i_relay_t` implementation for the 18 internal on-board relays via GPIO expanders (`mcp23s17_2` and `mcp23s17_3`).
  - Configured `mcp23s17_2` channels 0..11 to drive 4 primary AC units x 3 relays each (AA, SM, G) and `mcp23s17_3` channels 0..5 for 6 dedicated alarm outputs, preserving Waveshare 32CH Modbus RTU adapter for optional expansion units 5..8.
  - Added unit test suite `test_adapter_relay_gpio.c` in `tests/unit/hvac/src/` verifying binding, active-high channel actuation, pulse operations, cycle counters, and hardware error handling across both expanders.
  - Updated unit test verification: **31/31 tests passing on `native_sim` across 5 test suites** (100% PASS).
- **2026-11-06**: Implemented Hybrid Default Storage Profile & Verification:
  - Added centralized immutable profiles in `app/include/common/default_config_profiles.h` for Hybrid 4-unit baseline, Control-only 8-unit, Telemetry-only, DI mappings, and profile switchers.
  - Implemented Hybrid Default Storage Profile: 1 group of 4 AC units, 2+2 redundancy, 5 internal RTDs on `sensor_mask = 0x001F`, 16 unit alarms (HP, LP, Flow, Volt on DI 1..16 / MBSL32DI CH0..15), fire on DI 17 (CH16), door on DI 18 (CH17), fan/comp feedbacks on DI 19..26 (CH18..25), and OTA recompilation readiness.
  - Added unit test `test_default_hybrid_profile_verification` in `tests/unit/hvac/src/test_ao_hvac.c` validating profile integrity, DI channel assignments, profile switchers, and full temperature feed execution through `ao_hvac` (27/27 tests passing on `native_sim`, 100% PASS).
- **2026-11-06**: Implemented Multi-Sensor Group Temperature Aggregation:
  - Added pure domain function `hvac_compute_group_metrics()` in `hvac_group_entity.h/.c` aggregating channel temperatures using assigned group `sensor_mask` and channel `valid_mask` (computes highest, lowest, average, total, and valid counts).
  - Added `ao_hvac_feed_temperatures()` in `ao_hvac.h/.c` to ingest raw/calibrated temperature arrays and compute group metrics per configured `sensor_mask`.
  - Added unit tests `test_hvac_compute_group_metrics_multi_sensor` in `test_hvac_group.c` and `test_ao_hvac_feed_temperatures_multi_sensor` in `test_ao_hvac.c` (26/26 tests passing on `native_sim`, 100% PASS). Clean cross-compilation verified for `brightc-controllerv3`.
- **2026-11-06**: Resolved Finding 1 & Finding 2:
  - Finding 1: Replaced inline magic numbers in `composition_root_init_hvac()` with typed `static const config_hvac_t k_default_hvac_cfg`. Implemented synchronous boot loading from `CONFIG_DOMAIN_HVAC` in EEPROM with validation and fallback default saving.
  - Finding 2: Added runtime dynamic configuration update functions (`ao_hvac_set_group_config()`, `ao_hvac_apply_config()`, `app_container_set_hvac_group_config()`, `app_container_set_hvac_config()`) allowing live threshold and turn count alterations without reset, with async 500ms debounced EEPROM persistence.
  - Added unit tests `test_ao_hvac_runtime_group_config_update` and `test_ao_hvac_runtime_turn_count_update` in `tests/unit/hvac/src/test_ao_hvac.c` (24/24 tests passing). Clean cross-compilation verified for `brightc-controllerv3`.
- **2026-11-06**: Implemented Phase 2: Actuator Adapter (`adapter_hvac_actuator.c/.h`), Zbus Channel (`hvac_channels.c/.h`), Active Object Coordinator (`ao_hvac.c/.h`), Fake Relay (`fake_relay.c/.h`), and Composition Root wiring in `composition_root.c/.h`. Unit test suite expanded to 22 tests (100% PASS). Clean cross-compilation verified for `brightc-controllerv3`.
- **2026-11-06**: Implemented Domain Layer (`hvac_unit_entity.c/.h`, `hvac_group_entity.c/.h`), Interface Contract (`i_hvac_unit.h`), and In-Memory Fake (`fake_hvac_unit.c/.h`); added comprehensive Ztest suite in `tests/unit/hvac/` (14/14 unit tests passing, 100%); added domain entities to `app/CMakeLists.txt` and confirmed clean target compilation for `brightc-controllerv3`.
- **2026-11-06**: Implemented `app/include/common/hvac_types.h` (limits, enums, `hvac_io_endpoint_t`, `hvac_unit_hw_config_t`, `config_hvac_group_t`, `config_hvac_t`, and pure validators); integrated `CONFIG_DOMAIN_HVAC` in `config_types.h` and `adapter_storage_microconf.c`.
- **2026-11-05**: Architectural specification created and approved incorporating generic $N+M$ rotation, contactor discrepancy event hooks, and 3-mode operational profiles (Telemetry Only, Control Only, Hybrid Full).


