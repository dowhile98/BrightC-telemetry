# BrightC V3 — Requirements Folder

**What this folder contains, and what Revision F specifies**

| Item | Value |
| --- | --- |
| Folder | `doc/request` |
| Primary Specification | [`Mathematical_Model_Specification_REVF.md`](Mathematical_Model_Specification_REVF.md) — **Revision F** (English) |
| Source Specification (ES) | [`Mathematical_Model_Specification_REVF-ES.md`](Mathematical_Model_Specification_REVF-ES.md) — **Revisión F** (Spanish) |
| Client delivery | `client reference/` — three documents as received |
| Specification languages | English (Primary engineering baseline) / Spanish (Reference mirror) |
| This guide | English |

---

## 1. Contents

| Path | What it is | Language | Role |
| --- | --- | --- | --- |
| `client reference/ESPECIFICACIONES TÉCNICAS Y FUNCIONALES.docx` | Commercial and functional request: business case, edge-computing positioning, predictive alert matrix, dashboard and API expectations, security narrative. | Spanish | Initial delivery, kept verbatim |
| `client reference/Modelo Matematico.docx` | Early presentation of the mathematical model: two-probe concept, regression approach, diagnostic matrix. Page images, no editable text. | Spanish | Initial delivery, kept verbatim |
| `client reference/Telemetria predictiva de capacidad Climatica.docx` | Initial field and algorithm proposal: probe placement, sample register map, Python reference implementation, HMI layout. | Spanish | Initial delivery, kept verbatim |
| `Mathematical_Model_Specification_REVF.md` | Authoritative engineering specification at **Revision F**. | English | Primary engineering baseline |
| `Mathematical_Model_Specification_REVF-ES.md` | Spanish baseline specification at **Revisión F**. | Spanish | Source reference mirror |
| `README.md` | This guide. | English | Navigation and revision tracking |

### 1.1 `client reference/`

The material the client delivered at the start of the project. It defines the objective (telemetry, energy audit and predictive capacity analysis for critical cooling rooms in data centres and telco sites), the field installation concept, an early two-sensor deployment and a prototype algorithm.

These files are archived **as received**: they are not edited, corrected or reformatted.

### 1.2 `Mathematical_Model_Specification_REVF.md`

The engineering specification that turns the request into an implementable engineering baseline. It generalises the sensor field to an $(N+N)+M+L$ topology, decouples ventilation from compressor staging ($U_{vent\_ON}$), adopts Second Law / Carnot exergy metrics ($\eta_{II}$), mandates hardware 64-bit double precision for Ordinary Least Squares (OLS) with Tikhonov regularization, uses an exact 46-point Flash LUT for R410A thermodynamics, fixes the binary SD block to exactly 64 bytes, and details northbound interfaces.

---

## 2. Source Precedence

1. [`Mathematical_Model_Specification_REVF.md`](Mathematical_Model_Specification_REVF.md) is the authoritative engineering specification.
2. [`Mathematical_Model_Specification_REVF-ES.md`](Mathematical_Model_Specification_REVF-ES.md) is the Spanish language mirror.
3. If this README and the specification disagree, the specification wins.
4. `client reference/` is historical input. It explains requirements context, but is superseded by the engineering specifications.

---

## 3. What Revision F Specifies

| § | Content | Key Figures / Specifications |
| --- | --- | --- |
| 1 | Scope & operating philosophy | Passive parallel observer; zero interference with existing thermostats; real-time edge processing (1st & 2nd Law thermodynamics) |
| 2.1 | Thermal matrix, $(N + N) + M + L$ | $N$ supply/return pairs, $M$ room ambient probes, $L$ outdoor ambient probes; Pilot: $6 + 6 + 2 + 2 = 16$ PT100 Class A, 3-wire shielded |
| 2.2 | Temperature acquisition paths | Primary: `CWT-TM-16PT` 16-channel Modbus RTU (CH1–CH16 fully assigned); Internal stage: 5-channel RTD with `INA826` ($G_{eff} = 40.403$) kept in 100% reserve |
| 2.3 | Supply fan and compressor feedback | Digital inputs via optocoupled 24–48 V DC to `MCP23S17`; monitors supply fans ($U_{vent\_ON}$) and compressors ($c$); 50 Hz sampling, 500 ms debounce (25 samples) |
| 2.4 | Electrical metering & return humidity | `JSY-MK-339` over Modbus RTU (Active power at `0x0052`); industrial 4–20 mA transmitter at return grille ($HR_{ret}$) wired to `J9 (ADCx10)` with shunt and +12V loop power |
| 2.5 | 4–20 mA pressure reserve (Phase 2) | R410A pressure (0–45 bar) and filter differential pressure (0–500 Pa); 9 analog channels on `J9` kept free for Phase 2 |
| 3 | Compressor state machine | Per compressor: `OFF`, `INRUSH_WAIT` (0–15 s), `STEP_LOAD_CAPTURE` (15 s), `THERMAL_WARMUP` (15–180 s), `STEADY_STATE` (≥180 s); single-meter electrical disaggregation |
| 4.1–4.4 | Thermodynamic & psychrometric engine | Callendar-Van Dusen; barometric correction by altitude; $\dot{m}_{da,total}$ governed by fans $U_{vent\_ON}$; continuous condensation regime; $Q_{sensible}$, $Q_{latente}$, specific enthalpies $h_{ret,k}, h_{inj,k}$ |
| 4.5 | Carnot / Rankine & Second Law | Temperature lift between return air and outdoor condenser air ($\overline{T}_{ext}$); internal cycle saturation temperatures with pinch; $COP_{real}$, $COP_{Carnot}$, and weather-invariant exergetic efficiency $\eta_{II}$ (35%–50%) |
| 4.6 | Phase 2 R410A via Flash LUT | Static 46-point Flash LUT (`const float R410A_TSAT_LUT[46]`, 0 to 45 bar gauge) with $\mathcal{O}(1)$ linear interpolation (three FPU cycles) |
| 5.1 | OLS baseline & hardware DP-FPU | 7-day steady-state learning: $\widehat{Q}_{total,esp} = \beta_0 + \beta_1 \overline{T}_{ret} + \beta_2 \overline{T}_{ext} + \beta_3 F_{carga}$; mandatory 64-bit IEEE 754 `double` on STM32H7 DP-FPU; Tikhonov regularization $\lambda I_4$ ($\lambda = 10^{-6}$) |
| 5.2 | Predictive diagnostic rules | Filter restriction with 168-hour initialization lockout and 90-day ceiling; outdoor-decoupled refrigerant leak / compressor wear ($\eta_{II}$ drop); room hotspots; short cycling |
| 5.4 | Free capacity margin | High-ambient thermal derating ($k_{ext} = 0.5\%/^\circ\text{C}$ above $35^\circ\text{C}$); $Q_{max\_safe}$ in $N+1$ redundancy; $kW_{free}$ |
| 6 | Storage architecture | RAM: 720 hourly summaries (30 days) = 46.08 KB; EEPROM `AT24C256C`: topology, 64-bit OLS coefficients, baselines; MicroSD: exact 64-byte hourly record, 560.6 KB/year; §6.4 flash partitioning prerequisite |
| 7 | Interfaces | DWIN 4.3" HMI; Modbus TCP Server (port 502); SNMP v2c/v3 agent with traps/informs; RESTful JSON API with Bearer token & IP allowlist |

---

## 4. Revision History Summary

### Revision F (Current Engineering Baseline)
- **F-01 (§2.3, §4.3.4):** Airflow mass flow summation ($\dot{m}_{da,total}$) decoupled from compressors and bound to supply fan contactors ($U_{vent\_ON}$).
- **F-02 (§4.6.2):** Replaced polynomial and transcendental Antoine equations with a static 46-point Flash LUT for R410A dew-point saturation from 0 to 45 bar.
- **F-03 (§5.1):** Enforced 64-bit double precision (`double`) on hardware DP-FPU for OLS normal equations ($X^T X$, $X^T Y$) and matrix inversion, with Tikhonov regularization ($\lambda = 10^{-6}$).
- **F-04 (§5.2.1):** Established strict precedence hierarchy and 168-hour initialization lockout (`100 days` / `0xFFFF`) for filter "Days Remaining" projection.

### Revision E
- Added outdoor ambient sensors ($L=2$, $T_{ext}$) to form $(N+N)+M+L$ topology (16 PT100 probes total).
- Introduced Second Law thermodynamic model: $COP_{Carnot}$ and exergetic efficiency $\eta_{II}$ as climate-invariant health metric.
- Upgraded humidity sensing from digital I²C to industrial 4–20 mA current loop on the return grille ($HR_{ret}$) via connector `J9`.
- Fixed MicroSD hourly record layout to exactly 64 bytes.

### Earlier Revisions (B → C)
- Verified INA826 gain stage ($G_{INA} = 80.806$, $G_{effective} = 40.403$).
- Documented 24–48 V DC input conditioning before `MCP23S17`.
- Documented bus pre-commissioning address assignment.
- Identified flash memory layout prerequisite for networked application image.

---

## 5. Firmware Implementation Mapping

All thermodynamic, psychrometric, OLS, and diagnostic algorithms comply with Clean Architecture:
- **Pure Domain (`app/src/domain/`):** Pure ISO C99/C11, standard libc headers only, host-executable under POSIX `native_sim` via Ztest.
- **Abstract Ports (`app/include/interfaces/`):** Strict compliance with §4.1.D of `STRUCTURE.md` (v-table + handle + static inline validated wrappers).
- **Adapters (`app/src/infrastructure/adapters/`):** Zephyr drivers for `CWT-TM-16PT` (Modbus), `JSY-MK-339` (Modbus), `MCP23S17` (SPI GPIO), `ADCx10` (DMA), EEPROM (`microconf`), and MicroSD (`sdmmc1` / FATFS / LittleFS).
