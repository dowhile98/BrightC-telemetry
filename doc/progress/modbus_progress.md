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
- **2026-11-06**: Resolved audit finding P0-01 (Temperature Acquisition Chain):
  - Defined `ao_modbus_cwt_tm16pt_cb_t` callback in `ao_modbus.h` and integrated invocation in `ao_modbus.c` upon successful holding register reads for `MODBUS_DEV_TYPE_CWT_TM16PT`.
  - Wired callback in `composition_root.c` across all Modbus buses to forward parsed temperatures directly to `ao_hvac_feed_temperatures()` and `app_facade_update_modbus_temperatures()`.
- **2026-11-06**: Resolved audit findings P2-3, P2-9, P2-10:
  - P2-3 (Modbus Slave ID Kconfig & Dynamic Configuration): Defined `CONFIG_BRIGHTC_DEFAULT_MODBUS_ADDR_MBSL32DI` and `CONFIG_BRIGHTC_DEFAULT_MODBUS_ADDR_WAVESHARE_RELAY` in `app/Kconfig`. Added `app_facade_modbus_get_slave()` and shell command `modbus slave <id> ...` with EEPROM persistence.
  - P2-9 (Unified `k_default_slaves`): Centralized Modbus default slaves in `default_config_profiles.c/.h` accessed via `default_config_get_modbus_slaves()`, removing duplicated tables in `composition_root.c`.
  - P2-10 (Remove Magic Numbers in Waveshare / Modbus Setup): Defined `DEFAULT_MODBUS_WAVESHARE_TIMEOUT_MS` and queried registry dynamically with `modbus_registry_find_by_type()`.
- **2026-11-05**: Refactored `ao_modbus` to 5-phase canonical lifecycle.
- **2026-11-05**: Enriched `adapter_relay_modbus` with operational metrics (`cycle_count`, `last_toggle_ms`, `is_fault`).
