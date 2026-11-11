# Thermal and Energy System: Comprehensive Verification & Test Plan

- **Document Identifier**: `doc/architecture/thermal_energy_system_test_plan.md`
- **Subsystem**: Thermal and Energy System (`thermal_energy`)
- **Target Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Host Simulation**: `native_sim` (Zephyr Ztest Framework)
- **Reference Specification**: `doc/request/Mathematical_Model_Specification_REVF.md` (Revision F Baseline)
- **Author**: Lead Embedded Firmware Architect
- **Status**: Stage 1 Architecture Deliverable (Ready for Technical Review)

---

## 1. Executive Summary & Testing Hierarchy

The testing architecture for the Thermal and Energy System follows the **Test Pyramid** principle, enforcing validation at three distinct levels:

```
                            ┌────────────────────────┐
                            │ Hardware-in-the-Loop   │
                            │ (HIL on STM32H7 Board) │
                            ├────────────────────────┴───┐
                            │ Integration Tests          │
                            │ (Multi-AO & Zbus Routing)  │
                        ┌───┴────────────────────────────┴───┐
                        │ Unit Tests (Ztest on native_sim)   │
                        │ Pure Math, FSM, Mapping & Filters │
                        └────────────────────────────────────┘
```

> [!IMPORTANT]
> **Cardinal Testing Invariants**:
> 1. **Zero Hardware Dependencies for Domain Tests**: All thermodynamic balances, psychrometrics, exergy calculations, and OLS regressions must execute on Linux `native_sim` without touching physical hardware.
> 2. **Golden Reference Vectors**: Mathematical algorithms must be validated against verified external reference calculations (NIST REFPROP / ASHRAE Fundamentals) with exact precision tolerances.
> 3. **Defensive Boundary Assertion**: Every test suite must prove that invalid inputs, disconnected probes, out-of-range loops (< 3.5 mA), and singular matrices produce safe error flags without division by zero, NaN propagation, or thread lockup.

---

## 2. Unit Testing Specification (`native_sim` / Ztest)

Unit tests execute in `tests/unit/thermal_energy/` under Zephyr's Ztest framework.

### 2.1 Suite 1: Topology Mapping & Index Routing (`test_thermal_topology.c`)

| Test Case Identifier | Stimulus & Condition | Expected Behavior & Assertion |
| :--- | :--- | :--- |
| `test_topology_valid_pilot_mapping` | Supply pilot $6+6+2+2$ configuration with valid flat IDs (1..59). | `sensor_topology_validate()` returns `0`; pairs $0 \dots 5$, room probes $0 \dots 1$, and outdoor probes $0 \dots 1$ bind correctly. |
| `test_topology_reject_unregistered_id` | Assign flat sensor ID `99` (outside catalog). | Configuration rejected with `-EINVAL`; previous valid topology preserved. |
| `test_topology_reject_identical_pair_ids` | Assign `supply_flat_id = 10` and `return_flat_id = 10` for pair 0. | Rejected with `-EINVAL`; pair fails consensus check. |
| `test_topology_reject_duplicate_roles` | Assign flat ID `15` as both supply pair 2 and room probe 0. | Rejected with `-EINVAL`; cross-role collision detected. |
| `test_topology_incomplete_pair_handling` | Configure pair 3 with valid supply probe but `return_flat_id = 0` (unassigned). | Configuration accepted, but pair 3 `pair_valid_mask` bit remains `0`; pair delta $\Delta T_3$ marked `QUALITY_UNAVAILABLE`. |
| `test_topology_disconnected_probe_propagation` | Disconnect return probe on pair 1 (`is_open = true` in raw bank). | `pair_open_probe_mask` asserts bit 1; `pair_valid_mask` deasserts bit 1; $Q_{sensible,1}$ marked `QUALITY_INVALID`. |
| `test_topology_temperature_out_of_bounds` | Inject $T_{ret,0} = 155.0\,^\circ\text{C}$ (exceeds $150.0\,^\circ\text{C}$ sanity bound). | Reading marked invalid; dependent pair calculations flagged `QUALITY_OUT_OF_DOMAIN`. |

### 2.2 Suite 2: 4–20 mA Return Humidity Transducer (`test_humidity_transducer.c`)

| Test Case Identifier | Injected Current ($I_{loop}$) | Expected Output & Quality Status |
| :--- | :--- | :--- |
| `test_humidity_loop_break_fault` | $I_{loop} = 0.0\text{ mA}$ (disconnected) | $HR_{ret} = 0.0\%$, `quality = QUALITY_INVALID`, Loop Break alarm condition asserted. |
| `test_humidity_loop_sub_minimum` | $I_{loop} = 3.2\text{ mA}$ (< 3.5 mA NAMUR fault) | `quality = QUALITY_INVALID`, Loop Break alarm asserted. |
| `test_humidity_lower_margin_clamp` | $I_{loop} = 3.7\text{ mA}$ (3.5 to 4.0 mA margin) | $HR_{ret} = 0.0\%$, `quality = QUALITY_LIMITED`. |
| `test_humidity_exact_zero_scale` | $I_{loop} = 4.0\text{ mA}$ | $HR_{ret} = 0.0\%$, `quality = QUALITY_VALID`. |
| `test_humidity_mid_scale_reading` | $I_{loop} = 12.0\text{ mA}$ | $HR_{ret} = 50.0\% \pm 0.1\%$, `quality = QUALITY_VALID`. |
| `test_humidity_exact_full_scale` | $I_{loop} = 20.0\text{ mA}$ | $HR_{ret} = 100.0\% \pm 0.1\%$, `quality = QUALITY_VALID`. |
| `test_humidity_upper_margin_clamp` | $I_{loop} = 20.5\text{ mA}$ (20.0 to 21.0 mA margin)| $HR_{ret} = 100.0\%$, `quality = QUALITY_LIMITED`. |
| `test_humidity_over_current_fault` | $I_{loop} = 22.0\text{ mA}$ (> 21.0 mA) | $HR_{ret} = 100.0\%$, `quality = QUALITY_INVALID`, Over-current alarm asserted. |

### 2.3 Suite 3: Psychrometric & Airflow Properties (`test_psychrometrics.c`)

| Test Case Identifier | Reference Inputs | Expected Outputs & Tolerances |
| :--- | :--- | :--- |
| `test_psychrometric_sea_level_nominal` | $Z = 0\text{ m}$, $\overline{T}_{ret} = 24.0\,^\circ\text{C}$, $HR_{ret} = 50.0\%$ | $P_{atm} = 101.325\text{ kPa}$, $P_{ws} = 2.985\text{ kPa} \pm 0.005$, $P_w = 1.493\text{ kPa}$, $\omega_{ret} = 0.00931\text{ kg/kg} \pm 0.00005$, $T_{rocio} = 12.93\,^\circ\text{C} \pm 0.05$, $\rho_{da} = 1.170\text{ kg/m}^3 \pm 0.005$. |
| `test_psychrometric_high_altitude_bogota` | $Z = 2600\text{ m}$, $\overline{T}_{ret} = 22.0\,^\circ\text{C}$, $HR_{ret} = 60.0\%$ | $P_{atm} = 74.05\text{ kPa} \pm 0.1$, $P_{ws} = 2.645\text{ kPa}$, $P_w = 1.587\text{ kPa}$, $\omega_{ret} = 0.01362\text{ kg/kg} \pm 0.00005$, $\rho_{da} = 0.854\text{ kg/m}^3 \pm 0.005$. |
| `test_psychrometric_dry_regime` | $\overline{T}_{ret} = 24.0\,^\circ\text{C}$, $HR_{ret} = 40.0\%$ ($T_{rocio} = 9.3\,^\circ\text{C}$), $T_{inj} = 14.0\,^\circ\text{C}$ | Dry regime detected ($T_{inj} > T_{rocio}$): $\omega_{inj} = \omega_{ret}$, $Q_{latente} = 0.0\text{ kW}$, $SHF = 1.00$. |
| `test_psychrometric_condensing_regime` | $\overline{T}_{ret} = 24.0\,^\circ\text{C}$, $HR_{ret} = 65.0\%$ ($T_{rocio} = 17.0\,^\circ\text{C}$), $T_{inj} = 11.0\,^\circ\text{C}$ | Condensation regime detected: $\omega_{inj} = 0.00795\text{ kg/kg} < \omega_{ret} (0.01235)$, $Q_{latente} > 0.0\text{ kW}$, $\dot{m}_{cond} > 0.0\text{ L/h}$, $SHF < 0.85$. |
| `test_psychrometric_zero_airflow_lockout`| All fans confirmed off ($U_{vent\_ON} = \emptyset$). | $\dot{m}_{da,total} = 0.0\text{ kg/s}$, $Q_{sensible} = 0.0\text{ kW}$, $Q_{total} = 0.0\text{ kW}$, quality flags set to `QUALITY_UNAVAILABLE`. |

### 2.4 Suite 4: Thermodynamic Balances & Exergy (`test_thermodynamics.c`)

| Test Case Identifier | Reference Inputs | Expected Outputs & Tolerances |
| :--- | :--- | :--- |
| `test_cop_nominal_operation` | $Q_{total} = 42.0\text{ kW}$, $P_{elec\_total} = 12.0\text{ kW}$ | $COP_{real} = 3.500 \pm 0.001$, `quality = QUALITY_VALID`. |
| `test_cop_zero_power_division_guard` | $Q_{total} = 20.0\text{ kW}$, $P_{elec\_total} = 0.0\text{ kW}$ | Division by zero intercepted: $COP_{real} = 0.0$, `quality = QUALITY_UNAVAILABLE`. |
| `test_carnot_and_exergy_nominal` | $\overline{T}_{inj} = 12.0\,^\circ\text{C}$, $\overline{T}_{ext} = 30.0\,^\circ\text{C}$, $COP_{real} = 3.50$ | $T_{evap,sat} = 277.15\text{ K}$, $T_{cond,sat} = 313.15\text{ K}$, $COP_{Carnot} = 7.698 \pm 0.005$, $\eta_{II} = 45.46\% \pm 0.05\%$. |
| `test_carnot_extreme_cold_ambient` | $\overline{T}_{ext} = 5.0\,^\circ\text{C}$, $\overline{T}_{inj} = 14.0\,^\circ\text{C}$ ($T_{cond} < T_{evap}$) | Minimum lift clamp ($\max(5.0\text{ K}, T_{cond} - T_{evap})$) prevents negative or infinite Carnot COP. |

### 2.5 Suite 5: Compressor Stabilization State Machine (`test_compressor_fsm.c`)

| Test Case Identifier | Stimulus Sequence | Expected State Transitions & Timers |
| :--- | :--- | :--- |
| `test_fsm_power_on_initialization` | Initial boot; contact open. | FSM enters `STATE_OFF`; $t_{off}$ accumulates. |
| `test_fsm_short_cycle_increment` | Contact asserts when $t_{off} = 90\text{ s}$ (< 180 s). | Short-cycle counter increments by 1; FSM transitions to `STATE_INRUSH_WAIT`. |
| `test_fsm_inrush_suppression` | Time advances from $t_{on} = 0\text{ s}$ to $t_{on} = 14\text{ s}$. | FSM remains in `STATE_INRUSH_WAIT`; active power samples discarded. |
| `test_fsm_step_load_capture_success`| Time reaches $t_{on} = 15\text{ s}$; no other motors active. | $\Delta P_{comp,c} = P(15\text{s}) - P(0^-)$ captured; transitions to `STATE_THERMAL_WARMUP`. |
| `test_fsm_step_load_concurrency_lockout`| Second compressor starts at $t_{on} = 10\text{ s}$. | Concurrency conflict detected: $\Delta P_{comp,c}$ marked `UNAVAILABLE`. |
| `test_fsm_steady_state_entry` | Time reaches $t_{on} = 180\text{ s}$. | Transitions to `STATE_STEADY_STATE`; OLS and history accumulation enabled. |

### 2.6 Suite 6: Double-Precision OLS & Diagnostics (`test_predictive_ols.c`)

| Test Case Identifier | Stimulus & Condition | Expected Behavior & Assertion |
| :--- | :--- | :--- |
| `test_ols_initialization_lockout` | Accumulated steady-state hours $N_{hours} = 50\text{ h}$ (< 168 h). | `ols_trained = false`; Filter Days Remaining locked to initialization code `100 days` (`0xFFFF` in Modbus). |
| `test_ols_168h_training_convergence` | Feed 168 synthetic steady-state hourly vectors with known $\beta = [5.0, 1.2, -0.4, 15.0]$. | Matrix solved; computed $\beta$ matches reference within $\pm 0.05$; `ols_trained = true`. |
| `test_ols_singular_matrix_regularization`| Feed 168 vectors with identical constant return temperature ($23.0\,^\circ\text{C}$). | Without regularization, $\det(X^T X) \approx 0$; with Tikhonov $\lambda = 10^{-6}$, matrix successfully inverts without NaN or crash. |
| `test_filter_days_precedence_clean` | Filter loading $\%\text{Filter} = 15\%$ (< 25%), slope $m_T = 0.0\,^\circ\text{C/day}$. | Precedence Condition 2 asserts: clamped to healthy ceiling `90 days`. |
| `test_filter_days_active_degradation`| Loading $\%\text{Filter} = 40\%$, $T_{inj} = 10.5\,^\circ\text{C}$, slope $m_T = -0.15\,^\circ\text{C/day}$. | Condition 3 evaluates rate: $\text{Days} = (10.5 - 7.5) / 0.15 = 20\text{ days}$. |
| `test_refrigerant_leak_decoupled` | Outdoor temp rises to $38\,^\circ\text{C}$; $\eta_{II}$ remains at $44\%$ (Carnot drops naturally). | $\%\text{Leak} = 0.0\%$; zero false alarms during heatwave. |
| `test_refrigerant_leak_true_loss` | Outdoor temp $25\,^\circ\text{C}$; $\eta_{II}$ collapses to $20\%$, $\Delta T$ collapses. | $\%\text{Leak} > 80\%$; predictive leak alarm triggered. |

---

## 3. Integration Testing Specification (Zephyr RTOS)

Integration tests verify multi-threaded interaction, Zbus routing, and memory safety:

1. **End-to-End Ingestion Pipeline**:
   - Inject simulated physical frames from `adapter_sensor_zephyr` and Modbus JSY mock.
   - Verify that `ao_thermal_energy` wakes up, evaluates the coherent snapshot, and publishes `chan_thermal_energy_update`.
2. **Snapshot Coherence & Staleness Rejection**:
   - Introduce an intentional $2500\text{ ms}$ delay on electrical power updates while temperatures continue at 1 Hz.
   - Assert that thermal capacities remain valid, but $COP_{real}$ and exergy gracefully transition to `QUALITY_STALE`.
3. **Concurrent Query Port Thread Safety**:
   - Spawn 4 competing consumer threads (simulating HMI, Modbus TCP, SNMP, and REST API) querying `i_thermal_energy_get_latest_result()` in tight loops.
   - Assert that no torn reads occur, generation IDs are consistent, and mutex contention never delays the 1 Hz calculation thread by $> 5\,\mu\text{s}$.
4. **Persistent Storage Recovery & Corrupt Envelope Handling**:
   - Write known baseline signatures to EEPROM via `adapter_storage_microconf`.
   - Corrupt the CRC-16 of Slot A; reboot the container.
   - Assert that the storage engine detects the corruption, falls back to Slot B, and loads valid baselines without firmware panic.

---

## 4. Hardware-in-the-Loop (HIL) & Physical Board Validation

Physical verification on the DAECGE BrightC Controller V3 (STM32H743ZIT6 @ 480 MHz):

1. **Current Loop Transducer Calibration on Connector J9**:
   - Connect a calibrated 4–20 mA precision current calibrator to J9-1 (PF14).
   - Inject steps: $0.0\text{ mA}$, $3.4\text{ mA}$, $4.0\text{ mA}$, $8.0\text{ mA}$, $12.0\text{ mA}$, $16.0\text{ mA}$, $20.0\text{ mA}$, $22.0\text{ mA}$.
   - Verify loop fault detection at $3.4\text{ mA}$ and confirm %RH linearity across 4..20 mA within $\pm 0.5\%RH$.
2. **Physical RS-485 Modbus Multi-Drop Communication**:
   - Connect physical `CWT-TM-16PT` (address 1) and `JSY-MK-339` (address 2) to RS485-1 at 9600 8N1.
   - Verify zero frame collisions, stable 1-second polling turnaround, and verify that Modbus probe diagnostics pass at boot.
3. **Execution Timing & Stack Margin Benchmarking**:
   - Instrument GPIO toggle around `ao_thermal_energy` 1 Hz execution loop.
   - Assert total execution time on STM32H7 @ 480 MHz is $< 3.5\text{ ms}$ (leaving $> 99\%$ CPU available).
   - Profile stack usage under Zephyr MPU stack protection; confirm unused stack headroom $> 1024\text{ bytes}$ on the 3072-byte stack.

---

## 5. Acceptance Criteria Checklist

| Category | Requirement | Acceptance Criterion | Verification Method |
| :--- | :--- | :--- | :--- |
| **Domain Purity** | Zero `<zephyr/...>` headers in domain modules | `grep -rn "<zephyr/" app/src/domain/thermal/` returns 0 matches | Static CI lint gate |
| **Fat-Pointer Rule** | Zero direct v-table dereferences (`api->`) | `grep -rn "api->" app/src \| grep -v "infrastructure"` returns 0 matches | Static CI lint gate |
| **Numeric Safety** | No undefined floating-point operations | 100% of division, log, sqrt, and exp calls protected by domain guards | Unit test suite |
| **Precision** | Double-precision OLS matrix inversion | Cholesky solver executes in 64-bit `double` on DP-FPU | Benchmark & unit tests |
| **Phase 2 Scope** | Zero synthetic pressure data | All pressure fields flagged `QUALITY_UNAVAILABLE` in Phase 1 | Unit & integration tests |
| **Airflow Rule** | Decoupling of ventilation from compression | Mass flow $\dot{m}_{da}$ governed strictly by confirmed active fans | Unit test suite |
| **Memory Policy** | Zero dynamic allocation in runtime loops | Zero calls to `malloc`, `k_malloc`, or `k_heap` | Static audit & runtime hook |
| **Persistence** | Ping-pong EEPROM wear-leveling & CRC | Configuration survives power loss; corrupt slots recovered | Power-cut test suite |
