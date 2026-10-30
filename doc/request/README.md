# BrightC V3 — Telemetry, Energy Audit and Predictive HVAC Capacity

**Client request review, final specification baseline and implementation plan**

| Item | Value |
| --- | --- |
| Reviewed baseline | [`Mathematical Model Specification REVC`](Mathematical Model Specification REVC.md) — **Revision C** (maintained in the `REVB` file for link stability) |
| Initial client delivery | `client reference/` (three `.docx` files, unmodified, original language) |
| Target platform | DAECGE BrightC Controller V3 — STM32H743ZIT6, Cortex-M7 @ 480 MHz, Zephyr RTOS |
| Review date | 2026-10-30 |
| Verdict | **Revision C accepted as the Phase 1 implementation baseline.** 18 findings were raised; 5 are closed in Revision C and 13 remain open (§5.2). Each open finding must be closed before the phase it affects. |
| Code impact | None. This document is analysis and planning only; no firmware source file was changed. |

---

## 1. What this directory is

This directory is the requirements archive for the BrightC V3 telemetry, energy-audit and predictive
HVAC-capacity project. It holds two different kinds of material:

1. **`client reference/`** — the documents Entel delivered at the start of the project. They define the
   business objective, the field installation concept, an early two-sensor deployment and a prototype
   algorithm. They are historical input, kept verbatim.
2. **`Mathematical Model Specification REVC.md`** — the specification produced from that input. It
   generalises the sensor topology to `(N + N) + M`, defines the pilot as `6 + 6 + 2 = 14` PT100
   probes, fixes the operating-state machine, the thermodynamic and psychrometric model, the
   predictive diagnostics, the hybrid storage layout and the northbound interfaces.

This README is the English review of that material: what was asked, what Revision C specifies, what
was verified against the actual board and firmware, what remains to resolve, and what will be built.

In short: `client reference/` is the material **as received**; the specification in
`Mathematical Model Specification REVC.md`, now at **Revision C**, is the version that was **reviewed
and retained for implementation**.

> The source `.docx` files and Revision C are in Spanish. This README is the English interpretation.
> Where the two disagree, the finding is recorded in §5.2; this README does not silently override the
> client document.

---

## 2. Document inventory

| Document | Role | Content and reliability |
| --- | --- | --- |
| [`Mathematical Model Specification REVC.md`](Mathematical Model Specification REVC.md) | **Authoritative technical baseline — Revision C** | Complete and internally structured: configurable topology, pilot sizing, acquisition paths, compressor state machine, equations, diagnostics, storage, interfaces. Includes the B → C change log (C-01…C-05). This is the reviewed version retained for implementation. |
| [`client reference/ESPECIFICACIONES TÉCNICAS Y FUNCIONALES.docx`](<client reference/ESPECIFICACIONES TÉCNICAS Y FUNCIONALES.docx>) | Commercial and functional intent | Business case, edge-computing positioning, predictive alert matrix, `GET /api/v1/hvac/status` JSON sample, security narrative, dashboard expectations. Uses the wording “Machine Learning” and “internal three-phase measurement”, both resolved in Revision C. |
| [`client reference/Modelo Matematico.docx`](<client reference/Modelo Matematico.docx>) | Early model presentation | Three page images of prose only (no editable text, no equations). Restates the two-probe concept, the OLS/SD approach and the diagnostic matrix. Superseded by Revision C. |
| [`client reference/Telemetria predictiva de capacidad Climatica.docx`](<client reference/Telemetria predictiva de capacidad Climatica.docx>) | Initial field and algorithm proposal | Two PT100 per room on `RTD4`/`RTD5`, sample register map (`0x001D/0x001E`, `0x001B`, `0x0028`, `0x0033`), a Python reference implementation with fixed 3400/6800 m³/h airflow, HMI layout and dashboard widgets. |

### 2.1 Source precedence

1. Approved site electrical, mechanical and cybersecurity requirements (not yet delivered).
2. `Mathematical Model Specification REVC.md` at Revision C, as amended by the findings still open in §5.2.
3. `client reference/*.docx` for commercial intent and field assumptions only.
4. This README for the engineering interpretation and the implementation plan.

### 2.2 What the specification is not

Revision C is a mathematical and functional specification. It is **not** an interface contract, an
acceptance-test plan, a register map or a commissioning procedure. Those artefacts are listed as
missing in §5.2 (F-03, F-17) and are prerequisites for integration with NetEco.

---

## 3. From the initial delivery to Revision C

The initial documents describe a two-probe, backend-computed prototype. Revision B turned it into a
configurable edge product; Revision C applies the corrections found while verifying it against the
board, without changing the architecture or the model.

| Topic | Initial client reference | Revision C baseline | Status |
| --- | --- | --- | --- |
| Temperature topology | 2 PT100 per room (injection + return), wired to `RTD4`/`RTD5` | `(N + N) + M`; pilot `6 + 6 + 2 = 14` probes | **Changed** — accepted; pair-to-unit mapping still open (F-09) |
| Acquisition | Direct board RTD channels | `CWT-TM-16PT` Modbus RTU primary + 5 internal INA826 channels | **Changed** — accepted; channel inventory to reconcile (F-16) |
| Energy meter | `JSY-MK-333` or `JSY-MK-339`, power at `0x0033` | `JSY-MK-339`, total active power `0x0052`, `DATA/100` kW | **Changed** — register verified against the manual |
| Room humidity | I2C sensor on the board | Generic I²C sensor, `HR_sala`; model deferred | **Unchanged** — closed in Rev C (F-13) |
| Compressor state | Bitmask read over Modbus TCP from `0x0028` | Local 24–48 V DC auxiliary contacts, 50 Hz sampling, 500 ms debounce | **Changed** — on-board conditioning confirmed (F-06 closed) |
| Where calculations run | Backend (Python/Node-RED/SCADA), 60 s polling | On-controller, 1 Hz, C99, 5-state per-compressor FSM | **Changed** — accepted, this is the product differentiator |
| Air density / flow | Fixed 1.2 kg/m³, fixed 3400/6800 m³/h | Density from altitude and return temperature; flow from active units | **Improved** — accepted |
| Capacity | `Q = V·ρ·Cp·ΔT / 3600` | Same physics, per pair and total, plus latent term and COP | **Improved** — accepted |
| Alerts | Hard thresholds (`T_inj < 11 °C`, `ΔT > 14 K`, `ΔT < 6 K`, `HR > 65 %`) | Probability indices 0–100, 7-day baseline, days-to-clean estimate | **Changed** — accepted; thresholds need commissioning data |
| Short cycling | Not present | `t_off < 180 s`, more than 3 events per hour | **Added** — accepted; persistence policy open (F-10) |
| History | SD datalogger, undefined format | RAM 720 h + EEPROM baseline + SD 64 B hourly record | **Changed** — record layout has a defect (F-04) |
| HMI | 4.3" efficiency page | Same, plus per-pair detail page | **Unchanged** — accepted |
| Northbound | Modbus TCP to NetEco + REST API | Same, plus SNMP agent and traps | **Added** — maps must be frozen (F-03) |
| Analytics | “Machine Learning”, “light neural networks” | Ordinary Least Squares regression only | **Clarified** — wording to align (F-18) |
| Security | Bearer token + IP allowlist, “encrypted” | Same requirement, no transport specified | **Open** — conflicts with plain HTTP (F-12) |

---

## 4. Reviewed specification (Revision C)

### 4.1 Operating principle

The controller is a **passive observer in parallel** with the existing thermostats. It never
intervenes in the cooling loop. It measures air-side temperatures and total electrical power,
computes mass and energy balances, learns the room's thermal signature during the first seven days,
and publishes diagnostics northbound.

This passivity is a safety requirement, not a preference: the review assumes the telemetry firmware
**must not energise the HVAC relays**. That assumption has to be confirmed (D-07), because the same
board carries `relay_aa1..aa4` outputs.

### 4.2 Sensor topology

- `N` injection/return pairs, always installed as pairs: `T_inj,k` and `T_ret,k`, `k = 1..N`.
- `M` room probes: `T_sala,m`, `m = 1..M`.
- Pilot: `N = 6`, `M = 2`, 14 PT100 Class A, three-wire, shielded.
- Pairs may map to one air handler each, or sample a common plenum. This choice changes the physics
  of per-pair capacity and is still undefined (F-09).

### 4.3 Acquisition paths

| Path | Capacity | Interface | Notes |
| --- | --- | --- | --- |
| `CWT-TM-16PT` | 16 PT100 channels | Modbus RTU, RS-485, `9600 8N1`; address programmed before connection (C-04) | Pilot uses CH1–CH6 injection, CH7–CH12 return, CH13–CH14 room; 2 spare |
| Internal RTD stage | 5 channels, INA826 | STM32 ADC, 16-bit | INA826 `G = 80.806` (`Rg = 619 Ω`) followed by a 1:2 divider (10 kΩ/10 kΩ) → `G_eff = 40.403` at the ADC; 91 Ω reference arm; `ΔR_cable` in EEPROM |
| `J9 (ADCx10)` | 10 analog inputs | STM32 ADC | Reserved for Phase 2 4–20 mA sensors |
| Energy meter | `JSY-MK-339` | Modbus RTU, RS-485, `9600 8N1`; address programmed before connection (C-04) | Total active power `0x0052`, `DATA/100`, kW |
| Humidity | Digital sensor | I²C | `HR_sala`; generic sensor, model deferred (C-03) |
| Compressor feedback | 1 digital input per compressor | 24–48 V DC dry contact | On-board isolation/level conditioning before the expander (C-02); 20 ms sampling, 25 identical samples = 500 ms debounce |

### 4.4 Compressor stabilisation state machine

| State | Condition | Action |
| --- | --- | --- |
| 0 `OFF` | Contact open | Accumulate `t_off`; short-cycle event if re-start with `t_off < 180 s` |
| 1 `INRUSH_WAIT` | `0 < t_on < 15 s` | Discard electrical readings (inrush) |
| 2 `STEP_LOAD_CAPTURE` | `t_on = 15 s` | Single shot: `ΔP_comp,c = P_total(15 s) − P_total(0⁻)` |
| 3 `THERMAL_WARMUP` | `15 s < t_on < 180 s` | Live values shown, predictive maths and regression blocked |
| 4 `STEADY_STATE` | `t_on ≥ 180 s` | Thermodynamics, COP and hourly record enabled |

The intent is correct. Three implementation details are missing and are recorded as F-05: the
pre-trigger power sample, the behaviour when two compressors start inside the same 15 s window, and
the sampling rate needed to resolve the step.

### 4.5 Mathematical engine

For each pair `k`: `ΔT_k = T_ret,k − T_inj,k`. Room aggregates use the active pairs only. Air density
comes from the barometric formula at site altitude and the return temperature. Mass flow comes from
the nominal volumetric flow of the active air handlers. Then:

- `Q_sensible_total = ṁ_total · Cp · ΔT̄_clima`
- `Q_latente_total = Σ ṁ_k · h_fg · max(0, ω_ret − ω_inj,k)`
- `%Latente = Q_lat / (Q_sens + Q_lat) · 100`
- `COP_real = (Q_sens + Q_lat) / P_elec_total`
- Baseline: `COP_esperado = β₀ + β₁·T̄_sala + β₂·P_elec_total`, fitted by OLS over 168 h.
- Degradation: `clamp₀..₁₀₀((COP_esperado − COP_real) / COP_esperado · 100)`
- Free capacity: `Q_max_seguro = (Σ Q_nominal,u) · (1 − degradación/100)`, `kW_libres = max(0, Q_max_seguro − Q_sensible_total)`

The psychrometric constants (Magnus `0.61078 kPa`, `17.27`, `237.3`; `R_dry = 0.28705 kJ/kg·K`;
`h_fg = 2450 kJ/kg`) are appropriate for the HVAC range. The chain has two definition gaps
(F-08) and the OLS model form is statistically weak (F-07).

### 4.6 Predictive diagnostics

| Diagnostic | Index / rule (Revision C) | Assessment |
| --- | --- | --- |
| Filter restriction | `60·(T_inj_base − T_inj)/3.0 + 40·(ΔT − ΔT_base)/4.0`, clamped 0–100; days to clean from the 7–30 day slope | Physics correct; threshold inconsistency and missing guards (F-10) |
| Refrigerant leak / compressor fatigue | `((ΔT_base·F_carga) − ΔT)/(0.5·ΔT_base·F_carga)·100`, only if filter index < 50 % | Direction correct; `F_carga` and per-pair unit mapping undefined (F-09) |
| Hotspot / poor air distribution | Absolute difference between room probes > 3.5 K, or `T_sala,max > 27 °C` with `T̄_ret < 23 °C` | Accepted; thresholds to be validated on site |
| Humidity infiltration | `%Latente > 30 %`, or `HR_sala > 65 %` sustained | “Sustained” needs a duration (F-08) |
| Short cycling | More than 3 events with `t_off < 180 s` in one hour | Needs counter persistence and clear rule (F-10) |

### 4.7 Storage model

| Level | Medium | Content |
| --- | --- | --- |
| L1 | STM32H7 RAM | Per-second filtered values; 720 hourly summaries (30 days) |
| L2 | AT24C256C EEPROM, 32 KB | Site topology, OLS coefficients, per-pair baseline, per-compressor `ΔP`, calibration |
| L3 | microSD (SDMMC1, 4-bit) | One 64-byte binary record per hour, append-only; 8,760 h ≈ 560.6 kB/year |

### 4.8 Interfaces

- Local HMI: DWIN 4.3" on UART3, main page plus per-pair detail.
- Modbus TCP server, port 502, multiple clients (NetEco / SCADA).
- SNMP agent, UDP 161, plus traps/informs to UDP 162.
- Local REST/JSON: `GET /api/v1/hvac/status`, `GET /api/v1/hvac/logs?hours=X`, `POST /api/v1/hvac/config`, bearer token and IP allowlist.

---

## 5. Verification against the current platform

### 5.1 Confirmed on the board and in the firmware

| Claim in Revision C | Evidence | Result |
| --- | --- | --- |
| 5 internal RTD channels with INA826, 5.1 kΩ 0.1 %, 91 Ω reference | `BRIGHT C V3 TOP.PDF` p.6: `Rg = 619 Ω` (G = 80.806), `R24/R26 = 10 kΩ/10 kΩ` divider, `R25 = 91 Ω`, `R20/R21 = 5.1 kΩ 0.1 %` | Confirmed; `G_eff = 40.403` at the ADC (F-01 closed) |
| `J9 (ADCx10)` exposes 10 analog channels | Same schematic, `ADCx10` / `J9` block | Confirmed |
| microSD on SDMMC1, 4-bit, card detect | `sdmmc1` node, `bus-width = <4>`, `cd-gpios = <&gpiog 5>` | Confirmed |
| 32 KB EEPROM `AT24C256C` on I2C2 | `eeprom0: eeprom@50`, `size = <DT_SIZE_K(32)>` | Confirmed |
| RTC available for timestamps | `&rtc` enabled, alias `rtc` | Confirmed (battery not verified) |
| Ethernet PHY LAN8742A, RMII | `&mac`, `eth_phy` on MDIO, reset `PG4` | Confirmed |
| `CWT-TM-16PT` register map | `cwt_tm16pt_codec.c`: config `0x0010`, scaled `0x0020–0x002F`, float `0x0040–0x005E`; open-probe `0x7FFF` | Confirmed and already implemented |
| `JSY-MK-339` total active power at `0x0052`, `DATA/100` | Vendor manual, Table 2, entry 11 | Confirmed |
| 4–20 mA inputs reserved for Phase 2 | 10 free channels on `J9` | Confirmed |
| Compressor feedback inputs | 24 `alm_in_*` nodes on four MCP23S17 expanders | On-board isolation/level conditioning for 24–48 V DC before the expander, confirmed by the hardware owner (F-06 closed) |
| LED and buzzer available for status | `user_led` on PF2, `buzzer_pwm` on PWM15 channel 1 | Confirmed |

### 5.2 Findings

Eighteen findings were raised during the review. Five are closed in Revision C; thirteen remain open.

#### Closed in Revision C

| ID | Sev. | Rev C § | Finding | Resolution |
| --- | --- | --- | --- | --- |
| F-01 | B | 2.2 | Analog front-end gain disagreed by exactly 2×: Revision B used `G = 40.403`, the firmware library `transducer_rtd.h` defaults to `80.8061`. | Schematic page 6 confirms the chain: INA826 `G = 80.806` (`Rg = 619 Ω`) followed by the `R24/R26 = 10 kΩ/10 kΩ` divider, so the ADC sees `40.403`. Revision C documents the divider; the firmware default must change from `80.8061` to `40.403`. |
| F-02 | B | 6.4 | The devicetree still declares 256 KB MCUboot slots while a networked build exceeds 350 KB. | The OTA architecture (`doc/architecture/mcuboot_external_flash_ota_architecture.md`) already defines the asymmetric 1.75 MB slot; Revision C records it as a phase prerequisite. The DTS is intentionally not modified in this revision. |
| F-06 | B | 2.3 | Revision B requires 24–48 V DC auxiliary contacts, but the expander pins are 3.3 V logic. | The board includes an isolation and level-conditioning stage for 24–48 V DC before the MCP23S17 pin; direct connection to the designated inputs is valid (confirmed by the hardware owner). |
| F-13 | m | 2.4 | The devicetree labels I2C1 as “DHT22”, which is not an I²C device, and no humidity node exists. | The humidity sensor is specified as a generic I²C device; the exact model is deferred to detailed engineering. The DTS comment must be corrected when the part is selected. |
| F-14 | m | 2.2 | `CWT-TM-16PT` and `JSY-MK-339` both ship at address 1, `9600 8N1`. | Modules are programmed with distinct addresses (e.g. CWT = 1, JSY = 2) **before** being connected to the bus; the commissioning procedure is now part of the specification. |

#### Open findings

| ID | Sev. | Rev C § | Finding | Proposed resolution |
| --- | --- | --- | --- | --- |
| F-03 | **B** | 7 | The contractual interfaces are described in prose only: no Modbus TCP register map (types, scaling, endianness, `0x03`/`0x04` blocks), no SNMP MIB/OID list, no REST schemas, no alarm-code table. NetEco integration cannot be tested against prose. | Issue an annex per interface, frozen and versioned, before Phase 5. |
| F-04 | **B** | 6.3 | The hourly record is specified as 64 bytes, but the listed fields sum to 62, and the last field is “Phase 2 reserve + CRC-16” in 2 bytes, which cannot hold both. | Adopt the exact 64-byte layout in §6.5 (CRC included, no reserve), and move Phase 2 data to a version-2 record. |
| F-05 | **M** | 3 | `P_total(0⁻)` and the 15 s net step are required, but no power sampling rate, pre-trigger buffer or multi-start policy is defined. At 1 Hz the step resolution is marginal. | Sample `0x0052` at 1 Hz continuously and at 2 Hz during capture; keep a 60 s pre-trigger ring; discard the attribution if a second compressor switched in the window and mark the step as “unattributed”. |
| F-07 | **M** | 5.1 | `COP` is regressed on `P_elec_total`, but `COP = Q/P`, so the regressor appears in the response (endogeneity). Features are not centred or scaled, no sample gating, no minimum count, no retraining or invalidation rule. | Regress capacity `Q_esperado = f(T̄_sala, P, T_ext)` (or use load fraction), fit in `double` with centred/scaled features, accept only `STEADY_STATE` samples with `P` above a threshold, require ≥168 valid hours, and define baseline reset after filter service or major repair. |
| F-08 | **M** | 4.5 | `ω_ret` is computed with room `HR` but the saturation pressure at `T̄_ret`, mixing two different air states; `ω_inj` is undefined when there is no condensation; the 92 % injection RH is a hard-coded assumption. | Compute `ω` from the humidity sensor's own temperature, then propagate it as the room/return moisture content; define `ω_inj = min(ω_ret, 0.92·ω_sat(T_inj))` as a continuous law; make `0.92` configurable; define “sustained” as a configurable duration (proposal: 15 min). |
| F-09 | **M** | 2.1, 4.3, 4.4 | Per-pair capacity divides total mass flow equally among active pairs, which is meaningless when pairs sample a common plenum. `F_carga` and the leak index also need a pair-to-unit association. | Add an explicit `pair → unit` map and `V_nom,u`, `Q_nom,u` per unit in the site configuration; report per-pair `Q` only for dedicated pairs, otherwise report room-level `Q` and flag the pair value as indicative. Define `F_carga = compressors ON / compressors total` per unit. |
| F-10 | **M** | 5.2 | Freezing threshold is `9.0 °C` in the filter text but `7.5 °C` in the days-to-clean formula; the formula has no divide-by-zero or negative-day guard; short-cycle events have no clear/reset rule or persistence target. | Define one `T_FREEZE` constant, clamp days to `[0, 365]`, require an absolute slope of at least 0.02 K/day, else “insufficient trend”, and persist short-cycle counters in the event log with a daily clear. |
| F-11 | **M** | 6.1, 6.3 | Records carry UNIX timestamps, but no time source, sync mechanism or pre-sync behaviour is defined. | Use the on-board RTC, add SNTP when Ethernet is up, and define boot behaviour: monotonic clock plus “time not synchronised” flag; backfill is not attempted. |
| F-12 | **B** (deployment) | 7.4 | REST is bearer-token over plain HTTP, while the commercial document claims encrypted traffic; SNMPv3 USM also needs crypto. TLS changes the flash and RAM budget materially. | Choose explicitly: TLS 1.2/1.3 with mbedTLS and a provisioned certificate, or plaintext restricted to a management VLAN with documented risk acceptance. Define credential storage, rotation and rate limiting either way. |
| F-15 | **m** | 4.6, 5.4 | Phase 2 “superheat” is defined as `T_inj − T_sat`, which is an air-to-evaporator approach, not refrigerant superheat (that needs suction-line temperature). Capacity is derated only by COP degradation, ignoring ambient. | Rename to `ΔT_approach`; add suction-line temperature to the Phase 2 sensor list; state the ambient-derating limitation or add manufacturer derating data. |
| F-16 | **m** | 2.2, 2.5 | The text mixes “10 channels on J9” and “5 internal RTD channels” without an allocation table, while the SoC exposes 15 ADC inputs. | Publish a channel allocation table (connector pin → ADC channel → function) as part of the site configuration. |
| F-17 | **M** | 7, 5.2 | No alarm model: no identifiers, severity, latch/acknowledge semantics, clear conditions or mapping to SNMP traps, HMI and the indicator subsystem (which expects `alarm_id_t` and levels). | Define `alarm_id_t`, severity, latch policy and clear rule; publish to `chan_system_alarm`; map each alarm to a trap OID and an indicator priority. |
| F-18 | **m** | — | The commercial document promises “Machine Learning” and “light neural networks”; Revision C implements OLS. | State clearly that Phase 1 uses deterministic regression, not ML, and that any learned model must be explainable and auditable. |

### 5.3 Decisions required from the client

| ID | Decision | Why it blocks |
| --- | --- | --- |
| D-01 | Pilot topology: which pairs belong to which air handler, and the nominal flow and capacity per unit | Needed for `ṁ`, per-pair `Q`, `kW_libres` (F-09) |
| D-02 | Meter model (`JSY-MK-339` only), CT ratio, and the metering boundary (compressors only vs whole panel) | Defines the COP denominator (F-07) |
| D-03 | Humidity sensor location (panel vs return duct); the model is a generic I²C sensor, deferred to detailed engineering | Defines `ω_ret` validity (F-08); the bus contract is already fixed (F-13 closed) |
| D-04 | Compressor signal source semantics: real auxiliary contact vs relay command | Diagnostic validity only; the 24–48 V electrical interface is confirmed (F-06 closed) |
| D-05 | Ownership and freeze date of the Modbus TCP map, SNMP MIB and REST schemas | Contractual integration (F-03) |
| D-06 | Security posture: TLS or management-VLAN plaintext; certificate provisioning | Deployment approval (F-12) |
| D-07 | Does the telemetry firmware own the HVAC relays, or must they stay de-energised? | Safety of the passive-observer claim |
| D-08 | Acceptance tolerances and the commissioning data set | Acceptance criteria in §8 |

---

## 6. Implementation plan

### 6.1 Where each piece lands

The firmware follows the existing Clean Architecture layout: pure C99 domain, ports with v-table and
validated inline wrappers, Zephyr adapters, a single composition root, one Active Object per
concurrency domain, stacks at file scope (MPU invariant), zero dynamic allocation.

| Layer | Component | Responsibility | Status |
| --- | --- | --- | --- |
| Domain | `hvac_types.h` | Snapshot, site topology, quality flags, `pair → unit` map | New |
| Domain | `psychrometrics.c/.h` | `P_atm`, `ρ`, `P_ws`, `ω`, dew point | New |
| Domain | `thermal_engine.c/.h` | `Q_sens`, `Q_lat`, `%latent`, `COP` | New |
| Domain | `compressor_fsm.c/.h` | Five-state machine, short-cycle counter | New |
| Domain | `baseline_ols.c/.h` | Normal equations, 3×3 solve in `double`, gating | New |
| Domain | `hvac_diagnostics.c/.h` | Filter, leak, hotspot, humidity, short-cycle indices | New |
| Domain | `capacity_audit.c/.h` | `Q_max_seguro`, `kW_libres`, degradation | New |
| Domain | `history_record.c/.h` | 64-byte encode/decode, CRC-16, version | New |
| Domain | `cwt_tm16pt_codec.c` | CWT register parsing | **Exists** |
| Domain | `slave_scheduler.c` | Modbus polling policy | **Exists** |
| Lib | `transducer_rtd.c` | PT100 ADC → °C (needs `ΔR_cable` hook) | **Exists, to extend** |
| Lib | `filter_ema.c`, `filter_median.c` | Smoothing and spike rejection | **Exists** |
| Port | `i_modbus_client.h` | RTU transport | **Exists** |
| Port | `i_temperature_bank.h` | Temperature bank contract | **Exists as data type, needs v-table** |
| Port | `i_energy_meter.h` | Power, voltage, current, PF, energy, quality | New |
| Port | `i_compressor_input.h` | Debounced contact bank | New |
| Port | `i_humidity_sensor.h` | Room RH | New |
| Port | `i_history_store.h` | Append, read, restore, card status | New |
| Port | `i_time_source.h` | Wall clock, monotonic, sync state | New |
| Port | `i_config_storage.h` | Domain load/save | **Planned** |
| Port | `i_network.h` | Ethernet + Wi-Fi | **Planned** |
| Port | `i_display_port.h` | DWIN HMI | **Planned** |
| Port | `i_led.h`, `i_buzzer.h` | Status indicators | **Planned** |
| Adapter | `adapter_energy_jsy_mk339.c` | `0x0048–0x0052` block decode | New |
| Adapter | `adapter_compressor_mcp23s17.c` | 24 inputs, 20 ms sampling | New |
| Adapter | `adapter_humidity_i2c.c` | RH sensor | New |
| Adapter | `adapter_history_sdmmc.c` | Raw-block ring on microSD | New |
| Adapter | `adapter_time_rtc.c` | RTC + SNTP | New |
| Adapter | `adapter_modbus_nmbs_tcp.c` | Modbus TCP server over sockets | New |
| Adapter | `adapter_http_rest.c` | REST endpoints | New |
| Adapter | `adapter_snmp_agent.c` | MIB, GET/WALK, traps | New |
| Application | `ao_modbus` | RTU polling of CWT + JSY | **Exists, extend** |
| Application | `ao_hvac` | 50 Hz contacts, 1 Hz snapshot and engine | New |
| Application | `ao_datalogger` | Hourly record, RAM ring, boot restore | New |
| Application | `ao_storage`, `ao_network`, `ao_system_indicator` | Per architecture documents | **Planned** |
| Zbus | `chan_hvac_sample`, `chan_hvac_diagnostic`, `chan_hvac_alarm` | Data and alarm events | New |
| Zbus | `chan_system_config`, `chan_network_*`, `chan_indicator_*` | Per architecture documents | **Planned** |

### 6.2 Phases and exit criteria

| Phase | Scope | Exit criteria |
| --- | --- | --- |
| **P0 — Prerequisites** | OTA/flash repartition (already planned in the OTA architecture, F-02); interface annexes (F-03); hardware decisions D-01…D-03 and D-05…D-08; RTD gain default corrected in firmware (F-01) | Networking build fits the planned 1.75 MB slot; maps signed off; `BRIGHTC_RTD_GAIN_DEFAULT` = `40.403` validated on a decade box |
| **P1 — Acquisition core** | `ao_hvac` sampling, CWT and JSY adapters, compressor debounce, humidity, site configuration domain | 14 channels + power + 24 contacts acquired at the specified rates for 24 h without loss; unit tests green |
| **P2 — Thermodynamic engine** | Psychrometrics, thermal engine, compressor FSM, capacity audit, HMI main page | Engine validated against a spreadsheet with recorded data; COP stable at steady state |
| **P3 — History and baseline** | 64-byte record, RAM ring, SD raw ring, boot restore, daily OLS, baseline persistence | 720 h simulated; restore after 10 power cuts without gaps; baseline reproducible |
| **P4 — Predictive diagnostics** | Five diagnostic indices, alarm model, indicator integration, SNMP traps | Each diagnostic reproduced from synthetic and recorded data; alarm latency < 2 s |
| **P5 — Northbound** | `ao_network`, Modbus TCP server, SNMP agent, REST API, time sync, security | Third-party Modbus master, `snmpwalk` and REST client pass the acceptance script |
| **P6 — Phase 2 (optional)** | 4–20 mA pressure sensors, approach temperature, filter/leak confirmation | Not before P5 is accepted |

### 6.3 Sampling and timing budget

| Function | Rate | Cost | Note |
| --- | --- | --- | --- |
| Compressor contacts | 50 Hz (20 ms) | SPI1 burst read | 25 identical samples = 500 ms debounce |
| CWT burst read | 1 Hz | ≈47 ms of bus at 9600 bps | 16 registers, one transaction |
| JSY meter block | 1 Hz, 2 Hz during capture | ≈36 ms per transaction | `0x0048–0x0052` |
| Humidity | 0.2 Hz | I²C | Sensor-dependent conversion time |
| Snapshot + engine | 1 Hz | FPU, `N = 6`, `M = 2` | Microseconds |
| Hourly record | 1/h | SD append | Must survive power loss |
| OLS refit | 1/day | 3×3 solve | `double` precision |
| Northbound | on demand | Sockets | Bounded by network buffers |

Shared RS-485 utilisation is about 8 % at 1 Hz per slave and 12 % with 2 Hz power sampling; both
slaves fit on one bus once the commissioning address plan is applied (`CWT = 1`, `JSY = 2`, C-04).

### 6.4 Persistence map

| Region | Offset | Size | Content |
| --- | --- | --- | --- |
| `CONFIG_DOMAIN_DEVICE` | `0x0000` | 1 KB | Identity (existing) |
| `CONFIG_DOMAIN_MODBUS` | `0x0400` | 2 KB | Bus and slave table, CWT/JSY addresses (existing) |
| `CONFIG_DOMAIN_NET_ETH` | `0x0C00` | 1 KB | Ethernet parameters (existing) |
| `CONFIG_DOMAIN_NET_WIFI` | `0x1000` | 1 KB | Wi-Fi parameters (existing) |
| `CONFIG_DOMAIN_CALIBRATION` | `0x1400` | 3 KB | `ΔR_cable` per internal channel, ADC gain/offset, RTD front-end constants |
| `CONFIG_DOMAIN_HVAC_SITE` | `0x2000` | 1 KB | `N`, `M`, pair→unit map, `V_nom`, `Q_nom`, altitude, thresholds |
| `CONFIG_DOMAIN_HVAC_BASELINE` | `0x2400` | 2 KB | `β₀…β₂`, `ΔT_base[8]`, `T_inj_base[8]`, `ΔP_comp[8]`, validity and sample count |
| Reserved | `0x2C00` | 5 KB | Free |
| Event log | `0x4000` | 16 KB | Alarms, short cycles, maintenance events (existing region) |

### 6.5 Proposed 64-byte hourly record

| Offset | Size | Field | Encoding |
| --- | --- | --- | --- |
| 0 | 4 | UNIX timestamp | `uint32`, seconds |
| 4 | 16 | `T_inj[1..8]` | `int16`, 0.1 °C |
| 20 | 16 | `T_ret[1..8]` | `int16`, 0.1 °C |
| 36 | 8 | `T_sala[1..4]` | `int16`, 0.1 °C |
| 44 | 2 | `HR_sala` | `uint16`, 0.1 % |
| 46 | 4 | `P_elec_total` | `float32`, kW |
| 50 | 4 | `Q_sensible` | `float32`, kW |
| 54 | 4 | `Q_latente` | `float32`, kW |
| 58 | 2 | `COP_real` | `int16`, 0.01 |
| 60 | 2 | Compressor mask + start count | 8 bits mask, 8 bits count |
| 62 | 2 | CRC-16/CCITT | over bytes 0–61 |

File header carries magic, format version 1, record size and a generation counter, so Phase 2 can
introduce a version-2 record without ambiguity.

### 6.6 Test strategy

- **Unit (`native_sim`)**: psychrometric reference points against tables; OLS against a known matrix;
  FSM transitions including simultaneous starts and the 15 s capture; diagnostic clamps and
  divide-by-zero paths; record codec (size, CRC, timestamp); configuration validation.
- **Integration with fakes**: fake Modbus slave, fake SD, fake clock; hour rollover, card removal,
  corrupt record skip, reboot restore.
- **HIL**: real `CWT-TM-16PT` with a decade box, `JSY-MK-339` with a load bank, relay simulator for
  the 24 contacts, SD card, Modbus/TCP client, `snmpwalk`, REST client.
- **Soak**: 72 h with periodic power cuts; watchdog and stack high-water marks checked.

### 6.7 Traceability

| Requirement | Component | Verification |
| --- | --- | --- |
| Rev C §2.1 topology | `hvac_types`, site config | Config validation tests |
| §2.2 CWT acquisition | `cwt_tm16pt_codec`, `ao_modbus` | Existing codec tests + HIL |
| §2.2 internal RTD | `transducer_rtd` + calibration | Decade-box calibration |
| §2.3 compressor FSM | `compressor_fsm` | Unit tests with synthetic edges |
| §2.4 energy meter | `adapter_energy_jsy_mk339` | Register fixtures + HIL |
| §4.1–4.5 engine | `psychrometrics`, `thermal_engine` | Golden-vector tests |
| §5.1 baseline | `baseline_ols` | Matrix tests + 168 h replay |
| §5.2 diagnostics | `hvac_diagnostics` | Boundary tests + recorded data |
| §5.4 capacity | `capacity_audit` | Unit tests |
| §6 storage | `history_record`, `ao_datalogger` | Power-cut and restore tests |
| §7 interfaces | `adapter_modbus_nmbs_tcp`, `adapter_snmp_agent`, `adapter_http_rest` | Acceptance script |

---

## 7. Resource and risk assessment

### 7.1 Memory and flash

| Item | Estimate | Note |
| --- | --- | --- |
| RAM history (720 × 64 B) | 45 KiB | Fits comfortably in the H743 1 MB |
| Snapshot + configuration | < 2 KiB | Static |
| Thread stacks | ≈16 KiB | `ao_hvac`, `ao_datalogger`, `ao_network`, `ao_storage`, `ao_system_indicator` |
| Network buffers | 16–32 KiB | Depends on the socket counts |
| Application image | > 350 KiB with networking | Fits the planned 1.75 MB external slot (F-02) |
| SD long-term history | 560.6 kB/year | 64 B/h |

### 7.2 Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Firmware RTD gain default left at `80.8061` (F-01 closed in spec) | All temperatures and capacity wrong by 2× | Change `BRIGHTC_RTD_GAIN_DEFAULT` to `40.403` in P0; decade-box calibration per unit |
| Planned OTA repartition not yet in the devicetree (F-02 closed in spec) | Networking phase cannot be built until it lands | P0 gate: apply the already-designed 1.75 MB slot plan with human approval |
| Sensor installation quality | Stratification and probe placement dominate accuracy | Commissioning procedure, probe placement guide |
| SD power loss or missing card | Lost hours, corrupt file | Raw ring + CRC + sequence, RAM fallback, explicit alarm |
| Field wiring of the 24–48 V contacts (F-06 closed; conditioning is on-board) | Wrong common or polarity defeats the input | Commissioning checklist; document terminal polarity and common per channel |
| Interface maps not frozen (F-03) | Rework after integration | Contractual annex before P5 |
| Plain HTTP (F-12) | Security rejection | Decide TLS or VLAN policy in P0 |
| Baseline learned on a dirty filter (F-07) | False degradation | Reset baseline after service; gate during learning |
| Ambient derating not modelled (F-15) | Overestimated free capacity | Document limitation; add derating in Phase 2 |
| Module left at the factory address 1 during commissioning (F-14 closed in spec) | Bus collision, no response | Pre-flight bus scan; address the module before it reaches the bus (C-04) |

---

## 8. Proposed acceptance criteria (to be agreed)

1. Temperature: within ±0.3 K of the reference after two-point calibration, excluding probe Class A
   tolerance; ΔT repeatability ±0.1 K with 60 s averaging.
2. Electrical power: within ±1 % of the meter reading over 10–100 % of range.
3. COP: repeatability ±3 % at `STEADY_STATE` (≥180 s) with stable load.
4. History: 720 hourly records, no gap after 10 power cuts, restore in ≤100 ms (revised from the
   unverified 10 ms claim).
5. Alarms: asserted within 2 s of the condition persisting for its configured duration.
6. Diagnostics: each index reproducible from the recorded data set; no false positive on a 72 h
   clean baseline.
7. Interfaces: Modbus TCP map validated with a third-party master; `snmpwalk` returns the MIB;
   REST honours the token and the allowlist.
8. Stability: 72 h soak with no watchdog reset and CPU load below 60 %.

---

## 9. Change control and next actions

1. **Done — Revision C issued** in `Mathematical Model Specification REVC`, closing F-01, F-02, F-06,
   F-13 and F-14 (C-01…C-05). Thirteen findings remain open (§5.2) and drive the next steps.
2. Publish the interface annexes (Modbus TCP map, MIB, REST schemas, alarm codes) — closes F-03 and F-17.
3. Obtain the approved site drawing, the meter data sheet with the CT ratio, and the answers to
   D-01…D-03 and D-05…D-08.
4. Start P0: change the firmware RTD gain default to `40.403`, apply the already-planned OTA partition
   (human approval required), and sign off the remaining hardware interfaces.
5. Only then begin P1 implementation, test-first, against the components listed in §6.1.

This folder is now the requirements baseline: `client reference/` is the delivery as received, and
Revision C is the version to implement. The open findings in §5.2 are the outstanding work, not
defects in the reviewed text; no unresolved constant, threshold or register address in Revision C
should be treated as a production value.
