# Ports and Adapters (Hexagonal Architecture) Specification

- **Status**: Official Architecture Standard & Implementation Guide
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Environment**: Zephyr RTOS (West T2 Topology)
- **Layer**: Interfaces (`app/include/interfaces/`) & Infrastructure (`app/src/infrastructure/adapters/`)
- **Language**: English (per project convention)

---

## 1. Executive Summary & Design Principles

The **BrightC Telemetry** firmware implements the **Ports and Adapters pattern** (also known as Hexagonal Architecture) to isolate the application and domain layers from physical hardware, communications buses, and real-time operating system primitives.

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                       APPLICATION & DOMAIN LAYERS                           │
 │     (Active Objects, Use Cases, Codecs, State Machines, Zbus Listeners)     │
 └──────────────────────────────────────┬──────────────────────────────────────┘
                                        │ calls validated inline wrappers
                                        ▼
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                         PORTS (app/include/interfaces/)                     │
 │          Pure C99/C11 Contracts: i_relay.h, i_network.h, i_storage.h        │
 │              - v-table (i_<name>_api_t)                                     │
 │              - handle   (i_<name>_t { api, impl })                          │
 │              - binding check (i_<name>_is_bound())                          │
 │              - inline wrappers (i_<name>_<op>())                            │
 └──────────────────────────────────────▲──────────────────────────────────────┘
                                        │ implements v-table
                                        │
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │                  ADAPTERS (app/src/infrastructure/adapters/)                │
 │       (adapter_eth_stm32, adapter_relay_modbus, adapter_di_gpio, etc.)      │
 │              - Hardware driver integration (Zephyr APIs, GPIO, SPI)         │
 │              - Private static const v-table implementation                  │
 │              - Port factory method (adapter_<name>_port())                  │
 └──────────────────────────────────────▲──────────────────────────────────────┘
                                        │ retrieves devicetree & binds ports
 ┌──────────────────────────────────────┴──────────────────────────────────────┐
 │                      COMPOSITION ROOT (composition_root.c)                  │
 │      - The ONLY file permitted to call DEVICE_DT_GET()                      │
 │      - Wires hardware devices to adapters and ports to Active Objects       │
 └─────────────────────────────────────────────────────────────────────────────┘
```

### 1.1 Fundamental Invariants
1. **Rule §4.1.D Port Contract**: Every port interface MUST declare:
   - `i_<name>_api_t`: The operation function pointer table.
   - `i_<name>_t`: The fat-pointer handle combining `{ const i_<name>_api_t *api; void *impl; }`.
   - `i_<name>_is_bound(const i_<name>_t *port)`: Safety validation predicate verifying that both `api`, `impl`, and non-optional function pointers are valid.
   - `static inline i_<name>_<op>()`: Public type-safe wrappers performing precondition checks (handle validation, enum bounds, non-NULL buffers) before dispatching to `api-><op>()`.
2. **Port Encapsulation Invariant**: Application, domain, and consumer code **MUST NEVER** dereference `port->api->...` directly. Direct v-table access is strictly forbidden outside of the adapter implementing the port and the composition root.
3. **Pure C99/C11 Ports**: Headers in `app/include/interfaces/` MUST NOT include any `<zephyr/...>` headers or MCU silicon register headers. They must depend solely on standard libc headers (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<errno.h>`) and domain common types.
4. **Hardware Device Injection Invariant**: Adapters MUST NEVER call `DEVICE_DT_GET()` or probe devicetree directly. All Zephyr device pointers (`const struct device *`) MUST be obtained in `composition_root.c` and passed explicitly to `adapter_<name>_init()`.

---

## 2. Interface Segregation Principle (ISP) in Port Design

Ports should be fine-grained and tailored to specific consumer requirements:

### 2.1 Role-Based Segregation vs. Device Grouping (Mejora 1)
- **Anti-Pattern**: Forcing an interface to include methods that only some implementations can support (e.g., placing wireless `scan()` inside a generic network port, forcing wired Ethernet adapters to implement a dummy method returning `-ENOSYS`, or requiring callers to pass redundant `network_interface_t iface` arguments when the port handle already encapsulates the specific interface instance).
- **Best Practice**: Separate generic transport capabilities from medium-specific features:
  - `i_network_t`: Generic network interface port (Ethernet, WiFi Station, WiFi Soft-AP) exposing lifecycle (`init`, `start`, `stop`), configuration (`get_config`, `set_config`), and status observability (`get_status`). No redundant `iface` parameter; each bound handle represents a specific interface.
  - `i_wifi_control_t`: Dedicated wireless management port (`scan`, `connect_ap`, `disconnect`, `set_mode`) segregated from `i_network_t`. Ethernet consumers never see Wi-Fi scan or association methods.

```c
/* Generic network interface port contract (interfaces/i_network.h) */
typedef struct {
    int (*init)(void *impl);
    int (*start)(void *impl);
    int (*stop)(void *impl);
    int (*get_config)(void *impl, void *out_data, size_t max_size, size_t *out_size);
    int (*set_config)(void *impl, const void *data, size_t size);
    int (*get_status)(void *impl, net_status_t *out_status);
} i_network_api_t;

/* Dedicated wireless control port contract (interfaces/i_wifi_control.h) */
typedef struct {
    int (*scan)(void *impl);
    int (*connect_ap)(void *impl, const char *ssid, const char *psk, net_wifi_security_t sec);
    int (*disconnect)(void *impl);
    int (*set_mode)(void *impl, net_wifi_mode_t mode);
} i_wifi_control_api_t;
```

### 2.2 Actuator Status & Telemetry Observability
Actuators (e.g., relays, valves, solenoids) should not only support command operations (`set_state`), but also expose observable runtime diagnostics via `get_status`:
```c
typedef struct {
    relay_state_t state;           /**< Current physical contact state (OPEN / CLOSED) */
    uint32_t      cycle_count;     /**< Accumulated switching cycle count */
    uint32_t      last_toggle_ms;  /**< System uptime in milliseconds of last actuation */
    bool          is_fault;        /**< Bus communication or hardware fault flag */
} relay_channel_status_t;
```
This enables predictive maintenance, telemetry reporting, and electromechanical lifetime tracking.

---

## 3. Canonical Adapter Architecture

### 3.1 Structure & Encapsulation
An adapter represents the concrete implementation of a port. It encapsulates the hardware details, RTOS drivers, or communication protocols:
- Header (`adapter_<name>.h`): Defines the adapter instance context structure (`adapter_<name>_t`) and public initialization/port-binding prototypes.
- Source (`adapter_<name>.c`): Implements static functions corresponding to the port's v-table, registers a `static const i_<name>_api_t`, and implements the port binding function:
  ```c
  i_<name>_t adapter_<name>_port(adapter_<name>_t *ctx);
  ```

### 3.2 Canonical Implementation Template

#### Port Definition (`include/interfaces/i_relay.h`)
```c
typedef struct {
    int (*set_state)(void *impl, relay_channel_t channel, relay_state_t state);
    int (*get_state)(void *impl, relay_channel_t channel, relay_state_t *out_state);
    int (*get_status)(void *impl, relay_channel_t channel, relay_channel_status_t *out_status);
} i_relay_api_t;

typedef struct {
    const i_relay_api_t *api;
    void *impl;
} i_relay_t;

static inline bool i_relay_is_bound(const i_relay_t *relay)
{
    return relay != NULL && relay->api != NULL && relay->impl != NULL &&
           relay->api->set_state != NULL && relay->api->get_state != NULL;
}

static inline int i_relay_set_state(const i_relay_t *relay, relay_channel_t channel, relay_state_t state)
{
    if (!i_relay_is_bound(relay) || !relay_channel_is_valid(channel)) {
        return -EINVAL;
    }
    return relay->api->set_state(relay->impl, channel, state);
}
```

#### Adapter Implementation (`src/infrastructure/adapters/adapter_relay_modbus.c`)
```c
static int relay_api_set_state(void *impl, relay_channel_t channel, relay_state_t state)
{
    adapter_relay_modbus_t *self = (adapter_relay_modbus_t *)impl;
    /* Translate to Modbus FC05 coil write and track cycle counts */
    self->channels[channel].cycle_count++;
    self->channels[channel].last_toggle_ms = (uint32_t)k_uptime_get();
    self->channels[channel].state = state;
    return ao_modbus_post_write_coil(self->ao, self->slave_addr, (uint16_t)channel, state);
}

static const i_relay_api_t s_adapter_relay_modbus_api = {
    .set_state  = relay_api_set_state,
    .get_state  = relay_api_get_state,
    .get_status = relay_api_get_status,
};

int adapter_relay_modbus_port(adapter_relay_modbus_t *self, i_relay_t *port)
{
    if (self == NULL || port == NULL) {
        return -EINVAL;
    }
    port->api = &s_adapter_relay_modbus_api;
    port->impl = self;
    return 0;
}
```

### 3.3 Explicit Finite State Machine in Connectivity Adapters (Mejora 4)

Connectivity adapters (`adapter_eth_stm32` and `adapter_wifi_esp_hosted`) replace scattered boolean flags (`is_ready`, `is_started`) with an explicit finite state machine (`net_adapter_fsm_state_t` declared in `common/network_types.h`):

```c
typedef enum {
    NET_ADAPTER_STATE_UNINITIALIZED = 0,
    NET_ADAPTER_STATE_INIT,
    NET_ADAPTER_STATE_TRANSPORT_READY,
    NET_ADAPTER_STATE_LINK_UP,
    NET_ADAPTER_STATE_IP_ACQUIRED,
    NET_ADAPTER_STATE_DISCONNECTED,
    NET_ADAPTER_STATE_ERROR
} net_adapter_fsm_state_t;
```

#### Deterministic Lifecycle Transitions:
1. **Uninitialized (`NET_ADAPTER_STATE_UNINITIALIZED`)**: Context is zeroed or unallocated. Port factory yields an unbound handle (`NULL` api).
2. **Init (`NET_ADAPTER_STATE_INIT` -> `NET_ADAPTER_STATE_TRANSPORT_READY`)**:
   - `adapter_*_init()` binds the Zephyr `net_if`. If the network interface is discovered and ready, the state advances to `TRANSPORT_READY`.
   - The adapter's port handle is bound once state reaches `>= NET_ADAPTER_STATE_INIT`.
3. **Start (`NET_ADAPTER_STATE_LINK_UP` or `NET_ADAPTER_STATE_DISCONNECTED`)**:
   - Calling `i_network_start()` issues `net_if_up()` and applies IP configurations.
   - If physical carrier or radio link is established, state transitions to `LINK_UP`. Otherwise, it transitions to `DISCONNECTED`.
   - On driver failure, state transitions to `ERROR`.
4. **Acquired (`NET_ADAPTER_STATE_IP_ACQUIRED`)**:
   - Polling via `i_network_get_status()` detects IPv4 address assignment (via DHCP or static bind) and promotes state to `IP_ACQUIRED`.
   - Loss of carrier demotes state to `DISCONNECTED`.
5. **Stop (`NET_ADAPTER_STATE_DISCONNECTED`)**:
   - Calling `i_network_stop()` brings down the interface and resets state to `DISCONNECTED`.

#### Inspection & Testability:
Each adapter exports a dedicated state query function:
```c
net_adapter_fsm_state_t adapter_eth_stm32_get_fsm_state(const adapter_eth_stm32_t *ctx);
net_adapter_fsm_state_t adapter_wifi_esp_hosted_get_fsm_state(const adapter_wifi_esp_hosted_t *ctx);
```

---

## 4. Hardware Injection Rules in Composition Root

Adapters MUST be passed their device handles during initialization from `composition_root.c`:

```c
/* In composition_root.c: */
const struct device *mcp_dev0 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(mcp23s17_0));
const struct device *mcp_dev1 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(mcp23s17_1));

/* Pass devices explicitly to adapter */
int err = adapter_di_gpio_init(&self->di.adapter, mcp_dev0, mcp_dev1);
```

This guarantees 100% testability with mocks and fakes in `native_sim`, since tests can supply stub device structures without depending on live hardware devicetree nodes.
