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
2. **Hardware Independence (Hot Reassignment / Failover)**:
   - Field devices are not rigidly coupled to a physical port in code.
   - It is possible to transfer a device from Bus 1 to Bus 2 at runtime (`set_bus`), notifying threads and saving the new topology to EEPROM.
3. **Pure Planning and Fault Tolerance (Offline Backoff)**:
   - The logic that decides which slave is ready for polling and its health classification is **100% pure C code** (`slave_scheduler.c`), fully decoupled from Zephyr and testable on host PC (`native_sim`).
4. **Protection Against Disconnected Slaves**:
   - After 3 consecutive failures, the slave enters slow idle state (*probe ping* every 10 s), preventing repetitive timeouts from saturating the medium and delaying healthy devices.

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
 │        - Versioned export/import serializer for EEPROM storage                         │
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
- **Portable Serialization (`export` / `import`)**:
  - `modbus_registry_export()` packages configuration into a flat little-endian buffer independent of the CPU.
  - `modbus_registry_import()` validates the consistency of the entire buffer before writing the table to RAM.

### 3.3 Polling Active Objects (`application/active_objects/ao_modbus.c`)
- Each instance runs an event loop:
  1. Consults `i_slave_registry_claim_next_due()` for its `bus_id`.
  2. If a slave is ready, executes `poll_target()`.
  3. Reports the result with `i_slave_registry_record()`.
  4. If nothing is ready, calculates `next_due_in()` and sleeps in `k_msgq_get()` until the next expiration or until receiving a command in its queue.

---

## 4. Hot Bus Reassignment (Runtime Failover)

If the physical wiring or the `RS485-1` transceiver is damaged, the operator can move the cables to `RS485-2` and reassign the device through the port:

```c
int err = i_slave_registry_set_bus(&app->registry, device_id, MODBUS_BUS_RS485_2);
```

### 4.1 Internal Execution Sequence:
1. **Atomic Validation**: The registry checks that Bus 2 is not full (`MODBUS_MAX_SLAVES_PER_BUS = 16`) and that the slave address is not duplicated on the destination bus (`-EADDRINUSE`).
2. **Switching**: The `bus_id` field is updated inside the descriptor.
3. **Zbus Notification**: The event is emitted on `chan_system_config`.
4. **Destination Bus Wake**: `AO_MODBUS_EVENT_POLL_NOW` is sent to the Bus 2 thread so it does not wait idly and takes the slave instantly.
5. **EEPROM Persistence**: The `ao_storage` coordinator receives the Zbus event and stores the new assignment in EEPROM via `microconf`.

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
               │ 5. Registry & EEPROM Persistence                       │
               │    (Registers in registry and saves only)              │
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

### Step 5: Registration and Automatic Persistence
Upon initialization or via configuration command, the descriptor is registered:
```c
modbus_slave_descriptor_t desc = {
    .device_id = 2U,
    .bus_id = MODBUS_BUS_RS485_1,
    .slave_addr = 5U,
    .dev_type = MODBUS_DEV_TYPE_RELAY_EXPANDER,
    .poll_interval_ms = 500U,
    .timeout_ms = 200U,
    .is_enabled = true,
};
i_slave_registry_register_device(&app->registry, &desc);
```
The storage coordinator (`ao_storage`) detects the insertion and automatically saves the new slave to EEPROM.
