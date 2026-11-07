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
| **Unit Tests** | `tests/unit/digital_input/` | 100% | 16 tests passing on `native_sim` (100%) |

---

## 2. Pending Work
- [ ] High-speed pulse counter mode for dry-contact flow/energy meter pulses (frequency / accumulator).

---

## 3. Changelog & Refactoring History
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
