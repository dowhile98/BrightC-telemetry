# Diseño: registro Modbus y Composition Root (primera puesta en marcha)

- **Fecha**: 2026-10-28
- **Estado**: aprobado
- **Alcance**: subsistema Modbus RTU (RS485-1 / RS485-2) y raíz de composición de la aplicación

## 1. Intención y porqué

La capa `app/` tiene hoy tres problemas que bloquean cualquier crecimiento:

1. **Estado global suelto**: `modbus_slave_manager.c` guarda la tabla de esclavos en un
   `static` de fichero y expone funciones libres sin instancia. Es un *singleton implícito*:
   no se puede sustituir en pruebas ni tener dos buses independientes.
2. **Sin raíz de composición**: `app/src/infrastructure/di/` contiene un `.h` vacío y un `.c`
   con texto de cabecera. `main.c` no arranca nada. Cada módulo nuevo acabaría declarando
   su propio `static`.
3. **Sin aislamiento entre buses**: el gestor actual no distingue el resultado de una
   transacción (timeout vs CRC vs excepción Modbus) y no guarda la configuración eléctrica
   de cada bus.

El objetivo es dejar el subsistema Modbus funcionando sobre una raíz de composición única,
con dos Active Objects independientes, de forma que cualquier componente futuro
(telemetría, HMI, almacenamiento) se añada sin introducir variables globales nuevas.

## 2. Objetivos

- Un **único objeto estático de datos** en todo el firmware: `static app_container_t g_app`.
- El registro de esclavos pasa a ser una **instancia inyectada**, no un singleton.
- Dos **Active Objects** independientes, uno por bus físico, con stack y cola propios.
- El registro soporta **dos buses con configuración eléctrica distinta** (baud/paridad/stop).
- El resultado de cada transacción se clasifica (`OK`/`TIMEOUT`/`CRC`/`EXCEPTION`) y alimenta
  contadores separados.
- La lógica de planificación de sondeo y de salud es **C puro** y se prueba en `native_sim`
  sin hardware ni nanoMODBUS.
- `import`/`export` de configuración es **versionado y portable** (sin depender de
  `__attribute__((packed))` ni del endianness de la CPU).

## 3. No objetivos

- No se implementa el HMI, la telemetría por red ni el almacenamiento persistente.
- No se cambia el DTS del board (ya es correcto) ni el overlay salvo lo indicado en la fase 5.
- No se toca CI, linker scripts ni código de arranque.
- No se introduce `malloc`: todo el estado se asigna estáticamente en el contenedor.
- No se implementa todavía el failover automático (solo la operación `set_bus` manual).

## 4. Anti-objetivos (cómo sabríamos que salió mal)

- Aparece un segundo `static` de datos fuera de `composition_root.c` y `main.c`.
- El registro necesita que el llamante mantenga el lock (lock expuesto en la API).
- El AO llama directamente a `nmbs_z_*` en lugar de a `i_modbus_client`.
- Los tests de la lógica de salud necesitan un UART o `native_posix` con emulación.
- Un bus caído degrada la cadencia del otro.

## 5. Restricciones

| Restricción | Origen |
|---|---|
| C99/C11, sin C++ | `AGENTS.md` |
| Dominio sin `<zephyr/...>` | `doc/STRUCTURE.md` §3 |
| Sin heap (`CONFIG_HEAP_MEM_POOL_SIZE=0`) | build actual |
| `CONFIG_DCACHE=y` → RX por IRQ, no async | `.config` + `known_limitations.md` de nanoMODBUS |
| DE por hardware (RE y DE unidos físicamente) | DTS + confirmación del usuario |
| 512 KB RAM, ~22 KB de `.bss` en uso | board yaml + `size` del ELF |
| DCO sign-off, Conventional Commits | `AGENTS.md` |

## 6. Enfoques considerados

### 6.1 Active Object: un hilo vs dos

| Criterio | 1 hilo round-robin | **2 hilos (elegido)** |
|---|---|---|
| Inanición del bus sano | 16 esclavos caídos × 300 ms = 4,8 s de parada cada 10 s (~50 % del tiempo) | acotada al ciclo propio del bus (~1 s) |
| Aislamiento de fallos | un UART colgado detiene ambos buses | independiente |
| Estado compartido | un solo escritor → sin lock | 2 escritores → `k_mutex` |
| RAM | 1 stack | 2 stacks (~4 KB = 0,8 % de 512 KB) |
| CPU | idéntica (ambos bloqueados en espera de RX) | idéntica |

Descartado: un hilo con máquina de estados no bloqueante. `nmbs_z_client_*()` es bloqueante
con timeout; habría que reimplementar el *framing* RTU sobre el transporte.

**Decisión: dos `k_thread` preemptivas, una instancia `ao_modbus_t` por bus.**

### 6.2 Dónde vive la lógica de planificación

| Opción | Veredicto |
|---|---|
| Todo en `modbus_registry.c` (con lock) | la lógica de salud quedaría acoplada a Zephyr y no se podría probar sola |
| **`slave_scheduler` puro + `modbus_registry` con lock (elegido)** | la decisión (`is_due`, transición de salud) es 100 % testeable; el registro solo aporta almacenamiento y exclusión mutua |
| Servicio de dominio con vtable inyectada | sobreingeniería: son funciones puras sin estado |

### 6.3 Serialización de configuración

| Opción | Veredicto |
|---|---|
| `memcpy` de structs `packed` | no portable (endianness, padding del compilador, sin versión) |
| **Campo a campo a un buffer little-endian con cabecera de versión (elegido)** | testeable, portable, permite migrar el formato |
| microconf / Zephyr Settings | se integrará más adelante; el formato del buffer es el contrato |

## 7. Estructura decidida

### 7.1 Capas y ficheros

```
app/include/common/modbus_types.h            DTOs y enums (puro)
app/include/interfaces/i_slave_registry.h    Puerto del registro (vtable + void *impl)
app/include/interfaces/i_modbus_client.h     Puerto del cliente Modbus (vtable + void *impl)

app/src/domain/services/slave_scheduler.{c,h}   C puro: due / salud / backoff
app/src/application/modbus/modbus_registry.{c,h} tabla + k_mutex + import/export

app/src/infrastructure/adapters/adapter_modbus_nmbs.{c,h}   envuelve nmbs_z_client_t
app/src/infrastructure/di/composition_root.{c,h}             raíz de composición
app/src/application/active_objects/ao_modbus.{c,h}           un AO por bus

app/src/main.c                               único static: static app_container_t g_app
```

### 7.2 Forma del contenedor

```c
typedef struct {
    /* Puertos */
    i_slave_registry_t      registry;          /* vtable + impl apuntando a modbus_registry */
    i_modbus_client_t       client[MODBUS_BUS_MAX];

    /* Adaptadores por valor */
    modbus_registry_t       registry_impl;
    adapter_modbus_nmbs_t   nmbs[MODBUS_BUS_MAX];

    /* Active Objects + recursos estáticos */
    ao_modbus_t             ao[MODBUS_BUS_MAX];
    uint8_t                 ao_stack[MODBUS_BUS_MAX][AO_MODBUS_STACK_SIZE];
    struct k_msgq           ao_queue[MODBUS_BUS_MAX];
    uint8_t                 ao_queue_buf[MODBUS_BUS_MAX][AO_MODBUS_QUEUE_DEPTH * sizeof(modbus_event_t)];

    /* Configuración de buses (deja de estar huérfana) */
    modbus_bus_cfg_t        bus_cfg[MODBUS_BUS_MAX];

    uint8_t                 init_mask;         /* bit por subsistema */
} app_container_t;
```

### 7.3 Semántica de resultados

| Resultado | ¿El esclavo respondió? | `consecutive_fails` | Salud |
|---|---|---|---|
| `MODBUS_RESULT_OK` | sí | se resetea | `ONLINE` |
| `MODBUS_RESULT_TIMEOUT` | no | +1 | `DEGRADED` (1-2) / `OFFLINE_BACKOFF` (≥3) |
| `MODBUS_RESULT_CRC` | no (trama corrupta) | +1 | igual que timeout |
| `MODBUS_RESULT_EXCEPTION` | **sí** (código de excepción Modbus) | no cambia | se mantiene `ONLINE` |

`EXCEPTION` cuenta en su propio contador: el dispositivo está vivo pero la petición es
inválida. Marcarlo como caído ocultaría un error de configuración.

### 7.4 API del registro (resumen)

```c
int  modbus_registry_init(modbus_registry_t *self,
                          const modbus_bus_cfg_t cfg[MODBUS_BUS_MAX]);
int  modbus_registry_register(modbus_registry_t *self, const modbus_slave_descriptor_t *desc);
int  modbus_registry_claim_next_due(modbus_registry_t *self, uint8_t bus_id,
                                    uint64_t now_ms, modbus_poll_target_t *out);
int  modbus_registry_record(modbus_registry_t *self, uint8_t device_id,
                            modbus_transaction_result_t result, uint64_t now_ms);
int  modbus_registry_set_bus(modbus_registry_t *self, uint8_t device_id, uint8_t new_bus_id);
int  modbus_registry_get_bus_cfg(const modbus_registry_t *self, uint8_t bus_id,
                                 modbus_bus_cfg_t *out);
int  modbus_registry_snapshot(modbus_registry_t *self, uint8_t bus_id,
                              modbus_slave_snapshot_t *out, size_t max, size_t *out_count);
int  modbus_registry_export(const modbus_registry_t *self, void *buf, size_t max, size_t *out_size);
int  modbus_registry_import(modbus_registry_t *self, const void *buf, size_t size);
```

Reglas:
- Todo el locking es **interno**; ninguna función expone el mutex.
- `claim_next_due` es una sola operación atómica (sustituye a `is_ready_to_poll` +
  `record_result`), con cursor round-robin por bus y recuperación de *claims* obsoletos
  (si un AO muere entre claim y record, el esclavo vuelve a estar disponible pasado
  `AO_MODBUS_CLAIM_STALE_MS`).
- `register` rechaza `bus_id` inválido, `slave_addr` fuera de 1..247, `device_id` duplicado,
  dirección duplicada en el mismo bus y desbordamiento de `MODBUS_MAX_SLAVES_PER_BUS`.
- `import` valida todo lo anterior **antes** de mutar la tabla (se aplica sobre una copia).

### 7.5 Hilos

| Parámetro | Valor | Razón |
|---|---|---|
| Prioridad | `AO_MODBUS_PRIORITY` (7, preemptiva) | nunca cooperativa: un AO en bucle no debe bloquear el sistema |
| Stack | `AO_MODBUS_STACK_SIZE` (2048 B) | `LOG_*` es el mayor consumidor; se medirá con `CONFIG_THREAD_ANALYZER` |
| Cola | `AO_MODBUS_QUEUE_DEPTH` (8) eventos `modbus_event_t` | comandos (forzar sondeo, reconfigurar, parar) |
| Cadencia | `k_msgq_get(..., timeout = próximo vencimiento)` | sin `k_sleep` fijo: el AO duerme hasta el siguiente esclavo *due* |

## 8. Qué queda a criterio del implementador

- Nombres exactos de los campos de `modbus_poll_target_t` y `modbus_event_t`.
- Tamaño definitivo de `AO_MODBUS_STACK_SIZE` (se ajusta tras medir).
- Si el AO publica en `zbus` (fase posterior) o solo actualiza el registro.
- Formato concreto del buffer versionado (orden de campos), respetando: versión + contador +
  registros de tamaño fijo + configuración de bus.

## 9. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| El AO y el shell acceden al registro a la vez | locking interno; `snapshot()` copia y suelta |
| Un AO se cuelga con un claim tomado | recuperación por `AO_MODBUS_CLAIM_STALE_MS` |
| `import` de config corrupta deja el sistema inoperante | aplicar sobre copia y validar antes de commit |
| Reconfiguración de UART en caliente rompe el DE | se usa `nmbs_z_serial_transport_reconfigure()`, nunca `uart_configure()` con `FLOW_CTRL_NONE` |
| Regresión silenciosa: alguien añade un `static` | script `tools/check_no_globals.sh` como target de CMake opcional |
