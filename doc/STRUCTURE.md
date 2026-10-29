# Arquitectura de Software y Estructura del Proyecto BrightC Telemetry
**Guía Maestra de Clean Architecture, TDD y Zephyr RTOS**  
*Plataforma: DAECGE BrightC Controller V3 (STM32H743ZIT6 LQFP144)*  
*Entorno de Ejecución: Zephyr RTOS (Topología West T2)*

---

## 1. Visión Arquitectónica y Principios Rectores

El firmware de **BrightC Telemetry** está diseñado bajo los principios de **Clean Architecture (Puertos y Adaptadores / Arquitectura Hexagonal)** y **Desarrollo Guiado por Pruebas (TDD)**, aprovechando las capacidades del ecosistema **Zephyr RTOS**.

```
 +-----------------------------------------------------------------------+
 |                     PRESENTACIÓN & CLI (Shell)                        |
 |         (HMI DWIN UART3, Zephyr Shell UART1, HTTP/SNMP Handlers)      |
 +-----------------------------------+-----------------------------------+
                                     |
                                     v
 +-----------------------------------------------------------------------+
 |                        CAPA DE APLICACIÓN                             |
 |       (Active Objects, Casos de Uso, Orquestación, Zephyr Zbus, SMF)  |
 +------------------+---------------------------------+------------------+
                    |                                 |
                    v                                 v
 +------------------------------------+   +------------------------------+
 |          CAPA DE DOMINIO           |   |      PUERTOS / INTERFACES    |
 | (Lógica Pura C99/C11, Algoritmos,  |   | (Contratos abstractos en C:  |
 |  Modelos, Máquinas de Estado Puras)|   |  i_modbus.h, i_storage.h,    |
 |    *CERO dependencias de Zephyr*   |   |  i_sensor.h, i_network.h)    |
 +------------------------------------+   +--------------+---------------+
                                                         ^
                                                         | (Implementa)
 +-------------------------------------------------------+---------------+
 |                      CAPA DE INFRAESTRUCTURA                          |
 | (Adaptadores Zephyr: GPIO, NVS/Settings, Ethernet LAN8742, WiFi ESP32)|
 |        (Composition Root / Dependency Injection: composition_root.c)  |
 +-----------------------------------------------------------------------+
                                     |
                                     v
 +-----------------------------------------------------------------------+
 |                HARDWARE & DEVICETREE (DTS / Overlays)                 |
 |            (boards/daecge/brightc-controllerv3, app/boards/)          |
 +-----------------------------------------------------------------------+
```

### 1.1 Invariantes Fundamentales
1. **Regla de Dependencia Unidireccional**: Las capas internas jamás conocen a las capas externas. El Dominio desconoce la existencia de Zephyr, FreeRTOS, STM32 o cualquier hardware físico.
2. **Pureza del Dominio**: Todo archivo en `domain/` se compila con C estándar (`stdint.h`, `stdbool.h`, `stddef.h`). Está **estrictamente prohibido** incluir `#include <zephyr/...>` o registros de silicio en el Dominio.
3. **Inversión de Dependencias (IoC)**: El software interactúa con el mundo exterior mediante interfaces abstractas (*puertos* en `include/interfaces/`). La infraestructura implementa dichos puertos (*adaptadores*).
4. **Devicetree como Fuente Única de Verdad de Hardware**: No existen archivos `bsp.c` o llamadas a CubeMX. Toda la configuración de pines (pinctrl), relojes y buses reside en archivos `.dts` y `.overlay`.
5. **Testabilidad Inmediata (TDD)**: Toda la lógica de negocio y aplicación puede probarse en milisegundos en la máquina anfitriona (Host PC o `native_sim`) sin necesidad de conectar la placa física.
6. **Cero Malloc en el Bucle de Ejecución**: Memoria estática, pools preasignados (`k_mem_slab`, `k_heap` estático) y canales fuertemente tipados en tiempo de compilación.
7. **Definición de Stacks y Protección MPU**: Los stacks de hilos de Zephyr (`k_thread_stack_t`) **NUNCA** deben embeberse dentro de estructuras (`struct` en `.bss`), como el contenedor `app_container_t`. En microcontroladores Cortex-M con MPU (Cortex-M7 STM32H7) y `CONFIG_HW_STACK_PROTECTION` / `CONFIG_MPU_STACK_GUARD`, Zephyr exige que los stacks residan en secciones dedicadas alineadas por hardware con sus respectivas guardias MPU. Incrustar un stack en un struct desalinea la memoria y corrompe las regiones MPU, desatando fallos de hardware críticos como `MPU FAULT (Instruction Access Violation)`. Es **mandatorio** definir los stacks a nivel de archivo (file scope) mediante `K_THREAD_STACK_DEFINE` o `K_THREAD_STACK_ARRAY_DEFINE`.

---

## 2. Estructura Canónica del Proyecto (West T2 Topology)

El workspace de Zephyr organiza el proyecto mediante la topología **West T2**, donde `BrightC-telemetry` es el repositorio de aplicación y manifiesto.

```text
BrightC_TelemetryWorkspace/                  # Raíz del Workspace West
├── .west/                                   # Metadatos locales de west
├── zephyr/                                  # Árbol fuente de Zephyr RTOS (submódulo/upstream)
├── modules/                                 # Módulos externos (nanoMODBUS, microconf, hal_stm32, etc.)
│   └── lib/
│       ├── nanoMODBUS/
│       ├── microconf/
│       └── zephyr-snmp/
│
└── BrightC-telemetry/                        # Repositorio Principal de la Aplicación (Este Repo)
    ├── west.yml                             # Manifiesto West (pinning de dependencias y versiones)
    ├── CMakeLists.txt                       # CMake de nivel módulo Zephyr
    ├── Kconfig                              # Kconfig de nivel módulo Zephyr
    ├── zephyr/
    │   └── module.yml                       # Declaración del repo como módulo Zephyr (dts_root, board_root)
    │
    ├── boards/                              # Definiciones de placas personalizadas (Hardware Base)
    │   └── daecge/
    │       └── brightc-controllerv3/        # Placa BrightC BaseBoard V3 (STM32H743ZIT6)
    │           ├── board.cmake              # Configuración de flasheo (openocd / runner)
    │           ├── board.yml                # Metadatos de la placa
    │           ├── brightc-controllerv3.dts # Devicetree completo (pinctrl, clocks, buses, PHY)
    │           ├── brightc-controllerv3.yaml# Capacidades (RAM, Flash, periféricos)
    │           ├── brightc-controllerv3_defconfig # Configuración por defecto de la placa
    │           ├── Kconfig.brightc-controllerv3
    │           └── support/
    │               └── openocd.cfg          # Configuración OpenOCD (ST-Link / SWD)
    │
    ├── dts/                                 # Extensiones de Devicetree del proyecto
    │   └── bindings/                        # Bindings YAML para dispositivos propietarios
    │
    ├── drivers/                             # Drivers fuera del árbol (Out-of-tree Zephyr Drivers)
    │   ├── CMakeLists.txt
    │   ├── Kconfig
    │   └── sensor/                          # Drivers que implementan la API nativa de Zephyr
    │
    ├── lib/                                 # Librerías transversales y utilidades independientes
    │   ├── CMakeLists.txt
    │   └── Kconfig
    │
    ├── app/                                 # Aplicación Principal de Firmware
    │   ├── CMakeLists.txt                   # CMake de la aplicación (target 'app')
    │   ├── Kconfig                          # Menú de configuración Kconfig de la aplicación
    │   ├── prj.conf                         # Kconfig base común para producción
    │   ├── debug.conf                       # Overlay Kconfig para depuración (logs verbosos, asserts)
    │   ├── VERSION                          # Versión semántica del firmware
    │   │
    │   ├── boards/                          # Overlays de hardware específicos de la aplicación
    │   │   ├── brightc-controllerv3.overlay # Asignación de funciones de la app a nodos DTS
    │   │   ├── brightc-controllerv3.conf    # Opciones Kconfig específicas para la V3
    │   │   └── native_sim.overlay           # Simulación de periféricos (UARTs emuladas, etc.)
    │   │
    │   ├── include/                         # Puertos e Interfaces (Contratos puros)
    │   │   ├── interfaces/                  # Interfaces abstractas (V-Tables / contratos C)
    │   │   │   ├── i_telemetry_port.h       # Contrato de envío/recepción de telemetría
    │   │   │   ├── i_modbus_client.h        # Contrato de transporte Modbus (FC03, FC06, etc.)
    │   │   │   ├── i_slave_registry.h       # Contrato del gestor y planificación de esclavos
    │   │   │   ├── i_config_storage.h       # Contrato modular de persistencia en EEPROM / NVS
    │   │   │   ├── i_network.h              # Contrato de interfaz de red (Ethernet / WiFi)
    │   │   │   ├── i_led.h                  # Contrato de LED de usuario
    │   │   │   ├── i_buzzer.h               # Contrato de buzzer de señalización
    │   │   │   ├── i_relay.h                # Contrato de salidas discretas / relés de control
    │   │   │   ├── i_sensor_port.h          # Contrato de adquisición de mediciones analógicas
    │   │   │   ├── i_temperature_bank.h     # Contrato de lectura multi-canal de temperatura
    │   │   │   └── i_display_port.h         # Contrato para interfaz HMI DWIN
    │   │   └── common/                      # Tipos de datos primitivos agnósticos
    │   │       ├── config_types.h           # Dominios y eventos de configuración persistente
    │   │       ├── indicator_types.h        # Tipos de indicadores (LED, buzzer, alarmas)
    │   │       ├── modbus_types.h           # Structs de buses, descriptores, resultados
    │   │       ├── telemetry_types.h        # Structs de mediciones, alarmas, timestamps
    │   │       └── error_codes.h            # Códigos de error unificados
    │   │
    │   └── src/                             # Código fuente de la aplicación
    │       ├── domain/                      # 100% PURO: Lógica de negocio (Sin <zephyr/...>)
    │       │   ├── models/                  # Entidades (TelemetryFrame, AlarmState, DeviceProfile)
    │       │   ├── services/                # Reglas de negocio (AlarmEvaluator, CalibrationEngine)
    │       │   └── sm/                      # Máquinas de estado puras (TelemetryFSM)
    │       │
    │       ├── application/                 # Casos de uso y Orquestación (Usa Zephyr Zbus / RTOS)
    │       │   ├── active_objects/          # Hilos de ejecución asíncronos (Actors)
    │       │   │   ├── ao_telemetry.c       # Hilo coordinador de adquisición y muestreo
    │       │   │   ├── ao_modbus.c          # Hilo de servicio Modbus (RS485-1 / RS485-2)
    │       │   │   ├── ao_storage.c         # Hilo coordinador de persistencia asíncrona EEPROM
    │       │   │   ├── ao_network.c         # Hilo de conectividad (Ethernet / WiFi ESP32)
    │   │   │   ├── ao_system_indicator.c    # Hilo de indicadores (LED / Buzzer)
    │       │   │   └── ao_hmi.c             # Hilo de atención a pantalla LCD DWIN
    │       │   ├── zbus/                    # Canales de eventos Zephyr Zbus
    │       │   │   ├── telemetry_channels.h # Definición de zbus_channel (muestras, alarmas)
    │       │   │   └── telemetry_channels.c # Instanciación ZBUS_CHAN_DEFINE
    │   │   │   ├── indicator_channels.h   # Canales de eventos de indicadores
    │   │   │   └── indicator_channels.c # Instanciación ZBUS_CHAN_DEFINE para indicadores
    │       │   └── use_cases/               # Flujos (ProcessTelemetrySample, PublishMqttReport)
    │       │
    │       ├── infrastructure/              # Adaptadores a Zephyr y composición
    │       │   ├── adapters/                # Implementación de los puertos con APIs de Zephyr
    │       │   │   ├── adapter_modbus_nmbs.c      # nanoMODBUS montado sobre UART/RS485 Zephyr
    │       │   │   ├── adapter_storage_microconf.c# Persistencia microconf sobre EEPROM AT24C256C
    │       │   │   ├── adapter_sensor_zephyr.c    # Lectura de sensores usando <zephyr/drivers/sensor.h>
    │       │   │   ├── adapter_relay_gpio.c       # Manejo de relés usando <zephyr/drivers/gpio.h>
    │       │   │   ├── adapter_wifi_esp_hosted.c  # Driver de red ESP-Hosted (WiFi / BLE)
    │   │   │   ├── adapter_led_gpio.c         # Control de LED de usuario por GPIO
    │   │   │   ├── adapter_buzzer_pwm.c       # Control de buzzer por PWM
    │       │   │   └── adapter_display_dwin.c     # Parser serial DWIN sobre UART3 de Zephyr
    │       │   └── di/                      # Composition Root (Inyección de Dependencias)
    │       │       ├── composition_root.h   # API de arranque del grafo de dependencias
    │       │       └── composition_root.c   # DEVICE_DT_GET y vinculación puerto-adaptador
    │       │
    │       ├── presentation/                # Interfaz de Usuario / HMI
    │       │   ├── dwin/                    # Model-View-Presenter (MVP) para LCD DWIN
    │       │   │   ├── dwin_presenter.c     # Lógica de presentación de variables
    │       │   │   └── dwin_views.c         # Formateo de tramas de pantalla
    │       │   └── shell/                   # Comandos de CLI de Zephyr (Diagnóstico y Calibración)
    │       │       └── telemetry_shell.c    # Comandos SHELL_CMD_REGISTER para terminal UART1
    │       │
    │       └── main.c                       # Punto de entrada de la aplicación
    │
    ├── tests/                               # Suites de Pruebas Automatizadas (TDD)
    │   ├── testcase.yaml                    # Descriptor global para el runner 'twister'
    │   ├── unit/                            # Pruebas Unitarias Aisladas (Corren en native_sim)
    │   │   ├── domain/                      # Tests de lógica de negocio (100% Mockeado)
    │   │   │   ├── test_alarm_evaluator.c
    │   │   │   └── test_telemetry_fsm.c
    │   │   └── application/                 # Tests de casos de uso y Zbus
    │   │       └── test_process_sample.c
    │   ├── fakes/                           # Fakes e Implementaciones en Memoria para pruebas
    │   │   ├── include/
    │   │   │   ├── fake_storage_port.h
    │   │   │   ├── fake_sensor_port.h
    │   │   │   └── fake_relay_port.h
    │   │   └── src/
    │   │       ├── fake_storage_port.c
    │   │       ├── fake_sensor_port.c
    │   │       └── fake_relay_port.c
    │   ├── integration/                     # Tests de integración en native_sim / Emuladores
    │   │   └── test_modbus_loopback.c
    │   └── hil/                             # Pruebas en Hardware Real (BrightC Controller V3)
    │       └── test_adc_accuracy.c
    │
    └── doc/                                 # Documentación de Arquitectura y Referencia
        ├── STRUCTURE.md                     # Este documento maestro de arquitectura
        ├── architecture/                    # Especificaciones técnicas maestras por subsistema
        │   ├── modbus_subsystem.md          # Arquitectura completa del subsistema Modbus
        │   ├── network_subsystem.md         # Arquitectura del subsistema de red (Ethernet / WiFi)
        │   ├── storage_subsystem.md         # Arquitectura del subsistema de persistencia EEPROM
        │   └── system_indicators.md         # Arquitectura de indicadores de sistema (LED / Buzzer)
        ├── brightcV3.md                     # Referencia esquemática y pinctrl del HW
        └── Doxyfile                         # Generador de documentación API
```

---

## 3. Matriz de Clasificación de Componentes (Decision Tree)

Cuando necesites crear un nuevo archivo o funcionalidad, responde las siguientes preguntas en orden:

| Pregunta de Diseño | Clasificación Arquitectónica | Ubicación | Invariantes y Restricciones |
| :--- | :--- | :--- | :--- |
| **¿Contiene reglas de negocio, cálculos de alarmas, conversiones matemáticas o estados de telemetría?** | **Dominio (Domain)** | `app/src/domain/` | C99/C11 puro. Solo headers de libc (`stdint.h`). **PROHIBIDO** incluir `<zephyr/...>` o registros de microcontrolador. |
| **¿Define el contrato abstracto de un servicio para desacoplar el dominio del HW o SO?** | **Puerto / Interfaz (Port)** | `app/include/interfaces/` | Solo headers (`i_*.h`). Estructuras de punteros a función (v-tables) o APIs tipadas en C. |
| **¿Es un proceso asíncrono continuo, un actor que procesa mensajes de una cola o canal?** | **Aplicación / Active Object** | `app/src/application/active_objects/` | Posee su propio hilo/stack o workqueue; se comunica exclusivamente por **Zbus** o colas de mensajes. |
| **¿Implementa un puerto interactuando con drivers de Zephyr, RTOS o librerías externas?** | **Adaptador (Infrastructure)** | `app/src/infrastructure/adapters/` | Contiene los `#include <zephyr/drivers/...>`. Traduce tipos nativos de Zephyr a los tipos del Dominio. |
| **¿Dónde se obtienen los dispositivos de Devicetree y se conectan a los adaptadores?** | **Composition Root (DI)** | `app/src/infrastructure/di/` | Único lugar donde se invocan las macros `DEVICE_DT_GET()` para inyectar dependencias hacia arriba. |
| **¿Es un comando para la consola serial de depuración o una pantalla gráfica HMI?** | **Presentación / Shell** | `app/src/presentation/` o `app/src/shell/` | Usa Zephyr Shell o el patrón Model-View-Presenter (MVP). No manipula hardware directamente. |
| **¿Es un controlador para un chip de hardware que implementa el driver model de Zephyr?** | **Out-of-Tree Driver** | `drivers/<subsistema>/` | Usa `DEVICE_DT_DEFINE`, devicetree bindings (`dts/bindings/`) y APIs estándar (`sensor_driver_api`, etc.). |
| **¿Configura pines físicos, relojes, particiones flash o instancias de periféricos de la placa?** | **Devicetree / BSP** | `boards/` o `app/boards/` | Archivos `.dts`, `.overlay` y `.defconfig`. Cero código C. |

---

## 4. Implementación de Patrones Clave en Zephyr

### 4.1 Inversión de Control: Puertos y Adaptadores en C

#### A. El Puerto Abstracto (Sin dependencias de Zephyr)

Todo puerto vive en un único header `app/include/interfaces/i_<nombre>.h` y consta de
tres piezas inseparables:

1. La **v-table** `i_<nombre>_api_t`: los punteros a función que implementa el adaptador.
2. El **handle** `i_<nombre>_t`: `const ... *api` (la v-table) más `void *impl` (estado
   opaco del adaptador).
3. Una **función `static inline` por operación**, `i_<nombre>_<operación>()`, que valida
   el handle y los argumentos **antes** de despachar por la v-table.

```c
#ifndef APP_INTERFACES_I_RELAY_H_
#define APP_INTERFACES_I_RELAY_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { RELAY_CHANNEL_MAIN_POWER = 0, RELAY_CHANNEL_AUXILIARY, RELAY_CHANNEL_MAX } relay_channel_t;
typedef enum { RELAY_STATE_OPEN = 0, RELAY_STATE_CLOSED } relay_state_t;

static inline bool relay_channel_is_valid(relay_channel_t channel)
{
    return (unsigned int) channel < (unsigned int) RELAY_CHANNEL_MAX;
}

/* 1. V-table: detalle de implementación, nunca se llama desde la aplicación */
typedef struct {
    int (*set_state)(void *impl, relay_channel_t channel, relay_state_t state);
    int (*get_state)(void *impl, relay_channel_t channel, relay_state_t *out_state);
} i_relay_api_t;

/* 2. Handle que se inyecta a quien consume el puerto */
typedef struct {
    const i_relay_api_t *api;
    void *impl;
} i_relay_t;

static inline bool i_relay_is_bound(const i_relay_t *relay)
{
    return relay != NULL && relay->api != NULL && relay->impl != NULL;
}

/* 3. Wrappers validados: el único camino permitido hacia la v-table */
static inline int i_relay_set_state(const i_relay_t *relay, relay_channel_t channel,
                                    relay_state_t state)
{
    if (!i_relay_is_bound(relay) || !relay_channel_is_valid(channel)) {
        return -EINVAL;
    }

    return relay->api->set_state(relay->impl, channel, state);
}

static inline int i_relay_get_state(const i_relay_t *relay, relay_channel_t channel,
                                    relay_state_t *out_state)
{
    if (!i_relay_is_bound(relay) || !relay_channel_is_valid(channel) || out_state == NULL) {
        return -EINVAL;
    }

    return relay->api->get_state(relay->impl, channel, out_state);
}

#endif /* APP_INTERFACES_I_RELAY_H_ */
```

El consumidor escribe `i_relay_set_state(&port, RELAY_CHANNEL_MAIN_POWER, RELAY_STATE_CLOSED)`;
no ve la v-table y no puede olvidarse una comprobación.

#### B. El Adaptador de Infraestructura (Conectado a Zephyr GPIO)
`app/src/infrastructure/adapters/adapter_relay_gpio.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "interfaces/i_relay.h"

struct relay_gpio_context {
    struct gpio_dt_spec gpio_spec[RELAY_CHANNEL_MAX];
};

static int relay_gpio_set(void *impl, relay_channel_t channel, relay_state_t state)
{
    struct relay_gpio_context *ctx = impl;

    /* La validación estructural (handle, canal) ya la hizo el wrapper del
     * puerto: aquí solo se comprueba lo que depende del estado del adaptador. */
    if (ctx == NULL) {
        return -EINVAL;
    }

    return gpio_pin_set_dt(&ctx->gpio_spec[channel], (int) state);
}

static int relay_gpio_get(void *impl, relay_channel_t channel, relay_state_t *out_state)
{
    struct relay_gpio_context *ctx = impl;
    int val = gpio_pin_get_dt(&ctx->gpio_spec[channel]);

    if (val < 0) {
        return val;
    }

    *out_state = val ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
    return 0;
}

static const i_relay_api_t s_relay_api = {
    .set_state = relay_gpio_set,
    .get_state = relay_gpio_get,
};

/* Enlazar el puerto es competencia de la raíz de composición, no del adaptador:
 * el adaptador solo expone su v-table. */
const i_relay_api_t *relay_gpio_api(void)
{
    return &s_relay_api;
}
```

#### C. Composition Root (El puente estático de Hardware a Software)
`app/src/infrastructure/di/composition_root.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "interfaces/i_relay.h"

LOG_MODULE_REGISTER(comp_root, LOG_LEVEL_INF);

/* Estructura instanciada desde el Devicetree y propiedad del contenedor */
static struct relay_gpio_context s_relay_hw_ctx = {
    .gpio_spec = {
        [RELAY_CHANNEL_MAIN_POWER] = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(relay_main), gpios, {0}),
        [RELAY_CHANNEL_AUXILIARY]  = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(relay_aux), gpios, {0}),
    }
};

int composition_root_init(app_container_t *self)
{
    LOG_INF("Inicializando Composition Root & Verificación de Periféricos...");

    for (int i = 0; i < RELAY_CHANNEL_MAX; i++) {
        if (!gpio_is_ready_dt(&s_relay_hw_ctx.gpio_spec[i])) {
            LOG_ERR("Relay GPIO %d no está listo en Devicetree", i);
            return -ENODEV;
        }
        gpio_pin_configure_dt(&s_relay_hw_ctx.gpio_spec[i], GPIO_OUTPUT_INACTIVE);
    }

    /* Único punto del firmware que escribe la v-table del puerto. */
    self->relay.api = relay_gpio_api();
    self->relay.impl = &s_relay_hw_ctx;

    LOG_INF("Hardware vinculado correctamente a los puertos.");
    return 0;
}
```

---

#### D. Regla general para definir puertos (obligatoria)

Todo puerto nuevo, y toda modificación de uno existente, cumple estas reglas. No son
recomendaciones de estilo: son el contrato que permite testear el dominio sin hardware y
que impide que un `NULL` llegue a un driver.

1. **Ubicación y pureza.** Un solo header en `app/include/interfaces/i_<dominio>.h`,
   sólo cabecera (sin `.c` de puerto). Puede incluir `<stdint.h>`, `<stdbool.h>`,
   `<stddef.h>`, `<errno.h>` y otros headers de `include/common/`. **Nunca**
   `<zephyr/...>`, ni headers del MCU, ni de nanoMODBUS.
2. **Tres piezas.** La v-table `i_<dominio>_api_t`, el handle
   `i_<dominio>_t { const i_<dominio>_api_t *api; void *impl; }` y los wrappers.
   Los nombres son `snake_case` y el prefijo coincide con el del header.
3. **Un wrapper por operación, sin excepciones.** Cada entrada de la v-table tiene su
   `static inline i_<dominio>_<operación>()` en el mismo header. Si una operación no
   merece wrapper, no merece estar en la v-table.
4. **`static inline`, no función externa.** Así el puerto no añade símbolos, no
   necesita unidad de compilación propia y el compilador puede eliminar los wrappers
   que nadie usa.
5. **`i_<dominio>_is_bound()` público** y usado por todos los wrappers: el handle está
   enlazado sólo si `api` e `impl` no son `NULL`.
6. **Validación en dos niveles, sin duplicarla.**
   - *Estructural (wrapper)*: handle enlazado, punteros de salida no nulos, rangos e
     enumeraciones que forman parte del contrato, capacidades mayores que cero. Es
     barata, no necesita estado y usa los predicados puros de `common/` (por ejemplo
     `modbus_bus_id_is_valid()`), nunca los repite a mano.
   - *Semántica (implementación)*: ¿existe el dispositivo?, ¿está lleno el bus?, ¿la
     reclamación está vigente? El wrapper no puede responderlo y no lo intenta.
7. **Retorno uniforme.** `int` con errno negativo (`-EINVAL`, `-ENOENT`, `-ENOSPC`,
   `-EADDRINUSE`) para operaciones de estado o configuración; un enumerado del dominio
   (por ejemplo `modbus_transaction_result_t`) para resultados de una transacción.
   No se mezclan en la misma firma. Handle no enlazado o argumento inválido devuelve
   `-EINVAL` (o el valor "rechazado localmente" del dominio: `MODBUS_RESULT_INVALID`) y
   **no se despacha nada**.
8. **Prohibido llamar la v-table desde la aplicación.** `handle->api->...` sólo aparece
   en el adaptador que implementa el puerto y en la raíz de composición, que es la única
   que escribe `api`/`impl`. Verificación rápida antes de cada PR:
   ```bash
   grep -rn "api->" app/src | grep -v "infrastructure/adapters/" | grep -v "infrastructure/di/"
   ```
   Debe salir vacío (las implementaciones de puerto en `application/` sólo *crean* el
   handle, no lo consumen).
9. **Sin estado global.** El `impl` apunta a una instancia propiedad del contenedor de
   composición; el puerto no reserva memoria, no crea hilos y no conoce el RTOS.
10. **Los fakes implementan la misma v-table** y se ejercitan a través de los wrappers,
    no llamando a la v-table del fake: así el test verifica el contrato y no una copia
    del contrato.
11. **Documentación Doxygen** en el header para el handle, cada wrapper y cada parámetro
    de salida, indicando qué error devuelve cada rechazo. Identificadores y comentarios
    de código en inglés; la prosa de arquitectura (este documento) en español.

Ejemplos vivos de esta regla: `app/include/interfaces/i_slave_registry.h` e
`app/include/interfaces/i_modbus_client.h`; su cumplimiento se verifica en
`tests/unit/modbus/src/test_interfaces.c`.

> Un header `i_*.h` que sólo define **tipos de datos compartidos** (por ejemplo
> `i_temperature_bank.h`, que describe una lectura) no es un puerto: no lleva v-table ni
> wrappers. La regla aplica a cualquier header que declare una v-table de operaciones.

---

### 4.2 Desacoplamiento de Hilos con Zephyr Zbus

En aplicaciones complejas de telemetría, el acoplamiento directo entre hilos mediante llamadas directas provoca bloqueos, inversión de prioridades y código espagueti. **Zephyr Zbus** resuelve esto proveyendo un bus de mensajería asíncrono, con verificación en tiempo de compilación y cero copias dinámicas.

```
 [Sensor AO] ----(publica)----> [chan_telemetry_sample]
                                          |
                        +-----------------+-----------------+
                        |                                   |
                        v                                   v
                 [ao_modbus_sub]                     [ao_network_sub]
              (Actualiza Holding Regs)            (Transmite por Ethernet/MQTT)
```

`app/src/application/zbus/telemetry_channels.h`:
```c
#ifndef TELEMETRY_CHANNELS_H_
#define TELEMETRY_CHANNELS_H_

#include <zephyr/zbus/zbus.h>
#include "common/telemetry_types.h"

/* Mensajes fuertemente tipados */
typedef struct {
    uint32_t timestamp_ms;
    float current_ch1_amps;
    float current_ch2_amps;
    float bus_voltage_volts;
    uint32_t status_flags;
} telemetry_sample_msg_t;

typedef struct {
    uint16_t alarm_id;
    uint8_t severity;
    bool is_active;
} telemetry_alarm_msg_t;

/* Declaración de canales Zbus */
ZBUS_CHAN_DECLARE(chan_telemetry_sample);
ZBUS_CHAN_DECLARE(chan_telemetry_alarm);

#endif /* TELEMETRY_CHANNELS_H_ */
```

`app/src/application/zbus/telemetry_channels.c`:
```c
#include "zbus/telemetry_channels.h"

ZBUS_CHAN_DEFINE(chan_telemetry_sample,       /* Nombre del canal */
                 telemetry_sample_msg_t,       /* Tipo de dato */
                 NULL,                         /* Validador opcional */
                 NULL,                         /* Datos de usuario */
                 ZBUS_OBSERVERS(modbus_sample_sub, network_sample_sub, dwin_sample_sub),
                 ZBUS_MSG_INIT(0)              /* Valor inicial */
);

ZBUS_CHAN_DEFINE(chan_telemetry_alarm,
                 telemetry_alarm_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(alarm_sub),
                 ZBUS_MSG_INIT(0)
);
```

---

## 5. Metodología TDD (Test-Driven Development)

### 5.1 El Ciclo Red-Green-Refactor en Firmware
1. **RED**: Escribir una prueba unitaria en `tests/unit/domain/test_alarm_evaluator.c` que verifique un comportamiento requerido (ej: sobrecorriente > 50A activa alarma). La prueba **falla** al compilar o al correr.
2. **GREEN**: Escribir el código mínimo en `app/src/domain/services/alarm_evaluator.c` para hacer que la prueba pase.
3. **REFACTOR**: Mejorar el diseño, nombres de variables y eficiencia manteniendo todas las pruebas en verde.

### 5.2 Estructura de Test con Fakes en Memoria (Ztest)

Un fake implementa la **misma v-table** que el adaptador, con el mismo
`void *impl`, y se enlaza en un handle durante el test. El código bajo prueba lo
consume a través de los wrappers del puerto (regla §4.1.D), así que la prueba
valida el contrato, no una copia del contrato.

`tests/fakes/src/fake_relay.c`:
```c
#include "interfaces/i_relay.h"

typedef struct {
    relay_state_t states[RELAY_CHANNEL_MAX];
    uint32_t call_count[RELAY_CHANNEL_MAX];
} fake_relay_t;

static int fake_relay_set(void *impl, relay_channel_t channel, relay_state_t state) {
    fake_relay_t *fake = impl;

    fake->states[channel] = state;
    fake->call_count[channel]++;
    return 0;
}

static int fake_relay_get(void *impl, relay_channel_t channel, relay_state_t *out_state) {
    fake_relay_t *fake = impl;

    *out_state = fake->states[channel];
    return 0;
}

static const i_relay_api_t s_fake_api = {
    .set_state = fake_relay_set,
    .get_state = fake_relay_get,
};

void fake_relay_bind(i_relay_t *out, fake_relay_t *storage) {
    out->api = &s_fake_api;
    out->impl = storage;
}

relay_state_t fake_relay_spy_state(const fake_relay_t *fake, relay_channel_t ch) {
    return fake->states[ch];
}
```

`tests/unit/domain/test_alarm_evaluator.c`:
```c
#include <zephyr/ztest.h>
#include "domain/services/alarm_evaluator.h"
#include "fake_relay.h"

static fake_relay_t s_fake;
static i_relay_t s_relay;

ZTEST_SUITE(alarm_evaluator_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(alarm_evaluator_suite, test_should_trip_relay_on_overcurrent)
{
    (void) memset(&s_fake, 0, sizeof(s_fake));
    fake_relay_bind(&s_relay, &s_fake);
    alarm_evaluator_init(&s_relay);

    /* Muestra con corriente normal: relé debe mantenerse abierto */
    telemetry_sample_t normal_sample = { .current_ch1 = 15.0f };
    alarm_evaluator_process(&normal_sample);
    zassert_equal(fake_relay_spy_state(&s_fake, RELAY_CHANNEL_MAIN_POWER), RELAY_STATE_OPEN, 
                  "El relé no debe dispararse con corriente normal");

    /* Muestra con sobrecorriente crítica (60A): relé debe cerrarse/disparar */
    telemetry_sample_t fault_sample = { .current_ch1 = 60.0f };
    alarm_evaluator_process(&fault_sample);
    zassert_equal(fake_relay_spy_state(&s_fake, RELAY_CHANNEL_MAIN_POWER), RELAY_STATE_CLOSED,
                  "El relé debió dispararse ante sobrecorriente");
    zassert_equal(s_fake.call_count[RELAY_CHANNEL_MAIN_POWER], 1U, "Debe existir un único disparo");
}
```

### 5.3 Ejecución con Twister en `native_sim`
Para ejecutar todas las pruebas unitarias a la velocidad nativa de la CPU de tu máquina Linux sin tocar el microcontrolador:

```bash
# Correr suite de pruebas unitarias sobre native_sim
west twister -T tests/unit -p native_sim -v --inline-logs

# Correr pruebas con reporte de cobertura LCOV (gcov)
west twister -T tests/unit -p native_sim --coverage -v
```

---


---

## 6. Arquitectura del Subsistema Modbus Industrial (nanoMODBUS)

> **Documento Maestro**: Para el detalle exhaustivo de diagramas de secuencia, recuperación de claims obsoletos y guía paso a paso para añadir nuevos actuadores (ej. expansores de relés), consultar [**doc/architecture/modbus_subsystem.md**](architecture/modbus_subsystem.md).

El subsistema Modbus de BrightC Controller V3 está diseñado para alta disponibilidad, tolerancia a fallas de cableado y desacoplamiento físico total entre puertos RS485 y dispositivos de campo.

```
 +-------------------------------------------------------------------------------------------------+
 |                                   CAPA DE APLICACIÓN & ZBUS                                     |
 |   [chan_temperature_bank_16ch]       [chan_energy_metrics]           [chan_modbus_health]        |
 +-----------------^-----------------------------^--------------------------------^----------------+
                   |                             |                                |
                   +-----------------------------+--------------------------------+
                                                 |
 +-----------------------------------------------+-------------------------------------------------+
 |                    GESTOR DE ESCLAVOS Y ENRUTAMIENTO DE BUSES                                   |
 |                    (modbus_registry.c + slave_scheduler.c)                                      |
 |                                                                                                 |
 |  - Mantiene tabla de modbus_slave_descriptor_t (serializable para microconf / EEPROM)           |
 |  - Failover dinámico: reasigna dispositivos entre RS485-1 y RS485-2 en tiempo de ejecución      |
 |  - Máquina de estados de salud con Offline Backoff (aísla esclavos desconectados)               |
 +-----------------------------------------------+-------------------------------------------------+
                         |                                                |
                         v                                                v
 +-----------------------------------------------+  +----------------------------------------------+
 |   HILO RS485-1: ao_modbus.c (bus 0)           |  |   HILO RS485-2: ao_modbus.c (bus 1)          |
 |  - i_modbus_client_t bus1 -> adapter_modbus_nmbs (unico include de Zephyr)    |  |  - i_modbus_client_t bus2 -> adapter_modbus_nmbs                              |
 |  - Control de transceiver DE (PD4)            |  |  - Control de transceiver DE (PG8)           |
 |  - Reconfiguración en caliente de USART2      |  |  - Reconfiguración en caliente de USART6     |
 |  - Sondea: CWT-TM-16PT (16x PT100)            |  |  - Sondea: Medidores de energía, Relés       |
 +-----------------------+-----------------------+  +----------------------+-----------------------+
                         |                                                 |
                         v                                                 v
           [ Hardware: USART2 + DE ]                         [ Hardware: USART6 + DE ]
```

### 6.1 Principios de Diseño del Subsistema Modbus

1. **Aislamiento por Hilo (Active Object)**:
   - RS485 es un medio **half-duplex**. Cada puerto físico (`RS485-1` y `RS485-2`) es propiedad exclusiva de su propio hilo. Ningún componente externo realiza llamadas directas de lectura/escritura a la UART.
2. **Intercambiabilidad de Hardware (Failover en Caliente)**:
   - Los dispositivos de campo no están atados a un puerto fijo en código. Si se daña el transceptor de `RS485-1` por una descarga eléctrica, el cable A/B se mueve a `RS485-2` y se ejecuta:
     ```c
     (void) i_slave_registry_set_bus(&app->registry, DEV_ID_CWT_TM16PT, MODBUS_BUS_RS485_2);
     ```
   - El sistema reubica el sondeo al instante sin reiniciar ni modificar el firmware.
3. **Reconfiguración Dinámica de la UART en Zephyr**:
   - Gracias a `CONFIG_UART_USE_RUNTIME_CONFIGURE=y` y la función `uart_configure()`, la velocidad (baudrate) y paridad del bus se pueden modificar en caliente si se cambia la configuración de campo.
4. **Protección contra Esclavos Desconectados (Offline Backoff)**:
   - Si un esclavo sufre 3 fallos consecutivos, pasa al estado `MODBUS_HEALTH_OFFLINE_BACKOFF`.
   - En lugar de saturar el bus intentando leerlo cada 1 segundo (lo cual generaría demoras por timeout de 300 ms en cada ciclo), el gestor reduce su tasa de consulta a un sondeo lento (*probe ping*) cada 10 segundos, preservando el ancho de banda del bus para los dispositivos sanos.
5. **Preparación para Persistencia con `microconf` y EEPROM**:
   - Los descriptores de configuración (`modbus_slave_descriptor_t`) y transporte (`modbus_bus_transport_cfg_t`) son estructuras de memoria plana con tamaños fijos y sin punteros, listas para ser serializadas/deserializadas por bloques.

### 6.2 Driver del Sensor de Temperatura CWT-TM-16PT

El sensor industrial **CWT-TM-16PT** (16 canales PT100/PT1000) se decodifica en
[`cwt_tm16pt_codec.c`](app/src/domain/services/cwt_tm16pt_codec.c) (código puro, sin
E/S); la lectura de registros la realiza el Active Object a través del puerto
`i_modbus_client`, nunca incluyendo nanoMODBUS:
- **Lectura Rápida en Ráfaga**: Lee los 16 canales en una única solicitud de 16 palabras (`0x0020` a `0x002F`), requiriendo apenas ~45 ms de tiempo de bus a 9600 baudios.
- **Detección de Sonda Abierta**: Detecta el código centinela `0x7FFF` (3276.7 °C) o `-32768` y lo clasifica automáticamente en la máscara `open_probe_mask` sin generar valores espurios.
- **Reconfiguración Remota**: Puede reprogramar el Slave ID, baudrate y paridad del sensor mediante escritura en el registro `0x0010`.

---

## 7. Arquitectura del Subsistema de Almacenamiento Persistente (microconf)

> **Documento Maestro**: Para el mapa completo de particionamiento de la EEPROM AT24C256C, políticas de debounce y guía para registrar nuevos dominios de configuración, consultar [**doc/architecture/storage_subsystem.md**](architecture/storage_subsystem.md).

### 7.1 El Desafío de Concurrencia y la Solución con Active Object
La EEPROM física externa AT24C256C (I2C2) requiere entre 5 ms y 10 ms por ciclo de escritura de página. Si un hilo de control o de comunicación serie escribe de forma directa y síncrona en la EEPROM, se congelaría provocando pérdidas de tramas y jitter severo. Además, `microconf` no posee primitivas de sincronización multihilo internas.

Por ello, la arquitectura desacopla el almacenamiento en tres niveles:
1. **Memoria RAM**: Los subsistemas leen y mutan su configuración en memoria de forma instantánea bajo mutex internos (`modbus_registry`, `ao_network`).
2. **Notificación en Bus Zbus**: Al mutar una configuración, se emite un evento en el canal `chan_system_config`.
3. **Coordinador Asíncrono (`ao_storage`)**: Un Active Object dedicado recibe los eventos, aplica una ventana de retardo (*debounce* de 500 ms) para amortiguar ráfagas de cambios, y ejecuta de forma atómica y en segundo plano `mconf_save()`.

### 7.2 Particionamiento Modular de la EEPROM
Para evitar que un cambio en la configuración de red o de telemetría invalide los datos de Modbus o de calibración, la EEPROM de 32 KB se divide en regiones virtuales independientes mediante `mconf_zephyr_eeprom_init()`:
- `0x0000` (1 KB): Identidad del Dispositivo (`CONFIG_DOMAIN_DEVICE`)
- `0x0400` (2 KB): Registro Modbus y Buses (`CONFIG_DOMAIN_MODBUS`)
- `0x0C00` (1 KB): Parámetros de Red Ethernet (`CONFIG_DOMAIN_NET_ETH`)
- `0x1000` (1 KB): Parámetros de Red Wi-Fi (`CONFIG_DOMAIN_NET_WIFI`)
- `0x1400` (3 KB): Calibraciones y Offsets Analógicos (`CONFIG_DOMAIN_CALIBRATION`)
- `0x4000` (16 KB): Registro circular de auditoría (*Event Log*)

---

## 8. Flujo de Trabajo para Añadir un Nuevo Módulo

Cuando el equipo desarrolle una nueva característica (ej: Lectura de Energía por I2C / SPI):

1. **Definir el Puerto en `include/interfaces/`**:  
   Crear `i_energy_meter.h` siguiendo la **regla obligatoria de §4.1.D**: v-table
   `i_energy_meter_api_t`, handle `i_energy_meter_t` y un wrapper `static inline`
   validado por operación. Nadie consume la v-table directamente.
2. **Crear el Fake en `tests/fakes/`**:  
   Implementar `fake_energy_meter.c` para registrar lecturas simuladas y verificar llamadas.
3. **Escribir las Pruebas Unitarias del Dominio (TDD Rojo)**:  
   Crear `tests/unit/domain/test_energy_service.c` probando los cálculos de acumulación de kWh y detección de fallo.
4. **Implementar la Lógica de Dominio (TDD Verde)**:  
   Implementar `app/src/domain/services/energy_service.c`.
5. **Implementar el Adaptador de Hardware en `infrastructure/`**:  
   Crear `app/src/infrastructure/adapters/adapter_energy_ade7953.c` llamando a la API I2C/SPI de Zephyr.
6. **Conectar en Devicetree y Composition Root**:  
   Añadir el nodo en el overlay de la placa (`app/boards/brightc-controllerv3.overlay`) y registrar la vinculación en `composition_root.c`.
7. **Integrar con Zbus o Shell**:  
   Publicar los datos procesados en un canal Zbus o crear un comando de inspección en `app/src/shell/`.

---

## 9. Gobernanza de Código y Estándares

- **Estándar C**: ISO C99 o C11.
- **Tipos de Enteros**: Usar siempre `<stdint.h>` (`uint8_t`, `int32_t`, etc.). Prohibido usar `unsigned int`, `long` o `short` desnudos.
- **Manejo de Errores**: Retornar siempre códigos de error POSIX estándar negativos (`-EINVAL`, `-ENODEV`, `-ETIMEDOUT`, `-EIO`).
- **Nombres de Archivos**: `snake_case` estricto (ej: `telemetry_manager.c`, `i_modbus_port.h`).
- **Prefijos de Interfaces**: Todo archivo de interfaz pública debe iniciar con el prefijo `i_` (ej: `i_storage.h`).
- **Prefijos de Adaptadores**: Toda implementación física debe iniciar con `adapter_` (ej: `adapter_storage_nvs.c`).
- **Documentación**: Comentarios Doxygen en todas las funciones públicas de interfaz (`@brief`, `@param`, `@return`).
- **Puertos**: todo puerto cumple la regla de §4.1.D (v-table + handle + wrappers `static inline` con validación). Un puerto sin wrappers validados se rechaza en revisión.
- **Acceso a la v-table**: `handle->api->...` sólo está permitido en el adaptador que implementa el puerto y en la raíz de composición.
- **Definición de Stacks**: Prohibido embeber `k_thread_stack_t` dentro de structs en `.bss`. Usar exclusivamente `K_THREAD_STACK_DEFINE` o `K_THREAD_STACK_ARRAY_DEFINE` a nivel de archivo para preservar alineamiento MPU y guardias de stack.
