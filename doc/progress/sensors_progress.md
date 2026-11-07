# Sensors & Mathematical Model Subsystem Progress

- **Status**: **IN_PROGRESS (95%)**
- **Architecture Reference**: [`doc/architecture/sensor_subsystem.md`](../architecture/sensor_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Sensor Types** | `app/include/common/sensor_types.h`, `energy_types.h` | 100% | Data structures for RTDs, energy meters, pressure, humidity; multi-concentrator Modbus (up to 4 CWT concentrators = 64 channels) and Revision F limits (16 pairs, 16 room probes, 8 outdoor probes) |
| **Domain Interfaces** | `app/include/interfaces/i_sensor_matrix.h`, `i_energy_meter.h`, `i_temperature_bank.h` | 100% | Pure abstract ports for sensor ingestion |
| **Matrix Mapper** | `app/src/domain/services/sensor_matrix_mapper.c` | 100% | Normalizes raw channel data across multi-concentrator CWTs and internal/J9 channels to unified sensor matrix and flat 64-channel snapshot (`sensor_matrix_pack_flat_snapshot`) |
| **Modbus Codecs** | `app/src/domain/services/cwt_tm16pt_codec.c`, `jsy_mk339_codec.c` | 100% | Unpacking of 16 RTD channels and 3-phase AC energy parameters |
| **Thermodynamic Engine** | `app/src/domain/services/thermodynamic_engine.h`, `app/src/domain/services/thermodynamic_engine.c` | 100% | Pure C99/C11 implementation of Revision F baseline: psychrometrics, mass flow, sensible/latent balances, enthalpies, Carnot COP, exergy efficiency, Flash R410A LUT, 64-bit OLS solver |
| **Zbus Decoupling Channels** | `app/src/application/zbus/sensor_channels.h`, `app/src/application/zbus/sensor_channels.c` | 100% | `chan_sensor_snapshot` (flat array) & `chan_thermal_matrix` (semantic frame) channels for decoupled reactive consumer delivery |
| **Sensor Matrix AO** | `app/src/application/active_objects/ao_sensor_matrix.c` | 100% | Canonical 5-phase AO executing periodic acquisition and publishing to Zbus channels |

---

## 2. Pending Work
- [ ] Implement `adapter_temperature_bank.c` linking Modbus/internal sensors to `i_temperature_bank_t`.
- [ ] Implement `adapter_energy_meter.c` linking JSY-MK-339 to `i_energy_meter_t`.
- [x] Implement Mathematical Model Revision F (`Mathematical_Model_Specification_REVF.md`): psychrometric enthalpy, thermal capacity, sensible/latent heat, COP, compressor degradation.
- [ ] Implement ADC driver for 5 on-board PT100 channels (INA826) and 4-20mA current loop inputs (J9).

---

## 3. Changelog & Refactoring History
- **2026-11-07**: Complete Sensor Data Pipeline Zbus Decoupling & Consumer Alignment (`doc/issues/2026-11-07-sensor-data-pipeline-zbus-decoupling.md`):
  - Aligned Application Facade and Shell CLI with Zbus channels:
    - Added `app_facade_sensor_get_snapshot()` to read `chan_sensor_snapshot` atomically via `zbus_chan_read(..., K_NO_WAIT)`.
    - Migrated `app_facade_sensor_get_matrix()` to read `chan_thermal_matrix` atomically via `zbus_chan_read(..., K_NO_WAIT)`.
    - Migrated `app_facade_sensor_get_readings()` to unpack internal RTDs and Modbus CWT1 channels directly from `chan_sensor_snapshot`.
    - Completely deleted static `s_sensor_cache` struct, spinlock, and legacy functions `app_facade_update_modbus_temperatures()` and `app_facade_update_internal_temperatures()`.
    - Added Shell CLI command `sensor snapshot` displaying flat 64-channel snapshot with live validity statuses.
  - Decoupled `ao_sensor_matrix` & Pruned Dead Code:
  - Created Zbus channels `chan_sensor_snapshot` (`sensor_snapshot_msg_t`) and `chan_thermal_matrix` (`thermal_matrix_frame_t`) in `app/src/application/zbus/sensor_channels.h/.c`.
  - Decoupled `ao_sensor_matrix`: removed legacy `ao_sensor_matrix_frame_sink_t` callback from `ao_sensor_matrix_config_t` and `ao_sensor_matrix_t`. AO now publishes directly to `chan_thermal_matrix` and `chan_sensor_snapshot`.
  - Pruned obsolete domain code: deleted legacy `sensor_matrix_pack_hvac_bank()` from `sensor_matrix_mapper.h/.c`, making `sensor_matrix_pack_flat_snapshot()` the sole snapshot packing function.
  - Refactored `composition_root.c`: removed `on_sensor_matrix_frame()` and manual fallback call `ao_hvac_feed_temperatures()` in `on_modbus_cwt_tm16pt_reading()`. Wired synchronous Zbus listener `lis_hvac_sensor_feed` on `chan_sensor_snapshot` to feed `ao_hvac_feed_sensor_snapshot()`.
  - Updated all unit tests and suites (`test_ao_sensor_matrix.c`, `test_sensor_matrix_mapper.c`, `test_composition_root.c`).
  - Validated 100% test pass rate across all 8 suites (240/240 tests) on `native_sim` via Twister, and clean cross-compilation for `brightc-controllerv3` (Flash: 15.09%, RAM: 32.54%).
- **2026-11-07**: Implemented Phase 4 (Telemetry Data Pipeline & Unified Sensor Snapshot):
  - Added `sensor_matrix_pack_flat_snapshot()` to `sensor_matrix_mapper.h/.c` mapping internal RTDs (IDs 1..5), CWT Concentrator 1 (IDs 10..25), CWT Concentrator 2 (IDs 30..45), and J9 Analog current loop (IDs 50..59) into flat 64-entry array with 64-bit validity mask.
  - Added `sensor_matrix_raw_bank_t` typedef alias in `sensor_types.h`.
- **2026-11-06**: Integrated Sensor Matrix and Modbus Expander presentation & validity handling:
  - Added independent `inj_valid_mask` and `ret_valid_mask` to `thermal_matrix_frame_t` in `sensor_types.h`.
  - Added `app_facade_sensor_get_matrix()` and `sensor matrix` CLI command.
- **2026-11-06**: Resolved audit findings P0-01 & P0-03 (Sensor Cache Thread Safety & Calibration Persistence).
- **2026-11-06**: Implemented Mathematical Model Revision F (`thermodynamic_engine.h/.c`).
- **2026-11-06**: Implemented multi-concentrator Modbus RTU CWT support (up to 4 concentrators / 64 Modbus channels).
- **2026-11-05**: Defined abstract interfaces and initial codecs.
