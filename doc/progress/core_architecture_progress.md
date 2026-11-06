# Core Architecture & Framework Progress

- **Status**: **COMPLETED (100%)**
- **Architecture References**:
  - [`doc/architecture/active_object_architecture.md`](../architecture/active_object_architecture.md)
  - [`doc/architecture/dependency_injection_container_architecture.md`](../architecture/dependency_injection_container_architecture.md)
  - [`doc/architecture/ports_and_adapters_architecture.md`](../architecture/ports_and_adapters_architecture.md)

---

## 1. Implemented Components

| Component / Layer | File Path | Status | Verification |
| :--- | :--- | :---: | :--- |
| **Active Object Template** | All 5 AOs (`ao_modbus`, `ao_network`, `ao_storage`, `ao_di`, `ao_indicator`) | 100% | 5-phase lifecycle (`init`, `start`, `stop`, `wait_stopped`, `deinit`), MPU stack guards |
| **DI Container / Composition Root** | `app/src/infrastructure/di/composition_root.c`, `.h` | 100% | Vertical subsystem grouping, modular init, `wire()`, 10 DIP getters |
| **Hexagonal Port Encapsulation** | `app/include/interfaces/` | 100% | Rule §4.1.D fat-pointers, validated inline wrappers, 0 `api->` direct calls outside adapters |
| **Hardware Injection Rule** | `app/src/infrastructure/adapters/` | 100% | 0 `DEVICE_DT_GET()` inside adapters; all devices injected from `composition_root.c` |

---

## 2. Pending Work
- None. Core architecture standards are 100% enforced across all modules.

---

## 3. Changelog & Refactoring History
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
