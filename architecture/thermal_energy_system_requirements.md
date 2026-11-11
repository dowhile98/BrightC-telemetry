# Thermal and Energy System: Phase-1 Requirements

## Scope

Phase 1 implements REVF §§3–5.4 using available temperature, return-humidity, JSY electrical, and confirmed digital feedback. Pressure acquisition, pressure-derived equations, and pressure diagnoses remain Phase 2 provisions. A calculated value shall never stand in for an absent measurement.

## Functional requirements

1. Consume temperature readings through the existing sensor matrix and preserve per-reading validity, source timestamp, configuration identity, and age.
2. Map supply/return pairs, room probes, outdoor probes, and their HVAC unit association by persisted sensor IDs. `0` means unassigned. A pair is valid only when both assigned IDs are distinct, valid, fresh and healthy.
3. Convert one Kconfig-selected J9 physical channel from current to RH outside the mathematical engine. Below 3.5 mA is loop failure; 3.5–4.0 mA is out of calibrated range; 4–20 mA maps to 0–100 % with field calibration.
4. Consume the existing JSY `energy_reading_t`; use `total_active_power_kw` only when valid, finite, positive where a denominator is required, and temporally coherent with temperatures.
5. Derive `U_vent_ON` only from debounced fan auxiliary contacts. Compressor state is separately derived from compressor contacts. Missing mapping, disabled channel, source communication failure, or stale input produces `unknown` or `invalid`, never `off`.
6. Publish an immutable latest result snapshot and an update notification. Consumers query results; they do not access physical ports or recompute formulas.
7. Persist topology, site/calibration values and approved baselines through the current versioned storage system. Keep high-rate samples and hourly history out of EEPROM.

## Quality-state contract

Every input and output carries one of: `VALID`, `LIMITED` (computed with declared assumptions), `UNAVAILABLE`, `STALE`, `INVALID`, or `OUT_OF_DOMAIN`. Initialization is therefore distinguishable from a physical zero. A calculation is valid only when all mandatory input quality states are valid and timestamps lie within a configured coherence window.

## REVF traceability and availability

| REVF | Deliverable | Phase-1 condition |
|---|---|---|
| 3 | Compressor timing, inrush exclusion, step signature | Requires confirmed compressor feedback and coherent meter snapshots. |
| 4.1 | Pair deltas, means, room/outdoor metrics | Available from valid thermal topology; fan-filtered means require confirmed fan state. |
| 4.2 | RTD/current conversions | Existing acquisition performs conversion; humidity channel selection/calibration must be added. |
| 4.3–4.4 | Psychrometrics, mass flow, sensible/latent/total capacity | Require valid return temperature, RH, altitude, complete pairs, nominal airflow and confirmed fan state. |
| 4.5 | COP and exergy | Require valid capacity, positive total power, outdoor reference and valid pinch configuration. |
| 5.1 | OLS baseline/degradation | Requires 168 hours of steady state plus valid load fraction and COP inputs. |
| 5.2 | hotspot, bypass, infiltration, filter trend, short cycle | Each rule only evaluates when its explicit inputs are valid. |
| 5.4 | weather-corrected reserve | Requires nominal capacities, required redundancy and a valid degradation result. |

## Non-functional requirements

- Pure algorithms remain in the domain layer without Zephyr headers or hardware access.
- Acquisition callbacks remain brief; no OLS inversion, filesystem operation or transport operation executes there.
- All buffers are fixed-size, bounded by Kconfig maxima; no runtime allocation.
- Numerical operations reject nonfinite values, invalid logarithm/exponential domains, negative absolute temperatures, nonpositive denominators, and impossible pressure/humidity ratios.
- The design is extensible with pressure inputs later by adding optional normalized measurements, without changing existing calculation contracts.

