# TECHNICAL SPECIFICATION AND ENGINEERING MATHEMATICAL MODEL (REVISION F — PROJECT BASELINE)
**Project:** Telemetry, Energy Auditing, and Predictive Edge Computing System for Critical Climate Control Rooms (Data Centers / Telecom Nodes)  
**Hardware Platform:** BrightC V3 Industrial Controller (ARM Cortex-M7 STM32H7 Architecture with 64-bit DP-FPU)  
**Document Status:** Revision F (Final Engineering Baseline) — Mass flow correction tied to evaporator supply fan contactors $U_{vent\_ON}$ (F-01), adoption of static Lookup Table (46-point LUT) for R410A in Phase 2 (F-02), mandatory IEEE 754 double-precision arithmetic (64-bit `double` on hardware DP-FPU) with Tikhonov regularization for OLS matrix inversion (F-03), and 168-hour initialization/precedence hierarchy in filter days remaining projection (F-04). Supersedes Revision E.

### Change Log (E → F)

| ID | Section | Change |
| --- | --- | --- |
| F-01 | 2.3, 4.3.4 | **Decoupling Ventilation from Compression ($U_{vent\_ON}$):** The dry air mass flow summation ($\dot{m}_{da,total}$) is corrected by replacing $U_{ON}$ (compressors) with $U_{vent\_ON}$, mapped strictly to the active auxiliary contacts of the **evaporator supply fan contactors**. This preserves enthalpy balance validity when compressors cycle off on thermostat but air handlers continue recirculating air in the room. |
| F-02 | 4.6.2 | **R410A Saturation Curve via 46-Point Flash LUT:** The inverse Antoine logarithmic equation is removed (eliminating scale-offset drift and the computational burden of runtime `ln()` at $50\text{ Hz}$). A **static 46-point lookup table (LUT from $0\text{ to }45\text{ bar}$)** stored in Flash memory with deterministic $\mathcal{O}(1)$ linear interpolation is established as the Phase 2 standard. |
| F-03 | 5.1 | **Double-Precision OLS Linear Algebra (64-bit `double` on DP-FPU):** To prevent numerical ill-conditioning (collinearity of $\overline{T}_{ret}$ in tightly regulated rooms), moment accumulation $X^T X$, vector $X^T Y$, and matrix inversion are mandated to run in **64-bit double precision (`double`)** using the **STM32H7 hardware DP-FPU**, incorporating diagonal Tikhonov regularization ($\lambda I_4$) against singular matrices. |
| F-04 | 5.2.1 | **Precedence Hierarchy and Initialization Lockout for "Days Remaining":** A deterministic hierarchy is established for Modbus TCP / SNMP / REST API registers: during the initial $168\text{ hours}$ (incomplete historical buffer), the system reports a fixed initialization code (`100 days` in telemetry / `0xFFFF` in raw diagnostics); once trained, if $\% \text{Filter}_k < 25\%$ or $m_{T,k} \ge 0$, it clamps to a healthy ceiling of `90 days`, evaluating the differential rate equation only when genuine fouling degradation is detected. |

---

## 1. SCOPE AND OPERATING PHILOSOPHY

The **BrightC V3** system functions as an edge computing, energy auditing, and predictive analytics unit monitoring real-time thermodynamic, psychrometric, electrical, and mechanical performance of air conditioning systems in critical telecom and data center facilities (redundant configurations $2+1$, $3+1$, etc.).

The controller acts as a **parallel passive observer** (without interfering with the existing on-site thermostat control loops), acquiring direct physical variables (including indoor and outdoor boundary conditions) to compute mass, energy, and entropy/exergy balances (reverse Carnot/Rankine cycles), isolate electromechanical transients, establish empirical room baseline signatures, and publish predictive diagnostics to upper-tier management platforms via **Modbus TCP Server** (Huawei NetEco), **SNMP**, and a **Local RESTful API (HTTP/JSON)**, in addition to visualization on the local touch screen (**DWIN 4.3" HMI**).

---

## 2. CONFIGURABLE HARDWARE AND SENSOR ARCHITECTURE

The system design is **100% parametric and configuration-driven**. It dynamically defines the number of indoor temperature sensor pairs, room ambient sensors, outdoor ambient sensors, air handling units (HVAC), supply fans, and compressors per unit.

### 2.1. Thermal Matrix Topology: $(N + N) + M + L$ Scheme
Temperature acquisition is structured under an architecture of **paired thermal couples ($N + N$)**, a **room ambient monitoring network ($M$)**, and **outdoor ambient climate references ($L$)**:

1. **Thermodynamic Evaluation Pairs across Evaporators ($N$ Supply + $N$ Return):**
   * Measurement points on the internal cooling airflow are **always installed in pairs ($1+1$, $2+2$, $\dots$, $N+N$)**.
   * Each pair $k \in \{1 \dots N\}$ consists of a **Cold Air Supply Probe ($T_{inj,k}$)** and its paired **Warm Air Return Probe ($T_{ret,k}$)**.
2. **Room Ambient Temperature Sensors ($M$ Sensors):**
   * Independent of duct/equipment pairs, the system incorporates $M$ additional sensors ($T_{sala,m}$, where $m \in \{1 \dots M\}$) distributed along aisles or critical rack zones to audit room temperatures, detect hotspots, and evaluate airflow distribution.
3. **Outdoor Ambient / Condensing Temperature Sensors ($L$ Sensors):**
   * $L$ temperature sensors are deployed outside the shelter/facility ($T_{ext,l}$, where $l \in \{1 \dots L\}$), located near the outdoor condensing unit air intakes (shielded from direct solar radiation).
   * They measure the heat sink temperature into which compressors reject heat—an indispensable boundary condition to evaluate Carnot thermodynamic limits, maximum capacity derating, and vapor compression cycles (reverse Rankine).
4. **First Pilot Site Sizing ($6 + 6 + 2 + 2 = 16\text{ PT100 Probes}$):**
   * **$N = 6$ Cold Supply Air Probes:** $T_{inj,1} \dots T_{inj,6}$.
   * **$N = 6$ Warm Return Air Probes:** $T_{ret,1} \dots T_{ret,6}$.
   * **$M = 2$ Room Ambient Temperature Probes:** $T_{sala,1}, T_{sala,2}$.
   * **$L = 2$ Outdoor Temperature Probes:** $T_{ext,1}, T_{ext,2}$.
   * **Pilot Total:** **16 Class-A 3-wire shielded RTD PT100 sensors**.

### 2.2. Temperature Acquisition Paths (Internal ADC vs. External RS-485 Modbus)
To support compact setups up to dense deployments such as the 16-sensor pilot ($6+6+2+2$), two complementary acquisition paths are provided:

1. **RS-485 Digital Acquisition Module (`CWT-TM-16PT` — Primary Path for Pilot $6+6+2+2$):**
   * Industrial **16-channel PT100** concentrator with native 3-wire compensation on terminal blocks, connected to the BrightC V3 RS-485 bus via **Modbus RTU**.
   * Accommodates all **16 pilot probes** on a single module (`CH1` to `CH6` for Supply, `CH7` to `CH12` for Return, `CH13` to `CH14` for Room Ambient, and `CH15` to `CH16` for Outdoor Condenser Temperature), delivering all readings in a single Modbus transaction with $0.1\,^\circ\text{C}$ resolution or IEEE 754 floating point.
   * The 5 on-board internal RTD channels of the BrightC V3 remain 100% available as spare or future expansion.
2. **On-Board Internal Analog Stage on BrightC V3 (Up to 5 RTD Channels with `INA826`):**
   * Available for compact sites or supplemental channels. Based on a Wheatstone bridge at $3.3\text{ V DC}$ ($R_{sup} = 5.1\text{ k}\Omega$ at $0.1\%$, fixed reference $91\,\Omega$), **INA826** instrumentation amplifier ($G_{effective} = 40.403$), and sampling via the STM32H7 16-bit ADC.
   * **Verified Gain Chain:** The INA826 operates with $R_g = 619\,\Omega$ between pins 1 and 8 ($G_{INA} = 1 + 49.4\,\text{k}\Omega / 619\,\Omega = 80.806$). The amplifier output passes through an $R_{24}/R_{26} = 10\,\text{k}\Omega/10\,\text{k}\Omega$ voltage divider ($C_8 = 10\,\mu\text{F}$ filter), yielding $G_{effective} = 80.806 / 2 = 40.403$ seen by the ADC.
   * Includes software **lead wire resistance compensation ($\Delta R_{cable}$)** stored in EEPROM to zero out wiring resistance prior to Callendar-Van Dusen conversion.
   * **Bus Commissioning Rule:** Both `CWT-TM-16PT` and `JSY-MK-339` analyzer default from factory to address `1` at `9600 8N1`. Each module must be programmed with its designated address (e.g., `CWT = 1`, `JSY = 2`) **before** connection to the shared bus; address collisions are prohibited.

### 2.3. Digital Inputs for Supply Fans and Compressors (24V – 48V DC — Change F-01)
* **Dual Monitoring (Airflow Circulation vs. Compression Cycle):** The system monitors optocoupled digital inputs ($24\text{ V DC}$ to $48\text{ V DC}$ routed to the `MCP23S17` SPI expander) reading auxiliary dry contacts (NO/NC) from **evaporator supply fan contactors ($U_{vent\_ON}$)** and **compressor power contactors ($c$)**.
  * Where dedicated fan auxiliary contacts exist, their digital state directly governs whether handling unit $u$ delivers airflow ($u \in U_{vent\_ON}$).
  * Where fans run continuously or interlock with general unit power, configuration binds $U_{vent\_ON}$ to the unit run state, decoupling airflow from compressor thermostat cycling.
* **On-Board Conditioning:** BrightC V3 incorporates conditioning and galvanic isolation circuitry for $24\text{–}48\text{ V DC}$ upstream of `MCP23S17` pins. Dry contacts wire directly without external hardware.
* **Digital Debounce Filtering:** Sampled at $50\text{ Hz}$ ($20\text{ ms}$ interval), requiring continuous stability of **$500\text{ ms}$ (25 consecutive identical samples)** before committing state transitions, eliminating contact chatter.

### 2.4. Total Electrical Metering and Return Grille Humidity
* **Three-Phase Power Analyzer (`JSY-MK-339`):** Interfaced via **Modbus RTU** (RS-485 at $9600\text{ bps}$) measuring at the HVAC distribution board main feeder: phase voltages, line currents, Total Active Power ($P_{elec\_total}$ in $\text{kW}$, register `0x0052`), Power Factor, and Total Active Energy ($\text{kWh}$).
* **Industrial $4\text{–}20\text{ mA}$ Return Grille Hygrometer ($HR_{ret}$):** Relative humidity transmitter ($0\text{ to }100\,\%HR$) with **$4\text{–}20\text{ mA}$** current loop output, installed directly at the **warm air return grille** and wired to an analog channel on connector **`J9 (ADCx10)`** of BrightC V3 (featuring precision shunt resistor to ground and $+12\text{ V DC}$ auxiliary loop power on `J9`). Current loop signaling provides EMI immunity over long cable runs and loop break detection if $I_{loop} < 3.5\text{ mA}$.

### 2.5. Modular $4\text{–}20\text{ mA}$ Pressure Sensing (Phase 2 Design Provision)
* **Phase 1 Scope (Pilot):** Phase 1 firmware does not require physical installation of $4\text{–}20\text{ mA}$ pressure transducers (**Type 1 Sensor:** R410A refrigerant gauge pressure $0\text{ to }45\text{ bar}$; **Type 2 Sensor:** Air filter differential pressure $0\text{ to }500\text{ Pa}$).
* **Engineering Provision:** Connector `J9 (ADCx10)` retains **9 spare analog channels** (after allocating 1 channel for $HR_{ret}$), with the mathematical model fully defined in Section 4.6 and 5.3 for seamless Phase 2 enablement.

---

## 3. STABILIZATION AND ELECTRICAL DISAGGREGATION STATE MACHINE

Because compressors are governed by external third-party thermostats cycling asynchronously, the **STM32H7** executes an independent temporal state machine for each compressor $c$, preventing erroneous evaluation of vapor cycle equations during transient conditions:

1. **State 0 — Compressor Inactive (`OFF`):**
   * Compressor auxiliary contact is open; continuous stopped duration ($t_{off}$) is tracked. If the evaporator fan of that unit remains active ($u \in U_{vent\_ON}$), airflow continues to participate in room continuity, but without demanding cooling lift from this circuit.
   * **Short-Cycle Detection:** If the compressor restarts when $t_{off} < 180\text{ seconds}$, the **Short-Cycle Wear Counter** increments.
2. **State 1 — Electrical Inrush Suppression (`INRUSH_WAIT`, $0 < t_{on} < 15\text{ s}$):**
   * Electrical power samples are discarded during motor starting inrush current.
3. **State 2 — Step Load Disaggregation (`STEP_LOAD_CAPTURE`, $t_{on} = 15\text{ s}$):**
   * Using a single global energy meter, the system isolates the electrical demand of the newly engaged compressor by measuring the net active power step relative to the second immediately preceding its start (provided no other compressor or fan transitioned within that 15-second window):
     $$\Delta P_{comp,c} = P_{elec\_total}(t_{on} = 15\text{ s}) - P_{elec\_total}(t_{on} = 0^-)$$
4. **State 3 — Duct Thermal Stabilization (`THERMAL_WARMUP`, $15\text{ s} < t_{on} < 180\text{ s}$):**
   * Live temperatures are displayed on HMI and published over Modbus/API, but **predictive alerts and OLS regression updates remain blocked**, allowing evaporator coils and RTD sensors to reach thermal equilibrium.
5. **State 4 — Valid Steady-State Regime (`STEADY_STATE`, $t_{on} \ge 180\text{ s}$):**
   * Enables thermodynamic evaluations, actual and Carnot COP calculations, and hourly historical logging.

---

## 4. THERMODYNAMIC AND PSYCHROMETRIC ENGINE (PHASE 1: PILOT)

### 4.1. Processing the Temperature Matrix: $(N + N) + M + L$
At each sampling cycle, the system processes $N$ climate pairs ($k = 1 \dots N$, $N=6$ in pilot), $M$ room ambient sensors ($m = 1 \dots M$, $M=2$), and $L$ outdoor sensors ($l = 1 \dots L$, $L=2$):

1. **Thermal Differential per Pair ($\Delta T_k$):**
   $$\Delta T_k = T_{ret,k} - T_{inj,k} \quad \forall k \in \{1 \dots N\}$$
2. **Mean Temperatures and Global Climate Differential:**
   With $N_{vent\_activos}$ pairs mapped to handling units whose supply fan is active ($u \in U_{vent\_ON}$):
   $$\overline{T}_{inj} = \frac{1}{N_{vent\_activos}} \sum_{k \in U_{vent\_ON}} T_{inj,k} \quad ; \quad \overline{T}_{ret} = \frac{1}{N_{vent\_activos}} \sum_{k \in U_{vent\_ON}} T_{ret,k}$$
   $$\overline{\Delta T}_{clima} = \overline{T}_{ret} - \overline{T}_{inj}$$
3. **Parametric Room Ambient Metrics ($M$ Sensors):**
   Calculates room mean temperature ($\overline{T}_{sala}$), spatial extremes ($T_{sala,max}, T_{sala,min}$), **Room Spatial Thermal Spread ($\Delta T_{sala,max}$)** for any configured $M \ge 2$, and **Bypass / Stratification Gradient ($\Delta T_{bypass}$)**:
   $$\overline{T}_{sala} = \frac{1}{M} \sum_{m=1}^{M} T_{sala,m}$$
   $$T_{sala,max} = \max_{m \in \{1..M\}}\left(T_{sala,m}\right) \quad ; \quad T_{sala,min} = \min_{m \in \{1..M\}}\left(T_{sala,m}\right)$$
   $$\Delta T_{sala,max} = T_{sala,max} - T_{sala,min} \quad ; \quad \Delta T_{bypass} = \overline{T}_{sala} - \overline{T}_{ret}$$
4. **Effective Outdoor Reference Temperature for Condensation ($L$ Sensors):**
   To prevent solar radiation bias or localized heat rejection eddies, effective outdoor temperature ($\overline{T}_{ext}$) averages valid outdoor probes, defaulting to the minimum if probe spread exceeds $4.0\,^\circ\text{C}$:
   $$\overline{T}_{ext} = \begin{cases} \frac{1}{L} \sum_{l=1}^{L} T_{ext,l} & \text{if } \left(\max_{l}(T_{ext,l}) - \min_{l}(T_{ext,l})\right) \le 4.0\,^\circ\text{C} \\ \min_{l \in \{1..L\}}(T_{ext,l}) & \text{if } \left(\max_{l}(T_{ext,l}) - \min_{l}(T_{ext,l})\right) > 4.0\,^\circ\text{C} \end{cases}$$

### 4.2. Analytical Conversions for Internal Analog Channels (PT100 and $4\text{–}20\text{ mA}$)
1. **Internal PT100 Channels (`INA826`):** When using internal RTD channels, sensor resistance ($R_{PT100}$) is solved by subtracting cable compensation ($\Delta R_{cable}$):
   $$V_a = 0.05783\text{ V} + \frac{V_{ADC}}{40.403} \implies R_{PT100} = \left( \frac{5100\,\Omega \cdot V_a}{3.3\text{ V} - V_a} \right) - \Delta R_{cable}$$
   Applying the inverse **Callendar-Van Dusen** equation ($R_0 = 100\,\Omega$, $A = 3.9083 \times 10^{-3}\,^\circ\text{C}^{-1}$, $B = -5.775 \times 10^{-7}\,^\circ\text{C}^{-2}$):
   $$T\,[{}^\circ\text{C}] = \frac{-A + \sqrt{A^2 - 4B\left(1 - \frac{R_{PT100}}{100}\right)}}{2B}$$
2. **$4\text{–}20\text{ mA}$ Return Humidity Channel ($HR_{ret}$ on `J9`):** Across shunt resistor $R_{shunt}$ (e.g., $150\,\Omega$ at $0.1\%$), loop current is $I_{mA} = \frac{V_{ADC}}{R_{shunt}} \times 1000$. For $I_{mA} \in [4.0, 20.0]\text{ mA}$:
   $$HR_{ret}\,[\%] = \text{clamp}_{0..100}\left( \frac{I_{mA} - 4.0}{16.0} \times 100.0 \right)$$

### 4.3. Moist Air Return Properties and Dry Air Mass Flow ($\dot{m}_{da}$ — Change F-01)
With the $4\text{–}20\text{ mA}$ humidity transmitter located directly at the return grille ($HR_{ret}$), evaporator inlet psychrometrics are established by $(\overline{T}_{ret}, HR_{ret})$ and local altitude ($Z$ in meters above sea level):

1. **Local Atmospheric Pressure ($P_{atm}$):**
   $$P_{atm} = 101.325 \times \left(1 - 2.25577 \times 10^{-5} \cdot Z\right)^{5.25588} \quad [\text{kPa}]$$
2. **Saturation Vapor Pressure ($P_{ws,ret}$) and Return Partial Vapor Pressure ($P_{w,ret}$):**
   Via **Magnus-Tetens / ASHRAE** formulations:
   $$P_{ws}(\overline{T}_{ret}) = 0.61078 \cdot \exp\left(\frac{17.27 \cdot \overline{T}_{ret}}{\overline{T}_{ret} + 237.3}\right) \quad [\text{kPa}]$$
   $$P_{w,ret} = P_{ws}(\overline{T}_{ret}) \cdot \left(\frac{HR_{ret}}{100}\right) \quad [\text{kPa}]$$
3. **Return Humidity Ratio (Absolute Humidity) ($\omega_{ret}$):**
   $$\omega_{ret} = 0.62198 \cdot \frac{P_{w,ret}}{P_{atm} - P_{w,ret}} \quad \left[\text{kg}_{\text{vapor}} / \text{kg}_{\text{dry air}}\right]$$
4. **Return Dry Air Density ($\rho_{da,ret}$) and Fan-Governed Dry Air Mass Flow ($\dot{m}_{da}$ — Correction F-01):**
   By Dalton's law of partial pressures for ideal gas mixtures ($R_{da} = 0.287055\text{ kJ/(kg}\cdot\text{K)}$):
   $$\rho_{da,ret} = \frac{P_{atm} - P_{w,ret}}{0.287055 \cdot (\overline{T}_{ret} + 273.15)} \quad \left[\text{kg}_{\text{dry air}}/\text{m}^3\right]$$
   Total dry air mass flow ($\dot{m}_{da,total}$) is governed **strictly by units whose supply fan is active ($u \in U_{vent\_ON}$)**, irrespective of whether compressors are active or thermostat-cycled off:
   $$\dot{m}_{da,total} = \sum_{u \in U_{vent\_ON}} \left(\frac{V_{nom,u}}{3600}\right) \times \rho_{da,ret} \quad \left[\text{kg}_{\text{dry air}}/\text{s}\right]$$
   *(When a unit operates in pure ventilation/recirculation mode, its mass flow $\dot{m}_{da,u}$ remains active in the room continuity and enthalpy equations).*

### 4.4. Evaporator Psychrometric Balance: Sensible, Latent, and Enthalpy
In cooling and dehumidification processes, moist air specific heat in the return is $C_{p,m} = C_{p,da} + \omega_{ret} \cdot C_{p,v} = 1.006 + 1.86 \cdot \omega_{ret} \; [\text{kJ/(kg}_{da}\cdot{}^\circ\text{C)}]$.

1. **Return Air Dew Point Temperature ($T_{rocio,ret}$):**
   $$\gamma = \ln\left(\frac{P_{w,ret}}{0.61078}\right) \implies T_{rocio,ret} = \frac{237.3 \cdot \gamma}{17.27 - \gamma} \quad [{}^\circ\text{C}]$$
2. **Net Sensible Cooling Capacity ($Q_{sensible}$):**
   Evaluates sensible heat removal from active airflow pairs ($k \in U_{vent\_ON}$):
   * **Per Pair $k$ ($k = 1 \dots 6$):**
     $$Q_{sensible,k} = \dot{m}_{da,k} \cdot \left(1.006 + 1.86 \cdot \omega_{ret}\right) \cdot \left(T_{ret,k} - T_{inj,k}\right) \quad [\text{kW thermal}]$$
   * **Room Total:**
     $$Q_{sensible\_total} = \sum_{k \in U_{vent\_ON}} Q_{sensible,k} \quad [\text{kW thermal}]$$
3. **Condensation Regime and Supply Humidity Ratio ($\omega_{inj,k}$):**
   Psychrometric behavior presents two physical regimes determined by discharge temperature ($T_{inj,k}$) relative to return dew point ($T_{rocio,ret}$):
   * **Dry Regime ($T_{inj,k} \ge T_{rocio,ret}$):** Cooling occurs at constant humidity ratio ($\omega_{inj,k} = \omega_{ret}$). No coil condensation occurs ($Q_{latente,k} = 0$).
   * **Active Condensation Regime ($T_{inj,k} < T_{rocio,ret}$):** Air reaches the saturation line along coil fins (with typical bypass factor $BF \approx 0.08$, corresponding to an effective discharge relative humidity $HR_{inj} \approx 92\%$):
     $$P_{w,inj,k} = 0.92 \cdot P_{ws}(T_{inj,k}) = 0.92 \times 0.61078 \cdot \exp\left(\frac{17.27 \cdot T_{inj,k}}{T_{inj,k} + 237.3}\right) \quad [\text{kPa}]$$
     $$\omega_{inj,k} = \min\left(\omega_{ret}, \; 0.62198 \cdot \frac{P_{w,inj,k}}{P_{atm} - P_{w,inj,k}}\right) \quad \left[\text{kg}_{\text{vapor}} / \text{kg}_{\text{dry air}}\right]$$
4. **Latent Cooling Capacity ($Q_{latente}$) and Condensate Drainage Rate ($\dot{m}_{cond}$):**
   Using water latent heat of vaporization at coil conditions ($h_{fg} \approx 2465\text{ kJ/kg}$):
   $$Q_{latente,k} = \dot{m}_{da,k} \cdot h_{fg} \cdot \max\left(0, \; \omega_{ret} - \omega_{inj,k}\right) \quad [\text{kW thermal}]$$
   $$Q_{latente\_total} = \sum_{k \in U_{vent\_ON}} Q_{latente,k} \quad ; \quad \dot{m}_{cond} = \frac{Q_{latente\_total}}{h_{fg}} \times 3600 \quad [\text{Liters/hour condensed water}]$$
5. **Specific Enthalpy per Pair ($h_{ret,k}, h_{inj,k}$), Total Capacity ($Q_{total}$), and Sensible Heat Factor ($SHF$):**
   With a common return humidity ratio ($\omega_{ret}$), pair-specific inlet and outlet enthalpies are formulated as:
   $$h_{ret,k} = 1.006 \cdot T_{ret,k} + \omega_{ret} \cdot \left(2501 + 1.86 \cdot T_{ret,k}\right) \quad \left[\text{kJ/kg}_{da}\right]$$
   $$h_{inj,k} = 1.006 \cdot T_{inj,k} + \omega_{inj,k} \cdot \left(2501 + 1.86 \cdot T_{inj,k}\right) \quad \left[\text{kJ/kg}_{da}\right]$$
   Total system cooling capacity and latent load ratio are:
   $$Q_{total} = Q_{sensible\_total} + Q_{latente\_total} = \sum_{k \in U_{vent\_ON}} \dot{m}_{da,k} \cdot \left(h_{ret,k} - h_{inj,k}\right) \quad [\text{kW thermal}]$$
   $$SHF = \frac{Q_{sensible\_total}}{Q_{total}} \quad ; \quad \% \text{Latent Heat} = (1 - SHF) \times 100 = \left(\frac{Q_{latente\_total}}{Q_{total}}\right) \times 100$$

---

### 4.5. Reverse Carnot / Rankine Thermodynamic Cycle and Exergetic Efficiency

In vapor compression refrigeration (reverse Rankine cycle), compressor electrical work ($P_{elec\_total}$) depends directly on the **temperature lift** between the cold source (return air, $\overline{T}_{ret}$) and the hot source (outdoor air cooling the condenser, $\overline{T}_{ext}$).

With $L = 2$ outdoor temperature sensors, the **STM32H7** evaluates both First Law performance ($COP$) and Second Law efficiency ($\eta_{II}$) in real time:

1. **First Law Operational Coefficients of Performance ($COP_{real}$ and $COP_{sensible}$):**
   * **$COP_{real}$ (Total Machine Enthalpic COP):** Evaluates overall vapor compression cycle efficiency:
     $$COP_{real} = \frac{Q_{total}}{P_{elec\_total}} = \frac{Q_{sensible\_total} + Q_{latente\_total}}{P_{elec\_total}}$$
   * **$COP_{sensible}$ (IT-Useful COP):** Evaluates IT heat removed per electrical kW consumed:
     $$COP_{sensible} = \frac{Q_{sensible\_total}}{P_{elec\_total}}$$
2. **Absolute Source Temperatures and Phase-Change Temperatures (Reverse Rankine with Pinch):**
   Finite heat exchanger temperature differentials (pinch/approach) are modeled in the evaporator ($\Delta T_{pinch,evap} \approx 8\text{ K}$) and condenser ($\Delta T_{pinch,cond} \approx 10\text{ K}$):
   * **Cold Source Absolute Temperature (Return Air):** $T_{C,\text{air}} = \overline{T}_{ret} + 273.15 \; [\text{K}]$
   * **Hot Source Absolute Temperature (Outdoor Air):** $T_{H,\text{air}} = \overline{T}_{ext} + 273.15 \; [\text{K}]$
   * **Estimated Refrigerant Saturation Temperatures:**
     $$T_{evap,\text{sat}} = (\overline{T}_{inj} - \Delta T_{pinch,evap}) + 273.15 \quad [\text{K}]$$
     $$T_{cond,\text{sat}} = (\overline{T}_{ext} + \Delta T_{pinch,cond}) + 273.15 \quad [\text{K}]$$
3. **Ideal Carnot Coefficient of Performance ($COP_{Carnot}$):**
   Second Law theoretical ceiling operating between cycle saturation temperatures:
   $$COP_{Carnot} = \frac{T_{evap,\text{sat}}}{\max\left(5.0\text{ K}, \; T_{cond,\text{sat}} - T_{evap,\text{sat}}\right)}$$
4. **Second Law / Exergetic Efficiency ($\eta_{II}$ — Climate-Invariant Metric):**
   Ratio of measured $COP_{real}$ to theoretical $COP_{Carnot}$:
   $$\eta_{II} = \frac{COP_{real}}{COP_{Carnot}} \times 100 \quad [\%]$$
   * **Predictive Maintenance Property:** For healthy hermetic scroll compressors running R410A, $\eta_{II}$ remains stable between **$35\%$ and $50\%$** whether ambient outdoor air is $15\,^\circ\text{C}$ or $38\,^\circ\text{C}$. If a **refrigerant leak**, **condenser fouling**, or **valve degradation** occurs, $COP_{real}$ drops while $COP_{Carnot}$ tracks ambient boundaries, producing an unequivocal drop in $\eta_{II}$.

---

### 4.6. Phase 2 Pressure-Instrumented Thermodynamic Model (Change F-02)
*(Design provision for subsequent firmware phases when pressure sensors are activated)*:

1. **Dynamic Airflow Correction via Type 2 Sensor ($\Delta P_{air}$, $0\text{ to }500\text{ Pa}$ on Filters):**
   Differential pressure corrects dry air mass flow $\dot{m}_{da}$ for filter loading ($\alpha \approx 0.35$):
   $$\dot{m}_{da,\text{corrected}} = \dot{m}_{da,\text{nominal}} \times \sqrt{\max\left(0.20, \; 1.0 - \alpha \cdot \frac{\Delta P_{air} - \Delta P_{clean}}{\Delta P_{max} - \Delta P_{clean}}\right)}$$
2. **Exact R410A Saturation via 46-Point Flash Lookup Table (Correction F-02):**
   Refrigerant gauge pressure ($P_{bar} \in [0.0, 45.0]\text{ bar}$) on suction or discharge lines bypasses polynomial approximations and transcendent `ln()` routines.
   * **Static Flash LUT and $\mathcal{O}(1)$ Linear Interpolation:** Flash memory stores `const float R410A_TSAT_LUT[46]` containing **46 exact dew point saturation temperatures ($T_{sat}$ in $^\circ\text{C}$, NIST REFPROP standard for R410A)** spaced every $1.0\text{ bar}$ gauge from $0\text{ bar}$ to $45\text{ bar}$.
   * For measured $P_{bar} \in [0.0, 45.0)$, integer index $i = \lfloor P_{bar} \rfloor$ and residual fraction $f = P_{bar} - i$ yield saturation temperature and suction superheat ($\Delta T_{SH}$) in three FPU cycles:
     $$T_{sat}\,[^\circ\text{C}] = \text{LUT}[i] + f \cdot \left(\text{LUT}[i+1] - \text{LUT}[i]\right) \quad ; \quad \Delta T_{SH} = T_{suction} - T_{sat}$$

---

## 5. PREDICTIVE ANALYTICS AND DIAGNOSTIC MATRIX

### 5.1. Thermal Baseline Signature via Double-Precision OLS (64-bit `double` — Change F-03)
During the initial **7 days (168 hours)** of steady-state operation (`STEADY_STATE`), **STM32H7** trains an **Ordinary Least Squares (OLS)** model predicting **Expected Total Cooling Capacity ($\widehat{Q}_{total,esp}$)** driven by three exogenous variables: return temperature ($\overline{T}_{ret}$), outdoor temperature ($\overline{T}_{ext}$), and **fraction of active compressor capacity ($F_{carga} \in (0, 1]$)**:

$$\widehat{Q}_{total,esp} = \beta_0 + \beta_1 \cdot \overline{T}_{ret} + \beta_2 \cdot \overline{T}_{ext} + \beta_3 \cdot F_{carga} \quad [\text{kW thermal}]$$

#### Hardware Numerical Precision Mandate (Ill-Conditioning Mitigation — F-03):
In temperature-regulated telecom rooms, $\overline{T}_{ret}$ varies within narrow bounds ($22\,^\circ\text{C}$ to $25\,^\circ\text{C}$), inducing collinearity in design matrix $X$ against the constant term ($\beta_0$). Normal matrix $X^T X$ exhibits a near-zero determinant. Single-precision inversion (`float` 32-bit) yields truncation errors that corrupt $\beta$.
1. **Mandatory 64-bit Double Precision (`double` on DP-FPU):** Normal moment accumulation ($X^T X \in \mathbb{R}^{4 \times 4}$ and $X^T Y \in \mathbb{R}^{4 \times 1}$) and solver decomposition (Cholesky / Gauss-Jordan) must execute **strictly in IEEE 754 64-bit double precision (`double`, 53-bit mantissa)** using the hardware **Double-Precision FPU (DP-FPU)** of the ARM Cortex-M7 (`STM32H7`).
2. **Tikhonov Diagonal Regularization (Ridge Protection):** A regularization term ($\lambda = 10^{-6}$) is injected along the diagonal prior to inversion, ensuring mathematical invertibility even if room temperatures remain invariant:
   $$\beta = \left(X^T X + \lambda I_4\right)^{-1} X^T Y$$

Expected COP ($\widehat{COP}_{esperado}$) and percentage mechanical degradation are computed as:

$$\widehat{COP}_{esperado} = \frac{\widehat{Q}_{total,esp}}{P_{elec\_total}}$$

$$\% \text{Mechanical Degradation} = \text{clamp}_{0..100}\left( \max\left( \frac{\widehat{COP}_{esperado} - COP_{real}}{\widehat{COP}_{esperado}}, \; \frac{\eta_{II,\text{base}} - \eta_{II}}{\eta_{II,\text{base}}} \right) \times 100 \right)$$

---

### 5.2. Phase 1 Predictive Diagnostic Rules ($(N+N)+M+L$, $HR_{ret}$, and Power)

1. **Filter Restriction Prediction and Days Remaining Precedence Hierarchy (Change F-04):**
   * **Physics:** Filter loading restricts airflow; longer residence time across cold evaporator coils drives $T_{inj,k}$ toward freezing ($9.0\,^\circ\text{C}$) while $\Delta T_k$ rises above baseline ($\Delta T_{base,k}$).
   * **Probability Index ($0\text{ to }100\%$):**
     $$\% \text{Filter}_k = \text{clamp}_{0..100}\left( 60 \cdot \frac{T_{inj\_base,k} - T_{inj,k}}{3.0^\circ\text{C}} + 40 \cdot \frac{\Delta T_k - \Delta T_{base,k}}{4.0^\circ\text{C}} \right)$$
   * **Precedence Hierarchy for "Days Remaining" ($\text{Days Remaining}_k$ — Correction F-04):**
     To eliminate register ambiguity in **Modbus TCP**, **SNMP**, and **REST API** during startup or with clean filters, firmware evaluates the following conditions in strict priority:
     1. **Condition 1 — Initialization Window ($N_{hours\_RAM} < 168\text{ hours}$):** Until the initial 168 hours (7 days) accumulate in the RAM ring buffer, daily slope derivatives are statistically invalid. $\text{Days Remaining}_k$ is locked to the **initialization code**: numeric `100` (days) for HMI/JSON displays and raw code `0xFFFF` in the Modbus diagnostic register map.
     2. **Condition 2 — Clean Filter or Flat/Positive Trend ($\% \text{Filter}_k < 25\%$ or $m_{T,k} \ge -0.05\,^\circ\text{C/day}$):** If the 168 hours are satisfied but fouling is below $25\%$ or supply temperature shows no real negative drift, the register clamps to a **healthy operating ceiling of `90` days**.
     3. **Condition 3 — Active Degradation in Progress ($N_{hours\_RAM} \ge 168$, $\% \text{Filter}_k \ge 25\%$, and $m_{T,k} < -0.05\,^\circ\text{C/day}$):** Evaluates bounded extrapolation toward the coil freeze threshold ($7.5\,^\circ\text{C}$):
        $$\text{Days Remaining}_k = \min\left(90.0, \; \frac{\max\left(0.0, \; T_{inj,k} - 7.5^\circ\text{C}\right)}{\max\left(|m_{T,k}|, \; 0.05^\circ\text{C/day}\right)}ight)$$
2. **Refrigerant Leak, Dirty Condenser, or Compressor Fatigue Prediction (Decoupled from $\overline{T}_{ext}$):**
   * **Physics:** Incorporating outdoor temperature ($\overline{T}_{ext}$) differentiates two phenomena:
     * **Normal Heat Wave Consumption:** If $P_{elec\_total}$ rises and $\Delta T_k$ drops slightly due to $\overline{T}_{ext} > 32\,^\circ\text{C}$, Second Law efficiency ($\eta_{II}$) and expected capacity $\widehat{Q}_{total,esp}(\overline{T}_{ext})$ track ambient conditions $\implies$ **Zero false alarms**.
     * **True Mechanical / Charge Loss:** Exergetic efficiency $\eta_{II}$ drops $> 20\%$ below baseline $\eta_{II,\text{base}}$, $\Delta T_k$ collapses below $\Delta T_{base,k} \cdot F_{carga}$, and $T_{inj,k}$ turns warm ($> 15\,^\circ\text{C}$) despite moderate $\overline{T}_{ext}$.
   * **Probability Index ($0\text{ to }100\%$):**
     $$\% \text{Leak}_k = \text{clamp}_{0..100}\left( 50 \cdot \frac{(\Delta T_{base,k} \cdot F_{carga}) - \Delta T_k}{0.5 \cdot (\Delta T_{base,k} \cdot F_{carga})} + 50 \cdot \frac{\eta_{II,\text{base}} - \eta_{II}}{0.35 \cdot \eta_{II,\text{base}}} \right) \quad (\text{evaluated if } \% \text{Filter}_k < 50\%)$$
3. **Room Hotspot or Airflow Bypass Detection (Any $M$):**
   * **Room Spatial Asymmetry:** Triggers for any $M \ge 2$ when spatial spread exceeds uniformity limits:
     $$\Delta T_{sala,max} = \max_{m \in \{1..M\}}\left(T_{sala,m}\right) - \min_{m \in \{1..M\}}\left(T_{sala,m}\right) > 3.5\,^\circ\text{C}$$
   * **Airflow Bypass:** Triggers if $T_{sala,max} > 27.0\,^\circ\text{C}$ or $\Delta T_{bypass} = \overline{T}_{sala} - \overline{T}_{ret} > 3.0\,^\circ\text{C}$ (short-circuiting supply air returning without picking up rack heat).
4. **Envelope Infiltration / Latent Moisture Ingress ($HR_{ret}$ on Grille):**
   * Triggers when latent load exceeds limits ($\% \text{Latent Heat} > 30\%$ or $SHF < 0.70$) or when return humidity ratio sustains $\omega_{ret} > 0.012\text{ kg}_{v}/\text{kg}_{da}$ ($HR_{ret} > 65\%$ at $24\,^\circ\text{C}$).
5. **Electromechanical Short-Cycle Wear:**
   * Triggers when restart events with $t_{off} < 180\text{ s}$ exceed 3 occurrences within any rolling 60-minute window.

---

### 5.3. Phase 2 Diagnostic Expansion (Pressure-Instrumented)
*(Functional documentation for subsequent stages)*:
* With **Type 2 Sensor ($\Delta P_{air}$)**, filter fouling is measured directly via differential pressure: $\% \text{Filter} = \left(\frac{\Delta P_{air} - \Delta P_{clean}}{\Delta P_{max} - \Delta P_{clean}}\right) \times 100$.
* With **Type 1 Sensor ($P_{gas}$)** via Flash LUT, diagnostic logic isolates **refrigerant leaks** ($P_{bar} < 6.5\text{ bar}$ with low $\eta_{II}$) from **fouled outdoor condensers** (elevated high-side $P_{bar}$ with normal $\overline{T}_{ext}$) or **internal compressor valve failure** (suction $P_{bar} > 11.5\text{ bar}$ under closed contactor).

---

### 5.4. Capacity Auditing and Weather-Corrected Free Cooling Reserve ($kW_{free}$)
Nameplate rated capacity ($Q_{nominal,u}$) is specified at design outdoor temperature ($T_{ext,design} = 35.0\,^\circ\text{C}$, AHRI 210/240 standard). When real outdoor temperature ($\overline{T}_{ext}$) exceeds $T_{ext,design}$, condensing pressure rises and delivered refrigeration capacity derates (*Thermal Derating*).

1. **Weather-Corrected Nominal Capacity ($Q_{nom,corr,u}$):**
   Applying high-ambient derating coefficient ($k_{ext} \approx 0.005\,^\circ\text{C}^{-1}$, equivalent to $-0.5\%$ capacity per $1\,^\circ\text{C}$ rise above $T_{ext,design}$):
   $$Q_{nom,corr,u} = Q_{nominal,u} \times \left[ 1.0 - k_{ext} \cdot \max\left(0.0, \; \overline{T}_{ext} - T_{ext,design}\right) \right] \quad [\text{kW thermal}]$$
2. **Safe Maximum Redundant Capacity ($Q_{max\_safe}$) and Free Margin ($kW_{free}$):**
   For a room requiring $N+1$ operational redundancy ($U_{req}$ units required to support the heat load without infringing on standby units in a $2+1$ arrangement), discounting thermal derating and mechanical wear:
   $$Q_{max\_safe} = \left(\sum_{u=1}^{U_{req}} Q_{nom,corr,u}\right) \times \left(1.0 - \frac{\% \text{Mechanical Degradation}}{100.0}\right)$$
   $$kW_{free} = \max\left(0.0, \; Q_{max\_safe} - Q_{sensible\_total}\right) \quad [\text{kW thermal}]$$

---

## 6. DATA ARCHITECTURE AND HYBRID STORAGE (RAM + EEPROM + MICROSD)

The system prevents MicroSD endurance fatigue and guarantees real-time responsiveness across three physical memory tiers:

### 6.1. Tier 1: STM32H7 Internal RAM (Real-Time Computation and 30-Day Window)
* **Real-Time Accumulators (1 Hz):** Readings from all $2N + M + L$ temperature sensors (**16 sensors in pilot: $6+6+2+2$**), $4\text{–}20\text{ mA}$ return humidity ($HR_{ret}$), and electrical meters are filtered and averaged at 1-second intervals in internal RAM.
* **Historical Ring Buffer (720 Hours = 30 Days):** RAM retains the last **720 hourly summaries**. At a fixed record size of **64 bytes per hour**, 30 days occupy **$46.08\text{ KB}$** of available internal SRAM.
* **Zero Latency and 64-bit Algebra:** OLS regressions (accumulated and inverted in 64-bit `double` on the hardware DP-FPU), slope tracking, and 30-day REST queries execute in microseconds directly from RAM.

### 6.2. Tier 2: I²C EEPROM (`AT24C256C` 32 KB — Configuration and Baseline Signature)
Safeguards non-volatile configuration:
1. **Site Topology Parameters:** Number of configured pairs ($N=6$), room sensors ($M=2$), outdoor sensors ($L=2$), design outdoor temperature ($T_{ext,design} = 35.0\,^\circ\text{C}$), coefficient $k_{ext}$, `CWT-TM-16PT` address assignments, fan ($U_{vent\_ON}$) and compressor digital input mapping, and site altitude ($Z$).
2. **Calibration and Baseline Signature:** OLS regression coefficients $\beta_0, \beta_1, \beta_2, \beta_3$ stored in 64-bit `double` format (32 bytes total), Baseline Exergetic Efficiency ($\eta_{II,\text{base}}$), initial pair differentials ($\Delta T_{base,1..N}$, $T_{inj\_base,1..N}$), and compressor electrical step signatures ($\Delta P_{comp,c}$).

### 6.3. Tier 3: Industrial MicroSD Card (Long-Term Indexed Binary Datalogger)
* **Hourly Block Binary Structure (Exact 64 Bytes Fixed per Hour):**
  * `UNIX Timestamp` (4 bytes)
  * `Average Supply Temperatures Vector` ($N=6$ values in tenths of $^\circ\text{C}$, sized for up to 7 pairs: 14 bytes)
  * `Average Return Temperatures Vector` ($N=6$ values in tenths of $^\circ\text{C}$, sized for up to 7 pairs: 14 bytes)
  * `Average Room Ambient Temperatures Vector` ($M=2$ values in tenths of $^\circ\text{C}$, up to 2 sensors: 4 bytes)
  * `Average Outdoor Temperatures Vector` ($L=2$ values in tenths of $^\circ\text{C}$, up to 2 sensors: 4 bytes)
  * `Average Return Relative Humidity` ($HR_{ret}$ in tenths of $\%HR$: 2 bytes)
  * `Average Active Electrical Power` (4 bytes - IEEE 754 Float $\text{kW}$)
  * `Total Sensible and Latent Thermal Capacity` (8 bytes - 2x Float $\text{kW}$)
  * `Average Real COP and Carnot Efficiency (Eta_II)` (4 bytes - 2x UINT16 in hundredths)
  * `Fan State Mask (U_vent_ON), Compressors, Load Fraction, and Start Counter` (4 bytes)
  * `Integrity Checksum CRC-16` (2 bytes)
  * **Exact Total:** **64 bytes per hour** ($560.6\text{ KB}$ per year of continuous logging).
* **Boot Restore Routine:** Following power interruption, STM32H7 reads the last $46.08\text{ KB}$ from MicroSD, validates header/CRC records, and restores the 30-day RAM history in under $10\text{ ms}$ without resetting the 168-hour learning window if history already exists.

### 6.4. Platform Prerequisite: Flash Partitioning for Networked Image
The application image combining networking stacks (Ethernet/WiFi, sockets, SNMP, HTTP, TLS) exceeds the initial $256\text{ KB}$ flash slot. The OTA architecture (`doc/architecture/mcuboot_external_flash_ota_architecture.md`) defines the required asymmetric partitioning: internal flash Slot 0, external QSPI flash Slot 1 ($1.75\text{ MB}$), and a $256\text{ KB}$ LittleFS partition. This layout is a platform prerequisite for the interfaces phase; DTS remains unmodified in this specification.

---

## 7. COMMUNICATION AND VISUALIZATION INTERFACES

1. **Local Touch Screen (DWIN 4.3" HMI):**
   * Primary display presents global metrics ($\overline{T}_{ret}, \overline{T}_{inj}, \overline{\Delta T}_{clima}, \overline{T}_{sala}, \overline{T}_{ext}$), Return Humidity ($HR_{ret}$), Electrical Power ($\text{kW}$), Sensible and Latent Capacity ($\text{kW}$), $COP_{real}$, Carnot Efficiency ($\eta_{II}$), Free Capacity ($kW_{free}$), and active alarms. Dedicated sub-screens provide drill-down inspection for **6 Supply/Return pairs ($T_{inj,1..6}, T_{ret,1..6}, \Delta T_{1..6}$)**, **2 room sensors ($T_{sala,1..2}$)**, and **2 outdoor sensors ($T_{ext,1..2}$)**.
2. **Modbus TCP Server (Huawei NetEco / SCADA Integration):**
   * Controller acts as Modbus TCP Server (Port 502) over on-board Ethernet supporting concurrent client sessions. Holding (`0x03`) and Input (`0x04`) register blocks expose global room parameters, all **16 temperatures in $(6+6+2+2)$**, $4\text{–}20\text{ mA}$ return humidity, power analyzer metrics, fan ($U_{vent\_ON}$) and compressor states, $COP_{real}, COP_{Carnot}, \eta_{II}$, weather-corrected free margin ($kW_{free}$), mechanical degradation percentage, maintenance days remaining (supporting `0xFFFF` / `100` initialization codes), and predictive alarm matrices.
3. **SNMP Agent (Simple Network Management Protocol v2c / v3):**
   * Implements structured **MIB (Management Information Base)** OIDs for thermodynamic, psychrometric, electrical ($P_{elec\_total}, COP_{real}, COP_{Carnot}, \eta_{II}, kW_{free}$), and 16-channel temperature variables for NMS polling (*SNMP GET / WALK* on UDP 161, Zabbix/PRTG/OSS).
   * Generates asynchronous **SNMP Traps / Informs** (UDP 162) upon alarm state changes (filter loading, refrigerant loss, room hotspots, short-cycling).
4. **Local RESTful API (HTTP/JSON):**
   * Serves `GET /api/v1/hvac/status` (breaking down `thermal_pairs[1..6]`, `room_sensors[1..M]`, `outdoor_sensors[1..L]`, Carnot telemetries, return psychrometrics, and analytics), `GET /api/v1/hvac/logs?hours=X`, and `POST /api/v1/hvac/config`, secured via Bearer Token authentication and IP whitelisting.
