# Thermal and Energy System: Existing Implementation Audit

## Scope and evidence

This audit covers `doc/request/Mathematical_Model_Specification_REVF.md` (REVF) and the current BrightC-telemetry tree. It is an architecture review only; no firmware behavior is changed.

| Capability | Current implementation | State | Reuse / required change |
|---|---|---|---|
| PT100 acquisition | CWT-TM16PT is polled by `ao_modbus`; internal RTD/J9 channels are read by the ADC DMA engine and normalized by `sensor_matrix_mapper`. | Implemented | Reuse both paths and their validity masks. |
| Sensor identity and roles | `sensor_hardware_map.h` provides immutable flat IDs; `sensor_channel_cfg_t` assigns role, target index, calibration, and HVAC association. | Implemented, partial for REVF topology | Reuse IDs/configuration. Add a site topology validator rather than a second catalog. |
| Humidity 4–20 mA | J9 channels are present; mapper converts a configured `SENSOR_ROLE_HUMIDITY_RETURN` channel to percent RH. Default uses J9-1. | Partial | Expose selected J9 channel through Kconfig and retain mapper conversion. Validate the shunt/scaling contract and the <3.5 mA fault threshold. |
| Energy meter | `ao_modbus` decodes JSY-MK-339 into `energy_reading_t`: power, voltage, current, PF, energy, frequency, timestamp and `is_valid`. | Implemented, not published for REVF | Reuse the single polling path; add an internal snapshot consumer/cache owned by the thermal AO. Do not add a driver. |
| Fan/compressor feedback | `digital_input_types.h` defines fan and compressor roles; `hardware_pin_map.h` maps four fan contacts to MBSL32DI 16–19 and four compressor contacts to 24–27. AO scans at 20 ms and aggregation supports 500 ms debounce. | Implemented, not consumed by mathematics | Reuse only confirmed debounced roles. Extend semantic roles for units 5–8 before claiming full eight-unit coverage. |
| Thermal frame publication | `ao_sensor_matrix` publishes Zbus `chan_sensor_snapshot`; the richer `thermal_matrix_frame_t` already has pair, room, outdoor, humidity and validity fields. | Partial | Feed the rich frame or an equivalent normalized snapshot to the future thermal AO; the flat snapshot drops humidity and role information. |
| Persistent configuration | Microconf EEPROM adapter uses typed domains, versioned envelope and CRC. `CONFIG_DOMAIN_CALIBRATION` and `CONFIG_DOMAIN_HVAC` exist. | Implemented | Extend domains after size/migration analysis; do not create a second EEPROM manager. |
| History / OLS / diagnostics | No REVF thermal calculation, compressor stabilization FSM, history ring, OLS, or result contract exists. | Missing | New pure domain services and one coordinating AO are needed. |

## Hardware and profile findings

The board DTS declares ADC DMA logical slots 0–4 for five internal RTDs and 5–14 for J9-1..J9-10. It does not assign a humidity function; that decision currently occurs in `sensor_matrix_init_default_pilot_config()`, which hardcodes J9-1. Kconfig currently selects Modbus devices and addresses, but has no humidity-channel option.

The default sensor mapper assigns CWT CH1..6 as supply, CH7..12 as return, CH13..14 as room and CH15..16 as outdoor. This agrees with REVF's 6+6+2+2 acquisition path. However, the immutable catalog assigns different initial roles for CWT IDs 10..23, and the mapper uses room target indices 5 and 6 while internal RTDs use 0..4. The architecture must make the runtime/site topology authoritative and test it against the catalog instead of relying on comments alone.

The default hybrid HVAC profile uses only four controlled units and feeds back fan/compressor contacts for those units. Its HVAC temperature range is internal RTDs 1..5, not the 16-probe REVF pilot. This is a profile mismatch, not evidence that REVF measurements are available in the active profile.

## Integration gaps and impact

| Element | Current situation | Required architectural change | Risk | Tests |
|---|---|---|---|---|
| Sensor snapshot | Flat Zbus snapshot carries temperature values/mask but no humidity, pair mapping or per-value age. | Publish/consume a normalized thermal capture with all quality metadata. | High: calculations could mix unrelated values. | Coherent capture, stale input, missing pair. |
| JSY reading | Callback currently feeds voltage alarms only. | Route the existing decoded reading to thermal capture with its timestamp and validity. | High: COP cannot be trusted otherwise. | valid/invalid/stale power and unit scale tests. |
| Digital feedback | Contacts exist, but no mathematical consumer; channels 20..31 use generic roles for parts of units 5..8. | Build a confirmed/unknown/invalid feedback snapshot; validate site mapping. | High: `U_vent_ON` must never be inferred. | debounce, mapping, unknown state and short-cycle tests. |
| Humidity scaling | Mapper calls a generic current-loop converter with no explicit site shunt in the call. | Make electrical calibration explicit and validate loop-break threshold. | High: latent capacity depends on it. | 3.5/4/20 mA boundaries. |
| FPU assertion | REVF claims a double-precision FPU. Current primary `.config` has `# CONFIG_FPU is not set`; observed compiler command has Cortex-M7 but no hard-FPU flag. | Benchmark/verify target toolchain before selecting OLS precision policy. | BLOCKER for the stated performance premise. | ABI/config inspection and reference-vector timing. |

## Engineering observations

- **BLOCKER — requirements conflict:** task §3.2 first excludes HVAC feedback, then its later Phase-1 section requires fan/compressor contacts. Code and REVF support the latter for four units. The proposed design treats each contact as `confirmed`, `unknown`, or `invalid`; it does not assume all contacts are installed.
- **HIGH — REVF data record sizing:** the listed 64-byte hourly record fields add to more than 64 bytes under the stated widths. Define and assert one packed wire format before implementation.
- **HIGH — compressor step attribution:** a single total meter can attribute a 15-second step only when no other fan/compressor changes in the capture window. Otherwise the signature is unavailable.
- **MEDIUM — Phase 2 pressure:** mapper already has pressure roles, but no approved physical signal or Phase-1 calculation may use them.
- **MEDIUM — external time:** current acquisition timestamps are monotonic; REVF's hourly historical records need an explicit wall-clock validity policy.

