# Arquitectura del Subsistema de Red (Network Subsystem)

- **Estado**: Especificación Técnica de Arquitectura
- **Plataforma**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Entorno de Ejecución**: Zephyr RTOS (Topología West T2)
- **Autor**: Equipo de Firmware BrightC
- **Idioma del documento**: Español (documentación de arquitectura); código en inglés

---

## 1. Visión General

### 1.1 ¿Para qué existe este subsistema?

El BrightC Controller V3 dispone de **dos interfaces de red físicas**:

| Interfaz | Medio | Rol |
|----------|-------|-----|
| **Ethernet** | RMII (STM32H7 MAC + PHY LAN8742) | Interfaz de red cableada principal |
| **WiFi** | ESP32 como coprociódor vía ESP-Hosted-MCU sobre SPI4 | Interfaz de red inalámbrica secundaria |

El subsistema de red tiene como propósito **gestionar ambas interfaces de forma independiente y simultánea** (multi-homing), permitiendo que:

1. **Ambas interfaces estén UP al mismo tiempo** (no es failover de una a otra).
2. **Cada interfaz tenga su propia configuración** (IP, credenciales, modo de operación).
3. **Cada interfaz tenga su propia máquina de estados** (conectado, desconectado, etc.).
4. **La configuración se pueda cambiar en tiempo de ejecución** sin reiniciar el firmware.
5. **La configuración persista en EEPROM** a través del subsistema de storage (`microconf` + `ao_storage`).

### 1.2 ¿Qué NO hace este subsistema?

- **No implementa failover automático** entre Ethernet y WiFi. Si Ethernet cae, WiFi no toma automáticamente el tráfico salvo que un servicio superior decida cambiar de interfaz.
- **No implementa servicios de alto nivel** (MQTT, HTTP, SNMP, Modbus TCP). Esos servicios se diseñarán por separado y consumirán el estado de red expuesto por este subsistema.
- **No enruta tráfico** entre interfaces. Cada servicio elige a qué interfaz vincular sus sockets.

---

## 2. Decisiones de Diseño (Cerradas)

Las siguientes decisiones fueron acordadas y definen la arquitectura:

| # | Decisión | Justificación |
|---|----------|---------------|
| D1 | **Ambas interfaces activas simultáneamente** | El dispositivo debe poder atender tráfico por cable y por WiFi al mismo tiempo. No hay interfaz "de respaldo". |
| D2 | **Una sola Active Object `ao_network`** | Orquesta ambas interfaces. Evita dos hilos, dos `k_msgq`, y condiciones de carrera sobre `net_if` de Zephyr. Respeta la regla MPU de un solo stack a file scope. |
| D3 | **Configuración separada por interfaz** | Ethernet y WiFi tienen parámetros distintos. Se almacenan en dominios de storage independientes (`CONFIG_DOMAIN_NET_ETH` y `CONFIG_DOMAIN_NET_WIFI`). |
| D4 | **WiFi por defecto en modo AP** | Permite configurar credenciales de red la primera vez sin necesidad de Ethernet. |
| D5 | **Ethernet por defecto en DHCP** | Configuración de fábrica lista para usar en red cableada con servidor DHCP. Cambiable a IP estática en runtime. |
| D6 | **Servicios de alto nivel fuera de alcance** | MQTT, HTTP, SNMP, etc. se implementan como servicios separados que consumen `i_network_t` y escuchan estado vía Zbus. |

---

## 3. Arquitectura por Capas (Clean Architecture)

```
┌─────────────────────────────────────────────────────────────────────┐
│  SERVICIOS DE ALTO NIVEL (FUTURO: MQTT, HTTP, SNMP, Modbus TCP)   │
│  - Consultan estado de red vía Zbus                                 │
│  - Vinculan sus sockets a una interfaz específica                   │
└─────────────────────────────────────────────────────────────────────┘
                              │ Zbus: chan_network_eth_state
                              │ Zbus: chan_network_wifi_state
┌─────────────────────────────────────────────────────────────────────┐
│  CAPA DE APLICACIÓN — ao_network (Active Object único)             │
│  - Orquesta Ethernet y WiFi de forma independiente                 │
│  - Aplica configuración en arranque y en tiempo de ejecución       │
│  - Publica estado de cada interfaz en sus canales Zbus              │
└─────────────────────────────────────────────────────────────────────┘
                              │ i_network_t (puerto común)
┌─────────────────────────────────────────────────────────────────────┐
│  PUERTO COMÚN — i_network.h                                         │
│  - V-table: init, start, stop, get_config, set_config,              │
│    get_status, scan                                                 │
│  - Handle: { api, impl }                                            │
│  - Dos instancias: net_eth, net_wifi                                 │
└─────────────────────────────────────────────────────────────────────┘
                              │ implementa
┌─────────────────────────────────────────────────────────────────────┐
│  CAPA DE INFRAESTRUCTURA — Adaptadores                             │
│  - adapter_eth_stm32.c     → ETH_STM32_HAL, net_if, net_dhcpv4      │
│  - adapter_wifi_esp_hosted.c → wifi_mgmt, driver ESP-Hosted-MCU     │
└─────────────────────────────────────────────────────────────────────┘
                              │ DEVICE_DT_GET()
┌─────────────────────────────────────────────────────────────────────┐
│  HARDWARE / DEVICETREE                                              │
│  - ethernet@40028000 (RMII, LAN8742, PHY reset PG4)                │
│  - esp_hosted_mcu@0 en SPI4 → wifi + bt-hci                         │
└─────────────────────────────────────────────────────────────────────┘
                              │ I2C2 + EEPROM AT24C256C
┌─────────────────────────────────────────────────────────────────────┐
│  STORAGE — microconf + ao_storage                                  │
│  - CONFIG_DOMAIN_NET_ETH (0x0C00, 1 KB)                            │
│  - CONFIG_DOMAIN_NET_WIFI (0x1000, 1 KB)                           │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.1 Regla de Dependencia Unidireccional

- **Dominio** (`app/include/common/network_types.h`): C99 puro. Solo `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<errno.h>`. **Prohibido** `<zephyr/...>`.
- **Puertos** (`app/include/interfaces/i_network.h`): Contratos abstractos en C. Ninguna dependencia de Zephyr.
- **Adaptadores** (`app/src/infrastructure/adapters/`): Implementan los puertos usando APIs de Zephyr. Contienen `#include <zephyr/...>`.
- **Composition Root** (`composition_root.c`: Único archivo permitido para invocar `DEVICE_DT_GET()`.

---

## 4. Nivel de Dominio — Tipos de Datos

Archivo: `app/include/common/network_types.h` (por crear)

### 4.1 Identificadores y estados

```c
typedef enum {
    NETWORK_INTERFACE_ETHERNET = 0,
    NETWORK_INTERFACE_WIFI,
    NETWORK_INTERFACE_MAX
} network_interface_t;

typedef enum {
    NET_IF_STATE_DOWN = 0,         /* Interfaz desactivada administrativamente */
    NET_IF_STATE_DISCONNECTED,     /* Sin enlace / sin asociar */
    NET_IF_STATE_CONNECTING,       /* En proceso (DHCP, asociación WiFi) */
    NET_IF_STATE_CONNECTED,        /* Enlace UP y IP válida */
    NET_IF_STATE_FAILED            /* Error permanente */
} net_if_state_t;

typedef enum {
    NET_IP_MODE_DHCP = 0,
    NET_IP_MODE_STATIC
} net_ip_mode_t;

typedef enum {
    NET_WIFI_MODE_STA = 0,         /* Solo estación (cliente) */
    NET_WIFI_MODE_AP = 1,          /* Solo punto de acceso */
    NET_WIFI_MODE_STA_AP = 2       /* Estación + AP simultáneo */
} net_wifi_mode_t;

typedef enum {
    NET_WIFI_SEC_OPEN = 0,
    NET_WIFI_SEC_WPA_PSK = 2,
    NET_WIFI_SEC_WPA2_PSK = 3,
    NET_WIFI_SEC_WPA3_SAE = 6
} net_wifi_security_t;
```

### 4.2 Configuración Ethernet (plana, serializable)

```c
typedef struct {
    net_ip_mode_t ip_mode;         /* DHCP o estática */
    uint8_t  mac_addr[6];          /* Solo lectura, leída del HW en init */
    uint32_t ip_addr;              /* Host order */
    uint32_t subnet_mask;
    uint32_t gateway;
    bool     auto_dhcpv4;          /* Iniciar DHCP automático al link up */
} net_eth_config_t;
```

### 4.3 Configuración WiFi (campos separados para STA y AP)

El struct incluye ambos conjuntos de parámetros. El adaptador ignora los que no apliquen al modo activo:

```c
/* Parámetros solo usados cuando el modo incluye STA */
typedef struct {
    char                 ssid[33];             /* SSID de la red a unirse */
    char                 psk[65];              /* Contraseña (vacía si OPEN) */
    net_wifi_security_t  security;
    net_ip_mode_t        ip_mode;              /* DHCP o estática para IP de STA */
    uint32_t             ip_addr;
    uint32_t             subnet_mask;
    uint32_t             gateway;
} net_wifi_sta_params_t;

/* Parámetros solo usados cuando el modo incluye AP */
typedef struct {
    char                 ssid[33];             /* SSID del soft-AP */
    char                 psk[65];              /* Contraseña del AP (vacía si OPEN) */
    net_wifi_security_t  security;
    uint8_t              channel;              /* 1-13, default 9 */
    uint8_t              max_clients;          /* default 5 */
    uint32_t             ap_ip_addr;           /* IP gateway del AP (ej. 192.168.4.1) */
    uint32_t             ap_subnet_mask;
} net_wifi_ap_params_t;

typedef struct {
    net_wifi_mode_t        mode;               /* STA / AP / STA_AP */
    net_wifi_sta_params_t  sta;                /* Usado si mode == STA o STA_AP */
    net_wifi_ap_params_t   ap;                 /* Usado si mode == AP o STA_AP */
    uint8_t                mac_addr[6];        /* Solo lectura, leída del HW */
} net_wifi_config_t;
```

### 4.4 Estado en tiempo de ejecución (no se persiste)

```c
typedef struct {
    net_if_state_t state;
    uint32_t       ip_addr;
    uint32_t       subnet_mask;
    uint32_t       gateway;
    int8_t         rssi;             /* Solo WiFi; -128 si N/A */
    uint32_t       link_up_ms;       /* Tiempo con enlace UP */
} net_status_t;
```

### 4.5 Resultado de operaciones

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

## 5. Nivel de Puerto — `i_network.h`

Archivo: `app/include/interfaces/i_network.h` (por crear)

El puerto es **común para ambas interfaces**. Cada adaptador valida internamente el tipo de configuración que recibe.

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
    int (*scan)(void *impl, network_interface_t iface);   /* WiFi solo; ETH devuelve -ENOSYS */
} i_network_api_t;

typedef struct {
    const i_network_api_t *api;
    void *impl;
} i_network_t;
```

En `app_container_t` existirán dos instancias independientes:

```c
i_network_t net_eth;   /* Puerto Ethernet */
i_network_t net_wifi;  /* Puerto WiFi */
```

Ambos pueden estar bound y activos al mismo tiempo.

**Invariante:** `handle->api->...` solo se invoca dentro del adaptador y en `composition_root.c`. La aplicación nunca accede a la v-table directamente.

---

## 6. Nivel de Adaptadores (Infraestructura)

### 6.1 `adapter_eth_stm32`

Archivos: `app/src/infrastructure/adapters/adapter_eth_stm32.c`, `adapter_eth_stm32.h`

| Operación | Implementación Zephyr |
|-----------|----------------------|
| `init` | `DEVICE_DT_GET(eth_node)`, `device_is_ready()`, reset PHY vía GPIO `eth_phy_rst`, leer MAC del dispositivo |
| `start` | `net_if_up(iface)`. Si `ip_mode == DHCP` → `net_dhcpv4_start(iface)`. Si estático → `net_if_ipv4_addr_add()`, `net_if_ipv4_set_netmask()`, `net_if_ipv4_set_gw()` |
| `stop` | `net_dhcpv4_stop(iface)`, `net_if_down(iface)` |
| `get_config` | Leer `net_eth_config_t` guardado en RAM del adaptador |
| `set_config` | Validar que `size == sizeof(net_eth_config_t)`, aplicar cambios (reiniciar DHCP o reasignar IP estática) |
| `get_status` | Leer `net_if`, `net_if_carrier_ok()`, IPv4 asignada |
| `scan` | `-ENOSYS` (no aplica a Ethernet) |

Contexto del adaptador (`impl`):

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

Archivos: `app/src/infrastructure/adapters/adapter_wifi_esp_hosted.c`, `adapter_wifi_esp_hosted.h`

| Operación | Implementación Zephyr |
|-----------|----------------------|
| `init` | `DEVICE_DT_GET(wifi_node)`, `device_is_ready()`, leer MAC STA |
| `start` | Según `mode`: STA → `wifi_mgmt` connect con SSID/PSK; AP → `wifi_mgmt` AP enable; STA+AP → ambos. Si DHCP → `net_dhcpv4_start()` |
| `stop` | `wifi_mgmt` disconnect / AP disable, `net_dhcpv4_stop()`, `net_if_down()` |
| `get_config` | Leer `net_wifi_config_t` guardado en RAM |
| `set_config` | Validar `size == sizeof(net_wifi_config_t)`, aplicar credenciales/modo |
| `get_status` | Leer `wifi_iface_state`, RSSI, IP asignada |
| `scan` | `wifi_mgmt` scan request |

Contexto del adaptador:

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

**Convención de nomenclatura:** las funciones públicas siguen el patrón del proyecto (visto en `adapter_modbus_nmbs`):

- `adapter_eth_stm32_init()`
- `adapter_eth_stm32_port()`
- `adapter_wifi_esp_hosted_init()`
- `adapter_wifi_esp_hosted_port()`

---

## 7. Nivel de Aplicación — `ao_network` (Active Object único)

Archivos: `app/src/application/active_objects/ao_network.c`, `ao_network.h`

### 7.1 Responsabilidades

1. **Arranque:** inicializa ambos adaptadores y aplica la configuración cargada de storage.
2. **Mantenimiento:** monitoriza el estado de cada interfaz de forma independiente.
3. **Reconfiguración en caliente:** al recibir evento `CONFIG_DOMAIN_NET_ETH` o `CONFIG_DOMAIN_NET_WIFI` en `chan_system_config`, aplica cambios a la interfaz correspondiente **sin detener la otra**.
4. **Publicación de estado:** publica `net_status_t` en canales Zbus separados:
   - `chan_network_eth_state`
   - `chan_network_wifi_state`

### 7.2 Máquinas de Estados por Interfaz

Cada interfaz tiene su propia máquina de estados. No hay máquina global.

#### 7.2.1 Estados de Ethernet (independiente)

```
DOWN ──start()──▶ DISCONNECTED ──link up──▶ CONNECTING ──DHCP OK / IP estática──▶ CONNECTED
                                                       │
                                                     DHCP falló / error
                                                       ▼
                                                     FAILED
```

Eventos Ethernet:
- `EV_ETH_LINK_UP` / `EV_ETH_LINK_DOWN` (PHY)
- `EV_ETH_DHCP_LEASE_OBTAINED` / `EV_ETH_DHCP_LOST`
- `EV_ETH_IP_STATIC_APPLIED`

#### 7.2.2 Estados de WiFi (independiente, depende del modo)

**Modo STA:**
```
DOWN ──start()──▶ DISCONNECTED ──connect──▶ CONNECTING ──asociado+IP──▶ CONNECTED
```

**Modo AP:**
```
DOWN ──start()──▶ DISCONNECTED ──ap_enable──▶ CONNECTED (AP arriba, sin WAN)
```

**Modo STA+AP:**
```
DOWN ──start()──▶ DISCONNECTED ──ap_enable + sta_connect──▶ CONNECTED (ambos arriba)
```

Eventos WiFi:
- `EV_WIFI_STA_CONNECTED` / `EV_WIFI_STA_DISCONNECTED`
- `EV_WIFI_AP_ENABLED` / `EV_WIFI_AP_DISABLED`
- `EV_WIFI_SCAN_DONE`

### 7.3 Colas y Eventos de Entrada

| Evento | Origen | Descripción |
|--------|--------|-------------|
| `EV_ETH_LINK_UP/DOWN` | Adaptador Ethernet (polling o callback) | Cambio de enlace PHY |
| `EV_ETH_DHCP_OK/LOST` | Adaptador Ethernet | DHCPv4 obtuvo o perdió lease |
| `EV_WIFI_STA_CONNECTED/DISCONNECTED` | Driver ESP-Hosted vía `net_mgmt` | Asociación STA |
| `EV_WIFI_AP_ENABLED/DISABLED` | Driver ESP-Hosted vía `net_mgmt` | Soft-AP activado/desactivado |
| `EV_CONFIG_CHANGED` | Zbus `chan_system_config` | Cambio de config Ethernet o WiFi |

### 7.4 Stacks (Invariante MPU)

La stack del hilo `ao_network` se define a **file scope** con `K_THREAD_STACK_DEFINE`, nunca dentro de `app_container_t`, para preservar el alineamiento MPU en Cortex-M7 con `CONFIG_HW_STACK_PROTECTION`.

---

## 8. Nivel de Zbus — Canales de Estado por Interfaz

Archivos: `app/src/application/zbus/network_channels.{c,h}` (por crear)

Cada interfaz publica su propio estado en su propio canal. Los servicios futuros se suscriben al canal de la interfaz que les interesa.

```c
typedef struct {
    network_interface_t interface;  /* ETH o WIFI */
    net_status_t        status;
    uint32_t            timestamp_ms;
} net_state_msg_t;

ZBUS_CHAN_DECLARE(chan_network_eth_state);
ZBUS_CHAN_DECLARE(chan_network_wifi_state);
```

Suscriptores esperados (futuro):
- **SNMP**: se suscribe a `chan_network_eth_state`; cuando `CONNECTED`, abre socket UDP 161 en la IP de Ethernet.
- **Modbus TCP**: se suscribe a `chan_network_wifi_state`; cuando `CONNECTED`, escucha en puerto 502 de WiFi.
- **MQTT**: se suscribe a ambos; publica por cualquiera que esté `CONNECTED`.

---

## 9. Nivel de Storage — Configuración Persistente Separada

### 9.1 Dominios y regiones EEPROM

Ya definido en `doc/architecture/storage_subsystem.md`. Se extiende con dos dominios de red:

| Dominio | Offset EEPROM | Tamaño | Estructura |
|---------|---------------|--------|------------|
| `CONFIG_DOMAIN_NET_ETH` | `0x0C00` | 1 KB (2 slots × 512 B) | `net_eth_config_t` |
| `CONFIG_DOMAIN_NET_WIFI` | `0x1000` | 1 KB (2 slots × 512 B) | `net_wifi_config_t` |

### 9.2 Flujo de persistencia

```
Arranque:
  composition_root_init()
    → i_config_storage_load(CONFIG_DOMAIN_NET_ETH, &net_eth_cfg_ram)
    → i_config_storage_load(CONFIG_DOMAIN_NET_WIFI, &net_wifi_cfg_ram)
    → Si fallo (virgen/CRC): aplicar defaults (ETH=DHCP, WiFi=AP)
    → ao_network inicia ambas interfaces con su config

Runtime (cambio de configuración):
  1. Usuario/CLI llama i_network_set_config(&net_eth, ETH, &cfg, sizeof)
  2. ao_network muta net_eth_cfg_ram (mutex interno)
  3. Publica Zbus chan_system_config {CONFIG_DOMAIN_NET_ETH, DIRTY}
  4. ao_network aplica en caliente a ETH (net_if_down/up, DHCP, IP estática)
  5. ao_storage recibe evento, marca dirty_mask, reinicia debounce (500 ms)
  6. Al vencer debounce + dirty-check: i_config_storage_save(CONFIG_DOMAIN_NET_ETH) → EEPROM 0x0C00
  7. Publica CONFIG_EVENT_PERSISTED

Cambiar WiFi NO afecta a Ethernet, y viceversa.
```

**Invariantes de storage:**
- Zero `malloc`/`k_malloc` en el bucle de ejecución.
- Structs planos, sin punteros, serializables por bloque para `microconf`.
- Dirty-check evita reescrituras innecesarias de EEPROM (wear-leveling).

---

## 10. Nivel de Composition Root

Archivo: `app/src/infrastructure/di/composition_root.c` (modificar)

Único lugar donde se invoca `DEVICE_DT_GET()` para red.

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

    /* Cargar configuración persistente de cada dominio */
    i_config_storage_load(CONFIG_DOMAIN_NET_ETH, ...);
    i_config_storage_load(CONFIG_DOMAIN_NET_WIFI, ...);

    return 0;
}
```

> Los labels `eth0` y `esp_hosted_mcu_wifi` existen en el DTS, pero faltará configurar detalles en el `app/boards/brightc-controllerv3.overlay` (ver sección 13).

---

## 11. Secuencia de Arranque Completa

```
power-on
  │
  ▼
Zephyr kernel init (ETH MAC + WiFi driver se inicializan desde DTS)
  │
  ▼
main()
  │
  ▼
composition_root_init(&g_app)
  ├─ init_transport()        (Modbus, existente)
  ├─ init_registry()         (Modbus, existente)
  ├─ adapter_eth_stm32_init()  ← NUEVO: DEVICE_DT_GET(eth), reset PHY, leer MAC
  ├─ adapter_wifi_esp_hosted_init() ← NUEVO: DEVICE_DT_GET(wifi), leer MAC STA
  ├─ i_config_storage_load(CONFIG_DOMAIN_NET_ETH)  ← NUEVO: EEPROM 0x0C00
  │     ├─ éxito → inyectar en net_eth_cfg_ram
  │     └─ fallo → defaults: ETH=DHCP, marcar sucio
  ├─ i_config_storage_load(CONFIG_DOMAIN_NET_WIFI) ← NUEVO: EEPROM 0x1000
  │     ├─ éxito → inyectar en net_wifi_cfg_ram
  │     └─ fallo → defaults: WiFi=AP (SSID por definir), marcar sucio
  ├─ ao_network_init()       ← NUEVO: crear hilo AO, aplicar config al arranque
  │     ├─ Ethernet: net_if_up + DHCP (por defecto)
  │     └─ WiFi: modo AP, SSID por defecto, canal por defecto
  └─ init_active_objects()   (Modbus, existente)
  │
  ▼
composition_root_start(&g_app)
  └─ ao_network_start()      ← NUEVO: lanzar hilo de red
  │
  ▼
bucle principal (sleep)
```

---

## 12. Cambio en Tiempo de Ejecución — Ejemplos

### 12.1 Cambiar IP estática de Ethernet (DHCP → estática o viceversa)

```
CLI/SHELL: i_network_set_config(&g_app.net_eth, ETHERNET, &nueva_cfg_eth, sizeof)
  │
  ▼
ao_network:
  ├─ Mutar net_eth_cfg_ram (mutex interno)
  ├─ Publicar Zbus chan_system_config {CONFIG_DOMAIN_NET_ETH, DIRTY}
  ├─ Aplicar en caliente a ETH:
  │   net_if_down(eth_iface)
  │   net_if_ipv4_addr_rm(ip_vieja) / net_if_ipv4_addr_add(ip_nueva)
  │   net_if_ipv4_set_netmask(...) / net_if_ipv4_set_gw(...)
  │   net_dhcpv4_start() o net_dhcpv4_stop()
  │   net_if_up(eth_iface)
  │
  ▼
ao_storage: debounce 500 ms → i_config_storage_save(CONFIG_DOMAIN_NET_ETH) → EEPROM 0x0C00
  │
  ▼
Zbus: chan_network_eth_state publica estado actualizado
  │
  ▼
Servicios (SNMP, etc.) reaccionan al nuevo estado/IP de Ethernet
```

### 12.2 Cambiar configuración WiFi (modo AP → STA, o cambiar SSID/PSK)

```
CLI/SHELL: i_network_set_config(&g_app.net_wifi, WIFI, &nueva_cfg_wifi, sizeof)
  │
  ▼
ao_network:
  ├─ Mutar net_wifi_cfg_ram (mutex interno)
  ├─ Publicar Zbus chan_system_config {CONFIG_DOMAIN_NET_WIFI, DIRTY}
  ├─ Aplicar en caliente a WiFi:
  │   Si STA activo: wifi_mgmt disconnect
  │   Si modo AP: wifi_mgmt AP disable
  │   Aplicar nuevo modo (STA / AP / STA_AP) con nuevos parámetros
  │
  ▼
ao_storage: debounce 500 ms → i_config_storage_save(CONFIG_DOMAIN_NET_WIFI) → EEPROM 0x1000
  │
  ▼
Zbus: chan_network_wifi_state publica estado actualizado
```

> Cambiar WiFi **no afecta** a Ethernet. Ambas interfaces siguen operando de forma independiente.

---

## 13. Requisitos de Devicetree y Overlay

El DTS base (`boards/daecge/brightc-controllerv3/brightc-controllerv3.dts`) ya define:

- `ethernet@40028000` con PHY `ethernet-phy@0`, `phy-connection-type = "rmii"`.
- `esp_hosted_mcu@0` en SPI4 con subnodos `wifi` y `bt-hci`.
- `zephyr,wifi = &esp_hosted_mcu_wifi;` en el nodo `chosen`.
- GPIO `eth_phy_rst` (`&gpiog 4 GPIO_ACTIVE_LOW`).

**Falta configurar en `app/boards/brightc-controllerv3.overlay`:**

- Alias de red para facilitar el acceso desde `composition_root.c` (ej. `eth0 = &ethernet0`, `wifi0 = &esp_hosted_mcu_wifi` si los nodos tienen labels adecuados).
- Posibles ajustes de pinctrl o estado de nodos si se requieren.

No se requiere modificar el DTS base para agregar funcionalidad de red; solo asegurar que los labels existan para `DEVICE_DT_GET()`.

---

## 14. Requisitos Kconfig (`app/prj.conf`)

```conf
# --- Red Ethernet ---
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_IPV4=y
CONFIG_NET_DHCPV4=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_MGMT=y
CONFIG_NET_MGMT_EVENT=y
CONFIG_POSIX_API=y

# --- Red WiFi (ESP-Hosted-MCU) ---
CONFIG_WIFI_ESP_HOSTED_MCU=y
CONFIG_WIFI_ESP_HOSTED_MCU_AP_STA_MODE=y
CONFIG_WIFI_STA_AUTO_DHCPV4=y
CONFIG_WIFI_NM=y

# --- Stacks de red ---
CONFIG_NET_RX_STACK_SIZE=4096
CONFIG_NET_TX_STACK_SIZE=4096
CONFIG_NET_MGMT_EVENT_STACK_SIZE=2048

# --- Sockets (reservados para servicios futuros) ---
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_MAX_CONN=32
CONFIG_NET_MAX_CONTEXTS=32
CONFIG_NET_PKT_RX_COUNT=16
CONFIG_NET_PKT_TX_COUNT=16

# --- Zbus y Storage ---
CONFIG_ZBUS=y
CONFIG_EEPROM=y
CONFIG_EEPROM_AT24=y
```

---

## 15. Convenciones de Nomenclatura

Seguindo el patrón establecido por Modbus (`adapter_modbus_nmbs`, `ao_modbus`, `i_modbus_client`):

| Componente | Convención | Ejemplo |
|------------|------------|---------|
| Puerto | `i_<dominio>.h`, `i_<dominio>_api_t`, `i_<dominio>_t` | `i_network.h`, `i_network_api_t`, `i_network_t` |
| Adaptador Ethernet | `adapter_eth_stm32.c/.h` | `adapter_eth_stm32_init()`, `adapter_eth_stm32_port()` |
| Adaptador WiFi | `adapter_wifi_esp_hosted.c/.h` | `adapter_wifi_esp_hosted_init()`, `adapter_wifi_esp_hosted_port()` |
| Active Object | `ao_<subsistema>.c/.h` | `ao_network.c`, `ao_network_init()`, `ao_network_start()` |
| Canales Zbus | `chan_<nombre>_state` | `chan_network_eth_state`, `chan_network_wifi_state` |
| Tipos de dominio | `common/<nombre>_types.h` | `common/network_types.h` |
| Dominios de storage | `CONFIG_DOMAIN_NET_*` | `CONFIG_DOMAIN_NET_ETH`, `CONFIG_DOMAIN_NET_WIFI` |

---

## 16. Brechas por Implementar

| # | Elemento | Archivo destino | Estado |
|---|----------|-----------------|--------|
| 1 | `network_types.h` (tipos ETH, WiFi, estados) | `app/include/common/network_types.h` | Faltante |
| 2 | `config_types.h` con `CONFIG_DOMAIN_NET_ETH`, `CONFIG_DOMAIN_NET_WIFI` | `app/include/common/config_types.h` | Faltante |
| 3 | `i_network.h` (puerto común multi-interfaz) | `app/include/interfaces/i_network.h` | Faltante |
| 4 | `adapter_eth_stm32.c/.h` | `app/src/infrastructure/adapters/` | Faltante |
| 5 | `adapter_wifi_esp_hosted.c/.h` | `app/src/infrastructure/adapters/` | Faltante |
| 6 | `ao_network.c/.h` (una AO, máquinas de estados por interfaz) | `app/src/application/active_objects/` | Faltante |
| 7 | `network_channels.{c,h}` (Zbus separado por interfaz) | `app/src/application/zbus/` | Faltante |
| 8 | `i_config_storage.h` + `adapter_storage_microconf.c` | `app/src/infrastructure/adapters/` | Faltante |
| 9 | `ao_storage.c` | `app/src/application/active_objects/` | Faltante |
| 10 | Kconfig de red en `app/prj.conf` | `app/prj.conf` | Parcial (falta añadir símbolos) |
| 11 | Overlay de red en `app/boards/brightc-controllerv3.overlay` | `app/boards/` | Faltante (labels y detalles) |
| 12 | Integración en `composition_root.c` | `app/src/infrastructure/di/` | Por modificar |
| 13 | Pruebas unitarias (fakes de red) | `tests/unit/` | Faltante |

---

## 17. Resumen de Respuestas (¿Por qué? ¿Para qué? ¿Cómo?)

| Pregunta | Respuesta |
|----------|-----------|
| **¿Por qué dos interfaces?** | El BrightC necesita conectividad cableada (Ethernet, estable y de alta velocidad) e inalámbrica (WiFi, para despliegue flexible y configuración inicial vía AP). |
| **¿Para qué servirán?** | Ethernet: servicios de red principales (SNMP, etc.). WiFi: configuración inicial (modo AP por defecto), conectividad inalámbrica y servicios futuros (Modbus TCP, MQTT). |
| **¿Cómo se gestionan?** | Mediante una Active Object única (`ao_network`) que orquesta ambos adaptadores a través de un puerto común (`i_network.h`), con máquinas de estados independientes por interfaz, configuración persistente separada en EEPROM (dominios `CONFIG_DOMAIN_NET_ETH` y `CONFIG_DOMAIN_NET_WIFI`), y exposición de estado vía canales Zbus individuales (`chan_network_eth_state`, `chan_network_wifi_state`). |
| **¿Hay failover?** | No. Ambas interfaces pueden estar UP simultáneamente. Cada servicio futuro elige a qué interfaz vincular sus sockets. |
| **¿Cómo se cambia la config en runtime?** | Mutación en RAM → evento Zbus `chan_system_config` → `ao_network` aplica en caliente a la interfaz afectada → `ao_storage` guarda en EEPROM con debounce y dirty-check. |
| **¿Qué se deja para después?** | Servicios MQTT, HTTP, SNMP, Modbus TCP. Se implementarán como capas superiores que consumen `i_network_t` y escuchan estado vía Zbus. |

---

**Estado del documento**: Especificación técnica lista para implementación.  
**Estado del código**: Ningún archivo ha sido modificado aún. Todo está por construir sobre las bases definidas (DTS, arquitectura limpia, patrón de storage documentado).