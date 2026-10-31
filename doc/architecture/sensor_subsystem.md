# Architecture of the Industrial Sensor Subsystem & Hardware-Agnostic Ingestion

- **Status**: Technical Architecture Specification
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6)
- **Target Specification**: Mathematical Model Specification Revision F (`doc/request/Mathematical_Model_Specification_REVF.md`)
- **Architectural Pattern**: Clean Architecture (Ports & Adapters) + Hardware-Agnostic Semantic Ingestion + Microconf EEPROM Persistence
- **Language**: English (per project convention)

---

## 1. Executive Summary & Problem Statement

### 1.1 The Hardware Decoupling Challenge
In critical telecom climate auditing (HVAC telemetry), industrial controllers face two conflicting requirements:
1. **Physical Variety & Scalability:** Sensors originate from heterogeneous interfaces:
   - External RS-485 Modbus RTU transmitter (`CWT-TM-16PT`, 16 PT100 channels).
   - Future Modbus RTU / Modbus TCP slave modules (e.g., expansion blocks, multi-drop probes).
   - On-board analog stage (5 internal RTD PT100 channels with `INA826`).
   - Analog current loop inputs on connector `J9 (ADCx10)`: $4\text{–}20\text{ mA}$ return humidity ($HR_{ret}$) and Phase 2 pressure transducers ($0\text{–}45\text{ bar}$, $0\text{–}500\text{ Pa}$).
   - Future I2C / SPI digital environmental transducers.
2. **Domain Purity & Semantic Stability:**
   The thermodynamic engine (psychrometrics, sensible/latent capacity balances, Carnot/Rankine COP, OLS regression) must **never** know the physical origin of any channel. It requires purely abstract semantic entities:
   - $N$ paired Supply/Return couples ($T_{inj,k}, T_{ret,k}$).
   - $M$ Room ambient monitoring probes ($T_{sala,m}$).
   - $L$ Outdoor ambient condensing probes ($T_{ext,l}$).
   - Return grille relative humidity ($HR_{ret}$).
   - Refrigerant and air filter differential pressures (Phase 2).

### 1.2 The Field Swap Requirement (Fault Tolerance)
If a physical channel burns out or fails on-site (e.g., electrical surge destroys terminal CH15 on the external `CWT-TM-16PT` module measuring outdoor temperature), the maintenance technician must be able to move the physical probe wires to an empty channel (such as on-board `INTERNAL_RTD_CH1`) and reconfigure the role in software via the CLI Shell, HMI, or REST API **without recompiling, flashing, or stopping firmware execution**.

---

## 2. Architectural Design: Decoupled Multi-Tier Ingestion

```
  PHYSICAL LAYER              ADAPTER LAYER (Zephyr Drivers)           DOMAIN INGESTION ENGINE                APPLICATION & DOMAIN ENGINE
 ┌──────────────────────┐    ┌─────────────────────────────┐        ┌─────────────────────────────┐        ┌───────────────────────────────┐
 │ External Modbus RTU  │───>│ adapter_modbus_cwt.c        │───┐    │                             │        │                               │
 │ (CWT-TM-16PT 16xPT100│    │ (Registers 0x0020-0x002F)   │   │    │                             │        │                               │
 └──────────────────────┘    └─────────────────────────────┘   │    │                             │        │  thermal_matrix_frame_t       │
 ┌──────────────────────┐    ┌─────────────────────────────┐   ├───>│  sensor_matrix_mapper.c     │───────>│  - N Thermal Pairs (inj/ret)  │
 │ Internal Placa V3    │───>│ adapter_rtd_internal.c      │   │    │  (Pure C99/C11 Domain)      │        │  - M Room Probes              │
 │ (5x RTD INA826)      │    │ (ADC1 + Callendar-Van Dusen)│   │    │                             │        │  - L Outdoor Probes           │
 └──────────────────────┘    └─────────────────────────────┘   │    │  Consumes:                  │        │  - Pair & Probe Health Masks  │
 ┌──────────────────────┐    ┌─────────────────────────────┐   │    │  - Raw Physical Banks       │        └──────────────┬────────────────┘
 │ Analog 4-20 mA       │───>│ adapter_current_loop_j9.c   │───┤    │  - EEPROM Channel Descriptors│                       │
 │ (J9 ADCx10: HR, P)   │    │ (Shunt + 4-20mA Linearize)  │   │    │  - Calibration Offsets      │                       ▼
 └──────────────────────┘    └─────────────────────────────┘   │    │                             │        ┌───────────────────────────────┐
 ┌──────────────────────┐    ┌─────────────────────────────┐   │    │  Applies:                   │        │  Thermodynamic & Exergy Engine│
 │ Future Expansion Bus │───>│ adapter_modbus_generic.c    │───┘    │  - Routing & Pairing Matrix │───────>│  - Q_sensible, Q_latente      │
 │ (Modbus / I2C / SPI) │    │                             │        │  - Bitmask Aggregation      │        │  - COP_real, COP_Carnot, n_II │
 └──────────────────────┘    └─────────────────────────────┘        └──────────────┬──────────────┘        │  - OLS Predictive Diagnostics │
                                                                                   │                       └───────────────────────────────┘
                                                                                   ▼
                                                                    ┌─────────────────────────────┐
                                                                    │  EEPROM Storage Subsystem   │
                                                                    │  (CONFIG_DOMAIN_SENSOR_CFG) │
                                                                    └─────────────────────────────┘
```

---

## 3. Data Structures and Channel Descriptors

### 3.1 Physical Channel Enumeration (`physical_channel_id_t`)
Every hardware acquisition point is assigned a unique, fixed identifier:

```c
typedef enum {
    /* External Modbus RTU Concentrator: CWT-TM-16PT (Channels 1 to 16) */
    PHYS_CH_CWT_1 = 0,
    PHYS_CH_CWT_2,
    PHYS_CH_CWT_3,
    PHYS_CH_CWT_4,
    PHYS_CH_CWT_5,
    PHYS_CH_CWT_6,
    PHYS_CH_CWT_7,
    PHYS_CH_CWT_8,
    PHYS_CH_CWT_9,
    PHYS_CH_CWT_10,
    PHYS_CH_CWT_11,
    PHYS_CH_CWT_12,
    PHYS_CH_CWT_13,
    PHYS_CH_CWT_14,
    PHYS_CH_CWT_15,
    PHYS_CH_CWT_16,

    /* Internal On-Board RTD Stage with INA826 (Channels 1 to 5) */
    PHYS_CH_INTERNAL_RTD_1 = 16,
    PHYS_CH_INTERNAL_RTD_2,
    PHYS_CH_INTERNAL_RTD_3,
    PHYS_CH_INTERNAL_RTD_4,
    PHYS_CH_INTERNAL_RTD_5,

    /* Analog 4-20 mA Inputs on Conector J9 (Channels 1 to 10) */
    PHYS_CH_ANALOG_J9_CH1 = 21,
    PHYS_CH_ANALOG_J9_CH2,
    PHYS_CH_ANALOG_J9_CH3,
    PHYS_CH_ANALOG_J9_CH4,
    PHYS_CH_ANALOG_J9_CH5,
    PHYS_CH_ANALOG_J9_CH6,
    PHYS_CH_ANALOG_J9_CH7,
    PHYS_CH_ANALOG_J9_CH8,
    PHYS_CH_ANALOG_J9_CH9,
    PHYS_CH_ANALOG_J9_CH10,

    /* Total physical endpoints supported */
    PHYS_CH_MAX = 31
} physical_channel_id_t;
```

### 3.2 Semantic Role Classification (`sensor_role_t`)
Defines the engineering purpose of the physical channel:

```c
typedef enum {
    SENSOR_ROLE_DISABLED = 0,         /**< Channel unassigned / inactive */
    
    /* Temperature Roles (Revision F: (N+N) + M + L) */
    SENSOR_ROLE_TEMP_SUPPLY_AIR,      /**< Cold air supply probe (requires target_pair_idx 0..N-1) */
    SENSOR_ROLE_TEMP_RETURN_AIR,      /**< Warm air return probe (requires target_pair_idx 0..N-1) */
    SENSOR_ROLE_TEMP_ROOM_AMBIENT,    /**< Room zone ambient probe (target_probe_idx 0..M-1) */
    SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT, /**< Outdoor condenser reference (target_probe_idx 0..L-1) */

    /* Psychrometric & Pressure Roles */
    SENSOR_ROLE_HUMIDITY_RETURN,      /**< 4-20 mA Return grille relative humidity (HR_ret) */
    SENSOR_ROLE_PRESSURE_REFRIGERANT, /**< Phase 2: Refrigerant pressure 0-45 bar */
    SENSOR_ROLE_PRESSURE_AIR_FILTER   /**< Phase 2: Filter differential pressure 0-500 Pa */
} sensor_role_t;
```

### 3.3 Channel Mapping Descriptor (`sensor_channel_cfg_t`)
Packed flat struct without pointers, serializable for EEPROM persistence:

```c
typedef struct __attribute__((packed)) {
    uint8_t  physical_ch;             /**< physical_channel_id_t */
    uint8_t  role;                    /**< sensor_role_t */
    uint8_t  target_index;            /**< Pair index k (0..N-1) or Probe index m/l */
    int16_t  cal_offset_c_centi;      /**< 2-point calibration offset in 0.01 deg C (int16_t) */
    uint16_t cal_gain_ppm;            /**< 2-point calibration gain scale (ppm, 1000000 = 1.0) */
    uint8_t  associated_hvac_unit;    /**< HVAC unit index u (1..3) for mass flow continuity */
} sensor_channel_cfg_t;
```

### 3.4 Ingestion Matrix Output (`thermal_matrix_frame_t`)
Output delivered to the thermodynamic calculation engine:

```c
#define SENSOR_MATRIX_MAX_PAIRS          8   /**< Supports up to 8 Supply/Return pairs (Pilot uses 6) */
#define SENSOR_MATRIX_MAX_ROOM_PROBES    4   /**< Supports up to 4 Room probes (Pilot uses 2) */
#define SENSOR_MATRIX_MAX_OUTDOOR_PROBES 4   /**< Supports up to 4 Outdoor probes (Pilot uses 2) */

typedef struct {
    uint64_t timestamp_us;

    /* Paired Evaporator Temperatures */
    uint8_t  configured_pairs;        /**< N (e.g., 6 in pilot) */
    float    t_inj[SENSOR_MATRIX_MAX_PAIRS];
    float    t_ret[SENSOR_MATRIX_MAX_PAIRS];
    uint8_t  pair_hvac_unit_map[SENSOR_MATRIX_MAX_PAIRS]; /**< HVAC Unit ID owning this pair */
    uint16_t pair_valid_mask;         /**< Bit k = 1 if BOTH t_inj and t_ret are valid and healthy */
    uint16_t pair_open_probe_mask;    /**< Bit k = 1 if either probe is disconnected */

    /* Room Ambient Probes */
    uint8_t  configured_room_probes;  /**< M (e.g., 2 in pilot) */
    float    t_room[SENSOR_MATRIX_MAX_ROOM_PROBES];
    uint8_t  room_valid_mask;         /**< Bit m = 1 if probe healthy */

    /* Outdoor Condenser Probes */
    uint8_t  configured_outdoor_probes;/**< L (e.g., 2 in pilot) */
    float    t_outdoor[SENSOR_MATRIX_MAX_OUTDOOR_PROBES];
    uint8_t  outdoor_valid_mask;      /**< Bit l = 1 if probe healthy */

    /* Psychrometric & Pressure Channels */
    float    humidity_return_pct;     /**< HR_ret in % (0.0 to 100.0) */
    bool     humidity_return_valid;   /**< True if 4-20 mA loop current is within 3.5 - 21.0 mA */

    float    refrigerant_pressure_bar;/**< Phase 2: R410A gauge pressure (0 to 45 bar) */
    bool     refrigerant_pressure_valid;

    float    filter_diff_pressure_pa; /**< Phase 2: Air filter DP (0 to 500 Pa) */
    bool     filter_diff_pressure_valid;
} thermal_matrix_frame_t;
```

---

## 4. EEPROM Partitioning & Storage Subsystem Extension

To persist the sensor configuration and baseline signatures without risking corruption of existing Modbus or Network parameters, the memory map in `doc/architecture/storage_subsystem.md` is updated.

### 4.1 Updated AT24C256C Memory Map (32 KB)

```text
Base Offset   Total Size    Logical Domain             Structure and Purpose
───────────────────────────────────────────────────────────────────────────────────────────
0x0000        1024 B (1 KB)  CONFIG_DOMAIN_DEVICE       2 slots of 512 B (Serial, HW Ver, Alias)
0x0400        2048 B (2 KB)  CONFIG_DOMAIN_MODBUS       2 slots of 1 KB (Buses + 32 Slaves)
0x0C00        1024 B (1 KB)  CONFIG_DOMAIN_NET_ETH      2 slots of 512 B (Static IP/DHCP, MAC, Gateway)
0x1000        1024 B (1 KB)  CONFIG_DOMAIN_NET_WIFI     2 slots of 512 B (STA/AP mode, SSID, Credentials)
0x1400        1024 B (1 KB)  CONFIG_DOMAIN_CALIBRATION  2 slots of 512 B (Raw ADC Offsets and Factors)
0x1800        2048 B (2 KB)  CONFIG_DOMAIN_SENSOR_CFG   2 slots of 1 KB (Channel Routing & Calibration)
0x2000        2048 B (2 KB)  CONFIG_DOMAIN_BASELINE     2 slots of 1 KB (OLS 64-bit Betas, Baseline Signatures)
0x2800        6144 B (6 KB)  RESERVED                   Future expansions (SNMP, TLS Certificates)
0x4000       16384 B (16 KB) EVENT LOG / AUDIT TRAIL    Circular storage for system events
───────────────────────────────────────────────────────────────────────────────────────────
Total: 32,768 Bytes (32 KB)
```

### 4.2 Detailed Domain Breakdown: `CONFIG_DOMAIN_SENSOR_CFG` (0x1800)
- Managed by `microconf` using dual-slot ping-pong with CRC32 integrity check.
- Capacity: Slot 0 = 1024 B, Slot 1 = 1024 B.
- Structure payload:
  - Header (Magic, Version, Configured Counts $N, M, L$): 16 bytes.
  - Active Channel Table (up to 32 entries $\times$ 8 bytes): 256 bytes.
  - Checksum & Reserved: 240 bytes.
  - Total payload: ~512 bytes (fits with >50% headroom inside each 1024-byte slot).

### 4.3 Detailed Domain Breakdown: `CONFIG_DOMAIN_BASELINE` (0x2000)
- Stores learned 7-day parameters in IEEE 754 64-bit `double` format:
  - OLS coefficients: $\beta_0, \beta_1, \beta_2, \beta_3$ ($4 \times 8\text{ bytes} = 32\text{ bytes}$).
  - Baseline Exergetic Efficiency ($\eta_{II,\text{base}}$): 8 bytes.
  - Initial clean differentials per pair: $\Delta T_{base,1..N}$, $T_{inj\_base,1..N}$ ($16 \times 4\text{ bytes} = 64\text{ bytes}$).
  - Compressor electrical step signatures: $\Delta P_{comp,c}$ ($8 \times 4\text{ bytes} = 32\text{ bytes}$).
  - Total payload: <200 bytes per slot.

---

## 5. Pilot Site Configuration Profile (Default Factory Setup)

For the initial 14/16-probe deployment specified in Revision F ($6+6+2+2$):

| Physical Channel | Hardware Port | Default Role | Target Index | Associated HVAC Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `PHYS_CH_CWT_1` | CWT-TM-16PT Borna 1 | `SENSOR_ROLE_TEMP_SUPPLY_AIR` | Pair 0 ($k=1$) | Unit 1 | Supply probe $T_{inj,1}$ |
| `PHYS_CH_CWT_2` | CWT-TM-16PT Borna 2 | `SENSOR_ROLE_TEMP_SUPPLY_AIR` | Pair 1 ($k=2$) | Unit 1 | Supply probe $T_{inj,2}$ |
| `PHYS_CH_CWT_3` | CWT-TM-16PT Borna 3 | `SENSOR_ROLE_TEMP_SUPPLY_AIR` | Pair 2 ($k=3$) | Unit 2 | Supply probe $T_{inj,3}$ |
| `PHYS_CH_CWT_4` | CWT-TM-16PT Borna 4 | `SENSOR_ROLE_TEMP_SUPPLY_AIR` | Pair 3 ($k=4$) | Unit 2 | Supply probe $T_{inj,4}$ |
| `PHYS_CH_CWT_5` | CWT-TM-16PT Borna 5 | `SENSOR_ROLE_TEMP_SUPPLY_AIR` | Pair 4 ($k=5$) | Unit 3 | Supply probe $T_{inj,5}$ |
| `PHYS_CH_CWT_6` | CWT-TM-16PT Borna 6 | `SENSOR_ROLE_TEMP_SUPPLY_AIR` | Pair 5 ($k=6$) | Unit 3 | Supply probe $T_{inj,6}$ |
| `PHYS_CH_CWT_7` | CWT-TM-16PT Borna 7 | `SENSOR_ROLE_TEMP_RETURN_AIR` | Pair 0 ($k=1$) | Unit 1 | Return probe $T_{ret,1}$ |
| `PHYS_CH_CWT_8` | CWT-TM-16PT Borna 8 | `SENSOR_ROLE_TEMP_RETURN_AIR` | Pair 1 ($k=2$) | Unit 1 | Return probe $T_{ret,2}$ |
| `PHYS_CH_CWT_9` | CWT-TM-16PT Borna 9 | `SENSOR_ROLE_TEMP_RETURN_AIR` | Pair 2 ($k=3$) | Unit 2 | Return probe $T_{ret,3}$ |
| `PHYS_CH_CWT_10`| CWT-TM-16PT Borna 10| `SENSOR_ROLE_TEMP_RETURN_AIR` | Pair 3 ($k=4$) | Unit 2 | Return probe $T_{ret,4}$ |
| `PHYS_CH_CWT_11`| CWT-TM-16PT Borna 11| `SENSOR_ROLE_TEMP_RETURN_AIR` | Pair 4 ($k=5$) | Unit 3 | Return probe $T_{ret,5}$ |
| `PHYS_CH_CWT_12`| CWT-TM-16PT Borna 12| `SENSOR_ROLE_TEMP_RETURN_AIR` | Pair 5 ($k=6$) | Unit 3 | Return probe $T_{ret,6}$ |
| `PHYS_CH_CWT_13`| CWT-TM-16PT Borna 13| `SENSOR_ROLE_TEMP_ROOM_AMBIENT`| Room Probe 0 ($m=1$) | N/A | Cold Aisle $T_{sala,1}$ |
| `PHYS_CH_CWT_14`| CWT-TM-16PT Borna 14| `SENSOR_ROLE_TEMP_ROOM_AMBIENT`| Room Probe 1 ($m=2$) | N/A | Hot Aisle $T_{sala,2}$ |
| `PHYS_CH_CWT_15`| CWT-TM-16PT Borna 15| `SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT`| Outdoor 0 ($l=1$) | N/A | Condenser Air Intake $T_{ext,1}$|
| `PHYS_CH_CWT_16`| CWT-TM-16PT Borna 16| `SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT`| Outdoor 1 ($l=2$) | N/A | Condenser Air Intake $T_{ext,2}$|
| `PHYS_CH_ANALOG_J9_CH1` | Conector J9 Pin 1 | `SENSOR_ROLE_HUMIDITY_RETURN`| N/A | N/A | Return Grille $HR_{ret}$ (4-20mA) |
| `PHYS_CH_INTERNAL_RTD_1..5`| Placa V3 RTD1..5 | `SENSOR_ROLE_DISABLED` | N/A | N/A | 100% Free Spare Reserve |
| `PHYS_CH_ANALOG_J9_CH2..10`| Conector J9 Pins 2..10| `SENSOR_ROLE_DISABLED` | N/A | N/A | Reserved for Phase 2 Pressure |

---

## 6. Mathematical & Ingestion Invariants

1. **Domain Purity:**
   `sensor_matrix_mapper.c` resides strictly in `app/src/domain/` with zero `#include <zephyr/...>` dependencies. It compiles cleanly on both target STM32H7 and POSIX `native_sim`.
2. **Pair Health Consensus:**
   A thermal pair $k$ is marked valid (`pair_valid_mask & (1 << k) == 1`) **only if both** $T_{inj,k}$ and $T_{ret,k}$ report valid, uncorrupted readings within physical bounds ($-20.0\,^\circ	ext{C}$ to $+80.0\,^\circ	ext{C}$). If either probe is disconnected or open-circuit, the bit in `pair_valid_mask` is cleared and the bit in `pair_open_probe_mask` is asserted.
3. **Mass Flow Conservation:**
   When calculating global air metrics, only pairs with `pair_valid_mask == 1` belonging to units with active supply fans ($u \in U_{vent\_ON}$) participate in spatial averages and enthalpy summations.
4. **Hot-Swap Persistence:**
   Modifying channel assignments via CLI Shell or REST API persists immediately to RAM and signals `chan_system_config` via Zbus. The storage coordinator `ao_storage` writes the new table to EEPROM domain `CONFIG_DOMAIN_SENSOR_CFG` after its 500 ms debounce window.
