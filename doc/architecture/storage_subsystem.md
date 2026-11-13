# Architecture of the Persistent Storage Subsystem (Storage Subsystem)

- **Status**: Technical Architecture Specification
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6)
- **Physical Medium**: External EEPROM AT24C256C (32 KB, I2C2)
- **Integrity Engine**: `microconf` (Dual-Slot Ping-Pong + CRC32)
- **Architectural Pattern**: Clean Architecture (Ports and Adapters) + Active Object Coordinator

---

## 1. General Overview and Technical Justification

### 1.1 Why is a dedicated storage subsystem needed?

The BrightC V3 controller needs to persist multiple configuration categories that must survive restarts and unexpected power cuts:
- Configuration of buses and Modbus RTU slave table.
- Ethernet interface configuration (static IP/DHCP, MAC, gateway).
- Wi-Fi interface configuration (STA/AP/STA+AP mode, SSID, credentials, soft-AP IP).
- Device identity parameters (serial number, station name, telemetry endpoints).
- Analog calibrations (scale factors and ADC channel offsets).

### 1.2 The Problem: Starvation and Concurrency over the EEPROM

Allowing any thread or use case to write directly to the physical EEPROM introduces critical design risks:
1. **Blocking and Jitter in Real-Time Tasks**: The physical EEPROM (AT24C256C) has a page write cycle time ($t_{WR}$) of **5 ms to 10 ms**. If a control or serial communication thread (e.g. `ao_modbus`) writes synchronously to the EEPROM, it blocks for 10–25 ms per page, causing frame loss and severe jitter.
2. **Unsafe Concurrent Access over I2C2 Bus**: The `microconf` library guarantees slot structure, serialization, and CRC32 integrity, but **is not thread-safe or concurrent**: it lacks internal mutual exclusion or wait queues.
3. **Premature EEPROM Cell Wear-out (EEPROM Wear-out)**: Burst changes (such as an operator adjusting several parameters in a row from the Shell or DWIN screen) would destroy cell life if each modification triggered an immediate physical write. A debounce/coalescing buffering window is required.

### 1.3 The Solution: Asynchronous Storage Coordinator (`ao_storage`)

To solve these problems without violating Dependency Inversion (DIP):
- The application and domain interact exclusively through RAM and abstract contracts (`i_config_storage.h`).
- Domain mutations signal dirty state directly and deterministically to the coordinator via `ao_storage_mark_dirty(&app->storage.ao, domain, sub_id)`.
- A single asynchronous actor, the **Storage Active Object (`ao_storage`)**, is the exclusive owner of the physical EEPROM driver and `microconf` instances.
- Upon successful asynchronous flush, `ao_storage` broadcasts `CONFIG_EVENT_PERSISTED` on `chan_system_config` so interested observers know configuration is durable in silicon.
- **Fault-Tolerant Degraded Mode**: If the EEPROM device is absent, failing, or unready, the system does not abort; `ao_storage` enters degraded mode (operating purely on RAM-cached defaults, discarding writebacks, and logging warnings).

---

## 2. EEPROM AT24C256C Memory Map

The physical EEPROM has 32,768 bytes (32 KB, addresses `0x0000` to `0x7FFF`). Through the `base_offset` and `total_size` parameters of `mconf_zephyr_eeprom_init()`, memory is partitioned into independent logical regions:

```text
Base Offset   Total Size    Logical Domain             Structure and Purpose
───────────────────────────────────────────────────────────────────────────────────────────
0x0000        256 B          CONFIG_DOMAIN_DEVICE       2 slots of 128 B
0x0100        128 B          CONFIG_DOMAIN_NET_ETH      2 slots of 64 B
0x0180        640 B          CONFIG_DOMAIN_NET_WIFI     2 slots of 320 B
0x0400        1152 B         CONFIG_DOMAIN_HVAC         2 slots of 576 B
0x0880        384 B          CONFIG_DOMAIN_CALIBRATION  2 slots of 192 B
0x0A00        1536 B         CONFIG_DOMAIN_THERMAL_SITE 2 slots of 768 B
0x1000        1024 B         CONFIG_DOMAIN_THERMAL_BASELINE 2 slots of 512 B
0x1400        1024 B         RESERVED (ex-MODBUS)       Freed when the Modbus topology
                                                     became compile-time
0x1800..0x7FFF  Reserved     Available for future domains/event log
───────────────────────────────────────────────────────────────────────────────────────────
```

### 2.1 Region Isolation Principle
- Each region contains its own ping-pong slot pair managed by `microconf`.
- A change in network structure **does not invalidate** the fingerprint nor CRC of any other region.
- The Modbus bus/slave topology has **no domain at all**: it is compile-time configuration
  (`app/Kconfig.modbus` + devicetree), so nothing about the RS-485 plant is persisted or reloaded.
  Envelopes left at `0x1400` by older firmware are simply never read.
- If a region suffers corruption due to hardware failure, the other regions remain intact and readable.

### 2.2 CONFIG_DOMAIN_HVAC Specification & Lifecycle
- **Payload Structure**: [`config_hvac_t`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/app/include/common/hvac_types.h) defined in `app/include/common/hvac_types.h`.
- **Payload Size**: ~188 bytes (optimized from legacy ~476 bytes by removing the redundant 288-byte `hvac_unit_hw_config_t units[8]` array, leaving `hardware_pin_map.h` as the single authoritative source of truth for physical hardware routing). Encapsulates `hvac_operation_mode_t`, 4 thermal groups with temperature setpoints, safety timers, $N+M$ redundancy metrics, and safety interlocks.
- **Allocated Memory**: 2048 bytes at base address `0x2800` organized into 2 ping-pong slots of 1024 bytes each, leaving ample headroom (> 800 bytes) over the 32-byte `microconf` header plus payload.
- **Lifecycle & Synchronization**:
  1. **Bootstrapping**: Loaded synchronously during `composition_root_init()` before Active Object threads start. If empty or corrupted CRC, defaults are loaded.
  2. **Runtime Mutation**: Modifications (via HMI, CLI, or cloud command) update RAM and emit `CONFIG_EVENT_DIRTY` on `chan_system_config`.
  3. **Debounced Commit**: `ao_storage` coalesces writes over a 500 ms debounce window and performs an atomic ping-pong commit with CRC32 calculation.
  4. **Notification**: Broadcasts `CONFIG_EVENT_PERSISTED` upon completion to inform all subsystems that configuration is safe in silicon.

---

## 3. Architecture Diagram and Data Flow

```text
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                                APPLICATION LAYER                                    │
 │                                                                                     │
 │  ┌─────────────────────┐   ┌─────────────────────┐   ┌───────────────────────────┐  │
 │  │   modbus_registry   │   │     network_mgr     │   │      device_profile       │  │
 │  │ (RAM table)         │   │ (RAM Network Config)│   │ (MCU Identity in RAM)     │  │
 │  └──────────┬──────────┘   └──────────┬──────────┘   └─────────────┬─────────────┘  │
 │             │                         │                            │                │
 │             └─────────────────────────┼────────────────────────────┘                │
 │                                       ▼ (Publish change event)                      │
 │                      ═══════════════════════════════════════                        │
 │                           ZBUS CHANNEL: chan_system_config                          │
 │                      ════════════════════════════════════════                        │
 │                                       │                                             │
 │                                       ▼ (Async subscription)                        │
 │                      ┌──────────────────────────────────────┐                       │
 │                      │              ao_storage              │                       │
 │                      │       (Storage Coordinator AO)       │                       │
 │                      │                                      │                       │
 │                      │  - k_msgq message queue              │                       │
 │                      │  - Debounce Timer (500 ms)           │                       │
 │                      │  - Dirty-Check (avoid rewrite)       │                       │
 │                      └──────────────────┬───────────────────┘                       │
 └─────────────────────────────────────────┼───────────────────────────────────────────┘
                                           │ Consumes abstract interface
                                           ▼
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                        PORT / INTERFACE: i_config_storage.h                         │
 │                 (Pure C99 contract: save, load, reset_defaults)                     │
 └─────────────────────────────────────────┬───────────────────────────────────────────┘
                                           │ Implemented by
                                           ▼
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                      INFRASTRUCTURE LAYER (ADAPTERS)                                │
 │                                                                                     │
 │  ┌───────────────────────────────────────────────────────────────────────────────┐  │
 │  │ adapter_storage_microconf.c                                                    │  │
 │  │ - Maintains mconf_t and mconf_io_t instances per domain                        │  │
 │  │ - Implements mconf_zephyr_eeprom_init() towards Zephyr I2C driver              │  │
 │  └──────────────────────────────────────┬────────────────────────────────────────┘  │
 └─────────────────────────────────────────┼───────────────────────────────────────────┘
                                           │ I2C2 Control
                                           ▼
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                         HARDWARE & DEVICETREE (DTS)                                 │
 │                 External EEPROM AT24C256C (eeprom0: eeprom@50)                      │
 └─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Contracts and Interfaces of the Subsystem

### 4.1 Domain Definition (`app/include/common/config_types.h`)
```c
typedef enum {
    CONFIG_DOMAIN_DEVICE = 0,       /**< Device serial number, hardware rev, station alias */
    CONFIG_DOMAIN_NET_ETH,          /**< Ethernet interface configuration (IP, DHCP, MAC, Gateway) */
    CONFIG_DOMAIN_NET_WIFI,         /**< Wi-Fi interface configuration (Mode, SSID, Credentials) */
    CONFIG_DOMAIN_HVAC,             /**< HVAC sequencer operational setpoints & group ranges */
    CONFIG_DOMAIN_CALIBRATION,      /**< Analog input & PT100 temperature calibration offsets */
    CONFIG_DOMAIN_THERMAL_SITE,     /**< Site thermal & HVAC topology configuration */
    CONFIG_DOMAIN_THERMAL_BASELINE, /**< Machine-learned steady-state baseline signatures */
    CONFIG_DOMAIN_MAX               /**< Upper limit of active storage domains (7) */
} config_domain_t;
```

### 4.2 Abstract Port (`app/include/interfaces/i_config_storage.h`)
Following the three-piece rule ([`doc/STRUCTURE.md §4.1.D`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/doc/STRUCTURE.md#L377-L435)):

1. **V-Table**:
   ```c
   typedef struct {
       int (*save)(void *impl, config_domain_t domain, const void *data, size_t size);
       int (*load)(void *impl, config_domain_t domain, void *out_data, size_t max_size, size_t *out_size);
       int (*reset_defaults)(void *impl, config_domain_t domain);
   } i_config_storage_api_t;
   ```
2. **Handle**:
   ```c
   typedef struct {
       const i_config_storage_api_t *api;
       void *impl;
   } i_config_storage_t;
   ```
3. **Validated Wrappers**:
   - `static inline int i_config_storage_save(...)`
   - `static inline int i_config_storage_load(...)`
   - `static inline int i_config_storage_reset_defaults(...)`

---

## 5. Operation Dynamics and Persistence Policies

### 5.1 Boot Sequence (Bootstrapping)
Reading at boot is **completely synchronous** and occurs before initializing concurrent Active Objects, guaranteeing the system starts in a consistent and known state:

1. `composition_root_init()` initializes the EEPROM driver and calls the storage adapter.
2. For each registered domain (e.g. `CONFIG_DOMAIN_NET_ETH`):
   - `i_config_storage_load()` is invoked.
   - **Success case**: The loaded blob is validated (`config_envelope_unpack()`) and injected into the owning RAM structure.
   - **Failure case (virgin EEPROM or invalid CRC)**: Default firmware structures are loaded and the domain is marked as pending hardware initialization.
3. Active Object threads are launched (`ao_modbus`, `ao_storage`).

The Modbus subsystem is not part of this sequence: its topology is compiled in and its line
parameters come from the devicetree, so it has no domain to load and no boot dependency on
storage.

### 5.2 Runtime Modification and Saving Sequence
When a user or process modifies a parameter at runtime (e.g., changing Ethernet static IP or Wi-Fi SSID):

1. **RAM Mutation**: The component modifies its protected state in memory (e.g., `i_network_set_config()` for the affected interface). The response to the user or CLI is immediate (< 1 ms).
2. **Dirty Mark Request**: The modifying facade or active object directly marks the domain dirty in the storage coordinator:
   ```c
   /* Example: Mark Ethernet domain dirty for persistence */
   ao_storage_mark_dirty(&app->storage.ao, CONFIG_DOMAIN_NET_ETH, 0U);
   ```
   ```c
   /* Example: Mark HVAC group configuration dirty */
   ao_storage_mark_dirty(&app->storage.ao, CONFIG_DOMAIN_HVAC, (uint16_t)group_id);
   ```
3. **Coalescing & Debouncing in `ao_storage`**:
   - `ao_storage` records the dirty domain in its bitmask (`dirty_domains |= (1 << domain)`).
   - Starts or restarts the 500 ms debounce timer (`s_debounce_timer`).
4. **Debounce Expiration and Envelope Serialization**:
   - Upon timer expiry, `ao_storage` wakes up its thread.
   - For each dirty domain, it invokes the registered provider callback (`storage_provider_*`).
   - The provider obtains the authoritative domain config and wraps it in a versioned envelope (`config_envelope_t` with magic `0x4243544C`, version 1, payload length, and CRC16).
   - Invokes `i_config_storage_save()` to commit the blob atomically via `microconf` dual-slot ping-pong.
   - Publishes `CONFIG_EVENT_PERSISTED` on Zbus `chan_system_config` so observers know data is safe in silicon.

**Network domain isolation**: A change in Wi-Fi configuration **does not invalidate** the fingerprint nor CRC of the Ethernet region, and vice versa. Each domain is persisted independently.

---

## 6. Extension Guide: How to Add a New Domain

When developing a new subsystem requiring persistence (e.g., Ethernet / Wi-Fi network parameters):

1. **Assign Domain and Region**:
   - Register the new enumerator in `common/config_types.h` (e.g., `CONFIG_DOMAIN_NET_ETH`, `CONFIG_DOMAIN_NET_WIFI`).
   - Declare its `base_offset` and `total_size` in the EEPROM partition map in `adapter_storage_microconf.c`.
2. **Define Data Structure and `microconf` Schema**:
   - Create the configuration struct in `domain/models/` (flat, no pointers).
   - Define `mconf_entry_t` with their default values (e.g., IP `192.168.1.100`, DHCP enabled).
3. **Integrate into Boot Cycle**:
   - In `composition_root.c`, add the load call at boot: `i_config_storage_load(&app->storage, CONFIG_DOMAIN_NET_ETH, ...)` or `CONFIG_DOMAIN_NET_WIFI`.
4. **Notify Changes**:
   - In the network manager (`ao_network.c`), each time an Ethernet parameter is modified, publish on `chan_system_config` indicating `CONFIG_DOMAIN_NET_ETH`. For Wi-Fi, use `CONFIG_DOMAIN_NET_WIFI`.
5. **Done**:
   - `ao_storage` will automatically handle debounce, integrity verification, and EEPROM storage without touching a single line of other subsystems.
