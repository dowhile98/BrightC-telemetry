# Architecture of the Network Subsystem (Network Subsystem)

- **Status**: Technical Architecture Specification
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Execution Environment**: Zephyr RTOS (West T2 Topology)
- **Author**: BrightC Firmware Team
- **Document Language**: Spanish (architecture documentation); code in English

---

## 1. General Overview

### 1.1 What is this subsystem for?

The BrightC Controller V3 has **two physical network interfaces**:

| Interface | Medium | Role |
|-----------|--------|------|
| **Ethernet** | RMII (STM32H7 MAC + LAN8742 PHY) | Main wired network interface |
| **WiFi** | ESP32 coprocessor via ESP-Hosted-MCU on SPI4 | Secondary wireless interface |

The network subsystem's purpose is to **manage both interfaces independently and simultaneously** (multi-homing), allowing:

1. **Both interfaces UP at the same time** (not failover from one to the other).
2. **Each interface to have its own configuration** (IP, credentials, operating mode).
3. **Each interface to have its own state machine** (connected, disconnected, etc.).
4. **Configuration can be changed at runtime** without restarting the firmware.
5. **Configuration persists in EEPROM** through the storage subsystem (`microconf` + `ao_storage`).

### 1.2 What this subsystem does NOT do

- **Does not implement automatic failover** between Ethernet and WiFi. If Ethernet drops, WiFi does not automatically take traffic unless a higher-level service decides to switch interfaces.
- **Does not implement high-level services** (MQTT, HTTP, SNMP, Modbus TCP). Those services will be designed separately and will consume the network state exposed by this subsystem.
- **Does not route traffic** between interfaces. Each service chooses which interface to bind its sockets to.

---

## 2. Design Decisions (Closed)

The following decisions were agreed upon and define the architecture:

| # | Decision | Justification |
|---|----------|---------------|
| D1 | **Both interfaces active simultaneously** | The device must be able to serve traffic via cable and WiFi at the same time. There is no "backup" interface. |
| D2 | **Single Active Object `ao_network`** | Orchestrates both interfaces. Avoids two threads, two `k_msgq`s, and race conditions over Zephyr `net_if`. Respects the MPU rule of a single stack at file scope. |
| D3 | **Separate configuration per interface** | Ethernet and WiFi have different parameters. Stored in independent storage domains (`CONFIG_DOMAIN_NET_ETH` and `CONFIG_DOMAIN_NET_WIFI`). |
| D4 | **WiFi defaults to AP mode** | Allows configuring network credentials the first time without needing Ethernet. |
| D5 | **Ethernet defaults to DHCP** | Factory-ready configuration for use in wired network with DHCP server. Changeable to static IP at runtime. |
| D6 | **High-level services out of scope** | MQTT, HTTP, SNMP, etc. are implemented as separate services consuming `i_network_t` and listening for state via Zbus. |

---

## 3. Architecture by Layers (Clean Architecture)

```
 ┌─────────────────────────────────────────────────────────────────────┐
 │  HIGH-LEVEL SERVICES (FUTURE: MQTT, HTTP, SNMP, Modbus TCP)       │
 │  - Consult network state via Zbus                                   │
 │  - Bind their sockets to a specific interface                       │
 └─────────────────────────────────────────────────────────────────────┘
                               │ Zbus: chan_network_eth_state
                               │ Zbus: chan_network_wifi_state
 ┌─────────────────────────────────────────────────────────────────────┐
 │  APPLICATION LAYER — ao_network (single Active Object)             │
 │  - Orchestrates Ethernet and WiFi independently                    │
 │  - Applies configuration at startup and at runtime                 │
 │  - Publishes status of each interface on its Zbus channels         │
 └─────────────────────────────────────────────────────────────────────┘
                               │ i_network_t (common port)
 ┌─────────────────────────────────────────────────────────────────────┐
 │  COMMON PORT — i_network.h                                          │
 │  - V-table: init, start, stop, get_config, set_config,              │
 │    get_status, scan                                                 │
 │  - Handle: { api, impl }                                            │
 │  - Two instances: net_eth, net_wifi                                 │
 └─────────────────────────────────────────────────────────────────────┘
                               │ implements
 ┌─────────────────────────────────────────────────────────────────────┐
 │  INFRASTRUCTURE LAYER — Adapters                                   │
 │  - adapter_eth_stm32.c     → ETH_STM32_HAL, net_if, net_dhcpv4      │
 │  - adapter_wifi_esp_hosted.c → wifi_mgmt, ESP-Hosted-MCU driver     │
 └─────────────────────────────────────────────────────────────────────┘
                               │ DEVICE_DT_GET()
 ┌─────────────────────────────────────────────────────────────────────┐
 │  HARDWARE / DEVICETREE                                              │
 │  - ethernet@40028000 (RMII, LAN8742, PHY reset PG4)                │
 │  - esp_hosted_mcu@0 on SPI4 → wifi + bt-hci                         │
 └─────────────────────────────────────────────────────────────────────┘
                               │ I2C2 + EEPROM AT24C256C
 ┌─────────────────────────────────────────────────────────────────────┐
 │  STORAGE — microconf + ao_storage                                  │
 │  - CONFIG_DOMAIN_NET_ETH (0x0C00, 1 KB)                            │
 │  - CONFIG_DOMAIN_NET_WIFI (0x1000, 1 KB)                           │
 └─────────────────────────────────────────────────────────────────────┘
```

### 3.1 Unidirectional Dependency Rule

- **Domain** (`app/include/common/network_types.h`): Pure C99. Only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<errno.h>`. **Prohibited** `<zephyr/...>`.
- **Ports** (`app/include/interfaces/i_network.h`): Abstract contracts in C. No Zephyr dependency.
- **Adapters** (`app/src/infrastructure/adapters/`): Implement ports using Zephyr APIs. Contain `#include <zephyr/...>`.
- **Composition Root** (`composition_root.c`: Only file allowed to invoke `DEVICE_DT_GET()`.

---

## 4. Domain Level — Data Types

File: `app/include/common/network_types.h` (to be created)

### 4.1 Identifiers and states

```c
typedef enum {
    NETWORK_INTERFACE_ETHERNET = 0,
    NETWORK_INTERFACE_WIFI,
    NETWORK_INTERFACE_MAX
} network_interface_t;

typedef enum {
    NET_IF_STATE_DOWN = 0,         /* Interface administratively disabled */
    NET_IF_STATE_DISCONNECTED,     /* No link / not associated */
    NET_IF_STATE_CONNECTING,       /* In process (DHCP, WiFi association) */
    NET_IF_STATE_CONNECTED,        /* Link UP and valid IP */
    NET_IF_STATE_FAILED            /* Permanent error */
} net_if_state_t;

typedef enum {
    NET_IP_MODE_DHCP = 0,
    NET_IP_MODE_STATIC
} net_ip_mode_t;

typedef enum {
    NET_WIFI_MODE_STA = 0,         /* Station only (client) */
    NET_WIFI_MODE_AP = 1,          /* Access point only */
    NET_WIFI_MODE_STA_AP = 2       /* Station + AP simultaneous */
} net_wifi_mode_t;

typedef enum {
    NET_WIFI_SEC_OPEN = 0,
    NET_WIFI_SEC_WPA_PSK = 2,
    NET_WIFI_SEC_WPA2_PSK = 3,
    NET_WIFI_SEC_WPA3_SAE = 6
} net_wifi_security_t;
```

### 4.2 Ethernet Configuration (flat, serializable)

```c
typedef struct {
    net_ip_mode_t ip_mode;         /* DHCP or static */
    uint8_t  mac_addr[6];          /* Read-only, read from HW in init */
    uint32_t ip_addr;              /* Host order */
    uint32_t subnet_mask;
    uint32_t gateway;
    bool     auto_dhcpv4;          /* Start automatic DHCP on link up */
} net_eth_config_t;
```

### 4.3 WiFi Configuration (separate STA and AP fields)

The struct includes both parameter sets. The adapter ignores those not applicable to the active mode:

```c
/* Parameters only used when mode includes STA */
typedef struct {
    char                 ssid[33];             /* SSID of network to join */
    char                 psk[65];              /* Password (empty if OPEN) */
    net_wifi_security_t  security;
    net_ip_mode_t        ip_mode;              /* DHCP or static for STA IP */
    uint32_t             ip_addr;
    uint32_t             subnet_mask;
    uint32_t             gateway;
} net_wifi_sta_params_t;

/* Parameters only used when mode includes AP */
typedef struct {
    char                 ssid[33];             /* Soft-AP SSID */
    char                 psk[65];              /* AP password (empty if OPEN) */
    net_wifi_security_t  security;
    uint8_t              channel;              /* 1-13, default 9 */
    uint8_t              max_clients;          /* default 5 */
    uint32_t             ap_ip_addr;           /* AP gateway IP (e.g. 192.168.4.1) */
    uint32_t             ap_subnet_mask;
} net_wifi_ap_params_t;

typedef struct {
    net_wifi_mode_t        mode;               /* STA / AP / STA_AP */
    net_wifi_sta_params_t  sta;                /* Used if mode == STA or STA_AP */
    net_wifi_ap_params_t   ap;                 /* Used if mode == AP or STA_AP */
    uint8_t                mac_addr[6];        /* Read-only, read from HW */
} net_wifi_config_t;
```

### 4.4 Runtime State (not persisted)

```c
typedef struct {
    net_if_state_t state;
    uint32_t       ip_addr;
    uint32_t       subnet_mask;
    uint32_t       gateway;
    int8_t         rssi;             /* WiFi only; -128 if N/A */
    uint32_t       link_up_ms;       /* Time with link UP */
} net_status_t;
```

### 4.5 Operation Result

```c
typedef enum {
    NETWORK_RESULT_OK = 0,
    NETWORK_RESULT_INVALID = -1,
    NETWORK_RESULT_NOT_READY = -2,
    NETWORK_RESULT_NOT_CONNECTED = -3,
    NETWORK_RESULT_TIMEOUT = -4,
    NETWORK_RESULT_IO_ERROR = -5,
    NETWORK_RESULT_UNSUPPORTED = -6
} network_result_t;
```

---

## 5. Port Level — `i_network.h`

File: `app/include/interfaces/i_network.h` (to be created)

The port is **common for both interfaces**. Each adapter internally validates the configuration type it receives.

```c
typedef struct {
    int (*init)(void *impl, network_interface_t iface);
    int (*start)(void *impl, network_interface_t iface);
    int (*stop)(void *impl, network_interface_t iface);
    int (*get_config)(void *impl, network_interface_t iface,
                      void *out_data, size_t max_size, size_t *out_size);
    int (*set_config)(void *impl, network_interface_t iface,
                      const void *data, size_t size);
    int (*get_status)(void *impl, network_interface_t iface,
                      net_status_t *out_status);
    int (*scan)(void *impl, network_interface_t iface);   /* WiFi only; ETH returns -ENOSYS */
} i_network_api_t;

typedef struct {
    const i_network_api_t *api;
    void *impl;
} i_network_t;
```

In `app_container_t` there will be two independent instances:

```c
i_network_t net_eth;   /* Ethernet port */
i_network_t net_wifi;  /* WiFi port */
```

Both can be bound and active at the same time.

**Invariant:** `handle->api->...` is only invoked within the adapter and in `composition_root.c`. The application never accesses the v-table directly.

---

## 6. Adapter Level (Infrastructure)

### 6.1 `adapter_eth_stm32`

Files: `app/src/infrastructure/adapters/adapter_eth_stm32.c`, `adapter_eth_stm32.h`

| Operation | Zephyr Implementation |
|-----------|----------------------|
| `init` | `DEVICE_DT_GET(eth_node)`, `device_is_ready()`, PHY reset via GPIO `eth_phy_rst`, read MAC from device |
| `start` | `net_if_up(iface)`. If `ip_mode == DHCP` → `net_dhcpv4_start(iface)`. If static → `net_if_ipv4_addr_add()`, `net_if_ipv4_set_netmask()`, `net_if_ipv4_set_gw()` |
| `stop` | `net_dhcpv4_stop(iface)`, `net_if_down(iface)` |
| `get_config` | Read `net_eth_config_t` saved in adapter RAM |
| `set_config` | Validate `size == sizeof(net_eth_config_t)`, apply changes (restart DHCP or reassign static IP) |
| `get_status` | Read `net_if`, `net_if_carrier_ok()`, assigned IPv4 |
| `scan` | `-ENOSYS` (not applicable to Ethernet) |

Adapter context (`impl`):

```c
struct adapter_eth_stm32_ctx {
    const struct device *eth_dev;
    struct net_if *iface;
    net_eth_config_t cfg;
    net_status_t   status;
    bool           is_ready;
};
```

### 6.2 `adapter_wifi_esp_hosted`

Files: `app/src/infrastructure/adapters/adapter_wifi_esp_hosted.c`, `adapter_wifi_esp_hosted.h`

| Operation | Zephyr Implementation |
|-----------|----------------------|
| `init` | `DEVICE_DT_GET(wifi_node)`, `device_is_ready()`, read STA MAC |
| `start` | According to `mode`: STA → `wifi_mgmt` connect with SSID/PSK; AP → `wifi_mgmt` AP enable; STA+AP → both. If DHCP → `net_dhcpv4_start()` |
| `stop` | `wifi_mgmt` disconnect / AP disable, `net_dhcpv4_stop()`, `net_if_down()` |
| `get_config` | Read `net_wifi_config_t` saved in RAM |
| `set_config` | Validate `size == sizeof(net_wifi_config_t)`, apply credentials/mode |
| `get_status` | Read `wifi_iface_state`, RSSI, assigned IP |
| `scan` | `wifi_mgmt` scan request |

Adapter context:

```c
struct adapter_wifi_esp_hosted_ctx {
    const struct device *wifi_dev;
    struct net_if *iface_sta;
#if defined(CONFIG_WIFI_ESP_HOSTED_MCU_AP_STA_MODE)
    struct net_if *iface_ap;
#endif
    net_wifi_config_t cfg;
    net_status_t   status;
    bool           is_ready;
};
```

**Naming convention:** public functions follow the project pattern (seen in `adapter_modbus_nmbs`):

- `adapter_eth_stm32_init()`
- `adapter_eth_stm32_port()`
- `adapter_wifi_esp_hosted_init()`
- `adapter_wifi_esp_hosted_port()`

---

## 7. Application Level — `ao_network` (single Active Object)

Files: `app/src/application/active_objects/ao_network.c`, `ao_network.h`

### 7.1 Responsibilities

1. **Startup**: initializes both adapters and applies configuration loaded from storage.
2. **Maintenance**: monitors the status of each interface independently.
3. **Hot reconfiguration**: upon receiving event `CONFIG_DOMAIN_NET_ETH` or `CONFIG_DOMAIN_NET_WIFI` on `chan_system_config`, applies changes to the corresponding interface **without stopping the other**.
4. **Status publication**: publishes `net_status_t` on separate Zbus channels:
   - `chan_network_eth_state`
   - `chan_network_wifi_state`

### 7.2 State Machines per Interface

Each interface has its own state machine. There is no global machine.

#### 7.2.1 Ethernet states (independent)

```
DOWN ──start()──▶ DISCONNECTED ──link up──▶ CONNECTING ──DHCP OK / static IP──▶ CONNECTED
                                                        │
                                                      DHCP failed / error
                                                        ▼
                                                      FAILED
```

Ethernet events:
- `EV_ETH_LINK_UP` / `EV_ETH_LINK_DOWN` (PHY)
- `EV_ETH_DHCP_LEASE_OBTAINED` / `EV_ETH_DHCP_LOST`
- `EV_ETH_IP_STATIC_APPLIED`

#### 7.2.2 WiFi states (independent, depends on mode)

**STA mode:**
```
DOWN ──start()──▶ DISCONNECTED ──connect──▶ CONNECTING ──associated+IP──▶ CONNECTED
```

**AP mode:**
```
DOWN ──start()──▶ DISCONNECTED ──ap_enable──▶ CONNECTED (AP up, no WAN)
```

**STA+AP mode:**
```
DOWN ──start()──▶ DISCONNECTED ──ap_enable + sta_connect──▶ CONNECTED (both up)
```

WiFi events:
- `EV_WIFI_STA_CONNECTED` / `EV_WIFI_STA_DISCONNECTED`
- `EV_WIFI_AP_ENABLED` / `EV_WIFI_AP_DISABLED`
- `EV_WIFI_SCAN_DONE`

### 7.3 Queues and Input Events

| Event | Origin | Description |
|-------|--------|-------------|
| `EV_ETH_LINK_UP/DOWN` | Ethernet adapter (polling or callback) | PHY link change |
| `EV_ETH_DHCP_OK/LOST` | Ethernet adapter | DHCPv4 lease obtained or lost |
| `EV_WIFI_STA_CONNECTED/DISCONNECTED` | ESP-Hosted driver via `net_mgmt` | STA association |
| `EV_WIFI_AP_ENABLED/DISABLED` | ESP-Hosted driver via `net_mgmt` | Soft-AP enabled/disabled |
| `EV_WIFI_SCAN_DONE` | ESP-Hosted driver via `net_mgmt` | WiFi scan completed |
| `EV_CONFIG_CHANGED` | Zbus `chan_system_config` | Ethernet or WiFi configuration changed |

### 7.4 Stacks (MPU Invariant)

The stack of the `ao_network` thread is defined at **file scope** with `K_THREAD_STACK_DEFINE`, never inside `app_container_t`, to preserve MPU alignment on Cortex-M7 with `CONFIG_HW_STACK_PROTECTION`.

---

## 8. Zbus Level — Status Channels per Interface

Files: `app/src/application/zbus/network_channels.{c,h}` (to be created)

Each interface publishes its own status on its own channel. Future services subscribe to the channel of the interface they are interested in.

```c
typedef struct {
    network_interface_t interface;  /* ETH or WIFI */
    net_status_t        status;
    uint32_t            timestamp_ms;
} net_state_msg_t;

ZBUS_CHAN_DECLARE(chan_network_eth_state);
ZBUS_CHAN_DECLARE(chan_network_wifi_state);
```

Expected subscribers (future):
- **SNMP**: subscribes to `chan_network_eth_state`; when `CONNECTED`, opens UDP socket 161 on Ethernet IP.
- **Modbus TCP**: subscribes to `chan_network_wifi_state`; when `CONNECTED`, listens on port 502 of WiFi.
- **MQTT**: subscribes to both; publishes through whichever is `CONNECTED`.

---

## 9. Storage Level — Separate Persistent Configuration

### 9.1 Domains and EEPROM Regions

Already defined in `doc/architecture/storage_subsystem.md`. Extended with two network domains:

| Domain | EEPROM Offset | Size | Structure |
|--------|---------------|------|-----------|
| `CONFIG_DOMAIN_NET_ETH` | `0x0C00` | 1 KB (2 slots × 512 B) | `net_eth_config_t` |
| `CONFIG_DOMAIN_NET_WIFI` | `0x1000` | 1 KB (2 slots × 512 B) | `net_wifi_config_t` |

### 9.2 Persistence Flow

```
Startup:
  composition_root_init()
    → i_config_storage_load(CONFIG_DOMAIN_NET_ETH, &net_eth_cfg_ram)
    → i_config_storage_load(CONFIG_DOMAIN_NET_WIFI, &net_wifi_cfg_ram)
    → If failure (virgin/CRC): apply defaults (ETH=DHCP, WiFi=AP)
    → ao_network starts both interfaces with their config

Runtime (configuration change):
  1. User/CLI calls i_network_set_config(&net_eth, ETH, &cfg, sizeof)
  2. ao_network mutates net_eth_cfg_ram (internal mutex)
  3. Publishes Zbus chan_system_config {CONFIG_DOMAIN_NET_ETH, DIRTY}
  4. ao_network applies hot to ETH (net_if_down/up, DHCP, static IP)
  5. ao_storage receives event, marks dirty_mask, restarts debounce (500 ms)
  6. When debounce expires + dirty-check: i_config_storage_save(CONFIG_DOMAIN_NET_ETH) → EEPROM 0x0C00
  7. Publishes CONFIG_EVENT_PERSISTED

Changing WiFi does NOT affect Ethernet, and vice versa.
```

**Storage invariants:**
- Zero `malloc`/`k_malloc` in the execution loop.
- Flat structs, no pointers, serializable by block for `microconf`.
- Dirty-check avoids unnecessary EEPROM rewrites (wear leveling).

---

## 10. Composition Root Level

File: `app/src/infrastructure/di/composition_root.c` (modify)

Only place where `DEVICE_DT_GET()` is invoked for network.

```c
static struct adapter_eth_stm32_ctx s_eth_ctx;
static struct adapter_wifi_esp_hosted_ctx s_wifi_ctx;

static int init_network(app_container_t *self)
{
    const struct device *eth_dev = DEVICE_DT_GET(DT_NODELABEL(eth0));
    const struct device *wifi_dev = DEVICE_DT_GET(DT_NODELABEL(esp_hosted_mcu_wifi));

    if (!device_is_ready(eth_dev)) return -ENODEV;
    if (!device_is_ready(wifi_dev)) return -ENODEV;

    int err = adapter_eth_stm32_init(&s_eth_ctx, eth_dev);
    if (err != 0) return err;

    err = adapter_wifi_esp_hosted_init(&s_wifi_ctx, wifi_dev);
    if (err != 0) return err;

    adapter_eth_stm32_port(&s_eth_ctx, &self->net_eth);
    adapter_wifi_esp_hosted_port(&s_wifi_ctx, &self->net_wifi);

    /* Load persistent configuration of each domain */
    i_config_storage_load(CONFIG_DOMAIN_NET_ETH, ...);
    i_config_storage_load(CONFIG_DOMAIN_NET_WIFI, ...);

    return 0;
}
```

> The labels `eth0` and `esp_hosted_mcu_wifi` exist in the DTS, but overlay details in `app/boards/brightc-controllerv3.overlay` still need to be configured (see section 13).

---

## 11. Complete Startup Sequence

```
power-on
  │
  ▼
Zephyr kernel init (ETH MAC + WiFi driver initialized from DTS)
  │
  ▼
main()
  │
  ▼
composition_root_init(&g_app)
  ├─ init_transport()        (Modbus, existing)
  ├─ init_registry()         (Modbus, existing)
  ├─ adapter_eth_stm32_init()  ← NEW: DEVICE_DT_GET(eth), PHY reset, read MAC
  ├─ adapter_wifi_esp_hosted_init() ← NEW: DEVICE_DT_GET(wifi), read STA MAC
  ├─ i_config_storage_load(CONFIG_DOMAIN_NET_ETH)  ← NEW: EEPROM 0x0C00
  │     ├─ success → inject into net_eth_cfg_ram
  │     └─ failure → defaults: ETH=DHCP, mark dirty
  ├─ i_config_storage_load(CONFIG_DOMAIN_NET_WIFI) ← NEW: EEPROM 0x1000
  │     ├─ success → inject into net_wifi_cfg_ram
  │     └─ failure → defaults: WiFi=AP (default SSID), mark dirty
  ├─ ao_network_init()       ← NEW: create AO thread, apply config at startup
  │     ├─ Ethernet: net_if_up + DHCP (default)
  │     └─ WiFi: AP mode, default SSID, default channel
  └─ init_active_objects()   (Modbus, existing)
  │
  ▼
composition_root_start(&g_app)
  └─ ao_network_start()      ← NEW: launch network thread
  │
  ▼
main loop (sleep)
```

---

## 12. Runtime Change — Examples

### 12.1 Change static IP of Ethernet (DHCP → static or vice versa)

```
CLI/SHELL: i_network_set_config(&g_app.net_eth, ETHERNET, &nueva_cfg_eth, sizeof)
  │
  ▼
ao_network:
  ├─ Mutate net_eth_cfg_ram (internal mutex)
  ├─ Publish Zbus chan_system_config {CONFIG_DOMAIN_NET_ETH, DIRTY}
  ├─ Apply hot to ETH:
  │   net_if_down(eth_iface)
  │   net_if_ipv4_addr_rm(ip_old) / net_if_ipv4_addr_add(ip_new)
  │   net_if_ipv4_set_netmask(...) / net_if_ipv4_set_gw(...)
  │   net_dhcpv4_start() or net_dhcpv4_stop()
  │   net_if_up(eth_iface)
  │
  ▼
ao_storage: debounce 500 ms → i_config_storage_save(CONFIG_DOMAIN_NET_ETH) → EEPROM 0x0C00
  │
  ▼
Zbus: chan_network_eth_state publishes updated state
  │
  ▼
Services (SNMP, etc.) react to new Ethernet state/IP
```

### 12.2 Change WiFi configuration (mode AP → STA, or change SSID/PSK)

```
CLI/SHELL: i_network_set_config(&g_app.net_wifi, WIFI, &nueva_cfg_wifi, sizeof)
  │
  ▼
ao_network:
  ├─ Mutate net_wifi_cfg_ram (internal mutex)
  ├─ Publish Zbus chan_system_config {CONFIG_DOMAIN_NET_WIFI, DIRTY}
  ├─ Apply hot to WiFi:
  │   If STA active: wifi_mgmt disconnect
  │   If AP mode: wifi_mgmt AP disable
  │   Apply new mode (STA / AP / STA_AP) with new parameters
  │
  ▼
ao_storage: debounce 500 ms → i_config_storage_save(CONFIG_DOMAIN_NET_WIFI) → EEPROM 0x1000
  │
  ▼
Zbus: chan_network_wifi_state publishes updated state
```

> Changing WiFi **does not affect** Ethernet. Both interfaces continue to operate independently.

---

## 13. Devicetree and Overlay Requirements

The base DTS (`boards/daecge/brightc-controllerv3/brightc-controllerv3.dts`) already defines:

- `ethernet@40028000` with PHY `ethernet-phy@0`, `phy-connection-type = "rmii"`.
- `esp_hosted_mcu@0` on SPI4 with subnodes `wifi` and `bt-hci`.
- `zephyr,wifi = &esp_hosted_mcu_wifi;` in the `chosen` node.
- GPIO `eth_phy_rst` (`&gpiog 4 GPIO_ACTIVE_LOW`).

**Still to configure in `app/boards/brightc-controllerv3.overlay`:**

- Network aliases for easy access from `composition_root.c` (e.g., `eth0 = &ethernet0`, `wifi0 = &esp_hosted_mcu_wifi` if nodes have appropriate labels).
- Possible pinctrl or node state adjustments if required.

No modification of the base DTS is required to add networking functionality; only ensure labels exist for `DEVICE_DT_GET()`.

---

## 14. Kconfig Requirements (`app/prj.conf`)

```conf
# --- Ethernet Network ---
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_IPV4=y
CONFIG_NET_DHCPV4=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_MGMT=y
CONFIG_NET_MGMT_EVENT=y
CONFIG_POSIX_API=y

# --- WiFi Network (ESP-Hosted-MCU) ---
CONFIG_WIFI_ESP_HOSTED_MCU=y
CONFIG_WIFI_ESP_HOSTED_MCU_AP_STA_MODE=y
CONFIG_WIFI_STA_AUTO_DHCPV4=y
CONFIG_WIFI_NM=y

# --- Network Stacks ---
CONFIG_NET_RX_STACK_SIZE=4096
CONFIG_NET_TX_STACK_SIZE=4096
CONFIG_NET_MGMT_EVENT_STACK_SIZE=2048

# --- Sockets (reserved for future services) ---
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_MAX_CONN=32
CONFIG_NET_MAX_CONTEXTS=32
CONFIG_NET_PKT_RX_COUNT=16
CONFIG_NET_PKT_TX_COUNT=16

# --- Zbus and Storage ---
CONFIG_ZBUS=y
CONFIG_EEPROM=y
CONFIG_EEPROM_AT24=y
```

---

## 15. Naming Conventions

Following the pattern established by Modbus (`adapter_modbus_nmbs`, `ao_modbus`, `i_modbus_client`):

| Component | Convention | Example |
|------------|------------|---------|
| Port | `i_<domain>.h`, `i_<domain>_api_t`, `i_<domain>_t` | `i_network.h`, `i_network_api_t`, `i_network_t` |
| Ethernet Adapter | `adapter_eth_stm32.c/.h` | `adapter_eth_stm32_init()`, `adapter_eth_stm32_port()` |
| WiFi Adapter | `adapter_wifi_esp_hosted.c/.h` | `adapter_wifi_esp_hosted_init()`, `adapter_wifi_esp_hosted_port()` |
| Active Object | `ao_<subsystem>.c/.h` | `ao_network.c`, `ao_network_init()`, `ao_network_start()` |
| Zbus Channels | `chan_<name>_state` | `chan_network_eth_state`, `chan_network_wifi_state` |
| Domain Types | `common/<name>_types.h` | `common/network_types.h` |
| Storage Domains | `CONFIG_DOMAIN_NET_*` | `CONFIG_DOMAIN_NET_ETH`, `CONFIG_DOMAIN_NET_WIFI` |

---

## 16. Gaps to Implement

| # | Element | Destination File | Status |
|---|---------|-----------------|--------|
| 1 | `network_types.h` (ETH, WiFi types, states) | `app/include/common/network_types.h` | Missing |
| 2 | `config_types.h` with `CONFIG_DOMAIN_NET_ETH`, `CONFIG_DOMAIN_NET_WIFI` | `app/include/common/config_types.h` | Missing |
| 3 | `i_network.h` (common multi-interface port) | `app/include/interfaces/i_network.h` | Missing |
| 4 | `adapter_eth_stm32.c/.h` | `app/src/infrastructure/adapters/` | Missing |
| 5 | `adapter_wifi_esp_hosted.c/.h` | `app/src/infrastructure/adapters/` | Missing |
| 6 | `ao_network.c/.h` (one AO, state machines per interface) | `app/src/application/active_objects/` | Missing |
| 7 | `network_channels.{c,h}` (Zbus separate per interface) | `app/src/application/zbus/` | Missing |
| 8 | `i_config_storage.h` + `adapter_storage_microconf.c` | `app/src/infrastructure/adapters/` | Missing |
| 9 | `ao_storage.c` | `app/src/application/active_objects/` | Missing |
| 10 | Network Kconfig in `app/prj.conf` | `app/prj.conf` | Partial (missing symbols) |
| 11 | Network overlay in `app/boards/brightc-controllerv3.overlay` | `app/boards/` | Missing (labels and details) |
| 12 | Integration in `composition_root.c` | `app/src/infrastructure/di/` | To modify |
| 13 | Unit tests (network fakes) | `tests/unit/` | Missing |

---

## 17. Summary of Answers (Why? What for? How?)

| Question | Answer |
|----------|--------|
| **Why two interfaces?** | The BrightC needs wired connectivity (Ethernet, stable and high speed) and wireless connectivity (WiFi, for flexible deployment and initial setup via AP). |
| **What will they be used for?** | Ethernet: main network services (SNMP, etc.). WiFi: initial setup (AP mode by default), wireless connectivity, and future services (Modbus TCP, MQTT). |
| **How are they managed?** | Through a single Active Object (`ao_network`) orchestrating both adapters via a common port (`i_network.h`), with independent state machines per interface, separate persistent configuration in EEPROM (domains `CONFIG_DOMAIN_NET_ETH` and `CONFIG_DOMAIN_NET_WIFI`), and state exposure via individual Zbus channels (`chan_network_eth_state`, `chan_network_wifi_state`). |
| **Is there failover?** | No. Both interfaces can be UP simultaneously. Each future service chooses which interface to bind its sockets to. |
| **How is configuration changed at runtime?** | RAM mutation → Zbus event `chan_system_config` → `ao_network` applies hot to the affected interface → `ao_storage` saves to EEPROM with debounce and dirty-check. |
| **What is left for later?** | MQTT, HTTP, SNMP, Modbus TCP services. They will be implemented as upper layers consuming `i_network_t` and listening for state via Zbus. |

---

**Document status**: Technical specification ready for implementation.  
**Code status**: No file has been modified yet. Everything is to be built on the defined foundations (DTS, clean architecture, documented storage pattern).
