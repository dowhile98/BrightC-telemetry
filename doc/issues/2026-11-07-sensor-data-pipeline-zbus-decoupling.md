# Architecture Specification & Issue: Sensor Data Pipeline Zbus Decoupling

- **Date:** 2026-11-07
- **Status:** APPROVED ARCHITECTURAL REFACTOR & IMPLEMENTATION SPECIFICATION
- **Platform:** DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Environment:** Zephyr RTOS (West T2 Topology)
- **Reference Standard:** Clean Architecture (§4.1.D), SOLID Principles, Zero Dynamic Memory Allocation
- **Supersedes:** `ao_sensor_matrix_frame_sink_t` callback chaining, `sensor_matrix_pack_hvac_bank()`, and direct consumer polling

---

## 1. Executive Summary & Analysis of Sensor Data Flow

The DAECGE BrightC Controller V3 operates in industrial HVAC, telecommunications shelter, and data center environments requiring real-time thermal monitoring, fault protection, and telemetry streaming. The physical sensor infrastructure is fundamentally heterogeneous:

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                         HETEROGENEOUS PRODUCERS                             │
 ├──────────────────────────────────────────┬──────────────────────────────────┤
 │           ON-BOARD HARDWARE              │       MODBUS RS485 BUSES         │
 ├──────────────────────────────────────────┼──────────────────────────────────┤
 │ - 5x Precision RTD Channels (INA826)     │ - CWT-TM-16PT Concentrator #1    │
 │   ADC3 DMA + Callendar-Van Dusen eq.     │   16x Pt100/Pt1000 probes        │
 │ - 10x 4-20 mA Analog Loops (J9 Header)   │ - CWT-TM-16PT Concentrator #2    │
 │   ADC1/ADC2 DMA Current Sense            │   16x Pt100/Pt1000 probes        │
 └──────────────────────────────────────────┴──────────────────────────────────┘
                                      │
                                      ▼
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │               SENSOR MATRIX INGESTION & CALIBRATION STAGE                   │
 │                (adapter_sensor_zephyr + ao_sensor_matrix)                   │
 │                                                                             │
 │ - Validates electrical bounds, open-probe conditions, and channel status.   │
 │ - Applies 2-point gain/offset calibration per physical channel.             │
 │ - Maps physical channels into semantic roles and pair groupings.            │
 └─────────────────────────────────────────────────────────────────────────────┘
                                      │
                     NORMALIZED CLEAN DUAL-OUTPUT
                                      │
         ┌────────────────────────────┴────────────────────────────┐
         ▼                                                         ▼
 ┌───────────────────────────────┐         ┌───────────────────────────────┐
 │      chan_sensor_snapshot     │         │      chan_thermal_matrix      │
 │  Type: sensor_snapshot_msg_t  │         │  Type: thermal_matrix_frame_t │
 │                               │         │                               │
 │ Flat 64-element float array,  │         │ Supply/return semantic pairs, │
 │ 64-bit channel validity mask, │         │ indoor/outdoor ambients,      │
 │ monotonic sequence, timestamp.│         │ pairwise consensus masks.     │
 └───────────────────────────────┘         └───────────────────────────────┘
```

### 1.1 Ingestion & Normalization
Physical readings arrive from multiple asynchronous sources:
1. **STM32 On-Chip ADC DMA**: Cyclic acquisition for 5 internal RTDs and 10 analog current loops.
2. **NanoMODBUS RS485 Polling Engine**: Asynchronous frame acquisition from CWT-TM-16PT remote RTD concentrators over isolated half-duplex RS485 lines.

The Sensor Matrix adapter buffers physical readings into `raw_sensor_readings_t`. The domain mapping service (`sensor_matrix_mapper.c`) normalizes and validates each input:
- Verifies physical limits (`SENSOR_TEMP_MIN_VALID_C` to `SENSOR_TEMP_MAX_VALID_C`).
- Converts raw counts to temperature / current via calibration and CVD polynomials.
- Produces two clean, strongly-typed representations: a flat array indexed by fixed channel IDs, and a semantic structure of supply/return pairs and ambient temperatures.

---

## 2. Elimination of Direct Coupling

### 2.1 The Anti-Pattern of Direct Consumer Polling and Callback Chaining
In legacy implementations, consumers either polled sensor APIs directly or relied on synchronous callback chaining:
- Direct polling forced consumer threads (`ao_hvac`, telemetry workers, shell CLI) to lock sensor mutexes, check hardware readiness, and couple directly to low-level acquisition timing.
- Callback chaining via `frame_sink` (`ao_sensor_matrix_frame_sink_t`) required `ao_sensor_matrix` to invoke a function pointer provided by `composition_root.c`. Inside that callback (`on_sensor_matrix_frame()`), the composition root unpacked frames, converted representations, and called `ao_hvac_feed_temperatures()`.
- Furthermore, ad-hoc fallback calls (`ao_hvac_feed_temperatures()` inside `on_modbus_cwt_tm16pt_reading()`) bypassed the sensor matrix entirely whenever the matrix was considered uninitialized, creating conflicting sources of truth and split-brain states in HVAC control logic.

### 2.2 Why Zephyr Zbus is Mandatory
Zephyr Zbus (Zero-copy message bus) provides the standard, deterministic publish-subscribe mechanism in Zephyr RTOS:
- **Spatial Decoupling:** The producer (`ao_sensor_matrix`) has zero knowledge of who receives sensor snapshots. It simply publishes a completed snapshot to a named channel.
- **Temporal Decoupling:** Listeners receive notifications synchronously, while queue subscribers receive messages asynchronously into dedicated FIFOs without blocking the acquisition thread.
- **Concurrency & Reentrancy Safety:** Zbus channel messages are copied atomically into channel storage protected by the kernel. Thread-safe atomic reads via `zbus_chan_read()` or constant reference inspection via `zbus_chan_const_msg()` prevent race conditions.
- **Architectural Purity (§4.1.D & SOLID DIP):** Eliminating callbacks from Active Object configurations adheres strictly to Clean Architecture. Concrete consumer Active Objects depend only on the message contracts declared in Zbus channel headers.

---

## 3. Channel Specifications

### 3.1 `chan_sensor_snapshot`
- **Channel Name:** `chan_sensor_snapshot`
- **Payload Type:** `sensor_snapshot_msg_t`
- **Channel Header:** `app/src/application/zbus/sensor_channels.h`
- **Definition Source:** `app/src/application/zbus/sensor_channels.c`

```c
typedef struct {
    float    temperatures[64]; /**< Flat sensor readings array (degC or loop mA), indexed by (sensor_id - 1) */
    uint64_t valid_mask;       /**< 64-bit channel validity bitmask (bit k = channel k+1 valid) */
    uint64_t timestamp_ms;     /**< Acquisition uptime timestamp in milliseconds */
    uint32_t sequence;         /**< Monotonically increasing acquisition sequence counter */
} sensor_snapshot_msg_t;
```

**Channel Mapping Layout (Flat Sensor IDs 1..59):**
- Flat IDs `1..5` (indices `0..4`, bits `0..4`): On-board RTDs 1..5 (`PHYS_CH_INTERNAL_RTD_1..5`).
- Flat IDs `10..25` (indices `9..24`, bits `9..24`): Modbus CWT Concentrator 1 channels 1..16 (`PHYS_CH_CWT_1..16`).
- Flat IDs `30..45` (indices `29..44`, bits `29..44`): Modbus CWT Concentrator 2 channels 1..16 (`PHYS_CH_CWT2_1..16`).
- Flat IDs `50..59` (indices `49..58`, bits `49..58`): Analog J9 4-20 mA channels 1..10 (`PHYS_CH_ANALOG_J9_CH1..10`).
- All valid entries satisfy `(sensor_id - 1 < 64)`.

### 3.2 `chan_thermal_matrix`
- **Channel Name:** `chan_thermal_matrix`
- **Payload Type:** `thermal_matrix_frame_t`
- **Channel Header:** `app/src/application/zbus/sensor_channels.h`
- **Definition Source:** `app/src/application/zbus/sensor_channels.c`

Carries the semantic thermal matrix structure defined in `app/include/common/sensor_types.h`:
- `t_inj[SENSOR_MATRIX_MAX_PAIRS]`: Supply/injection air temperatures in degC.
- `t_ret[SENSOR_MATRIX_MAX_PAIRS]`: Return air temperatures in degC.
- `pair_valid_mask`: Consensus validity mask for supply/return pairs.
- `inj_valid_mask` / `ret_valid_mask`: Independent probe validity masks.
- `t_room[SENSOR_ROOM_PROBES_MAX]`: Indoor ambient room temperatures.
- `t_outdoor[SENSOR_OUTDOOR_PROBES_MAX]`: Outdoor ambient temperatures.
- `humidity_pct[SENSOR_HUMIDITY_PROBES_MAX]`: Relative humidity readings.

---

## 4. Consumer Architecture

```text
                               ┌────────────────────────┐
                               │    ao_sensor_matrix    │
                               └───────────┬────────────┘
                                           │
                    ┌──────────────────────┴──────────────────────┐
                    │ publishes                                   │ publishes
                    ▼                                             ▼
        ┌───────────────────────┐                     ┌───────────────────────┐
        │  chan_sensor_snapshot │                     │  chan_thermal_matrix  │
        └───────────┬───────────┘                     └───────────┬───────────┘
                    │                                             │
      ┌─────────────┼─────────────┐                               │
      │             │             │                               │
      ▼             ▼             ▼                               ▼
┌───────────┐ ┌───────────┐ ┌───────────┐                   ┌───────────┐
│  ao_hvac  │ │ Telemetry │ │ Shell CLI │                   │   Thermo  │
│ (Listener)│ │  Spooler  │ │  Facade   │                   │  Engine   │
│           │ │(Subscriber│ │  (Atomic  │                   │ (Revision │
│           │ │   FIFO)   │ │   Read)   │                   │    F)     │
└───────────┘ └───────────┘ └───────────┘                   └───────────┘
```

1. **`ao_hvac` (High Priority Real-Time Controller):**
   - Attached as a Zbus synchronous listener `lis_hvac_sensor_feed` on `chan_sensor_snapshot`.
   - On every snapshot notification, forwards the flat temperature array and 64-bit mask directly to `ao_hvac_feed_sensor_snapshot(&self->hvac.ao, msg->temperatures, msg->valid_mask, 64U)`.
   - Never calls sensor drivers, never blocks, and is notified immediately when sensor acquisition completes.
2. **`thermodynamic_engine` (Domain Analysis & Enthalpy Calculation):**
   - Consumes semantic pairs from `chan_thermal_matrix` to compute COP, delta-T efficiency, and sensible cooling capacity per HVAC stage.
3. **`ao_telemetry_spooler` (SD Card & Cloud MQTT Storage):**
   - Connects as an asynchronous message subscriber on `chan_sensor_snapshot` with a dedicated FIFO queue.
   - Serializes telemetry records to LittleFS/FatFS on MicroSD without delaying real-time control threads.
4. **Presentation Facade & Shell CLI:**
   - Atomic, non-blocking inspection using `zbus_chan_read(&chan_sensor_snapshot, &msg, K_NO_WAIT)`.
   - Eliminates shared mutable presentation caches and complex lock hierarchies.

---

## 5. Code Elimination / Cleanup Checklist

To eliminate dead code, ambiguous paths, and architectural debt:

- [x] **Remove `ao_sensor_matrix_frame_sink_t` callback:**
  - Remove `frame_sink` and `frame_sink_ctx` from `ao_sensor_matrix_config_t` and `ao_sensor_matrix_t`.
  - Remove callback typedef from `app/src/application/active_objects/ao_sensor_matrix.h`.
  - Remove callback execution from `ao_sensor_matrix_step()`.
- [x] **Remove `sensor_matrix_pack_hvac_bank()`:**
  - Remove declaration from `app/src/domain/services/sensor_matrix_mapper.h`.
  - Remove implementation from `app/src/domain/services/sensor_matrix_mapper.c`.
  - Establish `sensor_matrix_pack_flat_snapshot()` as the sole, authoritative snapshot packing engine.
- [x] **Remove `on_sensor_matrix_frame()` in `composition_root.c`:**
  - Remove static function and forward declaration.
  - Remove `.frame_sink` and `.frame_sink_ctx` assignments in `composition_root_init_sensor_matrix()`.
- [x] **Remove manual fallback feeding in `composition_root.c`:**
  - In `on_modbus_cwt_tm16pt_reading()`: remove manual fallback call `ao_hvac_feed_temperatures()`. Modbus CWT readings are submitted exclusively into the sensor matrix port (`i_sensor_matrix_submit_cwt_bank`), which triggers `ao_sensor_matrix`.
- [x] **Wire `lis_hvac_sensor_feed` listener in `composition_root.c`:**
  - Define `lis_hvac_sensor_feed` via `ZBUS_LISTENER_DEFINE` attached to `chan_sensor_snapshot`.
  - In listener callback, forward snapshot message to `ao_hvac_feed_sensor_snapshot()`.

---

## 6. Implementation Tasks and Verification Criteria

### 6.1 Implementation Tasks
1. **Create Zbus Channel Definitions:**
   - `app/src/application/zbus/sensor_channels.h` & `sensor_channels.c`.
   - Add `sensor_channels.c` to `app/CMakeLists.txt` and `tests/unit/sensor/CMakeLists.txt`, `tests/unit/modbus/CMakeLists.txt`.
2. **Refactor Mapper Domain Service:**
   - Delete `sensor_matrix_pack_hvac_bank()` in `sensor_matrix_mapper.h` and `.c`.
3. **Refactor Active Object `ao_sensor_matrix`:**
   - Remove `frame_sink` fields and types.
   - In `ao_sensor_matrix_step()`, publish `chan_thermal_matrix` and `chan_sensor_snapshot`.
4. **Refactor Composition Root:**
   - Remove `on_sensor_matrix_frame()` and fallback calls in `on_modbus_cwt_tm16pt_reading()`.
   - Define `lis_hvac_sensor_feed` listener.
5. **Update Unit Tests:**
   - Update `tests/unit/sensor/` tests (mapper tests and `test_ao_sensor_matrix.c`).
   - Verify `tests/unit/hvac/` and `tests/unit/modbus/`.

### 6.2 Verification Criteria
1. **Host Unit Tests:** All test suites pass 100% on `native_sim` via Twister:
   ```bash
   west twister -T tests/unit -p native_sim -v
   ```
2. **Zero Invariant Violations:**
   - Zero `<zephyr/...>` includes in `app/include/common/` and `app/src/domain/`.
   - Zero `DEVICE_DT_GET()` inside adapters.
   - Zero `api->` dereferences outside adapters and DI.
   - Zero dynamic memory allocation (`malloc`, `k_malloc`).
3. **Hardware Compilation:** Clean compilation for `brightc-controllerv3`:
   ```bash
   west build -p -b brightc-controllerv3 app -d build/primary
   ```
