# Arquitectura del Subsistema de Almacenamiento Persistente (Storage Subsystem)

- **Estado**: Especificación Técnica de Arquitectura
- **Plataforma**: DAECGE BrightC Controller V3 (STM32H743ZIT6)
- **Medio Físico**: EEPROM I2C Externa AT24C256C (32 KB, I2C2)
- **Motor de Integridad**: `microconf` (Dual-Slot Ping-Pong + CRC32)
- **Patrón Arquitectónico**: Clean Architecture (Puertos y Adaptadores) + Coordinador Active Object

---

## 1. Visión General y Justificación Técnica

### 1.1 ¿Por qué se necesita un subsistema de almacenamiento dedicado?
El controlador BrightC V3 requiere persistir múltiples categorías de configuración que deben sobrevivir a reinicios y cortes imprevistos de energía:
- Configuración de buses y tabla de esclavos Modbus RTU.
- Configuración de interfaz Ethernet (IP estática/DHCP, MAC, gateway).
- Configuración de interfaz Wi-Fi (modo STA/AP/STA+AP, SSID, credenciales, IP del soft-AP).
- Parámetros de identidad del dispositivo (número de serie, nombre de estación, endpoints de telemetría).
- Calibraciones analógicas (factores de escala y offsets de canales ADC).

### 1.2 El Problema: Inanición y Concurrencia sobre la EEPROM
Permitir que cualquier hilo o caso de uso escriba directamente sobre la EEPROM física introduce riesgos críticos de diseño:
1. **Bloqueo y Jitter en Tareas de Tiempo Real**: La EEPROM física (AT24C256C) tiene un tiempo de ciclo de escritura por página ($t_{WR}$) de **5 ms a 10 ms**. Si un hilo de control o de comunicación serie (ej. `ao_modbus`) escribe síncronamente en la EEPROM, se bloquea durante 10–25 ms por página, provocando pérdida de tramas y jitter severo.
2. **Acceso Concurrente no Seguro sobre el Bus I2C2**: La biblioteca `microconf` garantiza la estructura de slots, la serialización y la integridad por CRC32, pero **no es thread-safe ni concurrente**: carece de exclusión mutua interna o colas de espera.
3. **Desgaste Prematuro de Celdas (EEPROM Wear-out)**: Cambios ráfaga (como un operador ajustando varios parámetros seguidos desde el Shell o la pantalla DWIN) destruirían la vida útil de las celdas si cada modificación dispara una escritura física inmediata. Se requiere una ventana de amortiguación (*debouncing / coalescing*).

### 1.3 La Solución: Coordinador de Almacenamiento Asíncrono (`ao_storage`)
Para resolver estos problemas sin violar la Inversión de Dependencias (DIP):
- La aplicación y el dominio interactúan exclusivamente a través de memoria RAM y contratos abstractos (`i_config_storage.h`).
- Los cambios se anuncian a través del bus de eventos desacoplado **Zephyr Zbus** (`chan_system_config`).
- Un único actor asíncrono, el **Active Object de Almacenamiento (`ao_storage`)**, es el dueño exclusivo del bus I2C2, la EEPROM física y las instancias de `microconf`.

---

## 2. Mapa de Memoria de la EEPROM AT24C256C

La EEPROM física dispone de 32.768 bytes (32 KB, direcciones `0x0000` a `0x7FFF`). Mediante los parámetros `base_offset` y `total_size` de `mconf_zephyr_eeprom_init()`, la memoria se particiona en regiones lógicas independientes:

```text
Offset Base   Tamaño Total   Dominio Lógico         Estructura y Propósito
───────────────────────────────────────────────────────────────────────────────────────────
0x0000        1024 B (1 KB)  CONFIG_DOMAIN_DEVICE   2 slots de 512 B (Serial, HW Ver, Alias)
0x0400        2048 B (2 KB)  CONFIG_DOMAIN_MODBUS   2 slots de 1 KB (Buses + 32 Esclavos)
0x0C00        1024 B (1 KB)  CONFIG_DOMAIN_NET_ETH  2 slots de 512 B (IP estática/DHCP, MAC, gateway)
0x1000        1024 B (1 KB)  CONFIG_DOMAIN_NET_WIFI 2 slots de 512 B (Modo STA/AP/STA+AP, SSID, credenciales, IP del soft-AP)
0x1400        3072 B (3 KB)  CONFIG_DOMAIN_CALIB    2 slots de 1.5 KB (Offsets y Factores ADC)
0x2000        8192 B (8 KB)  RESERVADO              Futuras expansiones (SNMP, Certificados)
0x4000       16384 B (16 KB) EVENT_LOG / HISTÓRICO  Almacenamiento circular para auditoría
───────────────────────────────────────────────────────────────────────────────────────────
```

### 2.1 Principio de Aislamiento por Región
- Cada región contiene su propio par de slots en ping-pong administrados por `microconf`.
- Un cambio en la estructura de red **no invalida** el fingerprint ni el CRC de la región Modbus.
- Si una región sufre corrupción por fallo de hardware, las demás regiones permanecen intactas y legibles.

---

## 3. Diagrama de Arquitectura y Flujo de Datos

```text
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                                CAPA DE APLICACIÓN                                   │
 │                                                                                     │
 │  ┌─────────────────────┐   ┌─────────────────────┐   ┌───────────────────────────┐  │
 │  │   modbus_registry   │   │     network_mgr     │   │      device_profile       │  │
 │  │ (Tabla en RAM)      │   │ (Config Red en RAM) │   │ (Identidad del MCU en RAM)│  │
 │  └──────────┬──────────┘   └──────────┬──────────┘   └─────────────┬─────────────┘  │
 │             │                         │                            │                │
 │             └─────────────────────────┼────────────────────────────┘                │
 │                                       ▼ (Publican evento de cambio)                 │
 │                      ═══════════════════════════════════════                        │
 │                           CANAL ZBUS: chan_system_config                            │
 │                      ═══════════════════════════════════════                        │
 │                                       │                                             │
 │                                       ▼ (Suscripción asíncrona)                     │
 │                      ┌──────────────────────────────────────┐                       │
 │                      │              ao_storage              │                       │
 │                      │       (Storage Coordinator AO)       │                       │
 │                      │                                      │                       │
 │                      │  - Cola de mensajes k_msgq           │                       │
 │                      │  - Temporizador Debounce (500 ms)    │                       │
 │                      │  - Dirty-Check (evita reescritura)   │                       │
 │                      └──────────────────┬───────────────────┘                       │
 └─────────────────────────────────────────┼───────────────────────────────────────────┘
                                           │ Consume interfaz abstracta
                                           ▼
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                        PUERTO / INTERFAZ: i_config_storage.h                        │
 │                 (Contrato C99 puro: save, load, reset_defaults)                     │
 └─────────────────────────────────────────┬───────────────────────────────────────────┘
                                           │ Implementado por
                                           ▼
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                       CAPA DE INFRAESTRUCTURA (ADAPTADORES)                         │
 │                                                                                     │
 │  ┌───────────────────────────────────────────────────────────────────────────────┐  │
 │  │ adapter_storage_microconf.c                                                    │  │
 │  │ - Mantiene instancias mconf_t y mconf_io_t por cada dominio                    │  │
 │  │ - Implementa mconf_zephyr_eeprom_init() hacia el driver I2C de Zephyr         │  │
 │  └──────────────────────────────────────┬────────────────────────────────────────┘  │
 └─────────────────────────────────────────┼───────────────────────────────────────────┘
                                           │ Control I2C2
                                           ▼
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                         HARDWARE & DEVICETREE (DTS)                                 │
 │                 EEPROM Externa AT24C256C (eeprom0: eeprom@50)                       │
 └─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Contratos e Interfaces del Subsistema

### 4.1 Definición de Dominios (`app/include/common/config_types.h`)
```c
typedef enum {
    CONFIG_DOMAIN_DEVICE = 0,    /**< Información y metadatos del dispositivo */
    CONFIG_DOMAIN_MODBUS,        /**< Configuración de buses y tabla de esclavos */
    CONFIG_DOMAIN_NET_ETH,       /**< Configuración de interfaz Ethernet (IP, DHCP, MAC) */
    CONFIG_DOMAIN_NET_WIFI,      /**< Configuración de interfaz Wi-Fi (modo, SSID, credenciales) */
    CONFIG_DOMAIN_CALIBRATION,   /**< Constantes de calibración de entradas analógicas */
    CONFIG_DOMAIN_MAX            /**< Límite superior de dominios */
} config_domain_t;
```

### 4.2 Puerto Abstracto (`app/include/interfaces/i_config_storage.h`)
Siguiendo la regla de tres piezas ([`doc/STRUCTURE.md §4.1.D`](file:///media/jeffry/jqb/embedded-mcu/BrightC-workspace/BrightC-telemetry/doc/STRUCTURE.md#L377-L435)):

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
3. **Wrappers Validados**:
   - `static inline int i_config_storage_save(...)`
   - `static inline int i_config_storage_load(...)`
   - `static inline int i_config_storage_reset_defaults(...)`

---

## 5. Dinámica de Operación y Políticas de Persistencia

### 5.1 Secuencia de Arranque (Bootstrapping)
La lectura en el arranque es **completamente síncrona** y ocurre antes de inicializar los Active Objects concurrentes, garantizando que el sistema comience en un estado consistente y conocido:

1. `composition_root_init()` inicializa el driver de la EEPROM y llama al adaptador de almacenamiento.
2. Para cada dominio registrado (ej. `CONFIG_DOMAIN_MODBUS`):
   - Se invoca `i_config_storage_load()`.
   - **Caso Éxito**: El blob cargado se inyecta directamente al gestor en RAM (`modbus_registry_import()`).
   - **Caso Fallo (EEPROM virgen o CRC inválido)**: Se cargan las estructuras por defecto del firmware (`k_default_slaves`), se inyectan en RAM y se marca el dominio como pendiente de inicialización en hardware.
3. Se lanzan los hilos Active Object (`ao_modbus`, `ao_storage`).

### 5.2 Secuencia de Modificación y Guardado en Ejecución
Cuando un usuario o proceso modifica un parámetro en tiempo de ejecución (ej. cambiar IP estática de Ethernet o SSID de Wi-Fi):

1. **Mutación en RAM**: El componente modifica su estado protegido en memoria (ej. `i_network_set_config()` para la interfaz afectada). La respuesta hacia el usuario o CLI es inmediata (< 1 ms).
2. **Emisión de Evento Zbus**: El componente publica un mensaje en `chan_system_config` indicando el dominio específico afectado:
   ```c
   /* Ejemplo: cambio de configuración Ethernet */
   system_config_event_t evt = {
       .domain = CONFIG_DOMAIN_NET_ETH,
       .event_type = CONFIG_EVENT_DIRTY,
       .sub_id = 0,
   };
   zbus_chan_pub(&chan_system_config, &evt, K_MSEC(10));
   ```
   ```c
   /* Ejemplo: cambio de configuración Wi-Fi */
   system_config_event_t evt = {
       .domain = CONFIG_DOMAIN_NET_WIFI,
       .event_type = CONFIG_EVENT_DIRTY,
       .sub_id = 0,
   };
   zbus_chan_pub(&chan_system_config, &evt, K_MSEC(10));
   ```
3. **Recepción en `ao_storage`**:
   - `ao_storage` recibe el evento en su cola de mensajes.
   - Marca el flag interno `dirty_mask |= (1 << domain)` para el dominio correspondiente (`CONFIG_DOMAIN_NET_ETH` o `CONFIG_DOMAIN_NET_WIFI`).
   - Reinicia el temporizador de **Debounce** (500 ms).
4. **Vencimiento del Debounce y Escritura Física**:
   - Al cumplirse los 500 ms de inactividad, `ao_storage` despierta.
   - Llama a la exportación en memoria del dominio sucio (ej. configuración Ethernet o Wi-Fi).
   - Comprueba si el contenido actual difiere del almacenado (*Dirty-Check*).
   - Si cambió, invoca `i_config_storage_save()`, el cual ejecuta `mconf_save()` sobre la región correspondiente en la EEPROM (`0x0C00` para Ethernet, `0x1000` para Wi-Fi).
   - Publica en Zbus un evento `CONFIG_EVENT_PERSISTED` informando que los datos están a salvo en silicio.

**Aislamiento entre dominios de red**: Un cambio en la configuración de Wi-Fi **no invalida** el fingerprint ni el CRC de la región Ethernet, y viceversa. Cada dominio se persiste de forma independiente.

---

## 6. Guía para Extender: Cómo Agregar un Nuevo Dominio

Cuando se desarrolle un nuevo subsistema que requiera persistencia (ejemplo: Parámetros de Red Ethernet / Wi-Fi):

1. **Asignar Dominio y Región**:
   - Registrar el nuevo enumerador en `common/config_types.h` (ej. `CONFIG_DOMAIN_NET_ETH`, `CONFIG_DOMAIN_NET_WIFI`).
   - Declarar su `base_offset` y `total_size` en el mapa de particiones de la EEPROM en `adapter_storage_microconf.c`.
2. **Definir Estructura de Datos y Esquema `microconf`**:
   - Crear el struct de configuración en `domain/models/` (plano, sin punteros).
   - Definir los `mconf_entry_t` con sus valores por defecto (ej. IP `192.168.1.100`, DHCP habilitado).
3. **Integrar con el Ciclo de Arranque**:
   - En `composition_root.c`, añadir la llamada de carga en el boot: `i_config_storage_load(&app->storage, CONFIG_DOMAIN_NET_ETH, ...)` o `CONFIG_DOMAIN_NET_WIFI`.
4. **Notificar Cambios**:
   - En el gestor de red (`ao_network.c`), cada vez que se modifique un parámetro de Ethernet, publicar en `chan_system_config` indicando `CONFIG_DOMAIN_NET_ETH`. Para Wi-Fi, usar `CONFIG_DOMAIN_NET_WIFI`.
5. **Listo**:
   - `ao_storage` se encargará automáticamente del debounce, la verificación de integridad y el guardado en la EEPROM sin tocar una sola línea de los demás subsistemas.
