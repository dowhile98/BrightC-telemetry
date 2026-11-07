# Storage Subsystem Progress

- **Status**: **COMPLETED (100%)**
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
- **2026-11-07**: Optimized EEPROM Payload Sizing & Buffer Allocation:
  - Recalculated 64-byte EEPROM page-aligned layout for all 5 active domains (`DEVICE`: 128B slot, `NET_ETH`: 64B slot, `NET_WIFI`: 320B slot, `HVAC`: 576B slot, `CALIBRATION`: 192B slot).
  - Reduced `ADAPTER_STORAGE_MICROCONF_MAX_PAYLOAD` from 992B down to 544B (saving >2 KB static RAM).
  - Fixed `storage_api_load()` buffer sizing check by inspecting `config_envelope_header_t` payload length instead of full slot capacity, eliminating `-ENOBUFS` (-105) errors on boot.
  - Added `-ENOENT` detection for uninitialized EEPROM memory (0x00 / 0xFF).
  - Updated `composition_root.c` compile-time `BUILD_ASSERT`s and unit tests in `test_config_storage.c`.
- **2026-11-07**: Phase 2 Storage Pruning & ADR-2 Implementation:
  - Pruned configuration domains from 8 down to 5 active site-commissioning domains (`DEVICE`, `NET_ETH`, `NET_WIFI`, `HVAC`, `CALIBRATION`).
  - Removed hardware topology (Modbus slave tables, digital input routing, sensor channel mapping) from EEPROM, eliminating flash corruption hazards for safety cutouts.
  - Replaced `default_config_profiles.c/.h` with lightweight, compile-time immutable defaults in `app_defaults.h`.
  - Updated `adapter_storage_microconf.c` domain keys and `config_domain_get_layout()`.
  - Verified 100% pass on `tests/unit/storage` (11/11 tests passing on `native_sim`).
- **2026-11-06**: Resolved audit findings P0-03 and P1-04:
  - P0-03 (Real RAM State & Persistence for 4 Config Domains): Added RAM storage instances in `app_container_t` for `device_cfg`, `calibration_cfg`, `sensor_cfg`, and `baseline_cfg`. Loaded valid envelopes from storage on startup with fallback to defaults. Storage providers now pack live runtime data from RAM instances. `app_facade_sensor_set_calibration()` updates offsets and marks calibration domain dirty.
  - P1-04 (Safe Storage Reload & Factory Reset): In `app_facade_storage_reload_all()`, zeroed temporary buffers with `memset`, strictly verified that unpacked size matches destination structure size, and reloaded all 8 configuration domains. In `app_facade_storage_reset_defaults()`, erased EEPROM ping-pong slots and restored default RAM states across all 8 domains.
- **2026-11-06**: Resolved audit finding P2-1:
  - Eliminated zombie configuration domains by defining types (`config_device_t`, `config_calibration_t`, `config_sensor_cfg_t`, `config_baseline_t`), default profile accessors, and registering active providers for all 8 domains with versioned envelope packing and layout assertions. Added unit tests in `test_composition_root.c` validating provider registration and persistability across all 8 domains.
- **2026-11-06**: Resolved audit findings P1-6, P1-7, P1-8, P1-9, P1-10:
  - Added binary `config_envelope_t` header (`0x4243544C`) with CRC16 and version checking.
  - Fixed silent buffer truncation in `storage_api_load()`: returns `-ENOBUFS` on overflow.
  - Added compile-time `BUILD_ASSERT`s for domain slot sizes, non-overlapping layouts, and 32KB EEPROM capacity.
  - Implemented degraded mode: if EEPROM is missing or fails, storage boots without crash and logs warnings.
  - Factory reset route connected via boot button hold (> 5s) and `app_facade_storage_reset_defaults()`.
- **2026-11-06**: Integrated `CONFIG_DOMAIN_HVAC` (base `0x2800`, total `2048` B, 2 ping-pong slots of `1024` B) into `config_types.h` and `adapter_storage_microconf.c` (`"hvac"` key); updated storage unit test suite to validate domain enumerators and EEPROM layout.
- **2026-11-05**: Refactored `ao_storage` to 5-phase canonical lifecycle.
- **2026-11-05**: Fixed `s_storage_bounce_buffer` file-scope variable; embedded within `ao_storage_t`.
