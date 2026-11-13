# Core Architecture & Framework Progress

- **Status**: **COMPLETED (100%)**
- **Architecture References**:
  - [`doc/architecture/active_object_architecture.md`](../architecture/active_object_architecture.md)
  - [`doc/architecture/dependency_injection_container_architecture.md`](../architecture/dependency_injection_container_architecture.md)
  - [`doc/architecture/ports_and_adapters_architecture.md`](../architecture/ports_and_adapters_architecture.md)
  - [`doc/issues/2026-11-06-architecture-refactor-hvac-telemetry-isolation.md`](../issues/2026-11-06-architecture-refactor-hvac-telemetry-isolation.md)

---

## 1. Implemented Components

| Component / Layer | File Path | Status | Verification |
| :--- | :--- | :---: | :--- |
| **Active Object Template** | All 5 AOs (`ao_modbus`, `ao_network`, `ao_storage`, `ao_di`, `ao_indicator`) | 100% | 5-phase lifecycle (`init`, `start`, `stop`, `wait_stopped`, `deinit`), MPU stack guards |
| **DI Container / Composition Root** | `app/src/infrastructure/di/composition_root.c`, `.h` | 100% | Vertical subsystem grouping, modular init, `wire()`, 10 DIP getters |
| **Hexagonal Port Encapsulation** | `app/include/interfaces/` | 100% | Rule §4.1.D fat-pointers, validated inline wrappers, 0 `api->` direct calls outside adapters |
| **Hardware Injection Rule** | `app/src/infrastructure/adapters/` | 100% | 0 `DEVICE_DT_GET()` inside adapters; all devices injected from `composition_root.c` |
| **Declarative Hardware Pinout** | `app/include/common/hardware_pin_map.h` | 100% | Compile-time 24 local DIs, 32 remote DIs, 12 HVAC relays, 8 alarm relays |
| **Declarative Modbus Topology** | `app/include/common/modbus_device_table.h` | 100% | Compile-time RS485-1 (Telemetry) & RS485-2 (Control) slave descriptors |
| **Declarative Sensor Catalog** | `app/include/common/sensor_hardware_map.h` | 100% | Flat 1-byte sensor ID catalog (5 internal RTDs, 32 CWT, 10 J9 4-20mA) |

---

## 2. Pending Work
- Phase 3 of Architecture Refactor: Multi-room HVAC range engine (`[sensor_start, sensor_end]` and metric policies).

---

## 3. Changelog & Refactoring History
- **2026-11-12**: Standardized boot and composition-root observability:
  - Added structured `[BOOT]`, `[CFG]`, `[DI]`, and `[RUN]` prefixes in `app/src/main.c` and `app/src/infrastructure/di/composition_root.c`.
  - Added numbered initialization phases, dependency-wiring status, runtime start summary, and explicit degraded-storage reporting.
  - Enriched persisted-configuration logs with source and safe operational fields; Wi-Fi PSK is never logged.
- **2026-11-07**: Completed Phase 2 of Architectural Refactor (Storage Pruning):
  - Refactored `config_types.h` down to 5 active site-commissioning domains (`DEVICE`, `NET_ETH`, `NET_WIFI`, `HVAC`, `CALIBRATION`).
  - Removed volatile runtime mappings (Modbus tables, DIs, sensor tables) from EEPROM persistence.
  - Replaced `default_config_profiles.c/.h` with immutable compile-time defaults in `app_defaults.h`.
  - Updated `composition_root.c/.h` and `app_facade.c`: pruned provider registrations and RAM structs.
  - Verified 100% test pass rate across `storage` (11/11), `hvac` (33/33), and `modbus` (99/99).
- **2026-11-07**: Completed Phase 1 of Architectural Refactor (Dual-Pillar Isolation):
  - Created `app/include/common/hardware_pin_map.h`: Immutable 24 local optocoupled DIs on `mcp23s17_0/1`, 32 remote DIs on `MBSL32DI`, and 12 HVAC relays on `mcp23s17_2`.
  - Created `app/include/common/modbus_device_table.h`: Immutable bus configs and slave descriptors for RS485-1 (JSY-MK-339, CWT-TM-16PT) and RS485-2 (MBSL32DI, Waveshare Relay).
  - Created `app/include/common/sensor_hardware_map.h`: Flat 1-byte sensor IDs linking physical channels to semantic roles across operating profiles.
- **2026-11-06**: Resolved audit findings P2-1, P2-4, P2-8, P2-9, P2-10:
  - P2-1: Implemented and registered concrete storage providers (`storage_provider_device`, `storage_provider_calibration`, `storage_provider_sensor_cfg`, `storage_provider_baseline`) packing versioned envelopes for all 8 config domains in `composition_root.c` with compile-time slot size assertions.
  - P2-4: Documented and enforced mandatory boot dependency order in `composition_root.h/.c` (Storage -> Modbus -> Indicators -> Digital Inputs -> HVAC -> Network -> Shell). Added storage dependency guards returning `-EAGAIN` in modbus, network, and hvac initializers. Reversed teardown order in stop/deinit.
  - P2-8: Replaced `(void)` ignored return casts on `i_config_storage_save`, `ao_storage_register_provider`, `ao_storage_mark_dirty`, `adapter_hvac_actuator_set_hw_config`, and Active Object teardowns with explicit error checks and warnings/errors.
  - P2-9: Unified `k_default_slaves` table via `default_config_profiles.c` / `default_config_get_modbus_slaves()`, removing duplicate tables in `composition_root.c`.
  - P2-10: Eliminated magic numbers in Waveshare / Modbus configuration in `composition_root.c`, dynamically querying registry via `modbus_registry_find_by_type()` and using fallback constants.
- **2026-11-05**: Refactored all 5 Active Objects to canonical 5-phase lifecycle and removed `s_storage_bounce_buffer`.
- **2026-11-05**: Reorganized `app_container_t` with vertical grouping and added modular subsystem initializers and DIP getters.
- **2026-11-05**: Fixed hardware device injection in `adapter_di_gpio.c` (passed devices from `composition_root.c`).
- **2026-11-05**: Enriched `i_relay.h` with `relay_channel_status_t` and `cycle_count` telemetry.
- **2026-11-05**: Segregated `i_network.h` (removed `scan` and redundant `iface` parameter) and added `i_wifi_control.h`.
- **2026-11-05**: Added explicit FSM `net_adapter_fsm_state_t` in `adapter_eth_stm32` and `adapter_wifi_esp_hosted`.
