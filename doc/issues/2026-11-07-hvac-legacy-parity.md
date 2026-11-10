# Paridad funcional del subsistema HVAC — referencia legacy vs `BrightC-telemetry`

- **Fecha:** 2026-11-07
- **Estado:** análisis estático. **Sin cambios de código, sin build, sin flasheo.**
- **Árbol nuevo (referencia):** `BrightC-telemetry` @ `3c0dce6` (*fix bugs*), árbol de trabajo limpio.
- **Árbol legacy (referencia):** `/media/jeffry/jqb/brightc_/brightc_firmware` (sin control de versiones; última modificación de `src/domain/hvac/hvac_group_entity.c`: 2026-08-08).
- **Alcance:** subsistema HVAC: entidades de dominio, lazo de control, scheduler, adaptadores de relé, agregación de entradas, publicación de eventos y valores por defecto de configuración. Se citan los otros subsistemas (DI, storage) sólo cuando su comportamiento altera el HVAC.
- **Objetivo:** identificar qué comportamientos del firmware legacy —que "funcionaba, aunque con defectos"— **no** sobrevivieron a la reescritura, y separarlos de los defectos que ya existían en ambos árboles.
- **Documentos relacionados:**
  - [`2026-11-06-firmware-audit-second-pass.md`](./2026-11-06-firmware-audit-second-pass.md) (auditoría general; P0-01/P0-02 ya resueltos)
  - [`2026-11-06-architecture-refactor-hvac-telemetry-isolation.md`](./2026-11-06-architecture-refactor-hvac-telemetry-isolation.md) (ADR del rediseño)
  - [`2026-11-07-digital-input-pipeline-zbus-decoupling.md`](./2026-11-07-digital-input-pipeline-zbus-decoupling.md) (pipeline DI que alimenta el interlock de fuego)
  - [`2026-11-09-hvac-control-delay-sequence-pinout-review.md`](./2026-11-09-hvac-control-delay-sequence-pinout-review.md) (revisión de segunda pasada sobre pinout, delays, secuencia y tabla de verdad física del relé G)

## Rúbrica de severidad

| Nivel | Significado |
| :-- | :-- |
| **P0** | Seguridad o función de producto bloqueante: una orden explícita puede energizar actuadores, o una función de seguridad documentada no hace nada. |
| **P1** | Alto: una función documentada no opera, o el control no se recupera sin reinicio. |
| **P2** | Medio: desviación de comportamiento con impacto acotado o riesgo de carrera. |
| **P3** | Bajo: higiene, documentación, cobertura de tests. |

Convención de referencias: `ruta:línea` siempre relativa a la raíz del árbol indicado. Las líneas son las del commit/fecha anotados arriba.

---

## 1. Resumen ejecutivo

La reescritura **mejoró la estructura** (puertos/adaptadores, *active objects*, Zbus, configuración validada, detección de cambio antes de escribir relés) y añadió funciones que el legacy no tenía (rotación automática, política de métrica por grupo, rangos de sensores). Ese trabajo no debe revertirse.

El problema es de **paridad de dominio**. Al colapsar el contrato de salida `hvac_tri_state_t` y al reescribir las entidades de grupo/unidad se perdieron comportamientos que el legacy sí implementaba, incluidos dos que afectan la seguridad del equipo:

| ID | Hallazgo | Severidad |
| :-- | :-- | :-- |
| **R-01** | El relé **G** (ventilador / post-purga / `fan_only`) dejó de existir en el contrato y el actuador lo fuerza a `0`. El shell muestra `G=1` mientras el relé físico está abierto. | P1 |
| **R-02** | El **interruptor físico de bypass** nunca se lee: `BYPASS_CAUSE_PHYSICAL_SWITCH` no tiene ningún escritor y el GPIO on-chip no entra al pipeline DI. | **P0** |
| **R-03** | `BYPASS_CAUSE_COOLING_TIMEOUT` **nunca se limpia**: el grupo queda en bypass mecánico hasta reiniciar. | P1 |
| **R-04** | Se eliminaron el **canal de eventos HVAC** y sus observadores (buzzer, logger, puente de alarmas). `chan_hvac_status` no tiene suscriptores ni transiciones. | P1 |
| **R-05** | Los temporizadores de enfriamiento **no se reinician** al salir de `COOLING/COOL_BOOST` por manual. | P2 |
| **R-06** | `hvac_unit_entity_increment_fault()` **perdió la guardia de estado**; ahora cualquier estado acumula fallos. | P2 |
| **R-07** | Un **error de actuador/bus** incrementa el contador de **fallos mecánicos** y lleva la unidad a `FAULT` permanente. | P1 |
| **R-08** | `set_manual(true)` de grupo **arranca todas las unidades** sin escalonado; `hvac mode off`/`fan_only` pueden emitir ON a los compresores. | P1 |
| **R-09** | No hay **retardo de estabilización al boot**; la primera evaluación puede caer en `SENSOR_FAULT` y transferir al termostato mecánico. | P2 |
| **R-10** | Cambiar `unit_mask` en caliente **no reconstruye** la topología grupo↔unidad; máscaras solapadas no se validan. | P1 |
| **R-11** | *(No es regresión)* El legacy **no** rotaba automáticamente; el nuevo sí. Se conserva. | — |
| **R-12** | La escritura remota pasó de **síncrona con 3 reintentos** a **cola asíncrona sin reintento** con caché optimista. | P2 |
| **R-13** | El modo manual **dejó de ser un estado**: tras `mode off` durante `COOLING`, el `tick` sigue corriendo y a los `time_to_cool_s` **arranca todos los compresores**; a la hora vuelve a AUTO por sí solo. | **P0** |
| **R-14** | Las **alarmas mecánicas de las unidades 4–7** nunca llegan al HVAC: el agregador DI se inicializa sólo con los 24 canales locales y los roles de la unidad 4–7 viven en la tabla remota. | P1 |

**Total:** 13 regresiones (2 P0, 7 P1, 4 P2) más R-11, que documenta una mejora para que no se "corrija" por error.

**Mensaje central:** el legacy no era mejor por su arquitectura, sino porque su **máquina de estados de grupo** seguía siendo la fuente de verdad (`MANUAL` era un estado, el bypass físico era una causa, el timeout se limpiaba por temperatura y el ventilador era parte del contrato de salida). La versión nueva conserva las entidades pero movió parte de esa verdad a la capa de aplicación/adaptadores, donde se perdió.

---

## 2. Línea base legacy: qué hacía y por qué funcionaba

### 2.1 Flujo de un ciclo (1 Hz)

```text
hvac_thread_entry (prio propia, 2000 ms de estabilización al boot)
  └─ cada 1000 ms:
       compute_temperature_metrics()          // desde el repositorio de sensores
       hvac_input_aggregator_read()           // alarmas 1..16 + fire DI17 + bypass PD11
       process_hvac_control_uc_execute():
            por grupo:
              hvac_unit_entity_update_alarms()      // 1 vez por segundo
              hvac_group_entity_set_bypass_cause(PHYSICAL_SWITCH, sw)
              hvac_group_entity_set_fire(fire)
              hvac_group_entity_tick_1s()           // timers ANTES de evaluar
              hvac_group_entity_evaluate(metrics)   // decisión
              comparar prev/next y publicar hvac_event_t
              por unidad: get_tri_state() → driver (AA, SM, G)
```

Evidencia: `src/system/hvac_scheduler.c:137-181`, `src/application/use_cases/process_hvac_control_use_case.c:99-229`.

### 2.2 Contrato de salida (el que se perdió)

`hvac_tri_state_t` era un **struct de tres relés**, no un enum:

| Estado de unidad | `relay_aa` | `relay_sm` | `relay_g` |
| :-- | :--: | :--: | :--: |
| `SMART_OFF` (con `enable_fan_delay`) | 0 | 1 | 1 (`SMART_IDLE_ON`) |
| `SMART_ON` / `MANUAL` | 1 | 1 | 0 (compresor) |
| `WAIT_DELAY` (post-purga) | 0 | 1 | 0 → luego 1 |
| `BYPASS` | 0 | 0 | 0 |
| `FAULT` / `BLOCKED_ALARM` | 0 | 1 | 0 |

Evidencia: `src/domain/hvac/hvac_value.h:20-34`, `src/domain/hvac/hvac_unit_entity.c:493-546` (G en `:538-545`), `src/application/use_cases/process_hvac_control_use_case.c:205-229`.

### 2.3 Jerarquía de decisión del grupo

Orden real del legacy (`src/domain/hvac/hvac_group_entity.c:401-594`):

1. `valid_sensors == 0` → `SENSOR_FAULT`.
2. `highest_temp >= temp_bypass` → `HIGH_TEMP`.
3. `highest_temp <= temp_min` → **limpia `HIGH_TEMP` y `COOLING_TIMEOUT`**, resetea contadores (`:423-431`).
4. Todos los unidades en `FAULT` → `ALL_FAULTS`.
5. Fuego → `FIRE` + parada forzada.
6. Cualquier causa de bypass → `BYPASS`, `is_manual_active = false`, todas las unidades a `BYPASS` (`:457-479`).
7. Si venía de `BYPASS`, reposiciona las unidades en `SMART_OFF` con **fan ON** (`:480-491`).
8. `is_manual_active` → `GROUP_STATE_MANUAL`, retorno (`:492-496`).
9. `highest_temp <= temp_min` → parada con purga.
10. `>= temp_alarm` → `COOL_BOOST` (todas las unidades, escalonadas) + **incrementa el contador de fallos de todas** (`:519-532`, comportamiento dudoso, ver §8).
11. `>= temp_max` → `COOLING` con `turn_count` según esquema de rotación (3+1, 2+1, 1+2).

### 2.4 Efectos observables

- Los cambios de estado de grupo/unidad se publicaban en `hvac_event_chan` con **dos observadores reales**: logger y puente de alarmas/buzzer (`src/infrastructure/bus/zbus_hvac_publisher.c:18-22`, `src/infrastructure/observers/zbus_hvac_logger.c:94`, `src/infrastructure/observers/zbus_hvac_alarm_bridge.c:101`).
- El bypass físico y el contacto de fuego se aplicaban **en cada ciclo** antes de evaluar (`process_hvac_control_use_case.c:156-158`).
- El modo manual tenía timeout y al expirar **liberaba las unidades** (`src/domain/hvac/hvac_group_entity.c:341-346`).

---

## 3. Implementación nueva: flujo equivalente

```text
ao_hvac_thread_entry (prio 5, sin retardo de estabilización)
  └─ k_msgq_get(..., 1000 ms)
       ao_hvac_step():
         por grupo activo:
           hvac_group_entity_evaluate(metrics)   // decisión
           hvac_group_entity_tick_1s()           // timers DESPUÉS de evaluar
         por unidad activa:
           tri = hvac_unit_entity_get_tri_state()      // enum, sin G
           i_hvac_unit_set_tri_state(...)               // adaptador: G = 0
         publicar chan_hvac_status (sin observadores)
```

Evidencia: `app/src/application/active_objects/ao_hvac.c:181-232`, `app/src/domain/models/hvac_group_entity.c:183-355`, `app/src/infrastructure/adapters/adapter_hvac_actuator.c:59-122`.

Diferencias estructurales relevantes:

| Aspecto | Legacy | Nuevo |
| :-- | :-- | :-- |
| Entrada de sensores | repositorio, cálculo por ciclo | snapshot Zbus `chan_sensor_snapshot` → `ao_hvac_feed_sensor_snapshot` |
| Entrada de alarmas/fuego/bypass | agregador propio, 1 Hz, 16 alarmas + fuego + bypass | listener Zbus de DI, **50 Hz**, pero el snapshot sólo lleva 24 canales locales (R-14) |
| Salida de relés | driver con tri-state completo | enum + adaptador con detección de cambio |
| Estado de configuración | `unit_group[8]`, máscara de sensores | `unit_mask` por grupo + rango `sensor_start/end` |
| Estado manual | estado `GROUP_STATE_MANUAL` | flag `is_manual_active` |
| Eventos | canal + 2 observadores | `chan_hvac_status` con `ZBUS_OBSERVERS_EMPTY` |

---

## 4. Matriz de paridad

| # | Comportamiento | Legacy | Nuevo | Veredicto |
| :-- | :-- | :-- | :-- | :-- |
| 1 | Contrato de salida AA/SM/**G** | struct de 3 relés | enum de 3 estados; G fijo a 0 | **Regresión R-01** |
| 2 | Detección de cambio antes de escribir | no | sí (`adapter_hvac_actuator.c:71-73`) | Mejora (P0-02 resuelto) |
| 3 | Post-purga del ventilador | G = 1 tras `WAIT_DELAY` | calculado en `get_relays()`, no actuado | **Regresión R-01** |
| 4 | `fan_only` / `manual ... fan_on` | G = 1 | inerte (G = 0) | **Regresión R-01** |
| 5 | Bypass por interruptor físico | sí (PD11 cada ciclo) | no | **Regresión R-02** |
| 6 | Bypass por alta temperatura + histéresis | sí | sí (`evaluate` paso 4) | Conservado |
| 7 | Bypass por timeout de enfriamiento | sí, se limpia con T ≤ Tmin | sí, **permanente** | **Regresión R-03** |
| 8 | Bypass por fallo de sensores | sí | sí | Conservado |
| 9 | Bypass por fallo de todas las unidades | sí | sí | Conservado |
| 10 | Bypass remoto (HMI/Modbus/SNMP) | enum definido, sin escritor | enum definido, sin escritor | Deuda en ambos |
| 11 | Detección de discrepancia de feedback | no | causa definida, sin escritor; endpoints `fb_*` validados sin uso | Deuda |
| 12 | Eventos de transición y consumidores | 13 tipos + 2 observadores | snapshot 1 Hz sin observadores | **Regresión R-04** |
| 13 | Timeout de manual libera unidades | sí | no (sólo borra el flag) | **Regresión R-13** |
| 14 | Manual es estado de grupo | sí (`GROUP_STATE_MANUAL`) | no (flag) | **Regresión R-13** |
| 15 | Guardia de estado en `increment_fault` | sí | no | **Regresión R-06** |
| 16 | Error de driver/actuador | se ignora | cuenta fallo mecánico | **Regresión R-07** |
| 17 | Estabilización al boot | 2000 ms | ninguna | **Regresión R-09** |
| 18 | Rotación automática | no (sólo UC expuesto) | sí, por días | **Mejora** |
| 19 | Asignación unidad↔grupo | `unit_group[]` 1:1 | `unit_mask` por grupo, sin rebuild ni validación de solape | **Regresión R-10** |
| 20 | Métrica del grupo | siempre `highest_temp` | `max`/`avg`/`min` por grupo | **Mejora** |
| 21 | Selección de sensores | máscara 32 bits sobre 16 | rango 1-based + máscara | **Mejora** |
| 22 | Validación de configuración | no visible | `config_hvac_is_valid()` | **Mejora** |
| 23 | Escritura remota | síncrona, 3 reintentos | cola asíncrona, sin reintento, caché optimista | **Regresión parcial R-12** |
| 24 | Salida de bypass | unidades a `SMART_OFF` con fan ON | `request_stop()` deja fan OFF | **Regresión menor (R-01)** |
| 25 | Fallo al cruzar `T_alarm` | incrementa fallos de todas | no | Diferencia deliberada (§8) |
| 26 | Limpieza de bypass al entrar en manual | `is_manual_active = false` | no | Parte de **R-13** |
| 27 | Alarmas mecánicas unidades 4–7 (remotas) | sí, 16 alarmas del módulo remoto | no: el snapshot DI sólo tiene 24 canales locales | **Regresión R-14** |

---

## 5. Tarjetas de regresión

### R-01 · El relé G desapareció del contrato de salida

**Severidad:** P1 · **Cross-ref:** auditoría previa **P1-06**

**Síntoma.** El ventilador de post-purga nunca arranca; `hvac mode <g> fan_only` y `hvac manual <u> on comp_off fan_on` no tienen efecto físico; `hvac status` muestra `G=1` mientras el relé físico está abierto. La purga posterior al apagado del compresor (30 s por defecto) es inobservable en campo.

**Mecanismo.** `hvac_tri_state_t` pasó de `struct { relay_aa, relay_sm, relay_g }` a un enum de tres valores (`OFF/ON/BYPASS`). El enum no puede expresar "compresor apagado, ventilador encendido". El adaptador fija `g = false` en las tres ramas del `switch`. El cálculo de G sobrevive sólo en `hvac_unit_entity_get_relays()`, que se usa **únicamente** para el estado que imprime el shell.

**Evidencia legacy.**
- `src/domain/hvac/hvac_value.h:20-34` — struct con los tres relés.
- `src/domain/hvac/hvac_unit_entity.c:493-546` — `get_tri_state()` calcula `relay_g` desde `fan_state` (`:538-545`).
- `src/application/use_cases/process_hvac_control_use_case.c:205-229` — despacha el tri-state completo al driver.

**Evidencia nueva.**
- `app/include/common/hvac_types.h:66-70` — enum de tres estados.
- `app/src/domain/models/hvac_unit_entity.c:243-256` — `get_tri_state()` no puede devolver G.
- `app/src/infrastructure/adapters/adapter_hvac_actuator.c:75-96` — `g = false` en `OFF`, `ON` y `BYPASS`; `:103-122` escribe igualmente el canal G con `false`.
- `app/src/domain/models/hvac_unit_entity.c:260-289` — G calculado; usado sólo en `app/src/application/services/app_facade.c:165`.
- `app/src/application/services/app_facade.c:216-229` — `FAN_ONLY` fuerza `fan_state = SMART_IDLE_ON`, sin efecto físico.

**Efecto colateral (salida de bypass).** El legacy, al limpiar todas las causas, dejaba las unidades que estaban en `BYPASS` en `SMART_OFF` con fan `SMART_IDLE_ON` (`hvac_group_entity.c:480-491`). El nuevo llama `request_stop()`, que deja `fan_state = DISABLED` (`hvac_unit_entity.c:175-213`). Aun arreglando G, el ventilador no volvería tras un bypass.

**Reproducción propuesta (no ejecutada).** `hvac manual 0 on comp_off fan_on` → el estado reporta `G=1` y el canal G del adaptador nunca pasa a `RELAY_STATE_CLOSED`. El test actual `tests/unit/hvac/src/test_adapter_hvac_actuator.c:76-101` **afirma `relay_g == OPEN` en los tres estados**, es decir, codifica la regresión y no la detectará.

**Corrección propuesta.** Reintroducir el tri-state como value object (`struct` de tres bits) o añadir la dimensión de ventilador al comando; que el adaptador derive G de `fan_state` (incluyendo `WAIT_DELAY`/`SMART_IDLE_ON`) y que la detección de cambio cubra también G. Ajustar `request_stop()`/salida de bypass para restaurar `SMART_IDLE_ON` cuando corresponda. Actualizar el test para exigir G cerrado en post-purga y en `fan_only`.

> **Actualización 2026-11-09 (ver [`2026-11-09-hvac-control-delay-sequence-pinout-review.md` §3](./2026-11-09-hvac-control-delay-sequence-pinout-review.md)):** En el hardware de la tarjeta BrightC V3 (`BrightC_ETH_CycloneTCP_FileX`, `brightc_io_manager_thread_entry.cpp:402-433`), el relé **G** tiene su contacto **Normalmente Cerrado (NC)** en serie con la línea `G` del termostato: `relay_g = 0` (`LOW`, desenergizado) mantiene el **ventilador habilitado/encendido**, mientras que `relay_g = 1` (`HIGH`, energizado, `SMART_IDLE_ON` o `FIRE`) abre el contacto y **apaga/corta el ventilador** cuando el aire acondicionado no está encendido. Por defecto, el control de ventilador debe venir en **OFF** (`enable_fan_delay = 0` → `relay_g = 0`, ventilador siempre habilitado salvo en alarma de incendio), y cuando se activa (`enable_fan_delay = 1`), tras los `fan_delay_s` de post-purga (`relay_g = 0`), energiza `relay_g = 1` para apagar el ventilador mientras la unidad esté apagada (`OFF`, `STARTING`, `BLOCKED_ALARM`, `FAULT`).

---

### R-02 · El interruptor físico de bypass nunca se lee

**Severidad:** P0

**Síntoma.** Un técnico conmuta el selector de mantenimiento (`sw_bypass`, PD11) esperando que el grupo salga del control automático y pase al termostato mecánico. No ocurre nada: el grupo sigue decidiendo por temperatura y puede energizar compresores. `BYPASS_CAUSE_PHYSICAL_SWITCH` nunca aparece en el estado.

**Mecanismo (tres capas).**
1. **Ningún consumidor:** no hay ninguna escritura de `BYPASS_CAUSE_PHYSICAL_SWITCH` en `app/src` (sólo la definición en `hvac_types.h:101`).
2. **Rol mal asignado:** `DI_ROLE_BYPASS_SWITCH` está mapeado al expansor **remoto** `MBSL32DI`, pin 29 (`app/include/common/hardware_pin_map.h:365`), con comentario `/* Comp 6 */`, no al GPIO on-chip.
3. **Canal on-chip fuera del pipeline:** `ONCHIP_DI_SW_BYPASS` se configura en `adapter_di_gpio.c:39`, pero el agregador se inicializa con `hardware_pin_map_get_local_di()` — **24 canales locales y nada más** (`app_defaults.h:277-282`, `composition_root.c:663-669`, `hardware_pin_map.h:496-502`). La tabla remota de 32 canales no se concatena (ver R-14). Los `ONCHIP_DI_*` (`digital_input_types.h:131-134`) no tienen entrada en ninguna de las dos tablas, por lo que el switch físico no entra en el snapshot que consume `on_hvac_di_feed_cb`.

Además, el endpoint `config->bypass_switch` se valida (`hvac_types.h:196-197`, `:349-350`) y se define en `app_defaults.h:108-111` apuntando a `ONCHIP_DI_SW_BYPASS`, pero **no se lee en runtime**.

**Evidencia legacy.**
- `src/infrastructure/drivers/hvac_input_aggregator.c:47-48` — lee `USER_INPUT_BYPASS_SWITCH` en cada ciclo.
- `src/application/use_cases/process_hvac_control_use_case.c:156-157` — aplica `BYPASS_CAUSE_PHYSICAL_SWITCH`.
- `src/di/di_container.c:360-367` — instancia el switch sobre `sw_bypass`, activo-bajo, 50 ms.

**Reproducción propuesta.** Conmutar el selector y ejecutar `hvac status`: la máscara de bypass no cambia. En host: inyectar un snapshot DI con `DI_ROLE_BYPASS_SWITCH = true` y comprobar que el grupo no entra en `BYPASS` (hoy no hay listener).

**Corrección propuesta.** Incorporar el switch on-chip al pipeline DI (canal local o evento propio) y consumirlo en `lis_hvac_di_feed` para fijar/limpiar `BYPASS_CAUSE_PHYSICAL_SWITCH`, igual que el fuego. Añadir test de integración del listener y un test de que el estado del grupo pasa a `BYPASS`.

---

### R-03 · `BYPASS_CAUSE_COOLING_TIMEOUT` es permanente

**Severidad:** P1

**Síntoma.** Tras dos ciclos de enfriamiento fallidos, el grupo transfiere al termostato mecánico (`relay_sm = 0`) y **no vuelve nunca** al control smart, aunque la sala se enfríe. Sólo un reinicio limpia la causa.

**Mecanismo.** En el nuevo `evaluate()`, el paso 4 limpia únicamente `HIGH_TEMP` (`hvac_group_entity.c:225-235`). `COOLING_TIMEOUT` sólo se limpia dentro de `set_bypass_cause(false)`, y no existe ninguna llamada con esa causa. Peor: el paso 5 retorna en cuanto `bypass_causes_mask != 0` (`:238-247`), por lo que la rama de recuperación `ctrl_temp <= temp_min` (`:255-268`) **nunca se ejecuta** mientras haya cualquier causa activa.

**Evidencia legacy.** `src/domain/hvac/hvac_group_entity.c:423-431`:

```c
if (metrics->highest_temp <= self->temp_min) {
    set_bypass_cause(HIGH_TEMP, false);
    set_bypass_cause(COOLING_TIMEOUT, false);
    cooling_timeout_count = 0;
    cooling_timer_s = 0;
}
```

y ese bloque se ejecuta **antes** del despacho de `BYPASS`.

**Evidencia nueva.** `app/src/domain/models/hvac_group_entity.c:225-235` (sólo `HIGH_TEMP`), `:238-247` (retorno temprano), `:337-342` (set de la causa). El test `tests/unit/hvac/src/test_hvac_group.c:209-268` termina justo al comprobar el bypass y **no prueba la recuperación**.

**Reproducción propuesta.** Extender ese test: tras el segundo timeout, alimentar `{highest_temp = 20.0, valid_sensors = 2}` (≤ `temp_min`), llamar `evaluate()` y exigir `bypass_causes_mask == 0` y `state == IDLE`. Hoy falla.

**Corrección propuesta.** Limpiar `COOLING_TIMEOUT` (y `cooling_timeout_count`) en la rama de baja temperatura **antes** del `return` del paso 5, como hacía el legacy; o definir una histéresis/tiempo de recuperación explícita. Test de regresión obligatorio.

---

### R-04 · Se eliminaron el canal de eventos y sus observadores

**Severidad:** P1 · **Cross-ref:** auditoría previa **P1-09** / decisión **D-06**

**Síntoma.** No hay notificación de transiciones. El buzzer de arranque de A/C no suena, el puente de alarmas no existe, y cualquier consumidor futuro (SNMP, HMI, telemetría) no tiene de dónde leer transiciones. `chan_hvac_status` se publica cada segundo **aunque nada cambie** y sin suscriptores.

**Evidencia legacy.**
- `src/domain/hvac/hvac_value.h:63-75` — 13 tipos de evento (`COOLING_STARTED`, `BYPASS_ENTERED`, `FIRE_ACTIVATED`, `UNIT_FAULT_EXCEEDED`, `SHIFT_ROTATED`, `MANUAL_MODE_TIMEOUT`, …).
- `src/infrastructure/bus/zbus_hvac_publisher.c:18-22` — `ZBUS_OBSERVERS(hvac_logger_listener, hvac_alarm_bridge_listener)`.
- `src/application/use_cases/process_hvac_control_use_case.c:160-200` — compara `prev_group_state`/`prev_unit_states` y publica.
- `src/infrastructure/observers/zbus_hvac_alarm_bridge.c:101` — listener real; el trabajo pesado se difiere con `k_work`.

**Evidencia nueva.**
- `app/src/application/zbus/hvac_channels.c:11-18` — `ZBUS_OBSERVERS_EMPTY`.
- `app/src/application/active_objects/ao_hvac.c:213-231` — publica un snapshot, sin comparación prev/next ni tipos de evento.

**Corrección propuesta.** Publicar eventos de transición (grupo y unidad) en un canal dedicado y conectar consumidores (buzzer, logger, SNMP/telemetría). Mantener el snapshot para estado periódico. Resolver D-06 con una decisión explícita antes de añadir consumidores.

---

### R-05 · Los temporizadores de enfriamiento no se reinician al salir de `COOLING`

**Severidad:** P2

**Síntoma.** Si el grupo entra en `MANUAL` con `cooling_timer_s = 900` y luego vuelve a `COOLING`, el primer timeout llega a los pocos segundos en lugar de a los 1800 s configurados; se incrementan fallos y se dispara el *rescue boost* antes de tiempo.

**Mecanismo.** En el nuevo `tick_1s`, el contador sólo se reinicia al expirar (`:316`) o en los caminos de `set_bypass_cause` (`:98`, `:109-110`) y baja temperatura (`:257-258`). El modo manual (`set_manual`) no toca ninguno. El legacy reiniciaba timer y contador en cualquier estado distinto de `COOLING/COOL_BOOST/BYPASS` (`src/domain/hvac/hvac_group_entity.c:336-340`).

**Evidencia nueva.** `app/src/domain/models/hvac_group_entity.c:304-342`.

**Corrección propuesta.** Reiniciar `cooling_timer_s`/`cooling_timeout_count` al entrar en `MANUAL`, `FIRE` o al salir de `COOLING/COOL_BOOST` por cualquier motivo. Test: manual en mitad de un ciclo y retorno.

---

### R-06 · `increment_fault()` perdió la guardia de estado

**Severidad:** P2 (con R-07 el conjunto pasa a P1)

**Síntoma.** Una unidad apagada, en `IDLE` o recién parada puede acumular fallos y terminar en `BLOCKED_ALARM`/`FAULT` por eventos que no ocurrieron mientras estaba funcionando.

**Mecanismo.** La guardia se movió fuera de la función: hoy sólo `update_alarms()` comprueba `RUNNING/STARTING` (`hvac_unit_entity.c:43-62`). `increment_fault()` (`:65-84`) incrementa siempre. Sus otros dos llamadores no comprueban estado: `ao_hvac.c:206` (error de actuador) y `hvac_group_entity.c:326-331` (primer timeout del grupo, sobre las unidades del turno activo, que pueden estar paradas o en `STARTING`).

**Evidencia legacy.** `src/domain/hvac/hvac_unit_entity.c:117-136` — la guardia `SMART_ON || STARTUP_DELAY || BLOCKED_ALARM` está dentro de la función, por lo que **todos** los llamadores quedan protegidos.

**Corrección propuesta.** Restaurar la guardia dentro de `increment_fault()` o añadir un parámetro de motivo (`ACTUATOR`, `PRESSURE`, `TIMEOUT`) con política explícita. Test: timeout de grupo con unidades en `OFF` no debe incrementar fallos.

---

### R-07 · Un error de actuador se contabiliza como fallo mecánico

**Severidad:** P1

**Síntoma.** Una congestión de la cola Modbus o un fallo transitorio de relé incrementa el contador de fallos de la máquina. Tras tres ciclos (3 s), todas las unidades activas quedan en `FAULT` permanente, aunque estén sanas. El operador ve "fallo de equipo" cuando el problema es de comunicaciones.

**Evidencia nueva.**
- `app/src/application/active_objects/ao_hvac.c:200-210` — en error: `actuator_fault_count++` **y** `hvac_unit_entity_increment_fault()`.
- `app/src/infrastructure/adapters/adapter_relay_modbus.c:51-61` — la caché local se actualiza **antes** de encolar y se devuelve `-ENOMSG` si la cola está llena.

**Evidencia legacy.** El retorno del driver se ignoraba (`src/application/use_cases/process_hvac_control_use_case.c:224-228`). El legacy no detectaba el fallo, pero tampoco castigaba la máquina.

**Corrección propuesta.** Separar telemetría de actuador (`actuator_fault_count`, alarma de comunicación) del contador mecánico (`fault_counter`); reintentar la escritura; no llevar a `FAULT` por error de bus. Si se quiere política de "unidad no responde", implementarla como estado de comunicación, no como fallo del compresor. Test: `fake_hvac_unit_set_errors(-EIO, 3)` no debe producir `UNIT_STATE_FAULT`.

---

### R-08 · `set_manual(true)` arranca todas las unidades sin escalonado

**Severidad:** P1

**Síntoma.** `hvac mode <g> off` y `hvac mode <g> fan_only` pueden emitir `ON` a todos los compresores del grupo antes de apagarlos; cualquier futura llamada a `ao_hvac_set_manual(true)` arranca todas las unidades a la vez, sin `delay_on_s` ni límite de turno activo.

**Mecanismo.** `hvac_group_entity_set_manual(true)` no se limita a marcar el grupo: recorre todas las unidades y llama `hvac_unit_entity_set_manual(unit, true)`, que las pone en `RUNNING` (`hvac_group_entity.c:164-169`, `hvac_unit_entity.c:223-241`). El facade de `mode off` hace `set_manual(true)` y **después**, en otra sección crítica, apaga las unidades (`app_facade.c:192-201`). Entre ambas, el hilo HVAC (prio 5) puede ejecutar un `step`; si ocurre, el adaptador ve el cambio `OFF → ON` y escribe los relés. La ventana es pequeña (el hilo HVAC sólo está dentro del `step` unos milisegundos por segundo), pero el patrón es incorrecto y se repetirá en cada comando.

**Evidencia legacy.** `src/domain/hvac/hvac_group_entity.c:217-241` — `set_manual(true)` sólo fija `is_manual_active` y `GROUP_STATE_MANUAL`; **no toca las unidades**. El arranque manual se hace unidad por unidad, explícitamente.

**Corrección propuesta.** No arrancar unidades desde el manual de grupo; si se necesita "manual con arranque", exponer una operación de unidad con escalonado y límite de turno. En el facade, apagar primero y liberar después, o hacer la transición bajo una única sección crítica con el estado de grupo ya fijado a `OFF/MANUAL`. Test: `ao_hvac_set_manual(true)` seguido de `ao_hvac_step()` no debe producir ningún `RELAY_STATE_CLOSED` en AA.

---

### R-09 · No hay retardo de estabilización al boot

**Severidad:** P2

**Síntoma.** La primera evaluación del grupo puede ejecutarse con métricas vacías (`valid_sensors == 0`) y disparar `SENSOR_FAULT` → `BYPASS` → `relay_sm = 0` (transferencia al termostato mecánico) durante el arranque. Si el primer ciclo del sensor falla (ADC/DMA aún no listo), el grupo queda en bypass hasta que llegue un snapshot válido.

**Evidencia legacy.** `src/system/hvac_scheduler.c:151-152` — `k_msleep(2000)` antes del primer ciclo, documentado como espera para ADC DMA, filtros y debounce.

**Evidencia nueva.**
- `app/src/infrastructure/di/composition_root.c:1279-1303` — arranca DI, luego HVAC, luego sensor.
- `ao_hvac.h:41` — primer `step` a los 1000 ms.
- `ao_sensor_matrix.h:42` — primer update del sensor a los 500 ms.
- `hvac_group_entity.c:195-201` — `valid_sensors == 0` → `SENSOR_FAULT`.

Hoy el orden suele salvar el arranque (500 ms < 1000 ms), pero es una dependencia de planificación no declarada, no una garantía.

**Corrección propuesta.** *Gate* de arranque: no evaluar hasta tener el primer snapshot válido (o N ciclos consecutivos), o tratar "sin datos todavía" como estado `INIT` sin decisión. Añadir test de arranque con snapshot vacío.

---

### R-10 · La topología grupo↔unidad no se reconstruye y las máscaras no se validan

**Severidad:** P1

**Síntoma.** `hvac config <grp> <turn> <mask>` informa éxito y el `status` muestra la nueva máscara, pero el control sigue actuando las unidades del boot. Con dos grupos y máscaras solapadas, la misma unidad recibe dos comandos por ciclo (gana el último grupo evaluado) y el validador lo acepta.

**Mecanismo.**
- El binding sólo existe en `ao_hvac_init()` (`ao_hvac.c:100-120`): `hvac_group_entity_add_unit()`.
- `ao_hvac_apply_config()` (`:466-491`) y `ao_hvac_set_group_config()` sólo copian parámetros; no desasignan/reasignan unidades.
- `app_facade_hvac_configure_group_units()` (`app_facade.c:370-395`) escribe `s_app->hvac.config` y llama al setter, pero la máscara no llega al `unit_count` del grupo.
- `config_hvac_is_valid()` (`hvac_types.h:331-381`) valida rango y `active_turn_count <= unidades asignadas`, pero **no** intersección entre grupos.

**Evidencia legacy.** La asignación era 1:1: `config->unit_group[HVAC_MAX_UNITS_TOTAL]` (`src/domain/config/config_value.h:87`, `config_defaults.c:94-101`) y el contenedor construía cada grupo a partir de esa tabla (`src/di/di_container.c:480-495`). Por construcción no podía haber solape.

**Corrección propuesta.** Implementar un `rebuild_topology()` que vacíe los grupos y reasigne según `unit_mask`, ejecutado al aplicar configuración (bajo el mutex y con parada segura previa); o prohibir explícitamente cambios de topología en caliente y exigir reboot. Validar intersección y pertenencia en `config_hvac_is_valid()`. Test: cambiar 0x0F → 0x30 y verificar que las unidades 4-5 se actúan y las 0-3 no; dos grupos con solape deben rechazarse.

---

### R-11 · *(No es regresión)* Rotación automática

**Severidad:** — · **Veredicto:** mejora a conservar.

El legacy sólo exponía `trigger_hvac_rotation_use_case` (`src/application/use_cases/trigger_hvac_rotation_use_case.c:15-43`); el scheduler **no** lo llamaba (`src/system/hvac_scheduler.c` no contiene `rotate`). La versión nueva rota automáticamente cada `rotation_interval_days` (`hvac_group_entity.c:349-355`) y lo prueba (`tests/unit/hvac/src/test_ao_hvac.c:750`). No debe "restaurarse" el comportamiento legacy.

---

### R-12 · Escritura remota asíncrona sin reintento y con caché optimista

**Severidad:** P2

**Síntoma.** Una escritura remota puede perderse (cola llena) y la caché del relay reporta el estado nuevo; no hay reintento ni confirmación. Combinado con R-07, una cola llena se convierte en fallo mecánico.

**Evidencia legacy.** `src/infrastructure/drivers/hvac_modbus_remote_adapter.c:16-32` — `MODBUS_RETRY_COUNT 3`, 50 ms entre intentos; `:34-67` escribe estado y G en dos registros, síncrono.

**Evidencia nueva.** `app/src/infrastructure/adapters/adapter_relay_modbus.c:51-61` — actualiza `cache[channel]` y `cache_valid` **antes** de `ao_modbus_post()`; si la cola está llena devuelve `-ENOMSG`.

**Corrección propuesta.** No publicar la caché hasta confirmar (o marcarla *pending* y reconciliar con la lectura FC01); reintentar a nivel de cola o de adaptador; exponer el error como alarma de comunicación. Test: cola llena → `get_state()` no debe devolver el estado no aplicado.

---

### R-13 · El modo manual dejó de ser un estado: arranque autónomo tras `off`

**Severidad:** P0

**Síntoma (determinista).** `hvac mode <g> off` ejecutado mientras el grupo está en `COOLING`:
1. `is_manual_active = true`, pero el estado del grupo **sigue siendo `COOLING`**.
2. El `tick_1s` continúa incrementando `cooling_timer_s`.
3. A los `time_to_cool_s` (1800 s por defecto) se dispara el **primer timeout**: incrementa fallos de las unidades del turno y ejecuta el *rescue boost*, que **arranca todas las unidades**.
4. Al segundo timeout, transfiere a termostato mecánico.
5. A los `manual_timeout_s` (3600 s) el flag manual se borra y el grupo vuelve a AUTO por sí solo.

Si el grupo estaba en `IDLE` al apagar, el efecto es sólo el punto 5: una hora después el equipo reanuda el control automático sin intervención.

**Mecanismo.**
- `hvac_group_entity_set_manual()` ya no fija `GROUP_STATE_MANUAL` (`:155-181`); sólo cambia el flag y arranca/para unidades.
- `evaluate()` en modo manual hace `return` sin fijar estado (`:249-252`).
- `tick_1s()` ejecuta el bloque de cooling sin comprobar `is_manual_active` (`:312-342`).
- Al expirar el timer, sólo hace `is_manual_active = false` (`:304-311`), sin liberar unidades ni cambiar estado.

**Evidencia legacy.**
- `src/domain/hvac/hvac_group_entity.c:217-228` — `set_manual(true)` fija `GROUP_STATE_MANUAL`.
- `:492-496` — `evaluate()` en manual fija `GROUP_STATE_MANUAL` y retorna, por lo que el bloque de cooling del `tick` no se ejecuta.
- `:341-346` — al expirar el timeout llama `hvac_group_entity_set_manual(self, false)`, que libera las unidades.

**Variante (bypass).** El legacy limpiaba `is_manual_active` al entrar en bypass (`:457-459`); el nuevo no. Si el manual estaba activo y aparece una causa de bypass, al limpiarse la causa el paso 6 vuelve a retornar y el grupo queda parado hasta que expire el timer.

**Reproducción propuesta (host, 100 % determinista).**

```c
s_group.cooling_timeout_s = 50U;
metrics = { .highest_temp = 26.0f, .valid_sensors = 2U };
hvac_group_entity_evaluate(&s_group, &metrics);   /* COOLING */
hvac_group_entity_set_manual(&s_group, true);     /* "mode off" */
for (int i = 0; i < 50; i++) hvac_group_entity_tick_1s(&s_group);
/* Hoy: state == COOL_BOOST y todas las unidades RUNNING/STARTING */
```

Hoy el test `test_cooling_timeout_and_rescue_boost` no cubre el modo manual, por lo que la suite pasa.

**Corrección propuesta.** Devolver `GROUP_STATE_MANUAL`/`GROUP_STATE_OFF` como estados reales (o añadir un estado `OFF` explícito para el producto); no ejecutar el bloque de cooling timeout en manual/off; al expirar el timer, llamar a la transición completa (`set_manual(false)`) y liberar unidades; limpiar el manual al entrar en bypass. Test obligatorio de "off durante cooling".

---

### R-14 · Las alarmas mecánicas de las unidades 4–7 no llegan al control

**Severidad:** P1

**Síntoma.** Las unidades 4–7 (las cuatro remotas por RS-485) nunca reciben sus alarmas de alta/baja presión, flujo o tensión. `hvac_unit_entity_update_alarms()` se llama para las 8 unidades, pero para las 4 últimas siempre con `false/false/false/false`. Una unidad remota puede estar en fallo eléctrico y el control la sigue arrancando.

**Mecanismo.** El agregador DI se inicializa con `default_config_get_di_profile()` → `hardware_pin_map_get_local_di()`, que devuelve **sólo** `k_brightc_local_di_table` (`HARDWARE_LOCAL_DI_COUNT = 24`, `app_defaults.h:277-282`, `hardware_pin_map.h:496-502`). Esa tabla contiene las alarmas de las unidades 0–3 (16 canales) + instalación + usuarios; los roles `DI_ROLE_UNIT_4_ALARM_*` … `DI_ROLE_UNIT_7_ALARM_*` y `DI_ROLE_BYPASS_SWITCH` están en `k_brightc_remote_di_table` (`hardware_pin_map.h:324-368`), que **nunca se registra**. `adapter_di_gpio_set_modbus_raw()` guarda el valor bruto del MBSL32DI en el adaptador, pero el agregador no tiene canales remotos que escanear, así que `di_aggregator_pack_snapshot()` publica `channel_count = 24` y el listener de HVAC (`composition_root.c:1115-1147`) itera 8 unidades sin encontrar los roles 4–7.

**Evidencia legacy.** `src/infrastructure/drivers/hvac_input_aggregator.c:24-49` leía `physical_alarms_1_16` del módulo remoto cuando estaba disponible (`g_remote_di_valid`) y, si no, de los DI locales; el caso de uso mapeaba 4 alarmas por unidad en `process_hvac_control_use_case.c:107-123`. El legacy alimentaba las 8 unidades.

**Nota de diseño.** Concatenar las dos tablas no es trivial: 3 roles están duplicados (`DI_ROLE_USER_1..3` aparecen en ambas) y `di_aggregator_get_state(role)` devolvería el primero. Hay que decidir el mapa definitivo (roles únicos) o registrar un segundo agregador para los 32 canales remotos.

**Reproducción propuesta.** Inyectar `raw_inputs = 0x1` en `adapter_di_gpio_set_modbus_raw()` (bit 0 = `UNIT_4_ALARM_HP`) y verificar que `on_hvac_di_feed_cb` deja `unit[4].high_pressure_alarm = true`. Hoy no ocurre porque el canal no existe en el snapshot.

**Corrección propuesta.** Registrar la topología completa (24 + 32) resolviendo duplicados, o añadir los 32 canales remotos al agregador cuando el MBSL32DI esté presente. Test con perfil de 56 canales que verifique alarmas en unidades 4–7 y el switch de bypass.

---

## 6. Deuda heredada (no imputable a la migración)

| Deuda | Legacy | Nuevo | Comentario |
| :-- | :-- | :-- | :-- |
| `set_manual(false)` de unidad borra `FAULT` y `BLOCKED_ALARM` | `hvac_unit_entity.c:404-422` | `hvac_unit_entity.c:223-241` | Ambos permiten salir de `FAULT` sin reset explícito. El nuevo además respeta `FAULT` al encender. |
| Alarmas de unidad *level-triggered* | `update_alarms()` 1 Hz | `update_alarms()` **50 Hz** | Un episodio produce un solo incremento (la primera llamada pasa a `BLOCKED_ALARM`), pero se hacen ~450 adquisiciones de mutex/s. |
| Sin watchdog de sensores obsoletos | no | no | Si el productor de sensores muere, `group_metrics` conserva el último valor válido y el control sigue decidiendo. |
| Deriva del período del lazo | `k_msleep(1000)` + trabajo | `k_msgq_get(1000)` + trabajo | El período real es 1 s + ejecución. Afecta a `time_to_cool_s` y `fan_delay_s`. |
| Sin detección de discrepancia de feedback | no | causa `DISCREPANCY` definida, sin escritor; `fb_fan`/`fb_compressor` se validan y no se usan | Función de producto pendiente en ambos. |
| Bypass remoto (HMI/Modbus/SNMP) | causa definida, sin escritor | ídem | Pendiente. |
| Contacto de fuego NO (no *fail-safe*) | `DIGITAL_IN_17` activo-alto, NO | `DI_ROLE_SMOKE_FIRE` activo-bajo, NO (`hardware_pin_map.h:227-236`) | Un cable cortado se lee como "sin fuego" en ambos. Riesgo residual de instalación, no regresión. |
| `hvac mode off` deja el grupo en un estado distinto de OFF | no existía `mode off` | R-13 | Se documenta como regresión; el legacy no tenía este comando. |

---

## 7. Mejoras de la versión nueva que deben conservarse

1. **Detección de cambio por unidad** antes de escribir relés (`adapter_hvac_actuator.c:71-73`) — resuelve el P0-02 de la auditoría previa (saturación del bus).
2. **Política de métrica por grupo** (`max`/`avg`/`min`, `hvac_group_entity.c:220-226`) — el legacy usaba siempre `highest_temp`.
3. **Rangos de sensores 1-based + máscara** (`hvac_compute_group_metrics_range`, `:446-...`) con validación (`hvac_types.h:300-302`).
4. **Rotación automática** cada N días (`:349-355`) y `active_turn_count` genérico (N+M) en lugar de tres esquemas fijos.
5. **Validación de configuración** (`config_hvac_is_valid`, `hvac_types.h:331-381`) y endpoints tipados (`hvac_io_endpoint_t`).
6. **Parada segura** (`ao_hvac_safe_state`, `:236-258`) y ciclo de vida del AO (`init/start/stop/deinit`).
7. **Sensores y DI desacoplados por Zbus** (documentos del 2026-11-07): el HVAC ya no depende del adaptador de sensores ni del agregador DI.

---

## 8. Diferencias de diseño deliberadas (no restaurar sin decisión)

| Diferencia | Legacy | Nuevo | Observación |
| :-- | :-- | :-- | :-- |
| Fallo al cruzar `T_alarm` | Incrementa el contador de fallos de **todas** las unidades (`hvac_group_entity.c:519-532`) | No incrementa | El legacy castigaba a la máquina por hacer calor; con 3 olas de calor dejaba unidades en `FAULT`. **No restaurar** sin decisión de producto. |
| `set_bypass_cause()` | Sólo modifica la máscara | Además cambia estado y actúa unidades (`:88-118`) | Mezcla *setter* de valor con transición de estado. Es la causa raíz de varios efectos sutiles (p. ej. la salida de bypass). Si se conserva, documentarlo y testearlo; si se separa, volver a un `evaluate()` que despache. |
| Orden `evaluate()` → `tick()` | `tick` → `evaluate` | `evaluate` → `tick` | Un `delay_on_s = 10` se consume en 9 ticks efectivos. Cosmético, pero conviene fijar el orden y testear el escalonado exacto. |
| Sensores: `valid_sensors == 0` | Continúa evaluando el resto | `return` inmediato | El nuevo no evalúa `HIGH_TEMP`/`ALL_FAULTS` con sensores inválidos. Aceptable; documentar. |
| `metrics == NULL` | `return` sin acción | `SENSOR_FAULT` | El nuevo es más conservador. |

---

## 9. Mapa con las auditorías previas

| ID previo | Tema | Estado en este documento |
| :-- | :-- | :-- |
| **P0-01** | Cadena de temperatura rota | Resuelto (CWT → `chan_sensor_snapshot` → `ao_hvac_feed_sensor_snapshot`). No se reabre. |
| **P0-02** | Sin detección de cambio en relés | Resuelto; se cita en R-01 como condición a preservar al reintroducir G. |
| **P1-06** | `run_fan` ignorado / `FAN_ONLY == OFF` | Reclasificado: la causa raíz es **R-01** (G fuera del contrato). |
| **P1-09 / D-06** | Zbus sin suscriptores | Reclasificado: **R-04** (se perdió el canal de eventos, no sólo los suscriptores). |
| **P1-03 / D-01** | `device_id` no multiplexa | Sin cambios; afecta a R-10/R-12 cuando se usen varios expansores. |
| P2-7 | Manejo de error de actuador | Implementado, pero con el efecto colateral **R-07**. |
| D-02 | Motor psicrométrico en pausa | Sin impacto en estas tarjetas. |
| D-03 | Datos JSY-MK-339 en RAM | Sin impacto en estas tarjetas. |

---

## 10. Plan de corrección propuesto

> **No ejecutado.** Sólo análisis. Cualquier cambio de código, build o flasheo requiere aprobación del operador.

### Fase 0 — Seguridad (antes de cualquier prueba en banco)

1. **R-13**: devolver `MANUAL`/`OFF` como estado de grupo; no ejecutar cooling timeout en manual; liberar unidades al expirar.
2. **R-02**: leer el switch físico y aplicar `BYPASS_CAUSE_PHYSICAL_SWITCH`.
3. **R-08**: eliminar el arranque de unidades desde `set_manual(true)`; el apagado no debe pasar por ON.
4. **R-14**: registrar los 32 canales remotos para que las alarmas de las unidades 4–7 lleguen al control (bloquea cualquier prueba con 8 unidades).

### Fase 1 — Control y recuperación

5. **R-03**: limpiar `COOLING_TIMEOUT` con `T ≤ Tmin` (o política equivalente) y test de recuperación.
6. **R-10**: rebuild de topología o prohibición explícita + validación de solape.
7. **R-07 + R-06**: separar fallo de actuador de fallo mecánico; restaurar guardia.
8. **R-01**: reintroducir G en el contrato y en el adaptador (incluye `fan_only`, `manual fan`, salida de bypass).
9. **R-04**: restaurar eventos y consumidores (buzzer/logger/telemetría); decidir D-06.

### Fase 2 — Robustez

10. **R-05**, **R-09**, **R-12**.
11. Cobertura: tests de recuperación de bypass, arranque sin sensores, manual durante cooling, topología en caliente, G en post-purga, error de bus sin `FAULT` y alarmas de las 8 unidades.

---

## 11. Cómo reproducir y verificar (propuestas, no ejecutadas)

```bash
# Host unit tests (no ejecutado en este análisis)
west twister -T tests/unit/hvac -p native_sim -v

# Build de la placa (no ejecutado)
west build -p -b brightc-controllerv3 app -d build/primary
```

Tests a añadir o extender:

| Tarjeta | Test propuesto |
| :-- | :-- |
| R-01 | `test_relay_g_post_purge_and_fan_only` (unit + adaptador con fake relay) |
| R-02 | `test_physical_bypass_switch_sets_cause` (listener DI → grupo en BYPASS) |
| R-03 | extender `test_cooling_timeout_and_rescue_boost` con recuperación a `T ≤ Tmin` |
| R-04 | `test_group_transition_publishes_event` (contar eventos por transición) |
| R-05 | `test_manual_does_not_accumulate_cooling_timer` |
| R-06 | `test_group_timeout_does_not_fault_idle_units` |
| R-07 | `test_actuator_error_does_not_enter_fault_state` |
| R-08 | `test_manual_off_never_energizes_compressor` |
| R-09 | `test_boot_without_sensor_snapshot_does_not_bypass` |
| R-10 | `test_hot_unit_mask_rebuilds_topology` + `test_overlapping_masks_rejected` |
| R-12 | `test_relay_queue_full_does_not_report_applied` |
| R-13 | `test_off_during_cooling_stays_off` (50 ticks) |
| R-14 | `test_remote_unit_alarms_reach_hvac` (snapshot de 56 canales) |

---

## 12. Preguntas abiertas para el operador

1. **Contrato de salida:** ¿se restaura el `struct` de tres relés (AA/SM/G) como tipo de dominio, o se mantiene el enum y se añade un comando separado de ventilador? Afecta a puertos, adaptadores y tests.
2. **`mode off`:** ¿debe ser un estado terminal (hasta una orden explícita) o admite timeout? El legacy no tenía `mode off`; el comportamiento nuevo actual (volver a AUTO a la hora) parece no deseado.
3. **Fallo de cruce de `T_alarm`:** ¿se conserva el contador de fallos del legacy o se elimina? (Recomendación: eliminarlo; castiga máquinas sanas por clima).
4. **Bypass físico:** ¿el switch es *toggle* mantenido o pulsador? Define si la causa se limpia sola o requiere rearme.
5. **Feedback `fb_fan`/`fb_compressor`:** ¿se implementará la detección de discrepancia (`BYPASS_CAUSE_DISCREPANCY`) en esta fase o queda como deuda?
6. **Escritura remota:** ¿se acepta la cola asíncrona sin confirmación (con reconciliación por FC01) o se exige escritura confirmada para el HVAC?

---

## Anexo A · Índice de evidencia

### Legacy

| Archivo | Líneas | Contenido |
| :-- | :-- | :-- |
| `src/domain/hvac/hvac_value.h` | 20-34, 63-75 | tri-state struct; tipos de evento |
| `src/domain/hvac/hvac_unit_entity.c` | 79-105, 117-136, 159-215, 404-422, 493-546 | alarmas, guardia de fallo, tick, manual, tri-state |
| `src/domain/hvac/hvac_group_entity.c` | 162-179, 217-241, 289-350, 401-594 | bypass sin efectos, manual, tick, evaluate |
| `src/application/use_cases/process_hvac_control_use_case.c` | 99-229 | ciclo completo y despacho |
| `src/system/hvac_scheduler.c` | 137-181 | retardo de boot y lazo 1 Hz |
| `src/infrastructure/drivers/hvac_input_aggregator.c` | 24-49 | alarmas, fuego, bypass |
| `src/infrastructure/drivers/hvac_modbus_remote_adapter.c` | 16-67 | reintentos y escritura |
| `src/infrastructure/bus/zbus_hvac_publisher.c` | 18-22 | canal con observadores |
| `src/domain/config/config_defaults.c` | 94-108 | `unit_group[]`, `group_sensor_mask` |
| `src/di/di_container.c` | 360-367, 480-495 | switch físico; binding unidad→grupo |

### Nuevo

| Archivo | Líneas | Contenido |
| :-- | :-- | :-- |
| `app/include/common/hvac_types.h` | 66-70, 100-107, 196-197, 331-381 | enum tri-state, causas, endpoints, validación |
| `app/src/domain/models/hvac_unit_entity.c` | 43-84, 175-241, 243-289 | alarmas, fallo sin guardia, stop/manual, tri-state/relays |
| `app/src/domain/models/hvac_group_entity.c` | 88-181, 183-355, 446-... | bypass con efectos, manual sin estado, evaluate/tick, métricas |
| `app/src/application/active_objects/ao_hvac.c` | 100-120, 181-232, 236-258, 331-361, 391-407, 466-491 | init/binding, step, safe state, feed, fuego, apply |
| `app/src/application/services/app_facade.c` | 165, 192-229, 308-337, 369-395 | status, modos, manual de unidad, topología |
| `app/src/application/zbus/hvac_channels.c` | 11-18 | canal sin observadores |
| `app/src/infrastructure/adapters/adapter_hvac_actuator.c` | 59-122 | mapeo tri-state → relés (G = 0) |
| `app/src/infrastructure/adapters/adapter_relay_modbus.c` | 30-64 | caché optimista y cola |
| `app/src/infrastructure/di/composition_root.c` | 663-681, 1083-1147, 1279-1303, 1734-1752 | perfil DI local, listener DI/fuego, orden de arranque, reconfiguración |
| `app/include/common/hardware_pin_map.h` | 44-52, 227-236, 324-368, 365, 496-516 | contadores, fuego, tabla remota, rol bypass, getters |
| `app/include/common/app_defaults.h` | 104-145, 277-282 | endpoints HVAC, perfil DI = sólo local |
| `app/src/application/active_objects/ao_digital_input.h` | 39 | período de escaneo 20 ms |
| `tests/unit/hvac/src/test_adapter_hvac_actuator.c` | 76-101 | test que codifica G = OPEN |
| `tests/unit/hvac/src/test_hvac_group.c` | 209-268 | timeout sin recuperación |

### Convenciones

- `fan_state`: `DISABLED` / `WAIT_DELAY` / `COMPRESSOR_RUNNING` / `SMART_IDLE_ON`.
- `relay_aa`: contactor del compresor. `relay_sm`: selector smart(1)/bypass(0). `relay_g`: ventilador.
- "Bypass" en este documento = `BYPASS_CAUSE_*` del grupo, no el switch físico, salvo que se indique.
