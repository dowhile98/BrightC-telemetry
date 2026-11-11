# Digital Inputs & Contact Alarms Subsystem Progress

- **Status**: **IN_PROGRESS (95%)**
- **Architecture Reference**: [`doc/architecture/digital_input_subsystem.md`](../architecture/digital_input_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Aggregator & Debounce** | `app/src/domain/services/digital_input_aggregator.c` | 100% | Dual-rate software integrator debounce (500ms contactors, 60ms alarms, 0ms bypass for Modbus), polarity normalization, active duration tracking, snapshot packing (`di_aggregator_pack_snapshot`) |
| **Hardware Adapter** | `app/src/infrastructure/adapters/adapter_di_gpio.c` | 100% | 2x MCP23S17 SPI expanders + on-chip GPIOs + Modbus MBSL32DI 32-bit remote ingestion |
| **Active Object** | `app/src/application/active_objects/ao_digital_input.c` | 100% | Periodic hardware scan (20ms / 50 Hz), debouncing, Zbus event and snapshot publishing |
| **Zbus Channels** | `app/src/application/zbus/digital_input_channels.c` | 100% | `chan_digital_input_state` (transition events) & `chan_digital_input_snapshot` (consolidated 64-channel state, masks, durations) |
| **HVAC Safety Wiring** | `app/src/infrastructure/di/composition_root.c` | 100% | `lis_hvac_di_feed` synchronous listener driving fire shutdown (`ao_hvac_set_fire`) and unit mechanical alarm lockouts (`ao_hvac_set_unit_alarms`) |
| **Application Facade** | `app/src/application/services/app_facade.c` | 100% | Decoupled atomic queries (`app_facade_di_get_status`, `app_facade_di_get_raw`, `app_facade_di_get_snapshot`) via Zbus |
| **Unit Tests** | `tests/unit/digital_input/` | 100% | 18 tests passing on `native_sim` (100%) |

---

## 2. Pending Work
- [ ] High-speed pulse counter mode for dry-contact flow/energy meter pulses (frequency / accumulator).

---

## 3. Changelog & Refactoring History
- **2026-11-11**: Implemented Phase A & Phase B Hardware Pin Map and Digital Input Semantic Roles refactoring:
  - Extended `di_role_t` in `digital_input_types.h` with dedicated feedback roles `DI_ROLE_FAN_5_AUX`..`DI_ROLE_FAN_8_AUX` and `DI_ROLE_COMPRESSOR_5_AUX`..`DI_ROLE_COMPRESSOR_8_AUX`.
  - Added role string representations in `role_to_name()` in `app_facade.c`.
  - Created profile-specific digital input tables in `hardware_pin_map.h`: `k_brightc_local_di_control_table`, `k_brightc_local_di_telemetry_table` (dedicated mapping for `HVAC_MODE_TELEMETRY_ONLY` with pins 0..15 mapped to compressors 1..8 & fans 1..8 with `DI_DEBOUNCE_CONTACTOR_MS` 500ms), `k_brightc_system_di_control_table`, and `k_brightc_system_di_telemetry_table`.
  - Updated remote Modbus DI table `k_brightc_remote_di_table` to map channels 16..23 to `DI_ROLE_FAN_1_AUX`..`DI_ROLE_FAN_8_AUX` and channels 24..31 to `DI_ROLE_COMPRESSOR_1_AUX`..`DI_ROLE_COMPRESSOR_8_AUX`.
  - Implemented mode-aware accessors: `hardware_pin_map_get_di_profile(mode, &count)`, `hardware_pin_map_get_unit_for_mode(mode, unit_idx)` (with `HVAC_IO_BACKEND_NONE` for relays in telemetry mode and local MCP23S17 feedback routing), and updated `default_config_get_di_profile()`.
  - Added unit test `test_di_profile_modes_and_unit_mappings` in `test_digital_input.c`. 18/18 tests passing (100% PASS).
- **2026-11-09**: Fixed Double Polarity Inversion on On-Chip GPIO `sw_bypass` (`H-01` in `doc/issues/2026-11-09-hvac-control-delay-sequence-pinout-review.md`):
  - Changed `gpio_pin_get_dt(spec)` to raw `gpio_pin_get(spec->port, spec->pin)` for `DI_SOURCE_ONCHIP_GPIO` in `adapter_di_gpio.c` so `GPIO_ACTIVE_LOW` pins are not inverted twice before `di_normalize_raw_level()` (`DI_CONTACT_NORMALLY_CLOSED`).
- **2026-11-07**: Implemented Phase 0 — Security (R-02 Physical Bypass Switch & R-14 Remote Units 4..7 Alarms):
  - Added on-chip GPIO channels (`ONCHIP_DI_USER_BUTTON` and `ONCHIP_DI_SW_BYPASS`) and consolidated 58-channel system DI table (`k_brightc_system_di_table`) in `hardware_pin_map.h`.
  - Added unique user roles (`DI_ROLE_USER_5`..`DI_ROLE_USER_10`) in `digital_input_types.h` and eliminated duplicate semantic roles across local and remote tables.
  - Implemented `hardware_pin_map_get_full_di()` and updated `default_config_get_di_profile()` to provide the full 58-channel profile (24 local MCP23S17 + 2 on-chip GPIOs + 32 remote Modbus MBSL32DI channels).
  - Enhanced `adapter_di_gpio_read()` to support both `pin_index` and `device_id` for `DI_SOURCE_ONCHIP_GPIO`.
  - Added unit test `test_system_di_profile_58_channels` in `test_digital_input.c` validating 58-channel profile, on-chip bypass switch assertion, and remote unit 4..7 alarm ingestion. 17/17 tests passing (100% PASS).
- **2026-11-07**: Fixed Shell Stack Overflow and Polarity Defaults for Bench Hardware:
  - Fixed MPU fault / stack overflow during `di status` shell command: replaced monolithic 1.5 KB `app_facade_di_view_t` stack allocation with compact `di_snapshot_msg_t` and `app_facade_di_role_name()`.
  - Added `CONFIG_SHELL_STACK_SIZE=4096` to provide safe stack headroom on Cortex-M7.
  - Corrected `DI_ROLE_SMOKE_FIRE` and `DI_ROLE_PHASE_MONITOR` in `hardware_pin_map.h` to `DI_CONTACT_NORMALLY_OPEN` so open-circuit / floating optocoupler inputs are recognized as normal/quiescent.
- **2026-11-07**: Complete Digital Input Pipeline Zbus Decoupling & HVAC Interlock Alignment (`doc/issues/2026-11-07-digital-input-pipeline-zbus-decoupling.md`):
  - Defined `di_snapshot_msg_t` in `digital_input_types.h` and created Zbus channel `chan_digital_input_snapshot` in `digital_input_channels.h/.c`.
  - Implemented `di_aggregator_pack_snapshot()` in domain service `digital_input_aggregator.c/.h` packing `active_mask`, `raw_mask`, `roles`, `is_active`, and `active_duration_s`.
  - Updated `ao_digital_input_step()` to publish both edge transitions to `chan_digital_input_state` and consolidated snapshots with monotonic sequence to `chan_digital_input_snapshot`.
  - Wired `lis_hvac_di_feed` listener in `composition_root.c` driving `ao_hvac_set_fire()` on `DI_ROLE_SMOKE_FIRE` and `ao_hvac_set_unit_alarms()` on unit HP/LP/FLOW/VOLT alarms across units 0..7.
  - Decoupled `app_facade_di_get_status()`, `app_facade_di_get_raw()`, and added `app_facade_di_get_snapshot()` to read atomically from Zbus without direct aggregator access.
  - Added unit tests in `test_digital_input.c` and `test_composition_root.c` verifying snapshot packing, Zbus publication, listener delivery, and HVAC safety response.
- **2026-11-06**: Resolved audit finding P2-11 (Bus Load & SPI Batching for MCP23S17):
  - Added 16-bit port value caching (`cached_val`, `cache_valid`, `cache_time_ms`) to `adapter_di_gpio.c/.h` using `gpio_port_get_raw()`.
  - Batching reduced SPI transactions from 24 individual reads down to 2 transactions per 20ms scan interval (1 per chip).
- **2026-11-05**: Refactored `ao_digital_input` to 5-phase canonical lifecycle.
- **2026-11-05**: Removed `DEVICE_DT_GET()` from `adapter_di_gpio.c` and injected `mcp23s17_0`, `mcp23s17_1` from `composition_root.c`.
