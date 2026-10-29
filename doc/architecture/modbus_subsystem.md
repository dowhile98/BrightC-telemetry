# Arquitectura del Subsistema Modbus Industrial (Modbus Subsystem)

- **Estado**: Especificación Técnica de Arquitectura
- **Plataforma**: DAECGE BrightC Controller V3 (STM32H743ZIT6)
- **Puertos Físicos**: RS485-1 (USART2, DE=PD4) y RS485-2 (USART6, DE=PG8)
- **Pila de Protocolo**: `nanoMODBUS` (vía transceptor serial Zephyr RTOS)
- **Patrón Concurrente**: Active Object por puerto físico + Registro Inyectado con Exclusión Mutua

---

## 1. Visión General y Principios Rectores

El subsistema Modbus de BrightC Controller V3 está diseñado para adquisición de telemetría de campo y control de actuadores en entornos industriales ruidosos. Cumple con los siguientes requerimientos fundamentales:

1. **Aislamiento Total de Buses (Dos Hilos Concurrentes)**:
   - RS-485 es un medio de comunicación físico **half-duplex**.
   - Cada puerto (`RS485-1` y `RS485-2`) es propiedad exclusiva de su propio hilo de ejecución (`ao_modbus`).
   - Una falla catastrófica de cableado, cortocircuito o esclavo colgado en un bus jamás degrada el ancho de banda ni bloquea al otro bus.
2. **Independencia del Hardware (Reasignación en Caliente / Failover)**:
   - Los dispositivos de campo no están acoplados de forma rígida a un puerto físico en código.
   - Es posible transferir un dispositivo del Bus 1 al Bus 2 en tiempo de ejecución (`set_bus`), notificando a los hilos y guardando la nueva topología en la EEPROM.
3. **Planificación Pura y Tolerancia a Fallos (Offline Backoff)**:
   - La lógica que decide qué esclavo está listo para sondeo y su clasificación de salud es **100% código C puro** (`slave_scheduler.c`), totalmente desacoplada de Zephyr y testeable en host PC (`native_sim`).
4. **Protección contra Esclavos Desconectados**:
   - Tras 3 fallos consecutivos, el esclavo pasa a estado de reposo lento (*probe ping* cada 10 s), impidiendo que timeouts repetitivos saturen el medio y demoren a los dispositivos sanos.

---

## 2. Diagrama de Arquitectura del Subsistema

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   CAPA DE APLICACIÓN                                   │
 │                                                                                        │
 │                    GESTOR CENTRAL DE ESCLAVOS: modbus_registry.c                       │
 │        - Tabla en RAM de modbus_slave_snapshot_t (hasta 32 dispositivos)               │
 │        - Cursor round-robin por bus y recuperación de claims obsoletos                 │
 │        - Exclusión mutua interna mediante k_mutex (transparente a los hilos)           │
 │        - Serializador export/import versionado para almacenamiento en EEPROM           │
 │                                                                                        │
 └──────────────────────┬──────────────────────────────────────────┬──────────────────────┘
                        │                                          │
        i_slave_registry│ (Puerto compartido)      i_slave_registry│ (Puerto compartido)
                        ▼                                          ▼
 ┌──────────────────────────────────────┐   ┌─────────────────────────────────────────┐
 │   HILO RS485-1: ao_modbus.c (Bus 0)  │   │   HILO RS485-2: ao_modbus.c (Bus 1)     │
 │                                      │   │                                         │
 │ - Stack y Cola de eventos propios    │   │ - Stack y Cola de eventos propios       │
 │ - Sondea esclavos asignados a Bus 0  │   │ - Sondea esclavos asignados a Bus 1     │
 │ - Control exclusivo de USART2        │   │ - Control exclusivo de USART6           │
 │ - Driver Enable (DE) por HW en PD4   │   │ - Driver Enable (DE) por HW en PG8      │
 └──────────────────┬───────────────────┘   └────────────────────┬────────────────────┘
                    │                                            │
   i_modbus_client_t│ (Puerto Bus 0)             i_modbus_client_t│ (Puerto Bus 1)
                    ▼                                            ▼
 ┌──────────────────────────────────────┐   ┌─────────────────────────────────────────┐
 │ adapter_modbus_nmbs.c (Instancia 0)  │   │ adapter_modbus_nmbs.c (Instancia 1)     │
 │ - Envuelve nmbs_z_client_t           │   │ - Envuelve nmbs_z_client_t              │
 │ - API de lectura/escritura Modbus    │   │ - API de lectura/escritura Modbus       │
 └──────────────────┬───────────────────┘   └────────────────────┬────────────────────┘
                    │                                            │
                    ▼                                            ▼
           [ USART2 + DE (PD4) ]                        [ USART6 + DE (PG8) ]
           Transceptor Físico 1                         Transceptor Físico 2
```

---

## 3. Componentes y Separación de Responsabilidades

### 3.1 Dominio Puro: Planificación y Salud (`domain/services/slave_scheduler.c`)
- Contiene las reglas matemáticas puras sin depender de sockets, colas ni RTOS:
  - `slave_scheduler_is_due()`: Evalúa si un esclavo cumplió su intervalo según el reloj monotónico u64.
  - `slave_scheduler_record()`: Actualiza contadores y conmuta estados de salud:
    - `MODBUS_RESULT_OK`: Resetea contadores de fallo, pasa a `MODBUS_HEALTH_ONLINE`.
    - `MODBUS_RESULT_TIMEOUT` y `MODBUS_RESULT_CRC`: Incrementan `consecutive_fails`. Si llega a 3, pasa a `MODBUS_HEALTH_OFFLINE_BACKOFF`.
    - `MODBUS_RESULT_EXCEPTION`: El esclavo respondió (está vivo); no incrementa fallos ni degrada la salud, contabilizándose en su propia estadística.

### 3.2 Capa de Aplicación: Registro Concurrente (`application/modbus/modbus_registry.c`)
- Mantiene la tabla de descriptores y estados de ejecución.
- Protegido por un `k_mutex` interno: el llamante nunca gestiona locks.
- **Ciclo Atómico `claim_next_due` / `record`**:
  - Para evitar que dos llamadas concurrentes tomen el mismo esclavo, el reclamo marca el esclavo como `in_flight = true`.
  - Si un hilo se cuelga o muere antes de reportar, el esclavo se recupera automáticamente tras `MODBUS_CLAIM_STALE_MS` (5 segundos).
- **Serialización Portable (`export` / `import`)**:
  - `modbus_registry_export()` empaqueta la configuración en un buffer plano little-endian independiente de la CPU.
  - `modbus_registry_import()` valida la consistencia total del buffer antes de escribir la tabla en RAM.

### 3.3 Active Objects de Sondeo (`application/active_objects/ao_modbus.c`)
- Cada instancia corre un bucle de eventos:
  1. Consulta `i_slave_registry_claim_next_due()` para su `bus_id`.
  2. Si hay un esclavo listo, ejecuta `poll_target()`.
  3. Reporta el resultado con `i_slave_registry_record()`.
  4. Si no hay nada listo, calcula `next_due_in()` y duerme en `k_msgq_get()` hasta el próximo vencimiento o hasta recibir un comando en su cola.

---

## 4. Reasignación de Bus en Caliente (Runtime Failover)

Si el cableado físico o el transceptor de `RS485-1` sufre daño, el operador puede mover los cables a `RS485-2` y reasignar el dispositivo mediante el puerto:

```c
int err = i_slave_registry_set_bus(&app->registry, device_id, MODBUS_BUS_RS485_2);
```

### 4.1 Secuencia de Ejecución Interna:
1. **Validación Atómica**: El registro comprueba que el Bus 2 no esté lleno (`MODBUS_MAX_SLAVES_PER_BUS = 16`) y que la dirección esclavo no esté duplicada en el bus destino (`-EADDRINUSE`).
2. **Conmutación**: Se actualiza el campo `bus_id` dentro del descriptor.
3. **Notificación Zbus**: Se emite el evento en `chan_system_config`.
4. **Despertar del Bus Destino**: Se envía `AO_MODBUS_EVENT_POLL_NOW` al hilo del Bus 2 para que no espere inactivo y tome el esclavo al instante.
5. **Persistencia en EEPROM**: El coordinador `ao_storage` recibe el evento Zbus y almacena la nueva asignación en la EEPROM mediante `microconf`.

---

## 5. Guía para Extender: Cómo Agregar un Nuevo Dispositivo Modbus

Para incorporar un nuevo dispositivo de campo (ejemplo: un **Expansor de 8 Salidas a Relé por Modbus RTU**):

```text
               ┌────────────────────────────────────────────────────────┐
               │ 1. common/modbus_types.h: Identificador de dispositivo │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 2. domain/services/relay_expander_codec.{c,h}          │
               │    (Funciones puras de codificación/decodificación)    │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 3. application/active_objects/ao_modbus.c              │
               │    - Lectura periódica en poll_target()                │
               │    - Acciones de escritura asíncronas vía eventos      │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 4. interfaces/i_relay.h + adapter_relay_modbus.c       │
               │    (Expone interfaz estándar hacia la aplicación)      │
               └───────────────────────────┬────────────────────────────┘
                                           │
               ┌───────────────────────────▼────────────────────────────┐
               │ 5. Registro & Persistencia EEPROM                      │
               │    (Se registra en el registry y se guarda solo)       │
               └────────────────────────────────────────────────────────┘
```

### Paso 1: Declarar el Tipo en el Dominio (`app/include/common/modbus_types.h`)
Añadir el identificador en la enumeración:
```c
typedef enum {
    MODBUS_DEV_TYPE_UNKNOWN = 0,
    MODBUS_DEV_TYPE_CWT_TM16PT,
    MODBUS_DEV_TYPE_ENERGY_METER_SDM,
    MODBUS_DEV_TYPE_RELAY_EXPANDER,    /**< Módulo de salidas digitales Modbus */
} modbus_device_type_t;
```

### Paso 2: Implementar el Codec de Dominio Puro (`app/src/domain/services/relay_expander_codec.{c,h}`)
Escribir la lógica de conversión matemática y empaquetado de bits en C estándar, sin dependencias de hardware:
- Función para empaquetar una máscara de bits a solicitud Modbus FC05 (Write Single Coil) o FC15 (Write Multiple Coils).
- Función para decodificar la confirmación del esclavo o la lectura de coils (FC01).
- Escribir su suite de pruebas unitarias en `tests/unit/modbus/`.

### Paso 3: Integrar en el Bucle del Active Object (`app/src/application/active_objects/ao_modbus.c`)
Un actuador como un módulo de relés tiene dos tipos de interacción:

1. **Lectura Periódica de Estado (Sondeo)**:
   En `poll_target()`, agregar el caso:
   ```c
   case MODBUS_DEV_TYPE_RELAY_EXPANDER:
       return i_modbus_client_read_coils(self->client, target->slave_addr, ...);
   ```
   Al recibir la trama válida, se publica el estado actual de los relés en un canal Zbus (`chan_relay_state`).
2. **Escritura Asíncrona (Conmutación de un Relé bajo demanda)**:
   - Dado que el puerto serie es half-duplex, **ningún hilo externo debe transmitir directamente a la UART**.
   - Para conmutar un relé, la aplicación envía un evento a la cola del Active Object:
     ```c
     modbus_event_t evt = {
         .type = AO_MODBUS_EVENT_WRITE_COIL,
         .target_device_id = dev_id,
         .coil_index = 2,
         .coil_value = true,
     };
     ao_modbus_post(&app->ao[bus_id], &evt);
     ```
   - El hilo `ao_modbus` procesa el evento entre ciclos de sondeo y ejecuta `i_modbus_client_write_single_coil()`.

### Paso 4: Adaptador al Puerto Estándar de la Aplicación (`i_relay.h`)
- Para la lógica de negocio o alarmas, el control se realiza a través de `i_relay_set_state()`.
- Se implementa `adapter_relay_modbus.c` implementando la v-table `i_relay_api_t`, traduciendo la llamada a un mensaje hacia la cola del `ao_modbus`.
- La capa de dominio desconoce si el relé es un pin físico del STM32, un expansor SPI MCP23S17 o un esclavo Modbus.

### Paso 5: Registro y Persistencia Automática
Al inicializar o mediante comando de configuración, se registra el descriptor:
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
El coordinador de almacenamiento (`ao_storage`) detecta la inserción y guarda automáticamente el nuevo esclavo en la EEPROM.
