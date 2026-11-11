# Sensors & Mathematical Model Subsystem Progress

- **Status**: **COMPLETE (100%)**
- **Architecture Reference**: [`doc/architecture/sensor_subsystem.md`](../architecture/sensor_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Thermal & Energy Port Contract** | `app/include/interfaces/i_thermal_energy.h` | 100% | Rule §4.1.D fat-pointer port contract (`i_thermal_energy_t`, `i_thermal_energy_api_t`), safety predicate `i_thermal_energy_is_bound()`, and type-safe defensive wrappers |
| **Thermal & Energy Fake Double** | `tests/fakes/include/fake_thermal_energy.h`, `tests/fakes/src/fake_thermal_energy.c` | 100% | In-memory test double implementing `i_thermal_energy_t` for consumer testing with ring history support |
| **Hardware FPU & CMSIS-DSP** | `app/prj.conf`, `app/src/domain/thermal_energy/predictive_analytics.c` | 100% | Cortex-M7 FPv5-D16 hardware FPU + CMSIS-DSP `arm_mat_inverse_f64` and vector multiplication with ISO C99 fallback |
| **Sensor Matrix Normalizer Adapter** | `app/src/infrastructure/adapters/sensor_matrix_mapper.c/.h` | 100% | Signal ingestion layer mapping flat temperatures, J9 4-20mA humidity, JSY-MK-339 3-phase AC power, and mode-dependent contacts into `thermal_energy_capture_t` |
| **Thermal & Energy Types** | `app/include/common/thermal_energy_types.h` | 100% | Universal types (`quality_status_t`, `compressor_fsm_state_t`), array bounds, DTO contracts (`thermal_energy_capture_t`, `thermal_energy_result_t`), and dynamic Kconfig-configurable `thermal_hourly_record_t` |
| **Thermal Storage Configs** | `app/include/common/config_types.h`, `app_defaults.h` | 100% | EEPROM ping-pong layouts for `CONFIG_DOMAIN_THERMAL_SITE` (0x0A00, 1536 B, payload 184 B) and `CONFIG_DOMAIN_THERMAL_BASELINE` (0x1000, 1024 B, payload 216 B), validation predicates, and defaults |
| **Humidity Transducer (Domain)** | `app/src/domain/thermal_energy/humidity_transducer.c/.h` | 100% | 4-20 mA NAMUR NE43 return relative humidity transducer, fault bounds (<3.8 mA, >20.5 mA), and calibration |
| **Psychrometrics (Domain)** | `app/src/domain/thermal_energy/psychrometrics.c/.h` | 100% | Moist air thermodynamic properties: barometric pressure altitude correction, Magnus-Tetens saturation vapor pressure, mixing ratio, moist air enthalpy, dew point, wet-bulb estimation, air density |
| **Thermodynamic Engine Rev F (Domain)** | `app/src/domain/thermal_energy/thermodynamics.c/.h` | 100% | Mass flow rate, sensible/latent heat balance, total cooling capacity, COP (instantaneous, baseline, Carnot), exergy efficiency, and NaN/Inf-safe numerical floating-point operations |
| **Compressor FSM & Coherence (Domain)** | `app/src/domain/thermal_energy/compressor_fsm.c/.h` | 100% | 5-state compressor stabilization FSM (`COMPR_STATE_OFF`, `STARTING`, `STABILIZING`, `STEADY`, `FAULT`), anti-cycling timing, thermal capacity gating, and inrush power suppression |
| **Predictive Analytics & OLS (Domain)** | `app/src/domain/thermal_energy/predictive_analytics.c/.h` | 100% | 4-variable OLS solver in double precision with Tikhonov regularization, condition number calculation, expected COP, % mechanical degradation, filter fouling & F-04 precedence days remaining, decoupled refrigerant leak detection, hotspot/bypass/moisture alerts, and weather derating/free cooling margin |
| **Sensor Types** | `app/include/common/sensor_types.h`, `energy_types.h` | 100% | Data structures for RTDs, energy meters, pressure, humidity; multi-concentrator Modbus (up to 4 CWT concentrators = 64 channels) and Revision F limits (16 pairs, 16 room probes, 8 outdoor probes) |
| **Domain Interfaces** | `app/include/interfaces/i_sensor_matrix.h`, `i_energy_meter.h`, `i_temperature_bank.h` | 100% | Pure abstract ports for sensor ingestion |
| **Matrix Mapper** | `app/src/domain/services/sensor_matrix_mapper.c` | 100% | Normalizes raw channel data across multi-concentrator CWTs and internal/J9 channels to unified sensor matrix and flat 64-channel snapshot (`sensor_matrix_pack_flat_snapshot`) |
| **Modbus Codecs** | `app/src/domain/services/cwt_tm16pt_codec.c`, `jsy_mk339_codec.c` | 100% | Unpacking of 16 RTD channels and 3-phase AC energy parameters |
| **Thermodynamic Legacy Engine** | `app/src/domain/services/thermodynamic_engine.h`, `app/src/domain/services/thermodynamic_engine.c` | 100% | Implementation of Revision F baseline: psychrometrics, mass flow, sensible/latent balances, enthalpies, Carnot COP, exergy efficiency, Flash R410A LUT, 64-bit OLS solver |
| **Zbus Decoupling Channels** | `app/src/application/zbus/sensor_channels.h`, `app/src/application/zbus/sensor_channels.c` | 100% | `chan_sensor_snapshot` (flat array) & `chan_thermal_matrix` (semantic frame) channels for decoupled reactive consumer delivery |
| **Sensor Matrix AO** | `app/src/application/active_objects/ao_sensor_matrix.c` | 100% | Canonical 5-phase AO executing periodic acquisition and publishing to Zbus channels |
| **ADC DMA Engine Driver** | `drivers/adc_dma/src/adc_dma_engine.c`, `include/drivers/adc_dma_engine.h` | 100% | Multi-ADC DMA parallel driver with 16-sample burst oversampling and min/max rejection for 16-bit uint16_t counts |
| **Sensor Matrix Adapter** | `app/src/infrastructure/adapters/adapter_sensor_zephyr.c/.h` | 100% | Zephyr ADC DMA adapter with 1st-order bilinear IIR low-pass filtering (Fc=1.0Hz) and zero startup transient |

---

## 2. Pending Work
- [ ] Implement `adapter_temperature_bank.c` linking Modbus/internal sensors to `i_temperature_bank_t`.
- [ ] Implement `adapter_energy_meter.c` linking JSY-MK-339 to `i_energy_meter_t`.
- [x] Implement Phase 4 Interface Port Contract (`i_thermal_energy.h`), Fake (`fake_thermal_energy.c/.h`), and Signal Ingestion Layer (`sensor_matrix_mapper.c/.h`).
- [x] Enable Hardware FPU (`CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`) & CMSIS-DSP Acceleration (`arm_mat_inverse_f64`).
- [x] Implement Mathematical Model Revision F Phase 3 Domain Services (`predictive_analytics.c/.h`, `test_predictive_ols.c`).
- [x] Implement Mathematical Model Revision F Phase 2 Domain Services (`humidity_transducer.c`, `psychrometrics.c`, `thermodynamics.c`, `compressor_fsm.c`).
- [x] Implement ADC driver for 5 on-board PT100 channels (INA826) and 4-20mA current loop inputs (J9).

---

## 3. Changelog & Refactoring History
- **2026-11-11**: Implemented Phase 4 (Interface Port Contract & Signal Ingestion Layer) + Hardware FPU & CMSIS-DSP Acceleration:
  - Enabled Hardware Double-Precision FPU (`CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`) and ARM CMSIS-DSP (`CONFIG_CMSIS_DSP=y`) in `app/prj.conf`.
  - Added hardware-accelerated 4x4 matrix solve (`arm_mat_init_f64`, `arm_mat_inverse_f64`, `arm_mat_mult_f64`) in `app/src/domain/thermal_energy/predictive_analytics.c` under `#if defined(CONFIG_CMSIS_DSP) && !defined(CONFIG_ZTEST)` with pure C99 Gauss-Jordan fallback for host simulation.
  - Authored abstract port contract `app/include/interfaces/i_thermal_energy.h` adhering strictly to Rule §4.1.D fat-pointer encapsulation (`i_thermal_energy_api_t`, `i_thermal_energy_t`, `i_thermal_energy_is_bound()`, validated wrappers).
  - Implemented in-memory test double `tests/fakes/include/fake_thermal_energy.h` and `tests/fakes/src/fake_thermal_energy.c`.
  - Implemented normalizer adapter `app/src/infrastructure/adapters/sensor_matrix_mapper.c/.h` converting multi-source raw hardware readings into `thermal_energy_capture_t`:
    - Temperatures mapped according to `config_thermal_site_t` (supply, return, room, outdoor).
    - Return humidity mapped from 4-20 mA J9 CH1 via `humidity_transducer_process()`.
    - 3-phase electrical power ingested from `energy_reading_t`.
    - Auxiliary contacts mapped based on HVAC mode (`ALM_IN_1..16` in telemetry-only or `MBSL32DI` in hybrid/control).
  - Authored unit test suites `test_thermal_energy_port.c` (7 tests) and `test_sensor_matrix_mapper.c` (8 tests); all 60 tests in `tests/unit/thermal_energy/` pass 100% on `native_sim`.
  - Verified target build on physical board `brightc-controllerv3` (STM32H743): 0 errors, 0 warnings (Flash: 16.61%, RAM: 38.54%).

- **2026-11-11**: Implemented Phase 3 of Thermal and Energy Subsystem (REVF Baseline Mathematical Model Domain - Predictive Analytics & OLS Regression):
  - Created `app/src/domain/thermal_energy/predictive_analytics.c/.h`:
    - 4-variable OLS regression model (`ols_model_t`) in IEEE 754 64-bit `double` (`xtx[4][4]`, `xty[4]`, `beta[4]`).
    - Gauss-Jordan elimination with partial pivoting for normal equations inversion, 1-norm matrix condition number calculation ($\kappa_1$), and Tikhonov diagonal regularization ($\lambda = 10^{-6}$) handling ill-conditioned collinear matrices.
    - Expected COP calculation defended against $P_{elec} < 0.5\text{ kW}$.
    - Mechanical degradation percentage index ($\% \text{Degradation} = \text{clamp}_{0..100}(\max(\Delta COP / COP, \Delta \eta_{II} / \eta_{II}) \times 100)$).
    - Air filter fouling index ($\% \text{Filter}$) and F-04 Precedence Hierarchy maintenance days remaining ($100\text{ days}$ lockout for $N < 168\text{ h}$, $90\text{ days}$ healthy ceiling, and degradation rate projection).
    - Outdoor-decoupled refrigerant leak / coil fouling index with $50\%$ filter fouling lockout and $0.20$ load fraction gating.
    - Environmental anomaly checks: room hotspot ($> 3.5\,^\circ\text{C}$ spread), airflow bypass ($> 27\,^\circ\text{C}$ hotspot or $> 3.0\,^\circ\text{C}$ gradient), and moisture ingress ($> 30\%$ latent, $SHF < 0.70$, or $\omega_{ret} > 0.012\text{ kg/kg}$).
    - Weather-corrected nominal capacity derating ($0.5\%$ per $^\circ\text{C}$ above $35\,^\circ\text{C}$), safe redundant capacity ($Q_{max\_safe}$), and free cooling margin ($kW_{free}$).
  - Added unit test suite `predictive_ols_suite` in `tests/unit/thermal_energy/src/test_predictive_ols.c` (14 unit tests, 100% pass rate).
  - Verified target build on `brightc-controllerv3` (Flash: 16.73%, RAM: 38.03%).
  - Verified pure domain layer invariant (0 Zephyr headers, 0 dynamic allocations, 0 static mutable state).
- **2026-11-11**: Implemented Phase 2 of Thermal and Energy Subsystem (REVF Baseline Mathematical Model Domain Services):
  - Created `app/src/domain/thermal_energy/humidity_transducer.c/.h`: 4-20 mA NAMUR NE43 current loop transducer for return air relative humidity, under-range (<3.8 mA) and over-range (>20.5 mA) fault signaling, and 2-point linear calibration.
  - Created `app/src/domain/thermal_energy/psychrometrics.c/.h`: Moist air properties calculation including barometric pressure altitude compensation, Magnus-Tetens saturation vapor pressure, partial vapor pressure, mixing ratio $W$, moist air enthalpy $h$, dew point, wet-bulb estimation, and air density $\rho$.
  - Created `app/src/domain/thermal_energy/thermodynamics.c/.h`: Complete sensible and latent heat balances, air mass flow rate, total cooling capacity, COP (instantaneous, baseline, Carnot limit), exergetic efficiency, and strict IEEE-754 floating-point safety (NaN/Inf guards, division-by-zero clamping).
  - Created `app/src/domain/thermal_energy/compressor_fsm.c/.h`: 5-state compressor stabilization state machine (`COMPR_STATE_OFF`, `STARTING`, `STABILIZING`, `STEADY`, `FAULT`), anti-cycling run/stop timers, inrush power suppression, step load capture, and thermal capacity coherence gating.
  - Authored comprehensive unit test suite in `tests/unit/thermal_energy/` (`test_humidity_transducer.c`, `test_psychrometrics.c`, `test_thermodynamics.c`, `test_compressor_fsm.c`): 30/30 unit tests pass 100% on `native_sim`.
  - Target hardware build verified on `brightc-controllerv3`: 0 errors, 0 warnings (Flash: 16.73%, RAM: 38.03%).
- **2026-11-11**: Configurable `thermal_hourly_record_t` Temperature Array Lengths via Kconfig:
  - Added Kconfig options in `app/Kconfig` under `menu "BrightC Thermal and Energy Subsystem"`: `CONFIG_THERMAL_HOURLY_PAIRS_COUNT` (default 6, range 1..16), `CONFIG_THERMAL_HOURLY_ROOM_PROBES_COUNT` (default 2, range 1..16), and `CONFIG_THERMAL_HOURLY_OUTDOOR_PROBES_COUNT` (default 2, range 1..8).
  - Added non-Kconfig/host fallbacks and updated `thermal_hourly_record_t` in `app/include/common/thermal_energy_types.h` to use dynamic array lengths.
  - Implemented dynamic expected size macros `THERMAL_HOURLY_RECORD_BASE_SIZE (32U)`, `THERMAL_HOURLY_RECORD_TEMPS_SIZE`, and `THERMAL_HOURLY_RECORD_EXPECTED_SIZE` with `_Static_assert` validation.
  - Updated unit test in `tests/unit/storage/src/test_config_storage.c` to validate against `THERMAL_HOURLY_RECORD_EXPECTED_SIZE`.
- **2026-11-11**: Implemented Phase 1 of Thermal and Energy System (REVF Baseline): "Fundaciones de Tipos, DTOs y Persistencia en EEPROM":
  - Created `app/include/common/thermal_energy_types.h`: `quality_status_t` (`data_quality_t`), capacity constants (`THERMAL_MAX_PAIRS=16`, `THERMAL_MAX_ROOM_PROBES=16`, `THERMAL_MAX_OUTDOOR_PROBES=8`, `THERMAL_MAX_HVAC_UNITS=8`, `THERMAL_HOURLY_HISTORY_CAPACITY=720`), 5-state compressor FSM (`compressor_fsm_state_t`), `thermal_pair_mapping_t` (4 B), `thermal_energy_capture_t` (1 Hz snapshot), `thermal_energy_result_t` (thermodynamic output contract), and exact 64-byte `thermal_hourly_record_t` verified via `_Static_assert`.
  - Expanded storage memory map in `app/include/common/config_types.h`: added `CONFIG_DOMAIN_THERMAL_SITE` (domain 5, 0x0A00, 1536 B, slot 768 B) and `CONFIG_DOMAIN_THERMAL_BASELINE` (domain 6, 0x1000, 1024 B, slot 512 B), increasing `CONFIG_DOMAIN_MAX` to 7. Defined payload structs `config_thermal_site_t` (184 B) and `config_thermal_baseline_t` (216 B, 8-byte aligned) with compile-time assertions and inline validators.
  - Added compile-time immutable defaults in `app/include/common/app_defaults.h`: `k_default_thermal_site_cfg` (pilot 6 pairs, 2 room, 2 outdoor, 4500 m3/h, 14 kW, altitude 0) and `k_default_thermal_baseline_cfg` (untrained, exergetic efficiency 42.0%) with accessor APIs.
  - Integrated into composition root (`composition_root.h/.c`): added storage providers, dynamic runtime getters/setters, default fallback, and persistence synchronization.
  - Updated storage microconf adapter (`adapter_storage_microconf.c/.h`) with new domain keys and increased maximum payload buffer to 736 B.
  - Expanded unit test suite `tests/unit/storage/src/test_config_storage.c`: validated 7 domains, layouts, type alignments, static assertions, and storage pack/unpack roundtrips (14/14 tests pass 100%).
- **2026-11-10**: Completed Comprehensive Architectural Analysis & Specifications for REVF Thermal & Energy Subsystem (`doc/tasks/2.thermal.md`):
  - Audited existing codebase against REVF: verified domain algorithms (`thermodynamic_engine.c/.h`), ADC DMA driver, 16-channel Modbus RTU, JSY-MK-339 codec, and 500 ms contactor debouncing.
  - Authored 6 authoritative architecture specifications under `doc/architecture/`:
    - `thermal_energy_system_analysis.md`: Detailed audit, module taxonomy, hardware profiles, blocker/risk analysis (FPU configuration, 64-byte wire sizing, concurrency lockout).
    - `thermal_energy_system_requirements.md`: Phase 1 vs Phase 2 boundaries, equipment feedback decoupling ($U_{vent\_ON}$), data quality contracts, and non-functional invariants.
    - `thermal_energy_system_architecture.md`: Clean Architecture component boundaries, abstract interface `i_thermal_energy_t`, coherent capture cache, 720h RAM ring buffer, and decoupled consumer query port.
    - `thermal_energy_system_configuration.md`: Kconfig ADC channel selection for 4-20mA humidity, EEPROM memory layout (allocating domains `CONFIG_DOMAIN_THERMAL_SITE` and `BASELINE`), and exact 64-byte MicroSD wire record format.
    - `thermal_energy_system_mathematical_model.md`: Exhaustive algorithm-by-algorithm mapping, compressor stabilization FSM, Tikhonov regularized OLS in 64-bit `double`, and floating-point safety guards.
    - `thermal_energy_system_test_plan.md`: Test pyramid covering unit test suites on `native_sim`, integration Zbus synchronization tests, and HIL validation on STM32H743.
- **2026-11-09**: Redesigned internal ADC acquisition and RTD temperature conversion pipeline:
  - Fixed 16-bit ADC sample data type bug (`uint16_t` 0..65535 instead of `int16_t` in `adc_dma_engine.h`, `adc_dma_engine.c`, and `adapter_sensor_zephyr.c`) preventing false OPEN/INVALID from sign extension.
  - Implemented burst oversampling (16 samples per channel) with min/max rejection in `adc_dma_engine.c` to eliminate single-shot EMI noise and aliasing.
  - Added 1st-order bilinear IIR low-pass filter (Fc = 1.0 Hz, seeded with first sample for zero startup transient) in `adapter_sensor_zephyr.c` across all 5 RTD and 10 J9 channels.
  - Hardened RTD open probe detection in `transducer_rtd.c` (`adc_raw >= 64000.0f` or `a >= 3.25f`) and distinguished open circuit from out-of-range temperatures with [-25°C, -20°C] clamping in `sensor_matrix_mapper.c`.
  - Added unit test suite `test_adapter_sensor_zephyr.c` and extended `test_sensor_matrix_mapper.c` and `test_rtd_open_probe_detection`. All 51 sensor tests and all library tests pass 100%.
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
