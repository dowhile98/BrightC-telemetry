# Industrial Modbus Subsystem Progress

- **Status**: **IN_PROGRESS (90%)**
- **Architecture Reference**: [`doc/architecture/modbus_subsystem.md`](../architecture/modbus_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Codecs** | `app/src/domain/services/*_codec.c` | 100% | JSY-MK-339, CWT-TM-16PT, MBSL32DI, Waveshare Relay codecs |
| **Scheduler** | `app/src/domain/services/slave_scheduler.c` | 100% | Dynamic poll rate, error backoff, channel scheduling |
| **Registry** | `app/src/application/modbus/modbus_registry.c` | 100% | Slave registry, bus assignment, failover hook |
| **Client Adapter** | `app/src/infrastructure/adapters/adapter_modbus_nmbs.c` | 100% | nanoMODBUS client over Zephyr UART (USART2, USART6) with DE hardware control |
| **Relay Adapter** | `app/src/infrastructure/adapters/adapter_relay_modbus.c` | 100% | Implements `i_relay.h` (FC05 coil writes, cycle count & timing tracking) |
| **Diagnostics** | `app/src/application/diagnostics/modbus_bus_probe.c` | 100% | Bus communication probing and error classification |
| **Active Object** | `app/src/application/active_objects/ao_modbus.c` | 100% | Dedicated thread, message queue, periodic polling loop, event publishing |
| **Unit Tests** | `tests/unit/modbus/` | 100% | 10 test suites, 99 tests passing on `native_sim` |

---

## 2. Pending Work
- [ ] Automatic runtime slave auto-discovery and scanning tool.
- [ ] Automated bus failover supervisor (consecutive failure threshold triggering automatic switch from RS485-1 to RS485-2).

---

## 3. Changelog & Refactoring History
- **2026-11-11**: Mode-Aware Slave Topology Registration:
  - In `composition_root_init_modbus()`, synchronized Modbus slave registration with persisted operational mode (`self->hvac.config.operation_mode`) and `total_units`:
    * When `HVAC_MODE_TELEMETRY_ONLY`, `MBSL32DI` and `WAVESHARE_RELAY_32CH` are omitted from registry and polling loops.
    * When `HVAC_MODE_HYBRID_FULL` or `HVAC_CONTROL_ONLY`, `MBSL32DI` is registered, and `WAVESHARE_RELAY_32CH` is registered exclusively when `total_units > 4`.
  - External relay port `self->modbus.relay_ext_port` remains safely unbound when Waveshare relay is omitted.
  - Updated unit tests in `tests/unit/modbus/src/test_composition_root.c` verifying topology under Telemetry-Only, Hybrid (4 units), and Control-Only (8 units) configurations.
- **2026-11-07**: Conditional Modbus Slave Submenus (`menuconfig` + `if`):
  - Structured Modbus slave options in `app/Kconfig` as conditional submenus using `menuconfig` and `if` blocks.
  - Configured only the JSY-MK-339 3-Phase AC energy meter enabled by default (`default y`), with PT100 concentrators, MBSL32DI, and Waveshare relay disabled by default (`default n`).
  - Hides slave address configuration options when slaves are disabled, with `app/include/common/modbus_device_table.h` fallback macros guaranteeing valid default slave addresses (`1U..5U`).
- **2026-11-07**: Kconfig-driven Slave Enablement (`is_enabled`):
  - Added hidden marker `CONFIG_BRIGHTC_MODBUS_KCONFIG_PRESENT` and individual boolean flags `CONFIG_BRIGHTC_MODBUS_ENABLE_*` for all standard field slaves (`JSY_MK339`, `CWT_TM16PT_1`, `CWT_TM16PT_2`, `MBSL32DI`, `WAVESHARE_RELAY`) in `app/Kconfig`.
  - Defined fallback logic in `app/include/common/modbus_device_table.h` to ensure standalone C / unit test compatibility without Zephyr Kconfig.
  - Replaced hardcoded `.is_enabled` literals in `k_brightc_modbus_slave_table` with `MODBUS_ENABLE_*` macros.
- **2026-11-07**: Unified Modbus Slave Address & ID Configuration:
  - Simplified `app/Kconfig` under `menu "BrightC Modbus Configuration"` to eliminate redundant `DEV_ID` vs `ADDR` duplicate symbols, retaining single `CONFIG_BRIGHTC_MODBUS_ADDR_*` configuration entries.
  - Cleaned `app/include/common/modbus_device_table.h`: removed all `CONFIG_BRIGHTC_DEFAULT_MODBUS_DEV_ID_*` and `MODBUS_DEV_ID_*` symbols, configuring `k_brightc_modbus_slave_table` with `.device_id = MODBUS_ADDR_*` and `.slave_addr = MODBUS_ADDR_*`.
  - Updated `composition_root.c` and `adapter_hvac_actuator.c` to reference `MODBUS_ADDR_CWT_TM16PT_2` and `MODBUS_ADDR_WAVESHARE_RELAY`.
  - Verified 100% test pass (242/242 tests across 8 suites) and successful primary target build (`brightc-controllerv3`, 15.03% flash, 32.43% RAM).
- **2026-11-07**: Kconfig-driven Modbus Device IDs and Slave Addresses:
  - Added Kconfig symbols for both `device_id` and `slave_addr` across all 5 standard field slaves (`JSY_MK339`, `CWT_TM16PT_1`, `CWT_TM16PT_2`, `MBSL32DI`, `WAVESHARE_RELAY`) in `app/Kconfig`.
  - Added standalone fallback macros and `MODBUS_DEV_ID_*` / `MODBUS_ADDR_*` aliases in `app/include/common/modbus_device_table.h`.
  - Updated `k_brightc_modbus_slave_table` to use symbolic macros instead of magic numeric literals.
  - Updated `on_modbus_cwt_tm16pt_reading()` in `composition_root.c` to route readings from `MODBUS_DEV_ID_CWT_TM16PT_2` to CWT concentrator index 1.
- **2026-11-06**: Resolved audit finding P0-01 (Temperature Acquisition Chain):
  - Defined `ao_modbus_cwt_tm16pt_cb_t` callback in `ao_modbus.h` and integrated invocation in `ao_modbus.c` upon successful holding register reads for `MODBUS_DEV_TYPE_CWT_TM16PT`.
  - Wired callback in `composition_root.c` across all Modbus buses to forward parsed temperatures directly to `ao_hvac_feed_temperatures()` and `app_facade_update_modbus_temperatures()`.
- **2026-11-06**: Resolved audit findings P2-3, P2-9, P2-10:
  - P2-3 (Modbus Slave ID Kconfig & Dynamic Configuration): Defined `CONFIG_BRIGHTC_DEFAULT_MODBUS_ADDR_MBSL32DI` and `CONFIG_BRIGHTC_DEFAULT_MODBUS_ADDR_WAVESHARE_RELAY` in `app/Kconfig`. Added `app_facade_modbus_get_slave()` and shell command `modbus slave <id> ...` with EEPROM persistence.
  - P2-9 (Unified `k_default_slaves`): Centralized Modbus default slaves in `default_config_profiles.c/.h` accessed via `default_config_get_modbus_slaves()`, removing duplicated tables in `composition_root.c`.
  - P2-10 (Remove Magic Numbers in Waveshare / Modbus Setup): Defined `DEFAULT_MODBUS_WAVESHARE_TIMEOUT_MS` and queried registry dynamically with `modbus_registry_find_by_type()`.
- **2026-11-05**: Refactored `ao_modbus` to 5-phase canonical lifecycle.
- **2026-11-05**: Enriched `adapter_relay_modbus` with operational metrics (`cycle_count`, `last_toggle_ms`, `is_fault`).
