# Thermal & Energy System Audit Findings & Deficiencies

**Date:** 2026-11-11  
**Scope:** `app/src/application/active_objects/ao_thermal_energy.c`, `sensor_matrix_mapper.c`, `predictive_analytics.c`, `composition_root.c`, and interfaces  
**Status:** Resolved (All 5 Issues Remediated & Verified)  
**Review Basis:** Comprehensive Post-Implementation Architectural & Mathematical Audit (REVF Baseline)

## Summary Table

| ID | Area | Finding Summary | Priority | Status |
|---|---|---|---|---|
| THERMAL-ISSUE-001 | Integration / Modes | Operational mode (telemetry vs hybrid) is omitted, causing wrong digital contact mapping | P0 | Resolved |
| THERMAL-ISSUE-002 | Concurrency | `ao_thermal_energy_feed_energy_reading()` races with `ao_modbus` without mutex protection | P1 | Resolved |
| THERMAL-ISSUE-003 | Analytics / F-04 | Filter slope $m_{T,k}$ is hardcoded to `0.0f`, locking Days Remaining to 90d healthy ceiling | P1 | Resolved |
| THERMAL-ISSUE-004 | Data Quality | Telemetry freshness is not evaluated; frozen/unplugged sensors remain `QUALITY_VALID` indefinitely | P2 | Resolved |
| THERMAL-ISSUE-005 | Analytics / OLS | OLS moment matrix accumulates instantaneous second 3599 sample instead of integrated hourly average | P2 | Resolved |

---

## THERMAL-ISSUE-001 — Operational mode is omitted, causing wrong digital contact mapping

**Priority:** P0 (Critical)  
**Evidence:** 
- `app/src/application/active_objects/ao_thermal_energy.c:537`: `memset(&src, 0, sizeof(src));` sets `src.hvac_mode = 0` (`HVAC_MODE_TELEMETRY_ONLY`).
- `app/src/infrastructure/di/composition_root.c:1623`: `ao_thermal_energy_init()` does not pass `hvac_operation_mode_t`.
- `app/src/infrastructure/adapters/sensor_matrix_mapper.c:200-244`: In `HVAC_MODE_TELEMETRY_ONLY`, compressor/fan contacts are mapped from local pins `ALM_IN_1..16`.

**Impact:** When operating in `HVAC_MODE_HYBRID_FULL` or `HVAC_MODE_CONTROL_ONLY`, inputs `ALM_IN_1..16` are dedicated local high-pressure (HP), low-pressure (LP), flow, and voltage alarm cutouts. Because `ao_thermal_energy` defaults to telemetry mode, it mistakes pressure alarms for compressor/fan running contacts, while ignoring the real contacts coming over Modbus `MBSL32DI`.

**Remediation:**
1. Pass `hvac_operation_mode_t mode` in `ao_thermal_energy_init()` and store `self->hvac_mode`.
2. Add `ao_thermal_energy_set_hvac_mode(ao_thermal_energy_t *self, hvac_operation_mode_t mode)` for runtime profile switches.
3. In `ao_thermal_energy_step()`, set `src.hvac_mode = self->hvac_mode`.
4. In `composition_root_init_thermal_energy()`, pass `(hvac_operation_mode_t)self->hvac.config.operation_mode`.

---

## THERMAL-ISSUE-002 — Race condition in electrical telemetry injection

**Priority:** P1 (High)  
**Evidence:** `app/src/application/active_objects/ao_thermal_energy.c:354-364`:
`ao_thermal_energy_feed_energy_reading()` is executed by `ao_modbus` (priority 6). It copies `self->latest_elec_reading = *reading;` without acquiring `result_mutex`.
At the same time, `ao_thermal_energy` (priority 8) reads `self->latest_elec_reading` in `ao_thermal_energy_step()`.

**Impact:** Because `energy_reading_t` is > 48 bytes, a preemptive thread switch during the memcpy causes torn reads of electrical power and energy metrics.

**Remediation:**
Acquire and release `k_mutex_lock(&self->result_mutex, K_FOREVER)` inside `ao_thermal_energy_feed_energy_reading()`. Also lock while copying into `src.elec_reading` in `ao_thermal_energy_step()`.

---

## THERMAL-ISSUE-003 — Air filter slope $m_{T,k}$ hardcoded to 0.0f

**Priority:** P1 (High)  
**Evidence:** `app/src/application/active_objects/ao_thermal_energy.c:704`:
`predictive_calc_filter_days_remaining(..., 0.0f);` passes `0.0f` as `slope_c_per_day`.
In `predictive_analytics.c:202`, `0.0f >= -0.05f` always evaluates to true, taking the "healthy flat/clean" branch and returning 90 days.

**Impact:** Active coil freeze or heavy air filter loading is never diagnosed with a decreasing days-remaining countdown in production.

**Remediation:**
Calculate the daily temperature degradation slope $m_{T,k} = rac{T_{inj}(now) - T_{inj}(24h\_ago)}{1.0	ext{ day}}$ by querying the 720-hour RAM circular buffer `history` if at least 24 hours have elapsed. If fewer than 24 hours exist, pass 0.0f safely.

---

## THERMAL-ISSUE-004 — Stale telemetry is not detected

**Priority:** P2 (Medium)  
**Evidence:** `app/src/application/active_objects/ao_thermal_energy.c:520-555`:
If `zbus_chan_read(&chan_sensor_snapshot)` returns `-ENOMSG` or the bus stops publishing, `self->has_sensor_snapshot` remains `true` with the old timestamp. The calculation continues running with frozen values without flagging `QUALITY_SUSPECT` or `QUALITY_FAILED`.

**Impact:** Silent calculation on stale sensor data during bus disconnection or sensor module freeze violates REQ-VAL-02.

**Remediation:**
Check age: if `k_uptime_get() - self->last_sensor_snapshot.timestamp_ms > 2000`, mark thermal inputs as stale and degrade quality. If electrical reading timestamp is older than 3000 ms, mark electrical quality as stale.

---

## THERMAL-ISSUE-005 — OLS accumulates instantaneous sample instead of integrated hourly average

**Priority:** P2 (Low / Improvement)  
**Evidence:** `app/src/application/active_objects/ao_thermal_energy.c:476-480`:
`ao_thermal_energy_record_hour()` passes `self->latest_result.t_ret_mean`, `self->latest_result.t_outdoor_effective`, etc., which are the readings from the final second (second 3599) of the hour.

**Impact:** Brief transient spikes occurring precisely at the hour boundary pollute the 168-hour regression matrix.

**Remediation:**
Maintain running hourly accumulators (`hourly_sum_t_ret`, `hourly_sum_t_ext`, `hourly_sum_f_carga`, `hourly_sum_q_total`, `hourly_sample_count`) inside `ao_thermal_energy_t`. At the 3600-second mark, divide by the valid sample count to feed the exact integrated hourly average into `ols_model_accumulate()` and `thermal_hourly_record_t`.
