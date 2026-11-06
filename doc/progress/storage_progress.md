# Storage Subsystem Progress

- **Status**: **IN_PROGRESS (90%)**
- **Architecture Reference**: [`doc/architecture/storage_subsystem.md`](../architecture/storage_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Interface** | `app/include/interfaces/i_config_storage.h` | 100% | Pure configuration persistence port (`read`, `write`, `commit`, `erase`) |
| **EEPROM Adapter** | `app/src/infrastructure/adapters/adapter_storage_microconf.c` | 100% | AT24C256C I2C2 EEPROM driver with checksum validation, `-ENOBUFS` overflow protection |
| **Envelope Framing** | `app/include/common/config_envelope.h` | 100% | Binary payload framing: Magic (`0x4243544C`), version 1, length, and CRC16 |
| **Active Object** | `app/src/application/active_objects/ao_storage.c` | 100% | Asynchronous write queue, commit debouncing, degraded mode fallback |
| **Zbus Channels** | `app/src/application/zbus/config_channels.c` | 100% | `chan_system_config` broadcasting persistent events |
| **Unit Tests** | `tests/unit/storage/` | 100% | 11 tests passing on `native_sim` |

---

## 2. Pending Work
- [ ] Secondary flash backend adapter using Zephyr NVS (`adapter_storage_nvs.c`).
- [ ] EEPROM wear-leveling / write endurance tracking telemetry.

---

## 3. Changelog & Refactoring History
- **2026-11-06**: Resolved audit findings P1-6, P1-7, P1-8, P1-9, P1-10:
  - Added binary `config_envelope_t` header (`0x4243544C`) with CRC16 and version checking.
  - Fixed silent buffer truncation in `storage_api_load()`: returns `-ENOBUFS` on overflow.
  - Added compile-time `BUILD_ASSERT`s for domain slot sizes, non-overlapping layouts, and 32KB EEPROM capacity.
  - Implemented degraded mode: if EEPROM is missing or fails, storage boots without crash and logs warnings.
  - Factory reset route connected via boot button hold (> 5s) and `app_facade_storage_reset_defaults()`.
- **2026-11-06**: Integrated `CONFIG_DOMAIN_HVAC` (base `0x2800`, total `2048` B, 2 ping-pong slots of `1024` B) into `config_types.h` and `adapter_storage_microconf.c` (`"hvac"` key); updated storage unit test suite to validate domain enumerators and EEPROM layout.
- **2026-11-05**: Refactored `ao_storage` to 5-phase canonical lifecycle.
- **2026-11-05**: Fixed `s_storage_bounce_buffer` file-scope variable; embedded within `ao_storage_t`.
