# Storage Subsystem Progress

- **Status**: **IN_PROGRESS (75%)**
- **Architecture Reference**: [`doc/architecture/storage_subsystem.md`](../architecture/storage_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Interface** | `app/include/interfaces/i_config_storage.h` | 100% | Pure configuration persistence port (`read`, `write`, `commit`, `erase`) |
| **EEPROM Adapter** | `app/src/infrastructure/adapters/adapter_storage_microconf.c` | 100% | AT24C08 I2C2 EEPROM driver with checksum validation |
| **Active Object** | `app/src/application/active_objects/ao_storage.c` | 100% | Asynchronous write queue, commit debouncing, config cache in RAM |
| **Zbus Channels** | `app/src/application/zbus/config_channels.c` | 100% | `chan_config_state` broadcasting configuration changes |
| **Unit Tests** | `tests/unit/storage/` | 100% | 10 tests passing on `native_sim` |

---

## 2. Pending Work
- [ ] Secondary flash backend adapter using Zephyr NVS (`adapter_storage_nvs.c`).
- [ ] EEPROM wear-leveling / write endurance tracking telemetry.

---

## 3. Changelog & Refactoring History
- **2026-11-06**: Integrated `CONFIG_DOMAIN_HVAC` (base `0x2800`, total `2048` B, 2 ping-pong slots of `1024` B) into `config_types.h` and `adapter_storage_microconf.c` (`"hvac"` key); updated storage unit test suite to validate domain enumerators and EEPROM layout.
- **2026-11-05**: Refactored `ao_storage` to 5-phase canonical lifecycle.
- **2026-11-05**: Fixed `s_storage_bounce_buffer` file-scope variable; embedded within `ao_storage_t`.
