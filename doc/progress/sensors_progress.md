# Sensors & Mathematical Model Subsystem Progress

- **Status**: **IN_PROGRESS (50%)**
- **Architecture Reference**: [`doc/architecture/sensor_subsystem.md`](../architecture/sensor_subsystem.md)

---

## 1. Implemented Components

| Component | File Path | Status | Description |
| :--- | :--- | :---: | :--- |
| **Sensor Types** | `app/include/common/sensor_types.h`, `energy_types.h` | 100% | Data structures for RTDs, energy meters, pressure, humidity; multi-concentrator Modbus (up to 4 CWT concentrators = 64 channels) and Revision F limits (16 pairs, 16 room probes, 8 outdoor probes) |
| **Domain Interfaces** | `app/include/interfaces/i_sensor_matrix.h`, `i_energy_meter.h`, `i_temperature_bank.h` | 100% | Pure abstract ports for sensor ingestion |
| **Matrix Mapper** | `app/src/domain/services/sensor_matrix_mapper.c` | 100% | Normalizes raw channel data across multi-concentrator CWTs and internal/J9 channels to unified sensor matrix |
| **Modbus Codecs** | `app/src/domain/services/cwt_tm16pt_codec.c`, `jsy_mk339_codec.c` | 100% | Unpacking of 16 RTD channels and 3-phase AC energy parameters |

---

## 2. Pending Work
- [ ] Implement `adapter_temperature_bank.c` linking Modbus/internal sensors to `i_temperature_bank_t`.
- [ ] Implement `adapter_energy_meter.c` linking JSY-MK-339 to `i_energy_meter_t`.
- [ ] Implement Mathematical Model Revision F (`Mathematical_Model_Specification_REVF.md`): psychrometric enthalpy, thermal capacity, sensible/latent heat, COP, compressor degradation.
- [ ] Implement ADC driver for 5 on-board PT100 channels (INA826) and 4-20mA current loop inputs (J9).

---

## 3. Changelog & Refactoring History
- **2026-11-06**: Implemented multi-concentrator Modbus RTU CWT support (up to 4 concentrators / 64 Modbus channels) and expanded Revision F telemetry capacity:
  - 16 paired supply/return couples (`SENSOR_MATRIX_MAX_PAIRS = 16U`).
  - 16 room ambient monitoring probes (`SENSOR_MATRIX_MAX_ROOM_PROBES = 16U`) with `uint16_t room_valid_mask`.
  - 8 outdoor ambient condensing probes (`SENSOR_MATRIX_MAX_OUTDOOR_PROBES = 8U`).
  - Extended physical channel enumeration up to `PHYS_CH_MAX = 79` and inline routing helper `physical_channel_is_cwt()`.
  - Full unit test coverage in `test_sensor_types.c` and `test_sensor_matrix_mapper.c` (`test_multi_cwt_concentrator_mapping`).
- **2026-11-05**: Defined abstract interfaces and initial codecs.
