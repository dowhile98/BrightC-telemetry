# Revisión del subsistema de control HVAC — Pinout, Delays, Control de Ventilador (Relé G) y Secuencia de Control

- **Fecha:** 2026-11-09
- **Estado:** Diagnóstico completo y decisiones de diseño aprobadas.
- **Árbol actual:** `BrightC-telemetry` @ `eecaab0` (`feat(hvac): add test temperature override and related commands`).
- **Referencias cotejadas:**
  1. **Especificación de arquitectura:** `doc/architecture/hvac_control_subsystem.md`
  2. **Referencia 1 (Firmware legacy estable ThreadX/STM32Cube):** `/media/tecna/disk11/linux/github/BrightC-V3/Firmware/BrightC_ETH_CycloneTCP_FileX`
  3. **Referencia 2 (Primera migración a Zephyr con control funcional):** `/media/tecna/disk11/linux/Trabajo/Reference/brightc_firmware`
- **Documentos relacionados:**
  - [`2026-11-07-hvac-legacy-parity.md`](./2026-11-07-hvac-legacy-parity.md)
  - [`../architecture/hvac_control_subsystem.md`](../architecture/hvac_control_subsystem.md)

---

## Rúbrica de severidad

| Nivel | Significado |
| :-- | :-- |
| **P0** | Bloqueante en hardware: impide que el sistema controle en modo automático, apaga compresores activos cuando debería sumar apoyo o invierte el control físico del ventilador. |
| **P1** | Alto: rompe la secuencia térmica (`COOL_BOOST`), el escalonado de arranque (`delay_on_s`), el control/post-purga del ventilador (`enable_fan_delay` / `fan_delay_s`) o la señalización de alarma. |
| **P2** | Medio: inconsistencia en tablas de mapeo o perfiles de configuración secundarios. |

---

## 1. Resumen ejecutivo

Tras la revisión cruzada entre los dos proyectos de referencia y `BrightC-telemetry` (`eecaab0`), se identificaron las causas raíz por las cuales el subsistema HVAC no ejecuta los retardos (`delay_on_s` y `fan_delay_s`), no sigue la secuencia de control térmica, y no gobierna correctamente el relé `G` (ventilador).

| ID | Categoría | Hallazgo | Severidad |
| :-- | :-- | :-- | :-- |
| **H-01** | Pinout / DI | **Doble inversión lógica en `sw_bypass` (`PD11`) y `sw0` (`PD15`)**: `GPIO_ACTIVE_LOW` en DTS + `gpio_pin_get_dt()` + `DI_ACTIVE_LOW` en `k_brightc_system_di_table` provoca que el switch abierto (`3.3V`) se lea como `DI_STATE_ACTIVE`, forzando `GROUP_STATE_BYPASS` permanentemente. | **P0** |
| **H-02** | Secuencia | **`hvac_unit_entity_increment_fault()` fuerza `UNIT_STATE_BLOCKED_ALARM` (180 s)**: al vencer el 1er timeout de enfriamiento (`cooling_timeout_s`), el grupo llama a `increment_fault()` sobre las unidades del turno activo y las **apaga y bloquea** en lugar de mantenerlas encendidas y sumar las de reserva en `COOL_BOOST`. | **P0** |
| **H-03** | Secuencia | **`GROUP_STATE_COOL_BOOST` carece de histéresis de retención**: al entrar por `ctrl_temp >= temp_alarm` (`30.0 °C`), en cuanto la temperatura baja `0.1 °C` (`29.9 °C`), la condición deja de cumplirse y regresa a `GROUP_STATE_COOLING`, apagando prematuramente los aires de apoyo antes de llegar a `temp_min`. | **P0** |
| **H-04** | Delay | **Escalón `starting_delay_step` ignora unidades en `UNIT_STATE_STARTING`**: cuando una unidad de respaldo o de `COOL_BOOST` entra mientras otra unidad aún está en cuenta regresiva (`STARTING`), recibe `starting_delay_step = 0` (`delay = 0 s`) y arranca instantáneamente sin escalonamiento. | **P1** |
| **H-05** | Delay | **Orden invertido `evaluate()` → `tick_1s()` en `ao_hvac_step()`**: descuenta 1 segundo en el mismo ciclo en que se asignan `startup_timer_s` y `fan_delay_timer_s`, y retrasa 1 ciclo la reacción a vencimientos de timers. | **P1** |
| **H-06** | Ventilador (`Relay G`) | **Control de ventilador (`enable_fan_delay` / `relay_g`) roto en 7 puntos**: (1) configurado en `ON` (`1`) por defecto en vez de `OFF` (`0` = ventilador siempre habilitado); (2) cuando se activa (`1`), las unidades en reposo `OFF` quedan en `FAN_STATE_DISABLED` (`relay_g = 0`, ventilador encendido); (3) parada desde `STARTING` ejecuta post-purga sin haber encendido el compresor; (4) en `BLOCKED_ALARM`/`FAULT` pone `relay_g = 0` (enciende el ventilador); (5) en `FIRE` no fuerza `relay_g = 1` (no corta el ventilador); (6) `FAN_ONLY` y `manual fan_on/fan_off` tienen la lógica física invertida; (7) falta comando CLI/facade para configurar `enable_fan_delay` y `fan_delay_s`. | **P0 / P1** |
| **H-07** | Secuencia | **Salida prematura de `BYPASS_CAUSE_HIGH_TEMP` en `temp_max` en vez de `temp_min`**: el grupo sale de Bypass por alta temperatura al cruzar `temp_max` (`28 °C`) en vez de esperar a que todas las temperaturas caigan por debajo de `temp_min` (`26 °C`) como en Ref 1 y Ref 2. | **P1** |
| **H-08** | Pinout / Relé | **Polaridad invertida en `RELAY_CH_ALARM_OUT_1` (Alarma de Bypass, `IO4 GPB0`)** y falta de campo configurable NO/NC en EEPROM. | **P1** |
| **H-09** | Pinout / Config | **Desfase de canales en `k_brightc_hvac_hw_table` (`hardware_pin_map.h`)** (usa pines físicos MCP23S17 en lugar de `relay_channel_t` `0..11`) y `.units[]` vacío en `k_default_hvac_config_control_only_8units`. | **P2** |

---

## 2. Cotejo de Pinout y Mapeo de Hardware (Ref 1 vs Ref 2 vs Actual)

### 2.1 Tabla comparativa de pines físicos

| Función / Canal | Ref 1 (`BrightC_ETH_CycloneTCP_FileX`) | Ref 2 (`brightc_firmware`) | Actual (`BrightC-telemetry`) | Veredicto |
| :--- | :--- | :--- | :--- | :--- |
| **SPI1 CS Expansores** | `IO1=PG5`, `IO2=PG4`, `IO3=PG2`, `IO4=PD10` (`Core/Inc/main.h:102-121`) | `mcp23s17_0..3`: `PG5`, `PG4`, `PG2`, `PD10` (`brightc_h743zi.dts:261-321`) | `mcp23s17_0..3`: `PG5`, `PG4`, `PG2`, `PD10` (`brightc-controllerv3.dts:276-343`) | **OK** |
| **DI 1..12 (`IO1` / `mcp23s17_0`)** | `GPA0..GPA7` (0..7), `GPB0..GPB3` (8..11) (`brightc_io_manager_thread_entry.cpp:47-61`) | Pines `0..11`, `GPIO_ACTIVE_HIGH` + inversión en agregador | Pines `0..11`, lectura cruda `gpio_port_get_raw()` + `DI_ACTIVE_LOW` (`adapter_di_gpio.c:82-108`) | **OK** (1 sola inversión) |
| **DI 13..24 (`IO2` / `mcp23s17_1`)** | `GPA0..GPA7` (0..7), `GPB0..GPB3` (8..11) (`brightc_io_manager_thread_entry.cpp:62-76`) | Pines `0..11`, `GPIO_ACTIVE_HIGH` + inversión en agregador | Pines `0..11`, lectura cruda `gpio_port_get_raw()` + `DI_ACTIVE_LOW` (`adapter_di_gpio.c:82-108`) | **OK** (1 sola inversión) |
| **AA1 Relés (`IO3` / `mcp23s17_2`)** | `G=GPB0(8)`, `AA=GPB1(9)`, `SM=GPB2(10)` (`brightc_io_manager_thread_entry.cpp:28-32`) | `G=8`, `AA=9`, `SM=10` (`brightc_h743zi.dts:57-59`) | `RELAY_CH_AC1_AA=0`→pin 9, `SM=1`→pin 10, `G=2`→pin 8 (`adapter_relay_gpio.c:26-28`) | **OK** en `app_defaults.h`, **Defectuoso** en `hardware_pin_map.h` (**H-09**) |
| **AA2 Relés (`IO3` / `mcp23s17_2`)** | `G=GPB3(11)`, `AA=GPB4(12)`, `SM=GPB5(13)` (`brightc_io_manager_thread_entry.cpp:33-35`) | `G=11`, `AA=12`, `SM=13` (`brightc_h743zi.dts:61-63`) | `RELAY_CH_AC2_AA=3`→pin 12, `SM=4`→pin 13, `G=5`→pin 11 (`adapter_relay_gpio.c:29-31`) | **OK** en `app_defaults.h`, **Defectuoso** en `hardware_pin_map.h` (**H-09**) |
| **AA3 Relés (`IO3` / `mcp23s17_2`)** | `G=GPB6(14)`, `AA=GPB7(15)`, `SM=GPA0(0)` (`brightc_io_manager_thread_entry.cpp:36-38`) | `G=14`, `AA=15`, `SM=0` (`brightc_h743zi.dts:65-67`) | `RELAY_CH_AC3_AA=6`→pin 15, `SM=7`→pin 0, `G=8`→pin 14 (`adapter_relay_gpio.c:32-34`) | **OK** en `app_defaults.h`, **Defectuoso** en `hardware_pin_map.h` (**H-09**) |
| **AA4 Relés (`IO3` / `mcp23s17_2`)** | `G=GPA1(1)`, `AA=GPA2(2)`, `SM=GPA3(3)` (`brightc_io_manager_thread_entry.cpp:39-41`) | `G=1`, `AA=2`, `SM=3` (`brightc_h743zi.dts:69-71`) | `RELAY_CH_AC4_AA=9`→pin 2, `SM=10`→pin 3, `G=11`→pin 1 (`adapter_relay_gpio.c:35-37`) | **OK** en `app_defaults.h`, **Defectuoso** en `hardware_pin_map.h` (**H-09**) |
| **Alarm Out 1..6 (`IO4` / `mcp23s17_3`)** | `GPB0..GPB5` (8..13), `0` (LOW) en Bypass, `1` (HIGH) en Smart (`brightc_io_manager_thread_entry.cpp:395-427`) | `GPB0..GPB5` (8..13), `0` en Bypass, `1` en Smart (`hvac_local_gpio_adapter.c:158`) | `RELAY_CH_ALARM_OUT_1..6` (12..17)→pines 8..13; escribe `1` en Bypass y `0` en Smart (`adapter_hvac_actuator.c:127-129`) | **Polaridad invertida** (**H-08**) |
| **Switch Bypass (`PD11`)** | `GPIOD 11`, activo en bajo (`0V` = Bypass) (`main.h:118-119`) | `GPIO_ACTIVE_HIGH` en DTS + `ACTIVE_LOW` en DI (`brightc_h743zi.dts:133`, `di_container.c:363`) | `GPIO_ACTIVE_LOW` en DTS + `gpio_pin_get_dt()` + `DI_ACTIVE_LOW` en tabla DI (`brightc-controllerv3.dts:183`, `hardware_pin_map.h:553`) | **Doble inversión** (**H-01**) |
| **User Button (`PD15`)** | `GPIOD 15`, activo en bajo (`main.h:122-123`) | `GPIO_ACTIVE_HIGH` en DTS + `ACTIVE_LOW` en DI (`brightc_h743zi.dts:129`) | `GPIO_ACTIVE_LOW` en DTS + `gpio_pin_get_dt()` + `DI_ACTIVE_LOW` en tabla DI (`brightc-controllerv3.dts:178`, `hardware_pin_map.h:543`) | **Doble inversión** (**H-01**) |

---

## 3. Análisis comparativo profundo del Control de Ventilador (`Relé G` / `H-06`)

### 3.1 Lógica eléctrica del Relé `G` en la tarjeta BrightC
En el hardware de la tarjeta, el contacto **Normalmente Cerrado (NC)** del relé `G` está en serie con la señal `G` (ventilador) del termostato de cada unidad de aire acondicionado:
- **`relay_g = 0` (`LOW` / bobina desenergizada / `RELAY_STATE_OPEN`)**: el contacto NC permanece cerrado → **Ventilador HABILITADO / ENCENDIDO**.
- **`relay_g = 1` (`HIGH` / bobina energizada / `RELAY_STATE_CLOSED`)**: el contacto NC se abre → **Ventilador CORTADO / APAGADO**.

### 3.2 Cómo funciona en Referencia 1 (`BrightC_ETH_CycloneTCP_FileX`)
- **Configuración (`brightc_system_init_thread.c:118`)**:
  - `brightc.conf.fan = 0;` (**OFF por defecto**). En la interfaz web (`brightc_http_server_callbacks.c:346, 408`) se denomina `"controlVentilador"` (`fanCtrl`) y en la pantalla HMI (`brightc_hmi_thread_entry.c:268`) `HMI_FAN_CONTROL_ADDR`.
- **Actuación (`brightc_io_manager_thread_entry.cpp:399-434` y `brightc_hvac_ll_cotrol_thread_entry.c:45-178`)**:
  ```cpp
  case BRIGHTC_AA_BYPASS:
      //G(ON) SM(OFF) AA(OFF)
      IO3.write1(brightcAA.G[number], LOW);   // 0 -> Ventilador habilitado
      IO3.write1(brightcAA.SM[number], LOW);
      IO3.write1(brightcAA.AA[number], LOW);
      break;
  case BRIGHTC_AA_SMART_OFF:
      //G(OFF) SM(ON) AA(OFF)
      if ((fire == 1) || (brightc.conf.fan)) {
          IO3.write1(brightcAA.G[number], HIGH); // 1 -> Apaga/corta el ventilador
      } else {
          IO3.write1(brightcAA.G[number], LOW);  // 0 -> Ventilador siempre habilitado
      }
      IO3.write1(brightcAA.SM[number], HIGH);
      IO3.write1(brightcAA.AA[number], LOW);
      break;
  case BRIGHTC_AA_SMART_ON:
      //G(ON) SM(ON) AA(ON)
      IO3.write1(brightcAA.G[number], LOW);   // 0 -> Ventilador encendido con el compresor
      IO3.write1(brightcAA.SM[number], HIGH);
      IO3.write1(brightcAA.AA[number], HIGH);
      break;
  ```
- **Resumen del comportamiento en Ref 1**:
  1. Cuando `brightc.conf.fan == 0` (**Control de ventilador en OFF, valor por defecto**): el relé `G` se mantiene en `LOW` (`0`) tanto en `SMART_ON` como en `SMART_OFF` y `BYPASS` → **el ventilador siempre está habilitado**. Sólo si ocurre una alarma de incendio (`fire == 1`), `G` pasa a `HIGH` (`1`) para cortar el ventilador.
  2. Cuando `brightc.conf.fan == 1` (**Control de ventilador en ON**): mientras el aire está encendido (`SMART_ON`), `G = LOW` (`0`, ventilador encendido); cuando el aire no está encendido (`SMART_OFF`, ya sea por descanso, por `T <= Tmin` o por bloqueo de alarma), `G = HIGH` (`1`) → **se energiza el relé `G` para apagar el ventilador**.

### 3.3 Cómo se implementó en Referencia 2 (`brightc_firmware`)
En `brightc_firmware/src/domain/hvac/hvac_unit_entity.c`:
- El parámetro `brightc.conf.fan` pasó a llamarse `enable_fan_delay` y se le añadió un temporizador de post-purga `fan_delay_s` (30 s):
  - Con `enable_fan_delay == false` (`0`): `FAN_STATE_DISABLED` → `tri.relay_g = false` (`0`) siempre (ventilador siempre habilitado).
  - Con `enable_fan_delay == true` (`1`):
    - Aire encendido (`UNIT_STATE_SMART_ON`): `FAN_STATE_COMPRESSOR_RUNNING` → `tri.relay_g = false` (`0`, ventilador encendido).
    - Aire recién apagado (`SMART_ON -> SMART_OFF`): `FAN_STATE_WAIT_DELAY` durante `fan_delay_s` (30 s) → `tri.relay_g = false` (`0`, mantiene el ventilador encendido 30 s para evacuar el frío residual del evaporador).
    - Aire en reposo (`SMART_OFF` tras expirar `fan_delay_s` o desde el arranque): `FAN_STATE_SMART_IDLE_ON` → `tri.relay_g = true` (`1`, **energiza el relé `G` para apagar el ventilador** mientras el aire está apagado).
- **Defectos que ya tenía Ref 2**:
  1. En `config_defaults.c:86` dejó `enable_fan_delay = 1` por defecto en lugar de `0`.
  2. En `hvac_unit_entity_get_tri_state()` (`:527-531`) puso `tri.relay_g = false` (`0`) para `UNIT_STATE_FAULT` y `UNIT_STATE_BLOCKED_ALARM`, lo cual **enciende el ventilador** cuando una unidad se apaga por alarma o fallo permanente.
  3. En `FAN_STATE_FIRE` puso `relay_g = 0` en lugar de `1`, dejando el ventilador encendido en caso de incendio.

### 3.4 Los 7 defectos del Control de Ventilador (`Relé G`) en `BrightC-telemetry` (`H-06`)

1. **H-06a · Valor por defecto en `ON` (`1`) en vez de `OFF` (`0`)**:
   - En `app/include/common/app_defaults.h:129, 208` y `app/src/domain/models/hvac_group_entity.c:37`, `enable_fan_delay` está inicializado en `1` (`true`).
   - **Corrección**: Cambiar el valor por defecto a `0` (`false`) en `app_defaults.h` y `hvac_group_entity_init()` para que por defecto el ventilador esté siempre habilitado (`relay_g = 0`).

2. **H-06b · Cuando se habilita el control (`enable_fan_delay = true`), las unidades en `UNIT_STATE_OFF` nunca apagan su ventilador (`relay_g` queda en `0`)**:
   - En `hvac_unit_entity_init()` (`hvac_unit_entity.c:35`), `self->fan_state = HVAC_FAN_STATE_DISABLED`.
   - En `hvac_unit_entity_set_config()` (`:42-63`) y `hvac_group_entity_set_config()` (`:73-76`), al configurar `enable_fan_delay = true`, **no se actualiza `self->fan_state`**.
   - Y en `hvac_unit_entity_request_stop()` (`:209-211`), cuando la unidad ya está en `UNIT_STATE_OFF` (o sale de `UNIT_STATE_BYPASS` en `:187-193`), se eliminó la transición `self->fan_state = HVAC_FAN_STATE_SMART_IDLE_ON`.
   - **Resultado**: Las unidades en descanso o en `IDLE` permanecen con `fan_state = HVAC_FAN_STATE_DISABLED` (`relay_g = false`) y **nunca apagan el ventilador** hasta que alguna vez lleguen a encender el compresor y luego apagarse.
   - **Corrección**: Cuando `enable_fan_delay == true` y la unidad está en `UNIT_STATE_OFF` (en `init`, `set_config`, `request_stop` y al salir de `BYPASS` o `BLOCKED_ALARM`), fijar `self->fan_state = HVAC_FAN_STATE_SMART_IDLE_ON` (`relay_g = true`, ventilador apagado).

3. **H-06c · Estado del ventilador durante `UNIT_STATE_STARTING` y parada desde `STARTING`**:
   - Mientras una unidad está en `UNIT_STATE_STARTING` (esperando su `delay_on_s` antes de meter el compresor), el aire acondicionado sigue apagado (`relay_aa = 0`). Si `enable_fan_delay == true`, el ventilador debe permanecer apagado (`HVAC_FAN_STATE_SMART_IDLE_ON`, `relay_g = true`) hasta que el compresor arranque (`UNIT_STATE_RUNNING` → `HVAC_FAN_STATE_COMPRESSOR_RUNNING`, `relay_g = false`), o bien si se cancela el arranque (`request_stop` desde `UNIT_STATE_STARTING`), pasar directamente a `UNIT_STATE_OFF` con `HVAC_FAN_STATE_SMART_IDLE_ON` **sin** ejecutar los 30 s de `UNIT_STATE_STOPPING` (`WAIT_DELAY`), ya que el compresor nunca encendió.

4. **H-06d · Unidades en `UNIT_STATE_BLOCKED_ALARM` o `UNIT_STATE_FAULT` dejan el ventilador encendido**:
   - En `hvac_unit_entity_get_tri_state()` (`hvac_unit_entity.c:254-257`), `UNIT_STATE_FAULT` y `UNIT_STATE_BLOCKED_ALARM` fuerzan `tri.relay_g = false` (`0` = ventilador encendido).
   - **Corrección**: En `UNIT_STATE_BLOCKED_ALARM` y `UNIT_STATE_FAULT` (donde `relay_sm = true` y `relay_aa = false`), si `self->enable_fan_delay` está activo, `tri.relay_g` debe ser `true` (`1` = ventilador apagado), igual que en `BRIGHTC_AA_SMART_OFF` de Ref 1. En `UNIT_STATE_BYPASS` (`relay_sm = false`), `tri.relay_g` sí debe ser `false` (`0`).

5. **H-06e · En Alarma de Incendio (`GROUP_STATE_FIRE`), debe cortarse el ventilador (`relay_g = true`) inmediatamente**:
   - En Ref 1 (`brightc_io_manager_thread_entry.cpp:414`), `if ((fire == 1) || (brightc.conf.fan))` pone `G = HIGH (1)` inmediatamente para apagar el ventilador en caso de incendio sin importar el valor de `conf.fan` y sin esperar `fan_delay_s`.
   - En `BrightC-telemetry`, `HVAC_FAN_STATE_FIRE` existe en `hvac_types.h:59` pero nunca se asigna ni se evalúa en `get_tri_state()`.
   - **Corrección**: Cuando el grupo entra en `GROUP_STATE_FIRE` (`hvac_group_entity_set_fire(true)` o en `evaluate()`), poner las unidades en `UNIT_STATE_OFF` con `fan_state = HVAC_FAN_STATE_FIRE` y `fan_delay_timer_s = 0`, y en `hvac_unit_entity_get_tri_state()` retornar `tri.relay_g = true` cuando `self->fan_state == HVAC_FAN_STATE_FIRE` (cortando el ventilador de inmediato aunque `enable_fan_delay` sea `false`).

6. **H-06f · Lógica física invertida en `HVAC_MODE_FAN_ONLY` y `app_facade_hvac_set_unit_manual()` (`app_facade.c:210-222, 320-324`)**:
   - En `app_facade.c:218` y `:320-324`:
     - Cuando se pide `run_fan = true` (`fan_on` / `FAN_ONLY`), asigna `HVAC_FAN_STATE_SMART_IDLE_ON` (`relay_g = 1` → **abre el relé NC y APAGA el ventilador**).
     - Cuando se pide `run_fan = false` (`fan_off`), asigna `HVAC_FAN_STATE_DISABLED` (`relay_g = 0` → **cierra el relé NC y ENCIENDE el ventilador**).
   - **Corrección**: Invertir la asignación en modo manual/fan_only:
     - Para **encender** el ventilador con compresor apagado (`run_fan == true` o `HVAC_MODE_FAN_ONLY`): `relay_g` debe ser `false` (`0`, desenergizado → ventilador encendido), usando `HVAC_FAN_STATE_COMPRESSOR_RUNNING` / `HVAC_FAN_STATE_DISABLED`.
     - Para **apagar** el ventilador con compresor apagado (`run_fan == false`, o `HVAC_MODE_OFF` cuando `enable_fan_delay == true`): `relay_g` debe ser `true` (`1`, energizado → ventilador apagado), usando `HVAC_FAN_STATE_SMART_IDLE_ON`.

7. **H-06g · Falta de interfaz en Facade/Shell para configurar el control de ventilador (`enable_fan_delay` y `fan_delay_s`)**:
   - Añadir `app_facade_hvac_set_group_fan_control(uint8_t group_id, bool enable, uint16_t fan_delay_s)` con persistencia en EEPROM (`CONFIG_DOMAIN_HVAC`) y subcomando shell `hvac fan_ctrl <grp> <on|off> [delay_s]`, además de mostrar `FanCtrl: ON/OFF (delay Xs)` en `hvac status`.

### 3.5 Tabla de verdad física corregida de los 3 relés (`AA`, `SM`, `G`)

| Estado Operativo de la Unidad | `enable_fan_delay` (Control Ventilador) | `relay_sm` (Pin MCP) | `relay_aa` (Pin MCP) | `relay_g` (Pin MCP) | Estado Físico del Equipo |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **`BYPASS`** | `0` o `1` | `0` (LOW) | `0` (LOW) | `0` (LOW) | Termostato mecánico gobierna compresor y ventilador |
| **`RUNNING` / `MANUAL (comp_on)`** | `0` o `1` | `1` (HIGH) | `1` (HIGH) | `0` (LOW) | Modo Smart: Compresor **ON**, Ventilador **ON** |
| **`OFF` / `STARTING` / `BLOCKED_ALARM` / `FAULT`** | **`0` (OFF - Default)** | `1` (HIGH) | `0` (LOW) | **`0` (LOW)** | Modo Smart: Compresor **OFF**, Ventilador **SIEMPRE HABILITADO (ON)** |
| **`STOPPING` (`WAIT_DELAY`, post-purga)** | **`1` (ON)** | `1` (HIGH) | `0` (LOW) | **`0` (LOW)** | Modo Smart: Compresor **OFF**, Ventilador **ON** durante `fan_delay_s` |
| **`OFF` / `STARTING` / `BLOCKED_ALARM` / `FAULT`** | **`1` (ON)** | `1` (HIGH) | `0` (LOW) | **`1` (HIGH)** | Modo Smart: Compresor **OFF**, Relé G energizado → **Ventilador APAGADO** |
| **`FIRE` (Interlock de Incendio)** | `0` o `1` | `1` (HIGH) | `0` (LOW) | **`1` (HIGH)** | Emergencia Incendio: Compresor **OFF**, Relé G energizado → **Ventilador APAGADO** |

---

## 4. Detalle técnico del resto de hallazgos (`H-01` a `H-05`, `H-07` a `H-09`)

### H-01 · Doble inversión lógica en `sw_bypass` (`PD11`) y `sw0` (`PD15`) bloquea el control en `GROUP_STATE_BYPASS`

- **Severidad:** P0
- **Archivos afectados:**
  - `boards/daecge/brightc-controllerv3/brightc-controllerv3.dts:176-185`
  - `app/src/infrastructure/adapters/adapter_di_gpio.c:123-139`
  - `app/include/common/hardware_pin_map.h:540-558`
  - `app/src/domain/services/digital_input_aggregator.c:54-61, 122`
- **Causa raíz:**
  1. En `brightc-controllerv3.dts:183`, `sw_bypass` declara `gpios = <&gpiod 11 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;`.
  2. En `adapter_di_gpio.c:132`, `gpio_pin_get_dt(spec)` invierte el nivel físico por primera vez (`3.3V` → `0`).
  3. En `hardware_pin_map.h:553`, `k_brightc_system_di_table[25]` tiene `.active_level = DI_ACTIVE_LOW`, por lo que `di_normalize_raw_level()` invierte el valor por segunda vez (`0` → `true` = `DI_STATE_ACTIVE`).
- **Corrección:** En `adapter_di_gpio.c:132`, leer el nivel físico crudo con `gpio_pin_get(spec->port, spec->pin)` para que `di_normalize_raw_level()` realice una única inversión, igual que con `gpio_port_get_raw()` en los expansores MCP23S17.

---

### H-02 · `hvac_unit_entity_increment_fault()` fuerza `UNIT_STATE_BLOCKED_ALARM` y apaga las unidades activas en el 1er Timeout de Enfriamiento

- **Severidad:** P0
- **Archivos afectados:**
  - `app/src/domain/models/hvac_unit_entity.c:65-90, 92-120`
  - `app/src/domain/models/hvac_group_entity.c:416-435`
- **Causa raíz:**
  `hvac_unit_entity_increment_fault()` fue modificado para poner `self->state = UNIT_STATE_BLOCKED_ALARM` cuando `fault_counter < max_allowed_faults`. Al ocurrir el 1er timeout de enfriamiento (`cooling_timeout_count == 1`), `tick_1s()` llama a `increment_fault()` en las unidades del turno activo, apagándolas y bloqueándolas por 180 s en lugar de dejarlas corriendo y sumar las unidades de reserva en `GROUP_STATE_COOL_BOOST`.
- **Corrección:**
  - En `hvac_unit_entity_increment_fault()`, sólo incrementar `fault_counter` (con guardia de estado activo) y transicionar a `UNIT_STATE_FAULT` únicamente si `fault_counter >= max_allowed_faults`.
  - Mantener la transición a `UNIT_STATE_BLOCKED_ALARM` (`retry_timer_s = pressure_retry_s`) exclusivamente en `hvac_unit_entity_update_alarms()`.

---

### H-03 · `GROUP_STATE_COOL_BOOST` carece de histéresis de retención al bajar de `temp_alarm`

- **Severidad:** P0
- **Archivos afectados:**
  - `app/src/domain/models/hvac_group_entity.c:333-355`
- **Causa raíz:**
  La condición `else if ((ctrl_temp >= self->temp_alarm) || (self->cooling_timeout_count == 1U))` no incluye `|| (self->state == GROUP_STATE_COOL_BOOST)`. Al bajar la temperatura `0.1 °C` por debajo de `temp_alarm` (ej. `29.9 °C`), el grupo cae a `GROUP_STATE_COOLING` y apaga las unidades de apoyo antes de llegar a `temp_min`.
- **Corrección:** Mantener `GROUP_STATE_COOL_BOOST` activo (`|| (self->state == GROUP_STATE_COOL_BOOST)`) hasta alcanzar la condición de parada `metrics->highest_temp <= self->temp_min`.

---

### H-04 · El cálculo de `starting_delay_step` no escalona unidades detrás de unidades en `UNIT_STATE_STARTING` (Opción A)

- **Severidad:** P1
- **Archivos afectados:**
  - `app/src/domain/models/hvac_group_entity.c:335-378, 429-434`
- **Causa raíz:**
  `starting_delay_step` no se incrementa cuando encuentra una unidad en `UNIT_STATE_STARTING`, por lo que cualquier unidad nueva que entre (por sustitución o `COOL_BOOST`) recibe `delay = 0 s` y arranca inmediatamente solapándose con las que están esperando su `startup_timer_s`.
- **Corrección (Opción A aprobada):**
  - La 1ª unidad en arrancar desde reposo recibe `0 * delay_on_s = 0 s`, la 2ª `1 * delay_on_s`, etc.
  - Pre-calcular `starting_delay_step` contando cuántas unidades del grupo están actualmente en `UNIT_STATE_STARTING`, de modo que toda nueva unidad que se sume reciba `(N_starting + step) * delay_on_s`.

---

### H-05 · Orden invertido `evaluate()` antes de `tick_1s()` en `ao_hvac_step()`

- **Severidad:** P1
- **Archivos afectados:**
  - `app/src/application/active_objects/ao_hvac.c:260-269`
- **Corrección:** Ejecutar `hvac_group_entity_tick_1s()` **antes** de `hvac_group_entity_evaluate()` en cada ciclo de 1 Hz.

---

### H-07 · Salida de Bypass cuando todas las temperaturas caigan por debajo de `temp_min`

- **Severidad:** P1
- **Archivos afectados:**
  - `app/src/domain/models/hvac_group_entity.c:285-332`
- **Corrección (Decisión aprobada):**
  Exigir `metrics->highest_temp <= self->temp_min` (todas las temperaturas válidas del grupo `<= temp_min`) tanto para limpiar `BYPASS_CAUSE_HIGH_TEMP` y `BYPASS_CAUSE_COOLING_TIMEOUT` (rotando el turno al recuperar) como para detener el enfriamiento en `GROUP_STATE_COOLING` / `GROUP_STATE_COOL_BOOST`.

---

### H-08 · Polaridad del Relé de Alarma de Bypass (`RELAY_CH_ALARM_OUT_1`) y campo configurable NO/NC en EEPROM

- **Severidad:** P1
- **Archivos afectados:**
  - `app/include/common/hvac_types.h`
  - `app/include/common/app_defaults.h`
  - `app/src/infrastructure/adapters/adapter_hvac_actuator.h`
  - `app/src/infrastructure/adapters/adapter_hvac_actuator.c:124-131`
  - `app/src/infrastructure/di/composition_root.c`
- **Corrección (Decisión aprobada):**
  Añadir `uint8_t bypass_alarm_contact_type` (`HVAC_CONTACT_NORMALLY_CLOSED = 0` por defecto, paridad con Ref 1/Ref 2: escribe `0`/`OPEN` en Bypass y `1`/`CLOSED` en Smart; `HVAC_CONTACT_NORMALLY_OPEN = 1`: escribe `1`/`CLOSED` en Bypass y `0`/`OPEN` en Smart) en `config_hvac_t` para persistencia en EEPROM.

---

### H-09 · Desfase de canales en `k_brightc_hvac_hw_table` (`hardware_pin_map.h`) y `.units[]` en `k_default_hvac_config_control_only_8units`

- **Severidad:** P2
- **Archivos afectados:**
  - `app/include/common/hardware_pin_map.h:382-434`
  - `app/include/common/app_defaults.h:186-216`
- **Corrección:**
  Alinear `k_brightc_hvac_hw_table` con `RELAY_CH_AC1_AA..RELAY_CH_AC4_G` (`0..11`) y completar `.units[0..7]` en `k_default_hvac_config_control_only_8units`.

---

### H-10 · `on_hvac_di_feed_cb` (cada 50 ms) llama `hvac_group_entity_set_fire(group, false)` sin guarda de flanco, pasando la Unidad 0 a `STOPPING` y cancelando `STARTING` en las Unidades 1..3

- **Severidad:** P0 (Bloqueante — causa directa de que el primer aire pase a `STOPPING` y ningún otro aire encienda ni en `COOLING` ni en `COOL_BOOST`)
- **Archivos afectados:**
  - `app/src/domain/models/hvac_group_entity.c:177-217` (`hvac_group_entity_set_fire`, `hvac_group_entity_set_manual`)
  - `app/src/infrastructure/di/composition_root.c:1088-1116` (`on_hvac_di_feed_cb`)
- **Causa raíz:**
  1. `ao_digital_input` publica en `chan_digital_input_snapshot` cada **50 ms** (20 veces por segundo).
  2. El listener `on_hvac_di_feed_cb()` en `composition_root.c:1106` invoca `ao_hvac_set_fire(&s_active_container->hvac.ao, fire_active)` con `fire_active = false` en cada muestra.
  3. En Referencia 2 (`brightc_firmware/src/domain/hvac/hvac_group_entity.c:191-199`), `hvac_group_entity_set_fire()` simplemente asignaba `self->is_fire_active = active;`. En cambio, en `BrightC-telemetry`, `hvac_group_entity_set_fire(self, false)` tenía una rama `else` **sin guarda de idempotencia (`if (self->is_fire_active == active) return;`)** que en cada llamada con `active = false`:
     - Forzaba `self->state = GROUP_STATE_IDLE;` y ponía `cooling_timer_s = 0`.
     - Llamaba `hvac_unit_entity_request_stop(self->units[i], self->enable_fan_delay)` sobre todas las unidades (`0..3`).
  4. **Efecto observable en tarjeta cada segundo:**
     - En `t = 0.000 s` (`ao_hvac_step` a 1 Hz): `evaluate()` ve `T >= temp_max`, pasa `unit[0]` (`delay = 0 s`) a `UNIT_STATE_RUNNING` (escribiendo inmediatamente en los relés físicos `SM = 1, AA = 1, G = 0`) y pasa `unit[1]` (`delay = 10 s`) a `UNIT_STATE_STARTING` (`startup_timer_s = 10`, `AA = 0`).
     - En `t = 0.050 s` (50 ms después): `on_hvac_di_feed_cb()` llama `hvac_group_entity_set_fire(group, false)` (sin tocar los relés físicos, que sólo se actualizan dentro de `ao_hvac_step`):
       - `unit[0]` (que estaba en `RUNNING`) recibe `request_stop()` y pasa a **`UNIT_STATE_STOPPING`** (con el relé físico `AA1` aún encendido porque `ao_hvac_step` no vuelve a correr hasta `t = 1.000 s`).
       - `unit[1]` (que estaba en `STARTING` con `startup_timer_s = 10`) recibe `request_stop()`, lo que cancela `STARTING` y la devuelve a **`UNIT_STATE_OFF`** (`startup_timer_s = 0`).
       - Cuando `T >= temp_alarm` (`COOL_BOOST`), las unidades de descanso (`unit[2..3]`) entran en `UNIT_STATE_STARTING` en `t = 0.000 s` y 50 ms después son devueltas a **`UNIT_STATE_OFF`** (`startup_timer_s = 0`).
     - Por eso en `hvac status` el primer aire se veía siempre en `STOPPING` (con su relé físico `AA1 = 1`) y ni el segundo aire del turno ni los aires en descanso al superar `temp_alarm` lograban completar jamás su cuenta regresiva de encendido.
- **Corrección:**
  - Añadir guarda de idempotencia `if (self->is_fire_active == active) { return; }` en `hvac_group_entity_set_fire()`.
  - Añadir guarda de idempotencia `if (!active && !self->is_manual_active && (self->state != GROUP_STATE_MANUAL)) { return; }` en `hvac_group_entity_set_manual()`.
  - Propagar `BYPASS_CAUSE_PHYSICAL_SWITCH` a todos los grupos (`0..HVAC_MAX_GROUPS - 1`) en `on_hvac_di_feed_cb()`.

---

### H-11 · Migración de versión de sobre EEPROM (`CONFIG_ENVELOPE_VERSION_V2`) para invalidar registros previos con `enable_fan_delay = 1`

- **Severidad:** P1
- **Archivos afectados:**
  - `app/include/common/config_envelope.h`
- **Causa raíz:**
  El campo `bypass_alarm_contact_type` ocupó un byte de padding existente en `config_hvac_t`, manteniendo `sizeof(config_hvac_t)` idéntico a la versión anterior. Al arrancar la placa física, `composition_root_init_hvac()` cargaba el registro `V1` persistido en EEPROM antes del cambio, el cual tenía guardado `enable_fan_delay = 1` en lugar del nuevo valor por defecto `enable_fan_delay = 0`.
- **Corrección:**
  Incrementar `CONFIG_ENVELOPE_VERSION_CURRENT` a `CONFIG_ENVELOPE_VERSION_V2` (`2U`) y validar `hdr.version != CONFIG_ENVELOPE_VERSION_CURRENT` en `config_envelope_unpack()` para que cualquier registro `V1` obsoleto se invalide automáticamente y se inicialice con los nuevos defaults (`enable_fan_delay = 0`).

---

### H-12 · Verificación de alarmas activas en `is_unit_available()` dentro de `hvac_group_entity_evaluate()`

- **Severidad:** P1
- **Archivos afectados:**
  - `app/src/domain/models/hvac_group_entity.c`
- **Causa raíz:**
  En `hvac_group_entity_evaluate()`, `is_available` solo comprobaba `unit->state != UNIT_STATE_FAULT && unit->state != UNIT_STATE_BLOCKED_ALARM && unit->state != UNIT_STATE_BYPASS`, sin verificar si una unidad en `UNIT_STATE_OFF` tenía una alarma activa (`hp`, `lp`, `flow`, `volt`), ni comprobaba el valor de retorno de `hvac_unit_entity_request_start_staggered()`.
- **Corrección:**
  Definir `is_unit_available(const hvac_unit_entity_t *unit)` verificando estado y banderas de alarma activas, y comprobar el retorno de `hvac_unit_entity_request_start_staggered()`.

---

## 5. Plan de implementación paso a paso

1. **`H-01` (`adapter_di_gpio.c`)**:
   - Cambiar `gpio_pin_get_dt(spec)` por `gpio_pin_get(spec->port, spec->pin)` en `DI_SOURCE_ONCHIP_GPIO` para eliminar la doble inversión de `sw_bypass` (`PD11`) y `sw0` (`PD15`).
2. **`H-06` y `H-02` (`hvac_unit_entity.c`, `hvac_group_entity.c`, `app_defaults.h`, `app_facade.c`, `shell_brightc.c`)**:
   - Poner `enable_fan_delay = 0` (`false`) por defecto en `app_defaults.h` y `hvac_group_entity_init()`.
   - Corregir el Sub-FSM de `fan_state` y `hvac_unit_entity_get_tri_state()` según la tabla de verdad de la sección 3.5:
     - `enable_fan_delay == false` → `relay_g = false` (ventilador siempre habilitado), excepto en `HVAC_FAN_STATE_FIRE` donde `relay_g = true` (ventilador apagado por incendio).
     - `enable_fan_delay == true` → `relay_g = false` (ventilador encendido) únicamente en `UNIT_STATE_RUNNING`, `UNIT_STATE_STOPPING` (`HVAC_FAN_STATE_WAIT_DELAY`) y `UNIT_STATE_BYPASS`; `relay_g = true` (ventilador apagado) en `UNIT_STATE_OFF`, `UNIT_STATE_STARTING`, `UNIT_STATE_BLOCKED_ALARM`, `UNIT_STATE_FAULT` y `HVAC_FAN_STATE_FIRE`.
   - En `hvac_unit_entity_request_stop()`, no entrar en `UNIT_STATE_STOPPING` (`WAIT_DELAY`) si la unidad estaba en `UNIT_STATE_STARTING` (el compresor no encendió).
   - En `hvac_unit_entity_increment_fault()`, sólo incrementar `fault_counter` y pasar a `UNIT_STATE_FAULT` al llegar a `max_allowed_faults`; dejar `UNIT_STATE_BLOCKED_ALARM` exclusivamente en `hvac_unit_entity_update_alarms()`.
   - Corregir la inversión de `fan_state` en `HVAC_MODE_FAN_ONLY` y `app_facade_hvac_set_unit_manual()` en `app_facade.c`, y añadir soporte en facade/shell (`hvac fan_ctrl <grp> <on|off> [delay_s]`) para configurar `enable_fan_delay` y `fan_delay_s`.
3. **`H-03`, `H-04` y `H-07` (`hvac_group_entity.c`)**:
   - Retener `GROUP_STATE_COOL_BOOST` hasta que `metrics->highest_temp <= self->temp_min`.
   - Calcular `starting_delay_step` contando las unidades que ya están en `UNIT_STATE_STARTING` (Opción A).
   - Limpiar `BYPASS_CAUSE_HIGH_TEMP` y `BYPASS_CAUSE_COOLING_TIMEOUT` (rotando el turno) y detener el enfriamiento únicamente cuando `metrics->highest_temp <= self->temp_min`.
4. **`H-05` (`ao_hvac.c`)**:
   - Ejecutar `hvac_group_entity_tick_1s()` antes de `hvac_group_entity_evaluate()`.
5. **`H-08` (`hvac_types.h`, `app_defaults.h`, `adapter_hvac_actuator.c/.h`, `composition_root.c`)**:
   - Añadir `bypass_alarm_contact_type` en `config_hvac_t` (`HVAC_CONTACT_NORMALLY_CLOSED = 0` por defecto) y aplicarlo en `adapter_hvac_actuator_set_bypass_alarm()`.
6. **`H-09` (`hardware_pin_map.h`, `app_defaults.h`)**:
   - Corregir los canales de `k_brightc_hvac_hw_table` y `k_default_hvac_config_control_only_8units`.
7. **Pruebas unitarias (`tests/unit/hvac`)**:
   - Actualizar y ampliar `test_hvac_unit.c`, `test_hvac_group.c`, `test_ao_hvac.c` y `test_adapter_hvac_actuator.c` para verificar todos los escenarios (`H-01` a `H-09`).
