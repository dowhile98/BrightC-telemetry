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
    │   │   │   ├── i_modbus_port.h          # Contrato de comunicación Modbus RTU/TCP
    │   │   │   ├── i_storage_port.h         # Contrato para NVS / Configuración
    │   │   │   ├── i_sensor_port.h          # Contrato de adquisición de mediciones
    │   │   │   ├── i_relay_port.h           # Contrato de salidas discretas / control
    │   │   │   └── i_display_port.h         # Contrato para interfaz HMI DWIN
    │   │   └── common/                      # Tipos de datos primitivos agnósticos
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
    │       │   │   ├── ao_network.c         # Hilo de conectividad (Ethernet / WiFi ESP32)
    │       │   │   └── ao_hmi.c             # Hilo de atención a pantalla LCD DWIN
    │       │   ├── zbus/                    # Canales de eventos Zephyr Zbus
    │       │   │   ├── telemetry_channels.h # Definición de zbus_channel (muestras, alarmas)
    │       │   │   └── telemetry_channels.c # Instanciación ZBUS_CHAN_DEFINE
    │       │   └── use_cases/               # Flujos (ProcessTelemetrySample, PublishMqttReport)
    │       │
    │       ├── infrastructure/              # Adaptadores a Zephyr y composición
    │       │   ├── adapters/                # Implementación de los puertos con APIs de Zephyr
    │       │   │   ├── adapter_modbus_zephyr.c    # nanoMODBUS montado sobre UART/RS485 Zephyr
    │       │   │   ├── adapter_storage_nvs.c      # Persistencia con Zephyr Settings / NVS
    │       │   │   ├── adapter_sensor_zephyr.c    # Lectura de sensores usando <zephyr/drivers/sensor.h>
    │       │   │   ├── adapter_relay_gpio.c       # Manejo de relés usando <zephyr/drivers/gpio.h>
    │       │   │   ├── adapter_wifi_esp_hosted.c  # Driver de red ESP-Hosted (WiFi / BLE)
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
`app/include/interfaces/i_relay_port.h`:
```c
#ifndef I_RELAY_PORT_H_
#define I_RELAY_PORT_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RELAY_CHANNEL_MAIN_POWER = 0,
    RELAY_CHANNEL_AUXILIARY,
    RELAY_CHANNEL_MAX
} relay_channel_t;

typedef enum {
    RELAY_STATE_OPEN = 0,
    RELAY_STATE_CLOSED
} relay_state_t;

/* V-Table pura de operaciones del relé */
struct i_relay_api {
    int (*set_state)(relay_channel_t channel, relay_state_t state);
    int (*get_state)(relay_channel_t channel, relay_state_t *out_state);
};

typedef struct {
    const struct i_relay_api *api;
    void *context; /* Contexto opaco del adaptador */
} relay_port_t;

#endif /* I_RELAY_PORT_H_ */
```

#### B. El Adaptador de Infraestructura (Conectado a Zephyr GPIO)
`app/src/infrastructure/adapters/adapter_relay_gpio.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "interfaces/i_relay_port.h"

struct relay_gpio_context {
    struct gpio_dt_spec gpio_spec[RELAY_CHANNEL_MAX];
};

static int relay_gpio_set(relay_channel_t channel, relay_state_t state)
{
    if (channel >= RELAY_CHANNEL_MAX) {
        return -EINVAL;
    }
    /* Se interactúa directamente con el driver de Zephyr */
    extern struct relay_gpio_context g_relay_hw_ctx;
    return gpio_pin_set_dt(&g_relay_hw_ctx.gpio_spec[channel], (int)state);
}

static int relay_gpio_get(relay_channel_t channel, relay_state_t *out_state)
{
    if (channel >= RELAY_CHANNEL_MAX || !out_state) {
        return -EINVAL;
    }
    extern struct relay_gpio_context g_relay_hw_ctx;
    int val = gpio_pin_get_dt(&g_relay_hw_ctx.gpio_spec[channel]);
    if (val < 0) return val;
    *out_state = val ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
    return 0;
}

static const struct i_relay_api s_relay_api = {
    .set_state = relay_gpio_set,
    .get_state = relay_gpio_get,
};

const relay_port_t g_relay_port = {
    .api = &s_relay_api,
    .context = NULL,
};
```

#### C. Composition Root (El puente estático de Hardware a Software)
`app/src/infrastructure/di/composition_root.c`:
```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "interfaces/i_relay_port.h"

LOG_MODULE_REGISTER(comp_root, LOG_LEVEL_INF);

/* Estructura instanciada desde el Devicetree */
struct relay_gpio_context g_relay_hw_ctx = {
    .gpio_spec = {
        [RELAY_CHANNEL_MAIN_POWER] = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(relay_main), gpios, {0}),
        [RELAY_CHANNEL_AUXILIARY]  = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(relay_aux), gpios, {0}),
    }
};

int composition_root_init(void)
{
    LOG_INF("Inicializando Composition Root & Verificación de Periféricos...");

    for (int i = 0; i < RELAY_CHANNEL_MAX; i++) {
        if (!gpio_is_ready_dt(&g_relay_hw_ctx.gpio_spec[i])) {
            LOG_ERR("Relay GPIO %d no está listo en Devicetree", i);
            return -ENODEV;
        }
        gpio_pin_configure_dt(&g_relay_hw_ctx.gpio_spec[i], GPIO_OUTPUT_INACTIVE);
    }

    LOG_INF("Hardware vinculado correctamente a los puertos.");
    return 0;
}
```

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
`tests/fakes/src/fake_relay_port.c`:
```c
#include "interfaces/i_relay_port.h"

static relay_state_t s_fake_states[RELAY_CHANNEL_MAX];
static int s_call_count[RELAY_CHANNEL_MAX];

void fake_relay_reset(void) {
    for (int i = 0; i < RELAY_CHANNEL_MAX; i++) {
        s_fake_states[i] = RELAY_STATE_OPEN;
        s_call_count[i] = 0;
    }
}

static int fake_relay_set(relay_channel_t channel, relay_state_t state) {
    s_fake_states[channel] = state;
    s_call_count[channel]++;
    return 0;
}

static int fake_relay_get(relay_channel_t channel, relay_state_t *out_state) {
    *out_state = s_fake_states[channel];
    return 0;
}

static const struct i_relay_api s_fake_api = {
    .set_state = fake_relay_set,
    .get_state = fake_relay_get,
};

const relay_port_t g_fake_relay_port = {
    .api = &s_fake_api,
    .context = NULL,
};

relay_state_t fake_relay_spy_state(relay_channel_t ch) { return s_fake_states[ch]; }
int fake_relay_spy_calls(relay_channel_t ch) { return s_call_count[ch]; }
```

`tests/unit/domain/test_alarm_evaluator.c`:
```c
#include <zephyr/ztest.h>
#include "domain/services/alarm_evaluator.h"
#include "fake_relay_port.h"

ZTEST_SUITE(alarm_evaluator_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(alarm_evaluator_suite, test_should_trip_relay_on_overcurrent)
{
    fake_relay_reset();
    alarm_evaluator_init(&g_fake_relay_port);

    /* Muestra con corriente normal: relé debe mantenerse abierto */
    telemetry_sample_t normal_sample = { .current_ch1 = 15.0f };
    alarm_evaluator_process(&normal_sample);
    zassert_equal(fake_relay_spy_state(RELAY_CHANNEL_MAIN_POWER), RELAY_STATE_OPEN, 
                  "El relé no debe dispararse con corriente normal");

    /* Muestra con sobrecorriente crítica (60A): relé debe cerrarse/disparar */
    telemetry_sample_t fault_sample = { .current_ch1 = 60.0f };
    alarm_evaluator_process(&fault_sample);
    zassert_equal(fake_relay_spy_state(RELAY_CHANNEL_MAIN_POWER), RELAY_STATE_CLOSED,
                  "El relé debió dispararse ante sobrecorriente");
    zassert_equal(fake_relay_spy_calls(RELAY_CHANNEL_MAIN_POWER), 1, "Debe existir un único disparo");
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
 |                            (modbus_slave_manager.c)                                             |
 |                                                                                                 |
 |  - Mantiene tabla de modbus_slave_descriptor_t (serializable para microconf / EEPROM)           |
 |  - Failover dinámico: reasigna dispositivos entre RS485-1 y RS485-2 en tiempo de ejecución      |
 |  - Máquina de estados de salud con Offline Backoff (aísla esclavos desconectados)               |
 +-----------------------------------------------+-------------------------------------------------+
                         |                                                |
                         v                                                v
 +-----------------------------------------------+  +----------------------------------------------+
 |       HILO RS485-1: ao_modbus_rtu1.c          |  |       HILO RS485-2: ao_modbus_rtu2.c         |
 |  - Instancia nmbs_z_client_t bus1 (<zephyr/nanomodbus/nanomodbus_z.h>)         |  |  - Instancia nmbs_z_client_t bus2 (<zephyr/nanomodbus/nanomodbus_z.h>)        |
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
     modbus_slave_manager_set_bus(DEV_ID_CWT_TM16PT, MODBUS_BUS_RS485_2);
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

El sensor industrial **CWT-TM-16PT** (16 canales PT100/PT1000) se implementa en [`cwt_tm16pt.c`](app/src/infrastructure/modbus/devices/cwt_tm16pt.c):
- **Lectura Rápida en Ráfaga**: Lee los 16 canales en una única solicitud de 16 palabras (`0x0020` a `0x002F`), requiriendo apenas ~45 ms de tiempo de bus a 9600 baudios.
- **Detección de Sonda Abierta**: Detecta el código centinela `0x7FFF` (3276.7 °C) o `-32768` y lo clasifica automáticamente en la máscara `open_probe_mask` sin generar valores espurios.
- **Reconfiguración Remota**: Puede reprogramar el Slave ID, baudrate y paridad del sensor mediante escritura en el registro `0x0010`.

---

## 7. Flujo de Trabajo para Añadir un Nuevo Módulo

Cuando el equipo desarrolle una nueva característica (ej: Lectura de Energía por I2C / SPI):

1. **Definir el Puerto en `include/interfaces/`**:  
   Crear `i_energy_meter.h` definiendo los contratos de datos y la struct API con punteros a función.
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

## 8. Gobernanza de Código y Estándares

- **Estándar C**: ISO C99 o C11.
- **Tipos de Enteros**: Usar siempre `<stdint.h>` (`uint8_t`, `int32_t`, etc.). Prohibido usar `unsigned int`, `long` o `short` desnudos.
- **Manejo de Errores**: Retornar siempre códigos de error POSIX estándar negativos (`-EINVAL`, `-ENODEV`, `-ETIMEDOUT`, `-EIO`).
- **Nombres de Archivos**: `snake_case` estricto (ej: `telemetry_manager.c`, `i_modbus_port.h`).
- **Prefijos de Interfaces**: Todo archivo de interfaz pública debe iniciar con el prefijo `i_` (ej: `i_storage.h`).
- **Prefijos de Adaptadores**: Toda implementación física debe iniciar con `adapter_` (ej: `adapter_storage_nvs.c`).
- **Documentación**: Comentarios Doxygen en todas las funciones públicas de interfaz (`@brief`, `@param`, `@return`).
