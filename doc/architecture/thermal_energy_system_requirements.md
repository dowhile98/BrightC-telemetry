# Thermal and Energy System: Requirements Specification (Revision F Baseline)

- **Document Identifier**: `doc/architecture/thermal_energy_system_requirements.md`
- **Subsystem**: Thermal and Energy System (`thermal_energy`)
- **Target Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Framework**: Zephyr RTOS (West T2 Workspace)
- **Reference Specification**: `doc/request/Mathematical_Model_Specification_REVF.md` (Revision F Baseline)
- **Author**: Lead Embedded Firmware Architect
- **Status**: Stage 1 Architecture Deliverable (Ready for Technical Review)

---

## 1. Scope & Operating Philosophy

The **Thermal and Energy System** (`thermal_energy`) serves as the edge thermodynamic auditing, psychrometric evaluation, and predictive analytics engine for the BrightC Controller V3 in mission-critical telecommunications and data center climate control rooms.

### 1.1 Operating Philosophy: Parallel Passive Observer
1. **Passive Operation**: The subsystem acts as a **parallel, non-intrusive observer**. It acquires physical sensor data and contact states to calculate mass, energy, and exergy balances without modifying, overriding, or interfering with existing site thermostat or sequencer control loops.
2. **Deterministic Edge Computing**: All psychrometric properties, enthalpy balances, First/Second Law efficiencies, and Ordinary Least Squares (OLS) regressions execute locally on the STM32H7 microcontroller in real time.
3. **Decoupled Telemetry Distribution**: Results are maintained in a thread-safe, immutable snapshot and exposed to consumers (Local HMI, Modbus TCP Server, SNMP Agent, RESTful API, MicroSD Datalogger) via clean abstract contracts.

---

## 2. Functional Scope: Phase 1 vs Phase 2

### 2.1 Scope of Phase 1 (Included in this Version)
- **Temperature Matrix Acquisition**: Ingestion and mapping of up to 16 temperature probes in the pilot scheme $(N+N)+M+L = (6+6)+2+2$:
  * $N = 6$ cold air supply probes ($T_{inj,1} \dots T_{inj,6}$).
  * $N = 6$ warm air return probes ($T_{ret,1} \dots T_{ret,6}$).
  * $M = 2$ room ambient probes ($T_{sala,1}, T_{sala,2}$).
  * $L = 2$ outdoor ambient condenser reference probes ($T_{ext,1}, T_{ext,2}$).
- **Sensor Index Mapping**: Dynamic assignment of flat sensor IDs (1..59) to semantic pairs and probes without hardcoded physical bindings.
- **Return Grille Humidity Acquisition**: Ingestion of industrial 4–20 mA relative humidity transmitter ($HR_{ret}$) on connector `J9`, selectable via Kconfig, with NAMUR NE43 loop break detection (< 3.5 mA).
- **Total Electrical Metering**: Ingestion of 3-phase electrical parameters from the existing `JSY-MK-339` Modbus meter ($P_{elec\_total}$, voltages, currents, power factors, kWh).
- **Physical Equipment Run Feedback**: Debounced auxiliary contacts for evaporator supply fans ($U_{vent\_ON}$) and compressors ($c_1 \dots c_4$).
- **Compressor Stabilization State Machine**: 5-state temporal sequence per compressor (`OFF`, `INRUSH_WAIT`, `STEP_LOAD_CAPTURE`, `THERMAL_WARMUP`, `STEADY_STATE`).
- **Psychrometric & Thermodynamic Engine**: Real-time evaluation of $P_{atm}(Z)$, saturation and partial vapor pressure ($P_{ws}, P_w$), absolute humidity ($\omega_{ret}$), dew point ($T_{rocio}$), dry air density ($\rho_{da}$), fan-governed dry air mass flow rate ($\dot{m}_{da}$), sensible capacity ($Q_{sensible}$), latent capacity ($Q_{latente}$), total capacity ($Q_{total}$), condensate flow rate ($\dot{m}_{cond}$), Sensible Heat Factor ($SHF$), $COP_{real}$, $COP_{sensible}$, and Second Law exergetic efficiency ($\eta_{II}$).
- **Predictive Analytics & Baseline Learning**:
  * 168-hour steady-state baseline training via double-precision regularized OLS.
  * Expected capacity $\widehat{Q}_{total,esp}$, expected COP $\widehat{COP}_{esperado}$, and % mechanical degradation.
  * Filter restriction index ($\% \text{Filter}_k$) with 168-hour lockout hierarchy and days-remaining projection.
  * Outdoor-temperature-decoupled refrigerant leak / coil fouling detection ($\% \text{Leak}_k$).
  * Room spatial hotspots ($\Delta T_{sala,max} > 3.5\,^\circ\text{C}$) and airflow bypass ($\Delta T_{bypass} > 3.0\,^\circ\text{C}$).
  * Latent moisture infiltration alerts ($\% \text{Latent} > 30\%$ or $\omega_{ret} > 0.012\text{ kg/kg}$).
  * Electromechanical short-cycle detection ($t_{off} < 180\text{ s}$).
  * Weather-corrected derating ($Q_{nom,corr}$) and safe free cooling reserve ($kW_{free}$).
- **Hybrid Storage & History**:
  * Tier 1: STM32H7 RAM retaining 720 hourly summaries (30 days = 46.08 KB).
  * Tier 2: `AT24C256C` EEPROM storing site topology, calibration trim, and baseline signature.
  * Tier 3: Industrial MicroSD binary datalogging (exact 64-byte hourly records).

### 2.2 Exclusions in Phase 1 (Phase 2 Provisions)
1. **No Physical Pressure Transducers**:
   - Refrigerant high/low gauge pressure sensors ($0\text{–}45\text{ bar}$) are **not installed**.
   - Differential air filter pressure sensors ($0\text{–}500\text{ Pa}$) are **not installed**.
2. **Prohibited Calculations in Phase 1**:
   - Dynamic airflow correction via filter $\Delta P_{air}$ (Section 4.6.1) MUST NOT be calculated; nominal airflow $V_{nom,u}$ must be used directly.
   - Exact R410A saturation lookup via 46-point Flash LUT (Section 4.6.2) is kept as a provisioned data structure, but MUST NOT be fed with fictitious pressure readings.
   - Pressure-instrumented diagnostics (Section 5.3) for refrigerant pressure thresholds ($P_{bar} < 6.5\text{ bar}$ or $P_{bar} > 11.5\text{ bar}$) MUST NOT be executed.
3. **Zero Fictitious Data Rule**:
   - Firmware **MUST NEVER** synthesize fake pressure values or assume compressors/fans are running when contacts are open or unmapped. Unavailable metrics must report an explicit status (`UNAVAILABLE`), never an erroneous synthetic number.

---

## 3. Detailed Functional Requirements

### 3.1 Temperature Ingestion & Logical Topology Mapping
- **REQ-TEMP-01**: The system shall acquire temperatures from registered flat sensor IDs (1..59) gathered via external Modbus RTU (`CWT-TM-16PT`) and internal RTD stages (`INA826`).
- **REQ-TEMP-02**: The system shall support parametric configuration of $N$ thermal pairs ($1 \le N \le 16$, default 6), $M$ room ambient probes ($1 \le M \le 16$, default 2), and $L$ outdoor probes ($1 \le L \le 8$, default 2).
- **REQ-TEMP-03**: Each thermal pair $k \in \{0 \dots N-1\}$ shall be defined by a configuration tuple `{supply_flat_id, return_flat_id, hvac_unit_id}`.
- **REQ-TEMP-04**: The system shall validate that:
  - Both `supply_flat_id` and `return_flat_id` reference registered, valid sensor IDs in `sensor_hardware_map.h`.
  - `supply_flat_id` and `return_flat_id` are not identical.
  - No sensor ID is assigned to more than one role simultaneously.
  - Unassigned probes are designated by flat ID `0`.
- **REQ-TEMP-05**: Pair health consensus: Pair $k$ shall be marked valid (`pair_valid_mask` bit $k = 1$) **if and only if** both supply and return probes are configured, healthy, connected (not open-circuit), and within physical bounds ($[-20.0\,^\circ\text{C}, +150.0\,^\circ\text{C}]$).
- **REQ-TEMP-06**: If either probe of pair $k$ is disconnected, open-circuit, or out of range, the pair differential $\Delta T_k$ and dependent capacity calculations shall be marked `INVALID`.

### 3.2 4–20 mA Return Humidity Transducer
- **REQ-HUM-01**: The system shall acquire the return relative humidity signal ($HR_{ret}$) from an analog current loop on connector `J9 (ADCx10)`.
- **REQ-HUM-02**: The physical J9 channel (1..10) shall be selectable at build time via `CONFIG_BRIGHTC_THERMAL_HUMIDITY_J9_CHANNEL`.
- **REQ-HUM-03**: The system shall convert raw ADC counts to loop current:
  $$I_{loop} = \frac{V_{ADC}}{R_{shunt}} \times 1000 \quad [\text{mA}]$$
  where $R_{shunt} = 150.0\,\Omega \pm 0.1\%$.
- **REQ-HUM-04**: NAMUR NE43 Loop Fault Detection:
  - If $I_{loop} < 3.5\text{ mA}$, the system shall declare a **Loop Break Fault** (`HR_ret` quality = `INVALID`, alarm condition asserted).
  - If $3.5\text{ mA} \le I_{loop} < 4.0\text{ mA}$, the measurement shall be clamped to $0.0\%RH$ and marked `LIMITED`.
  - If $4.0\text{ mA} \le I_{loop} \le 20.0\text{ mA}$, $HR_{ret}$ shall be scaled linearly:
    $$HR_{ret} = \text{clamp}_{0..100}\left(\frac{I_{loop} - 4.0}{16.0} \times 100.0\right) \quad [\%]$$
  - If $20.0\text{ mA} < I_{loop} \le 21.0\text{ mA}$, $HR_{ret}$ shall be clamped to $100.0\%RH$ and marked `LIMITED`.
  - If $I_{loop} > 21.0\text{ mA}$, the system shall declare an **Over-Current Fault** (`HR_ret` quality = `INVALID`).

### 3.3 Equipment Auxiliary Contacts & Airflow Governance
- **REQ-FEEDBACK-01**: The system shall monitor optocoupled auxiliary contacts for evaporator supply fans ($U_{vent\_ON}$) and compressors ($c_1 \dots c_4$).
- **REQ-FEEDBACK-02**: Contactor signals shall be debounced at 50 Hz (20 ms interval) requiring 25 consecutive stable samples (500 ms) before state commitment per REVF §2.3.
- **REQ-FEEDBACK-03 (Decoupling Rule F-01)**: Dry air mass flow ($\dot{m}_{da,total}$) and pair airflow contributions shall be governed **strictly** by confirmed active fan contacts ($u \in U_{vent\_ON}$), irrespective of compressor thermostat status.
- **REQ-FEEDBACK-04**: A fan or compressor state shall be evaluated as:
  - `CONFIRMED_ON`: Contact asserted and debounced.
  - `CONFIRMED_OFF`: Contact open and debounced.
  - `UNKNOWN`: Contact unmapped, disabled, or source communication lost.
- **REQ-FEEDBACK-05**: Under no circumstances shall firmware assume an unmapped or unknown fan is running. If fan feedback is `UNKNOWN`, its associated unit mass flow shall be evaluated as $0.0\text{ kg/s}$, and its capacity flagged as `UNAVAILABLE`.

### 3.4 Compressor Temporal State Machine
- **REQ-FSM-01**: The system shall maintain an independent 5-phase temporal state machine for each configured compressor $c$:
  1. `STATE_OFF`: Contact open; tracks continuous stopped duration $t_{off}$. If restarted with $t_{off} < 180\text{ s}$, increment short-cycle counter.
  2. `STATE_INRUSH_WAIT` ($0 < t_{on} < 15\text{ s}$): Contact asserted; discard electrical samples during motor start inrush.
  3. `STATE_STEP_LOAD_CAPTURE` ($t_{on} = 15\text{ s}$): Measure net electrical power step:
     $$\Delta P_{comp,c} = P_{elec\_total}(t_{on} = 15\text{ s}) - P_{elec\_total}(t_{on} = 0^-)$$
     Lockout rule: If another fan or compressor transitions within this 15 s window, mark $\Delta P_{comp,c}$ as `UNAVAILABLE`.
  4. `STATE_THERMAL_WARMUP` ($15\text{ s} < t_{on} < 180\text{ s}$): Display live temperatures on HMI/Modbus, but **block OLS accumulation and predictive alerts**.
  5. `STATE_STEADY_STATE` ($t_{on} \ge 180\text{ s}$): Full thermodynamic evaluation enabled; hourly historical logging and OLS accumulation permitted.

### 3.5 Psychrometric & Thermodynamic Balances
- **REQ-MATH-01**: Local barometric pressure:
  $$P_{atm} = 101.325 \times \left(1.0 - 2.25577 \times 10^{-5} \cdot Z\right)^{5.25588} \quad [\text{kPa}]$$
  where $Z$ is persisted site altitude in meters above sea level.
- **REQ-MATH-02**: Saturation and return vapor pressure via Magnus-Tetens:
  $$P_{ws}(\overline{T}_{ret}) = 0.61078 \cdot \exp\left(\frac{17.27 \cdot \overline{T}_{ret}}{\overline{T}_{ret} + 237.3}\right) \quad [\text{kPa}]$$
  $$P_{w,ret} = P_{ws}(\overline{T}_{ret}) \cdot \left(\frac{HR_{ret}}{100.0}\right) \quad [\text{kPa}]$$
- **REQ-MATH-03**: Absolute humidity (humidity ratio):
  $$\omega_{ret} = 0.62198 \cdot \frac{P_{w,ret}}{P_{atm} - P_{w,ret}} \quad \left[\text{kg}_{\text{vapor}} / \text{kg}_{\text{dry air}}\right]$$
  Domain check: Enforce $P_{atm} - P_{w,ret} > 5.0\text{ kPa}$; reject invalid inputs.
- **REQ-MATH-04**: Return dew point temperature:
  $$\gamma = \ln\left(\frac{P_{w,ret}}{0.61078}\right) \implies T_{rocio,ret} = \frac{237.3 \cdot \gamma}{17.27 - \gamma} \quad [{}^\circ\text{C}]$$
- **REQ-MATH-05**: Return dry air density:
  $$\rho_{da,ret} = \frac{P_{atm} - P_{w,ret}}{0.287055 \cdot (\overline{T}_{ret} + 273.15)} \quad \left[\text{kg}_{\text{dry air}} / \text{m}^3\right]$$
- **REQ-MATH-06**: Total dry air mass flow rate:
  $$\dot{m}_{da,total} = \sum_{u \in U_{vent\_ON}} \left(\frac{V_{nom,u}}{3600.0}\right) \times \rho_{da,ret} \quad [\text{kg/s}]$$
- **REQ-MATH-07**: Sensible cooling capacity:
  $$Q_{sensible,k} = \dot{m}_{da,k} \cdot (1.006 + 1.86 \cdot \omega_{ret}) \cdot (T_{ret,k} - T_{inj,k}) \quad [\text{kW}]$$
  $$Q_{sensible\_total} = \sum_{k \in U_{vent\_ON}} Q_{sensible,k} \quad [\text{kW}]$$
- **REQ-MATH-08**: Condensation regime and supply humidity ratio $\omega_{inj,k}$:
  - If $T_{inj,k} \ge T_{rocio,ret}$ (Dry Regime): $\omega_{inj,k} = \omega_{ret}$, $Q_{latente,k} = 0.0\text{ kW}$.
  - If $T_{inj,k} < T_{rocio,ret}$ (Active Condensation):
    $$P_{w,inj,k} = 0.92 \times 0.61078 \cdot \exp\left(\frac{17.27 \cdot T_{inj,k}}{T_{inj,k} + 237.3}\right) \quad [\text{kPa}]$$
    $$\omega_{inj,k} = \min\left(\omega_{ret}, \; 0.62198 \cdot \frac{P_{w,inj,k}}{P_{atm} - P_{w,inj,k}}\right)$$
- **REQ-MATH-09**: Latent capacity and condensate flow rate ($h_{fg} = 2465.0\text{ kJ/kg}$):
  $$Q_{latente,k} = \dot{m}_{da,k} \cdot h_{fg} \cdot \max(0.0, \; \omega_{ret} - \omega_{inj,k}) \quad [\text{kW}]$$
  $$Q_{latente\_total} = \sum_{k \in U_{vent\_ON}} Q_{latente,k} \quad [\text{kW}]$$
  $$\dot{m}_{cond} = \left(\frac{Q_{latente\_total}}{h_{fg}}\right) \times 3600.0 \quad [\text{Liters/hour}]$$
- **REQ-MATH-10**: Specific enthalpies, total capacity, and SHF:
  $$h_{ret,k} = 1.006 \cdot T_{ret,k} + \omega_{ret} \cdot (2501.0 + 1.86 \cdot T_{ret,k}) \quad [\text{kJ/kg}]$$
  $$h_{inj,k} = 1.006 \cdot T_{inj,k} + \omega_{inj,k} \cdot (2501.0 + 1.86 \cdot T_{inj,k}) \quad [\text{kJ/kg}]$$
  $$Q_{total} = Q_{sensible\_total} + Q_{latente\_total} \quad [\text{kW}]$$
  $$SHF = \frac{Q_{sensible\_total}}{\max(0.001, \; Q_{total})}$$
- **REQ-MATH-11**: First Law COP and Exergetic Efficiency:
  $$COP_{real} = \frac{Q_{total}}{P_{elec\_total}} \quad ; \quad COP_{sensible} = \frac{Q_{sensible\_total}}{P_{elec\_total}}$$
  $$T_{evap,sat} = (\overline{T}_{inj} - 8.0) + 273.15 \quad [\text{K}] \quad ; \quad T_{cond,sat} = (\overline{T}_{ext} + 10.0) + 273.15 \quad [\text{K}]$$
  $$COP_{Carnot} = \frac{T_{evap,sat}}{\max(5.0, \; T_{cond,sat} - T_{evap,sat})}$$
  $$\eta_{II} = \left(\frac{COP_{real}}{COP_{Carnot}}\right) \times 100.0 \quad [\%]$$

### 3.6 Predictive Analytics & OLS Regression
- **REQ-PRED-01 (Mandate F-03)**: During the initial 168 hours of steady state, accumulate the OLS moment matrix $X^T X \in \mathbb{R}^{4 \times 4}$ and vector $X^T Y \in \mathbb{R}^{4 \times 1}$ in **IEEE 754 64-bit double precision (`double`)**.
- **REQ-PRED-02**: Apply diagonal Tikhonov regularization with $\lambda = 10^{-6}$:
  $$\beta = \left(X^T X + \lambda I_4\right)^{-1} X^T Y$$
- **REQ-PRED-03**: Compute expected capacity and % mechanical degradation:
  $$\widehat{Q}_{total,esp} = \beta_0 + \beta_1 \cdot \overline{T}_{ret} + \beta_2 \cdot \overline{T}_{ext} + \beta_3 \cdot F_{carga}$$
  $$\widehat{COP}_{esperado} = \frac{\widehat{Q}_{total,esp}}{P_{elec\_total}}$$
  $$\% \text{Degradation} = \text{clamp}_{0..100}\left(\max\left(\frac{\widehat{COP}_{esperado} - COP_{real}}{\widehat{COP}_{esperado}}, \; \frac{\eta_{II,\text{base}} - \eta_{II}}{\eta_{II,\text{base}}}\right) \times 100\right)$$
- **REQ-PRED-04 (Precedence Hierarchy F-04)**: For air filter fouling diagnostics:
  $$\% \text{Filter}_k = \text{clamp}_{0..100}\left(60.0 \cdot \frac{T_{inj\_base,k} - T_{inj,k}}{3.0} + 40.0 \cdot \frac{\Delta T_k - \Delta T_{base,k}}{4.0}\right)$$
  Days Remaining shall evaluate strictly in priority:
  1. Window incomplete ($N_{hours\_RAM} < 168\text{ h}$): Lock to initialization code `100 days` (`0xFFFF` in Modbus map).
  2. Clean filter ($\% \text{Filter}_k < 25\%$ or $m_{T,k} \ge -0.05\,^\circ\text{C/day}$): Clamp to healthy ceiling `90 days`.
  3. Active degradation ($N_{hours\_RAM} \ge 168$, $\% \text{Filter}_k \ge 25\%$, $m_{T,k} < -0.05\,^\circ\text{C/day}$):
     $$\text{Days Remaining}_k = \min\left(90.0, \; \frac{\max(0.0, \; T_{inj,k} - 7.5)}{\max(|m_{T,k}|, \; 0.05)}\right)$$
- **REQ-PRED-05**: Refrigerant leak detection decoupled from outdoor temperature:
  $$\% \text{Leak}_k = \text{clamp}_{0..100}\left(50.0 \cdot \frac{(\Delta T_{base,k} \cdot F_{carga}) - \Delta T_k}{0.5 \cdot (\Delta T_{base,k} \cdot F_{carga})} + 50.0 \cdot \frac{\eta_{II,\text{base}} - \eta_{II}}{0.35 \cdot \eta_{II,\text{base}}}\right)$$
  (Evaluated only when $\% \text{Filter}_k < 50\%$).
- **REQ-PRED-06**: Hotspot detection ($\Delta T_{sala,max} = T_{sala,max} - T_{sala,min} > 3.5\,^\circ\text{C}$) and Airflow Bypass ($\Delta T_{bypass} = \overline{T}_{sala} - \overline{T}_{ret} > 3.0\,^\circ\text{C}$).
- **REQ-PRED-07**: Latent moisture ingress alert ($\% \text{Latent} > 30\%$ or $\omega_{ret} > 0.012\text{ kg/kg}$).
- **REQ-PRED-08**: Weather-corrected nominal capacity and free cooling margin:
  $$Q_{nom,corr,u} = Q_{nominal,u} \times [1.0 - 0.005 \cdot \max(0.0, \; \overline{T}_{ext} - 35.0)] \quad [\text{kW}]$$
  $$Q_{max\_safe} = \left(\sum_{u=1}^{U_{req}} Q_{nom,corr,u}\right) \times \left(1.0 - \frac{\% \text{Degradation}}{100.0}\right) \quad [\text{kW}]$$
  $$kW_{free} = \max(0.0, \; Q_{max\_safe} - Q_{sensible\_total}) \quad [\text{kW}]$$

---

## 4. Data Quality & Validity Contracts

To ensure complete auditability, every input variable and computed result shall carry a quality enumeration:

```c
typedef enum {
    QUALITY_VALID = 0,         /**< Measurement/result physically valid, fresh and healthy */
    QUALITY_LIMITED,           /**< Computed under declared assumptions or physical clamps */
    QUALITY_UNAVAILABLE,       /**< Input not installed, unmapped, or Phase 2 provision */
    QUALITY_STALE,             /**< Data timestamp exceeds maximum coherence age */
    QUALITY_INVALID,           /**< Acquisition error, loop break, open circuit, or NaN */
    QUALITY_OUT_OF_DOMAIN      /**< Input violates thermodynamic domain (e.g. Patm <= Pw) */
} data_quality_t;
```

### 4.1 Temporal Coherence Contract
- A snapshot calculation requires inputs from a consistent time window.
- The maximum allowed age discrepancy between temperature samples, electrical power, and digital contact states is **$1000\text{ ms}$** (the 1 Hz calculation interval).
- If electrical power is older than $3000\text{ ms}$, COP and exergy calculations shall transition to `QUALITY_STALE`.
- If temperatures are older than $2000\text{ ms}$, all thermodynamic balances shall transition to `QUALITY_STALE`.

---

## 5. Non-Functional Requirements

1. **Clean Architecture & Domain Purity (§4.1.D)**:
   - All mathematical engines (`psychrometrics.c`, `thermodynamics.c`, `compressor_fsm.c`, `predictive_analytics.c`) shall reside in `app/src/domain/thermal/`.
   - Domain code **MUST NOT** include `<zephyr/...>` headers, hardware registers, or driver headers.
   - Permitted dependencies: ISO C99 standard headers (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<math.h>`, `<errno.h>`).
2. **Zero Dynamic Memory Allocation**:
   - Runtime execution loops shall never call `malloc`, `free`, `k_malloc`, or `k_heap`.
   - All buffers (including the 720-hour RAM ring buffer) must be statically allocated at compile time.
3. **MPU Stack Guard Safety (Cortex-M7)**:
   - Active Object stacks shall be allocated at file scope using `K_THREAD_STACK_DEFINE`. Never embed `k_thread_stack_t` in `.bss` structs.
4. **Deterministic Execution Time**:
   - The entire 1 Hz evaluation cycle (psychrometrics, balances, FSM, and diagnostics) shall complete in **under $5\text{ ms}$** of STM32H7 CPU execution time.
   - The hourly OLS matrix inversion shall complete in **under $200\,\mu\text{s}$** with hardware DP-FPU enabled.
5. **Thread Safety & Consumer Independence**:
   - The thermal subsystem shall publish an immutable generation event over Zbus when new results are ready.
   - External consumers shall read snapshots via a mutex-protected copy-out query port (`i_thermal_energy_query_t`). The producer thread shall never block waiting for external communication tasks.
