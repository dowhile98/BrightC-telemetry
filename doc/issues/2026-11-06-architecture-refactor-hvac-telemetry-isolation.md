# Architecture Refactor & Integration Issue: Dual-Pillar HVAC Control & Telemetry Subsystem

- **Date:** 2026-11-06 (Updated 2026-11-07)
- **Status:** APPROVED ARCHITECTURAL REFACTOR & IMPLEMENTATION SPECIFICATION
- **Platform:** DAECGE BrightC Controller V3 (STM32H743ZIT6 LQFP144, Cortex-M7 @ 480 MHz)
- **Environment:** Zephyr RTOS (West T2 Topology)
- **Devicetree Source:** `boards/daecge/brightc-controllerv3/brightc-controllerv3.dts`
- **Reference Standard:** Clean Architecture (Ports & Adapters), SOLID Embedded C, Zero Dynamic Allocation
- **Supersedes:** Dynamic runtime pin-muxing, Modbus slave tables in EEPROM, and cumbersome `default_config_profiles.c/.h`

---

## 1. Executive Summary & Problem Statement

The BrightC Controller V3 firmware was initially designed with high degrees of runtime configurability stored across 8 EEPROM domains. Analysis of field deployments and industrial automation realities revealed that dynamic runtime configuration of physical hardware introduces severe architectural risks:

1. **Safety & Plant Reliability Risks:**
   - Critical protections (high/low pressure switches, fire alarms, phase monitors) and actuator relays mapped dynamically via EEPROM create failure modes where EEPROM corruption or misconfiguration leaves safety cutouts and compressor coils disconnected without raising compilation or hardware alerts.
2. **Configuration Fragility & Complexity Bloat:**
   - Serializing 64-channel digital input tables, 32 Modbus slave descriptors, and 32 sensor channel routing objects into EEPROM required extensive envelope packing, CRC validation, slot wear-leveling, and runtime deserialization (`default_config_profiles.c` / `composition_root.c`), introducing accidental complexity.
3. **Coupled Subsystems:**
   - HVAC control logic and environmental telemetry were intertwined through shared configuration envelopes and overlapping data channels, rather than operating as independent, decoupled pillars.

---

## 2. Core Architectural Decisions (ADR)

### ADR-1: Dual-Pillar Independence (HVAC Control vs Data Acquisition & Telemetry)

The BrightC V3 firmware is organized into two completely decoupled operational pillars:

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                            BRIGHTC V3 ARCHITECTURE                          │
 ├──────────────────────────────────────┬──────────────────────────────────────┤
 │         PILAR 1: HVAC CONTROL        │     PILAR 2: DATA ACQUISITION &      │
 │         (High Priority / Real-Time)  │     TELEMETRY (Medium Priority)      │
 ├──────────────────────────────────────┼──────────────────────────────────────┤
 │ - Active Object: ao_hvac             │ - Active Object: ao_sensor           │
 │ - Multi-Group FSM (Up to 4 groups)   │ - Thermodynamic Engine (Revision F)  │
 │ - N+M Duty Rotation & Anti-Inrush    │ - SD Card Spooler (Store & Forward)  │
 │ - Direct physical relay tripping     │ - MQTT Publisher / Modbus TCP Server │
 │ - Hardware safety interlocks         │ - Continuous Energy Auditing (JSY)   │
 ├──────────────────────────────────────┴──────────────────────────────────────┤
 │                     DECOUPLING BUS: ZEPHYR ZBUS                             │
 │  - ao_hvac operates autonomously even if telemetry, network or SD card fail. │
 │  - ao_telemetry monitors and logs HVAC states without interfering in control.│
 │  - Three operating profiles: DUAL (HVAC+Telem), HVAC_ONLY, TELEMETRY_ONLY.  │
 └─────────────────────────────────────────────────────────────────────────────┘
```

#### Isolation Invariants:
- If the Wi-Fi or Ethernet network disconnects, or if the SD card fills up, **`ao_hvac` does not pause, jitter, or block**.
- If a site has no HVAC control requirements (monitoring-only shelter), `ao_hvac` is cleanly disabled without impacting sensor sampling.

---

### ADR-2: Compile-Time Declarative Hardware Mapping (`hardware_pin_map.h`)

Physical terminal blocks, on-board expanders (MCP23S17), STM32 GPIOs, and relays have fixed PCB silkscreen definitions. They are declared as immutable `const` structures in firmware:

1. **Digital Inputs (24 On-Board Channels):**
   - 24 channels on SPI1 MCP23S17 expanders (`mcp23s17_0` and `mcp23s17_1`) mapped directly to semantic roles.
2. **HVAC Actuator Relays (12 Local Relays):**
   - Relays on `mcp23s17_2` wired to Units 1..4 (AA: Smart Cooling, SM: Bypass Selector, G: Fan Purge).
   - Remote Modbus relays (Waveshare 32CH) wired to Units 5..8.
3. **No Dynamic Pin Configuration in EEPROM:**
   - Pin reassignments (if ever needed due to board revision or hardware changes) are made in code, verified by Zephyr Devicetree and the compiler, and deployed via MCUboot dual-bank OTA.

---

### ADR-3: Fixed Modbus Bus & Slave Topology (`modbus_device_table.h`)

Dual RS-485 physical buses are partitioned by operational criticality:

* **RS-485 Bus 1 (USART2): Telemetry & Environmental Instrumentation**
  - Slave ID 1: `JSY-MK-339` (3-Phase AC Energy Meter) @ 9600 bps.
  - Slave ID 2: `CWT-TM-16PT` (16-Channel PT100/PT1000 Concentrator #1) @ 9600 bps.
  - Slave ID 3: `CWT-TM-16PT` (16-Channel Concentrator #2, Optional Expansion) @ 9600 bps.
* **RS-485 Bus 2 (USART6): Critical HVAC Actuation & Remote I/O**
  - Slave ID 4: `MBSL32DI` (32-Channel Remote Digital Input Module) @ 19200 bps.
  - Slave ID 5: `Waveshare Relay 32CH` (Remote Actuation for Units 5..8) @ 9600 bps.

**Benefit:** Eliminates 2048 bytes of volatile Modbus descriptors in EEPROM. Standardized factory DIP switch settings eliminate field configuration errors.

---

### ADR-4: Declarative Sensor Catalog, Range Mapping & Profile Budget

#### 1. Flat Sensor Identification (1-Byte Enumeration):
All temperature acquisition channels are mapped to contiguous IDs:
- **IDs 1..5**: Internal On-Board RTDs (PT100 with INA826 on PCB V3).
- **IDs 10..25**: External Modbus Concentrator #1 (Channels 1..16).
- **IDs 30..45**: External Modbus Concentrator #2 (Channels 1..16).
- **IDs 50..59**: Analog 4-20 mA on Connector J9 (Ch1 = Room Humidity %, Ch2 = Filter DP Pa).

#### 2. Temperature Sensor Budget Across Operating Profiles:
Telemetry and Control share sensor readings via Zbus/RAM without exclusive ownership:
* **Control Need:** Maximum 8 ambient sensors (up to 4 groups × 2 sensors per group).
* **Telemetry Need:** Supply/Return pairs for up to 8 AC units (16 sensors) + Room ambient + Outdoor condenser.

| Operating Profile | Control Sensors (Room Ambient) | Telemetry Sensors (Machines & Ambient) | Total Physical Channels Used |
| :--- | :--- | :--- | :--- |
| **Profile 1: Hybrid Full (HVAC + Telemetry)** | Up to 8 sensors (e.g. 5 internal RTDs + Modbus 9..11) | 16 sensors (Modbus 1..16: 8 Supply + 8 Return) + Ambient + Outdoor | **24 to 26 channels** (5 internal + 1-2 CWT modules) |
| **Profile 2: HVAC Control Only** | Up to 8 sensors (5 internal RTDs or 1 CWT module) | None (telemetry math disabled) | **5 to 16 channels** (Low-cost standalone deployment) |
| **Profile 3: Telemetry Only** | None (HVAC relays disabled/bypass) | Up to 16 Supply/Return pairs + Cold/Hot aisle mapping + Outdoor | **Up to 37 channels** (5 internal + 2 CWT modules) |

#### 3. Multi-Room / Group Range Mapping:
In facilities with up to 3 or 4 rooms, each HVAC Group represents a thermal zone. Rather than storing large routing tables, each group defines a sensor ID range:
- `sensor_start`: First sensor ID in the zone (e.g. `10`).
- `sensor_end`: Last sensor ID in the zone (e.g. `13`).
- `metric_policy`: `SENSOR_METRIC_MAX` (detects hot spots, default for datacenters) or `SENSOR_METRIC_AVG`.

```c
typedef enum {
    SENSOR_METRIC_MAX = 0, /**< Highest temp in zone (hot-spot protection) */
    SENSOR_METRIC_AVG = 1, /**< Average of healthy zone probes */
    SENSOR_METRIC_MIN = 2  /**< Minimum temp in zone */
} sensor_metric_policy_t;
```

#### 4. Automatic Fault Resilience:
If one probe in the range disconnects or fails out-of-range ($-20^\circ	ext{C}$ to $+80^\circ	ext{C}$), the group continues regulating with the remaining healthy probes. If all probes in the range fail, `BYPASS_CAUSE_SENSOR_FAULT` is asserted for that specific group.

---

### ADR-5: Flat Digital Input Enumeration & Allocation Across Profiles

The BrightC V3 system integrates **24 local on-board optocoupled inputs** plus an optional **32-channel remote Modbus module (`MBSL32DI`)**, totaling **56 digital input channels**.

#### 1. Flat Digital Input ID Catalog (1..56):
* **IDs 1..24: Local On-Board Optocouplers (Placa V3)**:
  - `DI_01..DI_04`: Unit 1 Alarms (HP, LP, Fan Overload, Comp Overload)
  - `DI_05..DI_08`: Unit 2 Alarms (HP, LP, Fan Overload, Comp Overload)
  - `DI_09..DI_12`: Unit 3 Alarms (HP, LP, Fan Overload, Comp Overload)
  - `DI_13..DI_16`: Unit 4 Alarms (HP, LP, Fan Overload, Comp Overload)
  - `DI_17`: Facility Fire / Smoke Alarm Interlock (`SMOKE_FIRE`)
  - `DI_18`: Containment Door Contact (`DOOR_OPEN`)
  - `DI_19`: Water Leak Detection Cable (`WATER_LEAK`)
  - `DI_20`: Surge Protective Device (`SURGE_DPS`)
  - `DI_21`: 3-Phase Power Sequence / Loss Monitor (`PHASE_MONITOR`)
  - `DI_22..DI_24`: Auxiliary / Spares (or partial Unit 5 alarms)
* **IDs 25..56: Remote Modbus Inputs (`MBSL32DI`, 32 Channels)**:
  - `DI_25..DI_40` (16 channels): Units 5..8 Alarms (4 units × 4 alarms: HP, LP, Fan, Comp)
  - `DI_41..DI_48` (8 channels): Evaporator Fans 1..8 Run Feedback ($U_{vent\_ON}$)
  - `DI_49..DI_56` (8 channels): Compressors 1..8 Run Feedback ($c_1 \dots c_8$)

#### 2. Digital Input Allocation Across Profiles:

| Profile | Local Inputs (1..24) | Remote Expander MBSL32DI (25..56) | External Hardware Needed? |
| :--- | :--- | :--- | :--- |
| **Profile 1: Hybrid Full (HVAC + Telemetry)** | Units 1..4 Alarms (16) + Facility Alarms (5) + Spares (3) | Units 5..8 Alarms (16) + Contactor Feedback Fans/Comps (16) | **Yes (MBSL32DI)** for 8 units + full feedback |
| **Profile 2: HVAC Control Only** | Units 1..4 Alarms (16) + Facility Alarms (5) + Spares (3) | Units 5..8 Alarms (16). Feedbacks (41..56) remain unused/spare | **Optional** (Only needed if controlling > 4 units) |
| **Profile 3: Telemetry Only** | Fans 1..8 Feedback (8) + Comps 1..8 Feedback (8) + Facility Alarms (5) | Available for extra equipment or unused | **NO external expander needed** (24 local inputs cover all telemetry) |

**Key Architectural Insight:** In **Telemetry-Only mode**, the 24 local on-board inputs directly monitor all 8 fan contactors, all 8 compressor contactors, and the room alarms without requiring any external Modbus module!

---

### ADR-6: Streamlined EEPROM Storage (The 4 Essential Domains)

The non-volatile storage footprint is reduced from 8 complex domains to **4 strictly site-specific parameter domains**:

```text
Base Offset   Total Size    Logical Domain           Purpose
───────────────────────────────────────────────────────────────────────────────────────────
0x0000        1024 B (1 KB)  CONFIG_DOMAIN_DEVICE     Serial number, site alias, hardware rev
0x0400        1024 B (1 KB)  CONFIG_DOMAIN_NET        Ethernet (IP/DHCP) & Wi-Fi (SSID/PSK)
0x0800        1024 B (1 KB)  CONFIG_DOMAIN_TELEMETRY  MQTT broker URI, port, topic, poll rate
0x0C00        2048 B (2 KB)  CONFIG_DOMAIN_HVAC       Operational setpoints, N+M, group ranges
0x1400        1024 B (1 KB)  CONFIG_DOMAIN_CALIB      PT100 cable calibration trim offsets
0x1800       26624 B (26 KB) RESERVED / EVENT LOG     Circular audit log and future expansions
───────────────────────────────────────────────────────────────────────────────────────────
```

#### Streamlined HVAC Operational Structure:
```c
typedef struct {
    uint8_t  first_unit;         /* First assigned AC unit (e.g. 1) */
    uint8_t  unit_count;         /* Number of AC units in this room (e.g. 2) */
    uint8_t  active_turn_count;  /* Concurrently running units (N) (e.g. 1) */
    uint8_t  sensor_start;       /* First sensor ID (e.g. 10) */
    uint8_t  sensor_end;         /* Last sensor ID (e.g. 13) */
    uint8_t  metric_policy;      /* 0 = MAX, 1 = AVG */
    float    temp_min;           /* Cooling stop setpoint (e.g. 22.0 C) */
    float    temp_max;           /* Cooling start setpoint (e.g. 25.0 C) */
    float    temp_alarm;         /* Boost all N+M units (e.g. 28.0 C) */
    float    temp_bypass;        /* Critical mechanical bypass (e.g. 32.0 C) */
    uint16_t rotation_hours;     /* Shift rotation period (e.g. 12 hours) */
} config_hvac_group_params_t;

typedef struct {
    uint8_t                    operation_mode;    /* 0=TELEMETRY, 1=CONTROL, 2=DUAL */
    uint8_t                    total_groups;      /* 1 to 4 */
    uint8_t                    total_units;       /* 1 to 8 */
    uint8_t                    reserved;
    config_hvac_group_params_t groups[4];
} config_hvac_operational_t;
```

---

## 3. Concrete Hardware Pinout & Devicetree Cross-Reference

### 3.1 Digital Inputs (Placa V3 Devicetree Mapping)

From `brightc-controllerv3.dts`:

| ID | Semantic Role | Physical Endpoint | Devicetree Node | Debounce Setting |
| :---: | :--- | :--- | :--- | :---: |
| `1` | `DI_ROLE_HIGH_PRESSURE_1` | `mcp23s17_0`, Pin 0 | `alm_in_1` | 60 ms (Alarm) |
| `2` | `DI_ROLE_LOW_PRESSURE_1` | `mcp23s17_0`, Pin 1 | `alm_in_2` | 60 ms (Alarm) |
| `3` | `DI_ROLE_THERMAL_OVERLOAD_FAN_1`| `mcp23s17_0`, Pin 2 | `alm_in_3` | 60 ms (Alarm) |
| `4` | `DI_ROLE_THERMAL_OVERLOAD_COMP_1`| `mcp23s17_0`, Pin 3 | `alm_in_4` | 60 ms (Alarm) |
| `5` | `DI_ROLE_HIGH_PRESSURE_2` | `mcp23s17_0`, Pin 4 | `alm_in_5` | 60 ms (Alarm) |
| `6` | `DI_ROLE_LOW_PRESSURE_2` | `mcp23s17_0`, Pin 5 | `alm_in_6` | 60 ms (Alarm) |
| `7` | `DI_ROLE_THERMAL_OVERLOAD_FAN_2`| `mcp23s17_0`, Pin 6 | `alm_in_7` | 60 ms (Alarm) |
| `8` | `DI_ROLE_THERMAL_OVERLOAD_COMP_2`| `mcp23s17_0`, Pin 7 | `alm_in_8` | 60 ms (Alarm) |
| `9` | `DI_ROLE_HIGH_PRESSURE_3` | `mcp23s17_0`, Pin 8 | `alm_in_9` | 60 ms (Alarm) |
| `10`| `DI_ROLE_LOW_PRESSURE_3` | `mcp23s17_0`, Pin 9 | `alm_in_10` | 60 ms (Alarm) |
| `11`| `DI_ROLE_THERMAL_OVERLOAD_FAN_3`| `mcp23s17_0`, Pin 10 | `alm_in_11` | 60 ms (Alarm) |
| `12`| `DI_ROLE_THERMAL_OVERLOAD_COMP_3`| `mcp23s17_0`, Pin 11 | `alm_in_12` | 60 ms (Alarm) |
| `13`| `DI_ROLE_HIGH_PRESSURE_4` | `mcp23s17_1`, Pin 0 | `alm_in_13` | 60 ms (Alarm) |
| `14`| `DI_ROLE_LOW_PRESSURE_4` | `mcp23s17_1`, Pin 1 | `alm_in_14` | 60 ms (Alarm) |
| `15`| `DI_ROLE_THERMAL_OVERLOAD_FAN_4`| `mcp23s17_1`, Pin 2 | `alm_in_15` | 60 ms (Alarm) |
| `16`| `DI_ROLE_THERMAL_OVERLOAD_COMP_4`| `mcp23s17_1`, Pin 3 | `alm_in_16` | 60 ms (Alarm) |
| `17`| `DI_ROLE_SMOKE_FIRE` | `mcp23s17_1`, Pin 4 | `alm_in_17` | 60 ms (Alarm) |
| `18`| `DI_ROLE_DOOR_OPEN` | `mcp23s17_1`, Pin 5 | `alm_in_18` | 60 ms (Alarm) |
| `19`| `DI_ROLE_WATER_LEAK` | `mcp23s17_1`, Pin 6 | `alm_in_19` | 60 ms (Alarm) |
| `20`| `DI_ROLE_SURGE_PROTECTOR` | `mcp23s17_1`, Pin 7 | `alm_in_20` | 60 ms (Alarm) |
| `21`| `DI_ROLE_PHASE_MONITOR` | `mcp23s17_1`, Pin 8 | `alm_in_21` | 60 ms (Alarm) |
| `22..24`| Spare Auxiliary Alarms | `mcp23s17_1`, Pins 9..11 | `alm_in_22..24` | 60 ms |

### 3.2 Local HVAC Actuation Relays (Placa V3 Devicetree Mapping)

From `brightc-controllerv3.dts` (`hvac_relays` node on `&mcp23s17_2`):

| Unit | Signal | Relay Role | Devicetree Node | Expander Pin |
| :---: | :--- | :--- | :--- | :---: |
| **Unit 1** | `relay_aa1` | Smart Mode Cooling Actuator | `relay_aa1` | `mcp23s17_2`, Pin 9 |
| | `relay_sm1` | Smart Mode / Mechanical Bypass Selector | `relay_sm1` | `mcp23s17_2`, Pin 10 |
| | `relay_g1` | Fan Forced Purge Actuator | `relay_g1` | `mcp23s17_2`, Pin 8 |
| **Unit 2** | `relay_aa2` | Smart Mode Cooling Actuator | `relay_aa2` | `mcp23s17_2`, Pin 12 |
| | `relay_sm2` | Smart Mode / Mechanical Bypass Selector | `relay_sm2` | `mcp23s17_2`, Pin 13 |
| | `relay_g2` | Fan Forced Purge Actuator | `relay_g2` | `mcp23s17_2`, Pin 11 |
| **Unit 3** | `relay_aa3` | Smart Mode Cooling Actuator | `relay_aa3` | `mcp23s17_2`, Pin 15 |
| | `relay_sm3` | Smart Mode / Mechanical Bypass Selector | `relay_sm3` | `mcp23s17_2`, Pin 0 |
| | `relay_g3` | Fan Forced Purge Actuator | `relay_g3` | `mcp23s17_2`, Pin 14 |
| **Unit 4** | `relay_aa4` | Smart Mode Cooling Actuator | `relay_aa4` | `mcp23s17_2`, Pin 2 |
| | `relay_sm4` | Smart Mode / Mechanical Bypass Selector | `relay_sm4` | `mcp23s17_2`, Pin 3 |
| | `relay_g4` | Fan Forced Purge Actuator | `relay_g4` | `mcp23s17_2`, Pin 1 |

---

## 4. Implementation & Refactoring Backlog (Tasks)

| Phase | Component | Action Items | Priority |
| :---: | :--- | :--- | :---: |
| **Phase 1** | **Hardware Declarations** | Created `app/include/common/hardware_pin_map.h`, `modbus_device_table.h`, and `sensor_hardware_map.h`. Consolidates terminal block assignments (24 local DIs, 32 remote DIs, 12 local relays, 5 internal RTDs). | **COMPLETED** |
| **Phase 2** | **Storage Pruning** | Refactored `config_types.h` to 5 site-specific domains (`DEVICE`, `NET_ETH`, `NET_WIFI`, `HVAC`, `CALIBRATION`). Removed obsolete `default_config_profiles.c/.h` in favor of `app_defaults.h`. Simplified `ao_storage` provider registrations. Verified 100% test pass on storage, hvac, and modbus suites. | **COMPLETED** |
| **Phase 3** | **HVAC Multi-Room Range Engine** | Implemented `[sensor_start, sensor_end]` range evaluation, `sensor_metric_policy_t` (MAX/AVG/MIN), `hvac_compute_group_metrics_range()`, `ao_hvac_feed_sensor_snapshot()`, and updated default profiles in `app_defaults.h`. Verified with 37 unit tests. | **COMPLETED** |
| **Phase 4** | **Telemetry Data Pipeline** | Implemented `sensor_matrix_pack_flat_snapshot()` mapping internal RTDs (1..5), CWT 1 (10..25), CWT 2 (30..45), and J9 (50..59) into 64-bit snapshot. Updated `on_modbus_cwt_tm16pt_reading()` for CWT concentrators 1 and 2, and fed unified snapshot to `ao_hvac_feed_sensor_snapshot()` in `composition_root.c`. Verified 100% test pass. | **COMPLETED** |
| **Phase 5** | **Facade & Shell Alignment** | Updated `app_facade_hvac_status_t` with `sensor_start`, `sensor_end`, and `metric_policy`. Implemented `app_facade_hvac_configure_group_sensors()` and `app_facade_hvac_configure_group_units()`. Updated `shell_brightc.c` with `hvac sensors <grp> <start> <end> [max|avg|min]` and `hvac config <grp> <active_turn> [unit_mask_hex]`. Displayed sensor ranges and metric policy in `hvac status`. Verified 100% test pass. | **COMPLETED** |
| **Phase 6** | **Unit & Integration Tests** | Update Ztest suites on `native_sim` (`tests/unit/hvac/`, `tests/unit/storage/`, `tests/unit/sensors/`, `tests/unit/digital_input/`). Verify 100% pass rate. | **P1** |

---

## 5. Verification Criteria

1. **Compilation & Invariants:**
   - 0 `DEVICE_DT_GET` inside adapters.
   - 0 `<zephyr/` headers in domain.
   - 0 `api->` direct calls outside adapters and DI.
2. **Deterministic Startup:**
   - Boot succeeds instantly even with a blank/unformatted EEPROM, using default compile-time hardware mappings.
3. **Control Isolation:**
   - Disconnecting network or simulating storage error has 0 impact on `ao_hvac` tick and relay actuation.
