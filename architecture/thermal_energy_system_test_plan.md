# Thermal and Energy System: Test Plan

## Unit tests

| Area | Cases | Acceptance |
|---|---|---|
| Topology validation | unknown/duplicate IDs, incomplete pair, invalid association, unassigned optional sensors | Reject invalid config with no partial topology. |
| Quality propagation | invalid, stale, disconnected, unavailable and out-of-domain inputs | Output identifies the cause; no physical-looking zero is emitted. |
| RH conversion | 3.4, 3.5, 4, 12, 20, 20.1 mA; calibration | Loop break and range boundaries match policy; 4–20 maps to 0–100 %. |
| Psychrometrics | nominal reference vectors, extreme T/RH/altitude, nonfinite values | Results meet reference tolerance or become invalid safely. |
| Capacity/COP | dry and condensing regimes, no fans, unknown fan, zero power | No capacity without confirmed airflow; no division by zero. |
| Stabilization | OFF→inrush→capture→warmup→steady, restart <180 s | Gates and short-cycle count meet REVF timing. |
| OLS | 168 samples, fewer samples, singular matrix, regularization/reference beta | Training status is explicit; stable reference coefficients within approved tolerance. |
| Diagnostics | each threshold, missing baseline, clean filter precedence, no pressure | Correct quality and initialization codes; no pressure diagnosis. |

## Integration tests

1. Feed fake CWT/internal ADC frames, fake JSY readings and fake debounced DI snapshots through the actual AO boundaries; assert a coherent generated result.
2. Make one source older than the coherence/staleness threshold; assert only dependent metrics invalidate.
3. Verify one JSY poll path supplies both existing voltage alarm handling and thermal capture without duplicate Modbus requests.
4. Switch hardware/profile configuration and ensure only the selected J9 channel is interpreted as RH.
5. Read results concurrently while updates occur; each returned snapshot has one generation ID and no torn fields.
6. Corrupt persisted topology/baseline envelopes; assert fallback/default behavior and no use of corrupt learned values.

## Reference and system validation

- Generate independent reference vectors for Magnus, humidity ratio, enthalpy, capacity, Carnot COP and OLS using a reviewed desktop calculation.
- Run target timing/stack measurements with the final floating-point ABI and worst-case topology.
- Commission hardware contacts individually: confirm each mapped fan/compressor input and its polarity, 500 ms debounce behavior, timestamp age, and the correspondence between physical HVAC unit and thermal pairs.
- Verify RH loop current with calibrated injected currents and confirm the actual J9 shunt value.

## Acceptance criteria

The subsystem is ready for implementation review when every result contract has validity semantics, every REVF formula has a named input source and units, unavailable data cannot produce a valid metric, all Phase-2 pressure logic remains disabled, and the test suite covers nominal, boundary, corrupted configuration, stale input and concurrency cases. Architecture approval is still required before implementation.

