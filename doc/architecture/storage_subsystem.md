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
- Changes are announced through the decoupled **Zephyr Zbus** event bus (`chan_system_config`).
- A single asynchronous actor, the **Storage Active Object (`ao_storage`)**, is the exclusive owner of the I2C2 bus, the physical EEPROM, and the `microconf` instances.

---

## 2. EEPROM AT24C256C Memory Map

The physical EEPROM has 32,768 bytes (32 KB, addresses `0x0000` to `0x7FFF`). Through the `base_offset` and `total_size` parameters of `mconf_zephyr_eeprom_init()`, memory is partitioned into independent logical regions:

```text
Base Offset   Total Size    Logical Domain         Structure and Purpose
───────────────────────────────────────────────────────────────────────────────────────────
0x0000        1024 B (1 KB)  CONFIG_DOMAIN_DEVICE   2 slots of 512 B (Serial, HW Ver, Alias)
0x0400        2048 B (2 KB)  CONFIG_DOMAIN_MODBUS   2 slots of 1 KB (Buses + 32 Slaves)
0x0C00        1024 B (1 KB)  CONFIG_DOMAIN_NET_ETH  2 slots of 512 B (Static IP/DHCP, MAC, gateway)
0x1000        1024 B (1 KB)  CONFIG_DOMAIN_NET_WIFI 2 slots of 512 B (STA/AP/STA+AP mode, SSID, credentials, soft-AP IP)
0x1400        3072 B (3 KB)  CONFIG_DOMAIN_CALIB    2 slots of 1.5 KB (Offsets and ADC Factors)
0x2000        8192 B (8 KB)  RESERVED               Future expansions (SNMP, Certificates)
0x4000       16384 B (16 KB) EVENT LOG / HISTORY    Circular storage for auditing
───────────────────────────────────────────────────────────────────────────────────────────
```

### 2.1 Region Isolation Principle
- Each region contains its own ping-pong slot pair managed by `microconf`.
- A change in network structure **does not invalidate** the fingerprint nor CRC of the Modbus region.
- If a region suffers corruption due to hardware failure, the other regions remain intact and readable.

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
    CONFIG_DOMAIN_DEVICE = 0,    /**< Device information and metadata */
    CONFIG_DOMAIN_MODBUS,        /**< Bus and slave configuration */
    CONFIG_DOMAIN_NET_ETH,       /**< Ethernet interface configuration (IP, DHCP, MAC) */
    CONFIG_DOMAIN_NET_WIFI,      /**< Wi-Fi interface configuration (mode, SSID, credentials) */
    CONFIG_DOMAIN_CALIBRATION,   /**< Analog input calibration constants */
    CONFIG_DOMAIN_MAX            /**< Upper limit of domains */
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
2. For each registered domain (e.g. `CONFIG_DOMAIN_MODBUS`):
   - `i_config_storage_load()` is invoked.
   - **Success case**: The loaded blob is injected directly into the RAM manager (`modbus_registry_import()`).
   - **Failure case (virgin EEPROM or invalid CRC)**: Default firmware structures (`k_default_slaves`) are loaded, injected into RAM, and the domain is marked as pending hardware initialization.
3. Active Object threads are launched (`ao_modbus`, `ao_storage`).

### 5.2 Runtime Modification and Saving Sequence
When a user or process modifies a parameter at runtime (e.g., changing Ethernet static IP or Wi-Fi SSID):

1. **RAM Mutation**: The component modifies its protected state in memory (e.g., `i_network_set_config()` for the affected interface). The response to the user or CLI is immediate (< 1 ms).
2. **Zbus Event Emission**: The component publishes a message on `chan_system_config` indicating the affected domain:
   ```c
   /* Example: Ethernet configuration change */
   system_config_event_t evt = {
       .domain = CONFIG_DOMAIN_NET_ETH,
       .event_type = CONFIG_EVENT_DIRTY,
       .sub_id = 0,
   };
   zbus_chan_pub(&chan_system_config, &evt, K_MSEC(10));
   ```
   ```c
   /* Example: Wi-Fi configuration change */
   system_config_event_t evt = {
       .domain = CONFIG_DOMAIN_NET_WIFI,
       .event_type = CONFIG_EVENT_DIRTY,
       .sub_id = 0,
   };
   zbus_chan_pub(&chan_system_config, &evt, K_MSEC(10));
   ```
3. **Reception in `ao_storage`**:
   - `ao_storage` receives the event in its message queue.
   - Marks internal flag `dirty_mask |= (1 << domain)` for the corresponding domain (`CONFIG_DOMAIN_NET_ETH` or `CONFIG_DOMAIN_NET_WIFI`).
   - Restarts the **Debounce** timer (500 ms).
4. **Debounce Expiration and Physical Write**:
   - Upon completing 500 ms of inactivity, `ao_storage` wakes up.
   - Calls the in-memory export of the dirty domain (e.g., Ethernet or Wi-Fi configuration).
   - Checks if current content differs from stored content (*Dirty-Check*).
   - If changed, invokes `i_config_storage_save()`, which executes `mconf_save()` on the corresponding EEPROM region (`0x0C00` for Ethernet, `0x1000` for Wi-Fi).
   - Publishes on Zbus an event `CONFIG_EVENT_PERSISTED` informing that data is safe in silicon.

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
