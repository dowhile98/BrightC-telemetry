# Architecture of the Industrial Modbus Subsystem (Modbus Subsystem)

- **Status**: Technical Architecture Specification
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6)
- **Physical Ports**: RS485-1 (USART2, DE=PD4) and RS485-2 (USART6, DE=PG8)
- **Protocol Stack**: `nanoMODBUS` (via serial Zephyr RTOS transceiver)
- **Concurrent Pattern**: Active Object per physical port + Mutex-protected Injected Registry

---

## 1. General Overview and Guiding Principles

The BrightC Controller V3 Modbus subsystem is designed for field telemetry acquisition and actuator control in noisy industrial environments. It meets the following fundamental requirements:

1. **Total Bus Isolation (Two Concurrent Threads)**:
   - RS-485 is a physical **half-duplex** medium.
   - Each port (`RS485-1` and `RS485-2`) is owned exclusively by its own execution thread (`ao_modbus`).
   - A catastrophic wiring failure, short circuit, or hung slave on one bus never degrades bandwidth nor blocks the other bus.
2. **Static, Compile-Time Topology**:
   - Which slaves exist, on which bus, at which unit address and with which device type is
     fixed at build time by `app/Kconfig.modbus` and instantiated in one immutable table
     (`modbus_device_table.c`). A given binary always polls exactly the same devices.
   - The electrical parameters of each line (baudrate, parity, stop bits) are owned by the
     devicetree. Nothing is loaded from EEPROM and nothing is changed at runtime, so a
     firmware image is fully described by its Kconfig + devicetree pair.
3. **Pure Planning and Fault Tolerance (Offline Backoff)**:
   - The logic that decides which slave is ready for polling and its health classification is **100% pure C code** (`slave_scheduler.c`), fully decoupled from Zephyr and testable on host PC (`native_sim`).
4. **Protection Against Disconnected Slaves**:
   - After 3 consecutive failures, the slave enters slow idle state (*probe ping* every 10 s), preventing repetitive timeouts from saturating the medium and delaying healthy devices.
5. **Modbus TCP Server Complementarity**:
   - While this document specifies field bus telemetry acquisition (RS-485 Modbus RTU Master), supervisory SCADA and BMS integration over Ethernet and Wi-Fi is provided by the Modbus TCP Server subsystem. See [`doc/architecture/modbus_tcp_server_subsystem.md`](modbus_tcp_server_subsystem.md) for multi-session socket multiplexing on port 502, zero-malloc transport bridge, and comprehensive system alarm register maps.

---

## 2. Modbus Subsystem Architecture Diagram

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   APPLICATION LAYER                                    │
 │                                                                                        │
 │                    CENTRAL SLAVE MANAGER: modbus_registry.c                            │
 │        - RAM table of modbus_slave_snapshot_t (up to 32 devices)                       │
 │        - Round-robin cursor per bus and stale claim recovery                           │
 │        - Internal mutual exclusion via k_mutex (transparent to threads)                │
 │        - Read-only port: no insert / move / reconfigure operation is exposed           │
 │                                                                                        │
 └──────────────────────┬──────────────────────────────────────────┬──────────────────────┘
                        │                                          │
        i_slave_registry│ (Shared port)      i_slave_registry│ (Shared port)
                        ▼                                          ▼
 ┌──────────────────────────────────────┐   ┌─────────────────────────────────────────┐
 │   RS485-1 THREAD: ao_modbus.c (Bus 0)│   │   RS485-2 THREAD: ao_modbus.c (Bus 1)   │
 │                                      │   │                                         │
 │ - Own stack and event queue          │   │ - Own stack and event queue             │
 │ - Polls slaves assigned to Bus 0     │   │ - Polls slaves assigned to Bus 1        │
 │ - Exclusive control of USART2        │   │ - Exclusive control of USART6           │
 │ - HW Driver Enable (DE) on PD4       │   │ - HW Driver Enable (DE) on PG8          │
 └──────────────────┬───────────────────┘   └────────────────────┬────────────────────┘
                    │                                            │
       i_modbus_client_t│ (Bus 0 port)             i_modbus_client_t│ (Bus 1 port)
                    ▼                                            ▼
 ┌──────────────────────────────────────┐   ┌─────────────────────────────────────────┐
 │ adapter_modbus_nmbs.c (Instance 0)   │   │ adapter_modbus_nmbs.c (Instance 1)      │
 │ - Wraps nmbs_z_client_t              │   │ - Wraps nmbs_z_client_t                 │
 │ - Modbus read/write API              │   │ - Modbus read/write API                 │
 └──────────────────┬───────────────────┘   └────────────────────┬────────────────────┘
                    │                                            │
                    ▼                                            ▼
           [ USART2 + DE (PD4) ]                        [ USART6 + DE (PG8) ]
           Physical Transceiver 1                         Physical Transceiver 2
```

---

## 3. Components and Separation of Responsibilities

### 3.1 Pure Domain: Planning and Health (`domain/services/slave_scheduler.c`)
- Contains pure mathematical rules without depending on sockets, queues, or RTOS:
  - `slave_scheduler_is_due()`: Evaluates whether a slave fulfilled its interval according to the monotonic u64 clock.
  - `slave_scheduler_record()`: Updates counters and toggles health states:
    - `MODBUS_RESULT_OK`: Resets failure counters, passes to `MODBUS_HEALTH_ONLINE`.
    - `MODBUS_RESULT_TIMEOUT` and `MODBUS_RESULT_CRC`: Increment `consecutive_fails`. If it reaches 3, passes to `MODBUS_HEALTH_OFFLINE_BACKOFF`.
    - `MODBUS_RESULT_EXCEPTION`: The slave responded (is alive); does not increment failures nor degrade health, counted in its own statistic.

### 3.2 Application Layer: Concurrent Registry (`application/modbus/modbus_registry.c`)
- Maintains the table of descriptors and execution states.
- Protected by an internal `k_mutex`: the caller never manages locks.
- **Atomic `claim_next_due` / `record` cycle**:
  - To avoid two concurrent calls taking the same slave, the claim marks the slave as `in_flight = true`.
  - If a thread hangs or dies before reporting, the slave is automatically recovered after `MODBUS_CLAIM_STALE_MS` (5 seconds).
- **Read-only port**: `i_slave_registry_t` exposes claim/report/read operations only. The
  concrete `modbus_registry_register()` is used by the composition root to seed the compiled
  table at boot and by the factory-reset path; no other component can alter the topology.

### 3.3 Polling Active Objects (`application/active_objects/ao_modbus.c`)
- Each instance runs an event loop:
  1. Consults `i_slave_registry_claim_next_due()` for its `bus_id`.
  2. If a slave is ready, executes `poll_target()`.
  3. Reports the result with `i_slave_registry_record()`.
  4. If nothing is ready, calculates `next_due_in()` and sleeps in `k_msgq_get()` until the next expiration or until receiving a command in its queue.

---

## 4. Static Topology: Compile-Time Configuration

The topology is data, not state: it is compiled into the image and is never mutated.

| Element | Source of truth | Changed by |
| :--- | :--- | :--- |
| Slave set, bus, unit address, device type, poll interval, timeout | `app/Kconfig.modbus` → `k_brightc_modbus_slave_table` | Rebuilding the firmware |
| UART instance, DE pin, baudrate, parity, stop bits | `app/boards/brightc-controllerv3.overlay` (devicetree) | Rebuilding the firmware |
| Health, round-robin cursor, `in_flight` claim | RAM, `modbus_registry.c` | Polling runtime only |

### 4.1 Compile-Time Enablement

Each device has a Kconfig switch plus its unit address:

```text
CONFIG_BRIGHTC_MODBUS_ENABLE_JSY_MK339=y      CONFIG_BRIGHTC_MODBUS_ADDR_JSY_MK339=2
CONFIG_BRIGHTC_MODBUS_ENABLE_CWT_TM16PT_1=y   CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_1=1
CONFIG_BRIGHTC_MODBUS_ENABLE_CWT_TM16PT_2=n   CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_2=5
CONFIG_BRIGHTC_MODBUS_ENABLE_MBSL32DI=y       CONFIG_BRIGHTC_MODBUS_ADDR_MBSL32DI=3
CONFIG_BRIGHTC_MODBUS_ENABLE_WAVESHARE_RELAY=y CONFIG_BRIGHTC_MODBUS_ADDR_WAVESHARE_RELAY=4
```

`modbus_device_table.h` turns each `CONFIG_*_ENABLE_*` into a `MODBUS_ENABLE_*` constant and
`_Static_assert`s the address plan at compile time:

- every address is inside `MODBUS_SLAVE_ADDR_MIN .. MODBUS_SLAVE_ADDR_MAX`;
- no two devices share an address **within a bus** (a duplicate would fail `-EADDRINUSE` at boot);
- no two devices share an address **across the plant**, because `device_id == slave_addr` is the
  registry key (`hardware_pin_map.h` uses `MODBUS_ADDR_MBSL32DI` as the source device id of the
  remote digital inputs).

A duplicated address, or a topology larger than `MODBUS_MAX_TOTAL_SLAVES`, is therefore a build
error instead of a field failure.

### 4.2 Boot Sequence

1. `composition_root_init_modbus()` fills every bus slot with `MODBUS_BUS_CFG_UNAVAILABLE_INIT`
   and brings up one `adapter_modbus_nmbs` per bus from its devicetree configuration.
2. Each adapter reports the line parameters the devicetree applied
   (`i_modbus_client_get_bus_cfg()`); the registry stores them read-only.
3. The compiled table is walked once: rows with `is_enabled == false` are skipped, enabled rows
   are inserted with `modbus_registry_register()`. A slave whose bus transport never came up is
   not registered at all, so it cannot be reported as ONLINE while nothing polls it.
4. One `ao_modbus` thread is created per available bus.

The storage subsystem is **not** a boot dependency of Modbus: no configuration is read from or
written to EEPROM. `composition_root_init_modbus()` therefore succeeds on a container whose
storage was never initialised.

### 4.3 Consequences

- **No runtime mutation API**: `app_facade_modbus_configure_slave()`, `configure_bus()`,
  `i_slave_registry_register_device()`, `i_slave_registry_set_bus()` and
  `i_modbus_client_set_bus_cfg()` no longer exist. The previous `set_bus` failover path
  (moving a device to the other RS-485 pair by rewriting EEPROM) is replaced by changing the
  Kconfig address/bus and rebuilding.
- **Factory reset** only drops the accumulated health statistics and the round-robin cursors;
  the UART is never reconfigured and the slave table cannot change.
- **`CONFIG_DOMAIN_MODBUS` is gone**: `config_domain_t` now has 7 domains and the 0x1400 EEPROM
  window is reserved. Orphaned envelopes written by older firmware are ignored harmlessly.
- **Shell**: `modbus slave <device_id>` is a read-only query; it prints the compiled descriptor
  and states that the topology is fixed at build time.

---

## 5. Extension Guide: How to Add a New Modbus Device

To incorporate a new field device (e.g., an **8-Channel Relay Expander via Modbus RTU**):

```text
               ┌────────────────────────────────────────────────────────┐
               │ 1. common/modbus_types.h: Device identifier            │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 2. domain/services/relay_expander_codec.{c,h}          │
               │    (Pure encoding/decoding functions)                  │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 3. application/active_objects/ao_modbus.c              │
               │    - Periodic reading in poll_target()                 │
               │    - Asynchronous write actions via events             │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 4. interfaces/i_relay.h + adapter_relay_modbus.c       │
               │    (Exposes standard interface to application)         │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 5. Compile-Time Registration                           │
               │    (Kconfig switch + address, immutable table entry)   │
               └────────────────────────────────────────────────────────┘
```

### Step 1: Declare the Type in the Domain (`app/include/common/modbus_types.h`)
Add the identifier in the enumeration:
```c
typedef enum {
    MODBUS_DEV_TYPE_UNKNOWN = 0,
    MODBUS_DEV_TYPE_CWT_TM16PT,
    MODBUS_DEV_TYPE_ENERGY_METER_SDM,
    MODBUS_DEV_TYPE_RELAY_EXPANDER,    /**< Modbus digital output module */
} modbus_device_type_t;
```

### Step 2: Implement the Pure Domain Codec (`app/src/domain/services/relay_expander_codec.{c,h}`)
Write the mathematical conversion logic and bit packing in standard C, without hardware dependencies:
- Function to pack a bitmask into Modbus FC05 (Write Single Coil) or FC15 (Write Multiple Coils) request.
- Function to decode slave confirmation or coil reading (FC01).
- Write its unit test suite in `tests/unit/modbus/`.

### Step 3: Integrate into the Active Object Loop (`app/src/application/active_objects/ao_modbus.c`)
A relay module actuator has two types of interaction:

1. **Periodic Status Reading (Polling)**:
   In `poll_target()`, add the case:
   ```c
   case MODBUS_DEV_TYPE_RELAY_EXPANDER:
       return i_modbus_client_read_coils(self->client, target->slave_addr, ...);
   ```
   Upon receiving a valid frame, the current relay state is published on a Zbus channel (`chan_relay_state`).

2. **Asynchronous Writing (Relay Switching on Demand)**:
   - Since the serial port is half-duplex, **no external thread must transmit directly to the UART**.
   - To switch a relay, the application sends an event to the Active Object queue:
     ```c
     modbus_event_t evt = {
         .type = AO_MODBUS_EVENT_WRITE_COIL,
         .target_device_id = dev_id,
         .coil_index = 2,
         .coil_value = true,
     };
     ao_modbus_post(&app->ao[bus_id], &evt);
     ```
   - The `ao_modbus` thread processes the event between polling cycles and executes `i_modbus_client_write_single_coil()`.

### Step 4: Adapter to the Application Standard Port (`i_relay.h`)
- For business logic or alarms, control is performed through `i_relay_set_state()`.
- `adapter_relay_modbus.c` is implemented implementing the `i_relay_api_t` v-table, translating the call into a message to the `ao_modbus` queue.
- The domain layer does not know whether the relay is an STM32 physical pin, an MCP23S17 SPI expander, or a Modbus slave.

### Step 5: Compile-Time Registration
There is no runtime registration call. Add a Kconfig switch and its unit address to
`app/Kconfig.modbus`:

```text
menuconfig BRIGHTC_MODBUS_ENABLE_RELAY_EXPANDER
	bool "8-channel Modbus relay expander"
	default n

config BRIGHTC_MODBUS_ADDR_RELAY_EXPANDER
	int "Modbus slave address for the relay expander"
	default 6
	range 1 247
```

Then add one row to `k_brightc_modbus_slave_table` in
`app/src/application/modbus/modbus_device_table.c`:

```c
{
    .device_id        = MODBUS_ADDR_RELAY_EXPANDER,
    .bus_id           = (uint8_t) MODBUS_BUS_RS485_1,
    .slave_addr       = MODBUS_ADDR_RELAY_EXPANDER,
    .dev_type         = (uint8_t) MODBUS_DEV_TYPE_RELAY_EXPANDER,
    .poll_interval_ms = 500U,
    .timeout_ms       = 200U,
    .is_enabled       = MODBUS_ENABLE_RELAY_EXPANDER,
},
```

`device_id` must stay equal to `slave_addr`: the registry keys devices by that value and
`hardware_pin_map.h` derives the source device id of the mapped channels from it. The address
must be unique across the whole plant — the `_Static_assert`s in `modbus_device_table.h` enforce
it. Nothing is written to EEPROM and no command is needed at the bench: flashing the new image is
the whole commissioning step.
