# Arquitectura de Indicadores de Sistema (LED + Buzzer)

- **Estado**: Especificación Técnica de Arquitectura
- **Plataforma**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Entorno de Ejecución**: Zephyr RTOS (Topología West T2)
- **Autor**: Equipo de Firmware BrightC
- **Idioma del documento**: Español (documentación de arquitectura); código en inglés

---

## 1. Visión General

### 1.1 ¿Para qué existe este subsistema?

El BrightC Controller V3 dispone de dos dispositivos de señalización física:

| Dispositivo | Medio | Nodo DTS | Alias / Label |
|-------------|-------|----------|---------------|
| **LED de usuario** | GPIO (compat `gpio-leds`) | `user_led: led_0` en `leds` | `led0 = &user_led` |
| **Buzzer** | PWM (compat `pwm-leds`) | `buzzer_pwm: buzzer` en `buzzer` | PWM15, canal 1 |

El subsistema de indicadores de sistema tiene como propósito **comunicar al operador el estado del dispositivo mediante señales visuales y acústicas**, sin que los subsistemas emisores conozcan cómo se implementa físicamente el LED o el buzzer.

Se utiliza para:

- **Estado de salud del sistema** (sano, degradado, fallo)
- **Alarmas activas** (informativa, advertencia, crítica)
- **Eventos transitorios** (inicio del firmware, configuración guardada, error de comunicación)
- **Identificación del dispositivo** (parpadeo forzado para localizar la unidad físicamente)

### 1.2 ¿Qué NO hace este subsistema?

- No detecta sensores ni toma decisiones de control por sí mismo.
- No persiste estados de alarma en EEPROM (las alarmas son transitorias).
- No implementa el motor de alarmas completo (pendiente de definición futura). Se limita a consumir eventos de alarma y salud publicados por otros subsistemas.
- No reemplaza a Zbus como transporte de datos de telemetría; solo consume eventos de estado.

---

## 2. Decisiones de Diseño (Cerradas)

| # | Decisión | Justificación |
|---|----------|---------------|
| D1 | **Un solo Active Object `ao_system_indicator`** que coordina LED y buzzer | Evita dos hilos, dos stacks (riesgo MPU en Cortex-M7 con `CONFIG_HW_STACK_PROTECTION`) y condiciones de carrera al actuar sobre dispositivos físicos compartidos. |
| D2 | **Dos puertos separados**: `i_led.h` y `i_buzzer.h` | Son dispositivos físicos distintos con APIs distintas (GPIO vs PWM). Un puerto común forzaría abstracciones artificiales. |
| D3 | **Máquina de estados con prioridad de alarma** | CRITICAL > WARNING > INFO > HEALTHY. Cuando hay varias alarmas activas, se muestra la de mayor prioridad. |
| D4 | **Zbus como canal de entrada** | `ao_system_indicator` se suscribe a `chan_system_alarm` y `chan_system_health`. Cualquier componente publica ahí sin conocer al indicador. |
| D5 | **Comandos manuales vía `chan_indicator_command`** | Permite al Shell o HMI forzar un estado (ej. parpadeo para identificar dispositivo) independientemente de alarmas. |
| D6 | **Temporización por software dentro del AO** | El AO usa ticks del sistema (`k_uptime_get_32()` o `k_timer`) para manejar parpadeo del LED y tonos del buzzer sin bloquear el hilo. |
| D7 | **Buzzer con frecuencia configurable por PWM** | Permite generar tonos distintos según el tipo de alarma o evento. |

---

## 3. Arquitectura por Capas (Clean Architecture)

```
┌─────────────────────────────────────────────────────────────────────┐
│  EMISORES DE EVENTOS (cualquier subsistema)                        │
│  - modbus_registry, ao_network, ao_storage, slave_scheduler, etc. │
│  - Publican en chan_system_alarm / chan_system_health              │
└─────────────────────────────────────────────────────────────────────┘
                              │ Zbus
┌─────────────────────────────────────────────────────────────────────┐
│  CAPA DE APLICACIÓN — ao_system_indicator (Active Object único)    │
│  - Máquina de estados con prioridad de alarma                      │
│  - Temporización por software para parpadeo LED y tonos buzzer     │
│  - Orquesta LED y buzzer según estado actual                       │
│  - Aplica comandos manuales desde Shell/HMI                        │
└─────────────────────────────────────────────────────────────────────┘
                              │ i_led_t / i_buzzer_t (puertos)
┌─────────────────────────────────────────────────────────────────────┐
│  PUERTOS — i_led.h  y  i_buzzer.h                                  │
│  - V-tables y handles independientes                               │
│  - Wrappers static inline validados                                │
└─────────────────────────────────────────────────────────────────────┘
                              │ implementa
┌─────────────────────────────────────────────────────────────────────┐
│  CAPA DE INFRAESTRUCTURA — Adaptadores                             │
│  - adapter_led_gpio.c     → <zephyr/drivers/gpio.h>                │
│  - adapter_buzzer_pwm.c   → <zephyr/drivers/pwm.h>                 │
└─────────────────────────────────────────────────────────────────────┘
                              │ DEVICE_DT_GET()
┌─────────────────────────────────────────────────────────────────────┐
│  HARDWARE / DEVICETREE                                              │
│  - user_led: led_0 (gpio-leds)                                     │
│  - buzzer_pwm: buzzer (pwm-leds, PWM15 canal 1)                   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 4. Nivel de Dominio — Tipos de Datos (Puro C99)

Archivo: `app/include/common/indicator_types.h` (por crear)

### 4.1 Estados del LED

```c
typedef enum {
    LED_STATE_OFF = 0,           /**< LED apagado */
    LED_STATE_ON,                /**< LED encendido fijo */
    LED_STATE_BLINK_SLOW,        /**< Parpadeo lento (~1 Hz) */
    LED_STATE_BLINK_FAST,        /**< Parpadeo rápido (~5 Hz) */
    LED_STATE_BLINK_HEARTBEAT,   /**< Latido suave (indica vida del sistema) */
} led_state_t;
```

### 4.2 Patrones del buzzer

```c
typedef enum {
    BUZZER_PATTERN_SILENT = 0,           /**< Sin sonido */
    BUZZER_PATTERN_BEEP_SHORT,           /**< Un beep corto */
    BUZZER_PATTERN_BEEP_LONG,            /**< Un beep largo */
    BUZZER_PATTERN_ALARM_INTERMITTENT,   /**< Beeps repetidos con pausa */
    BUZZER_PATTERN_ALARM_CONTINUOUS,     /**< Sonido continuo */
} buzzer_pattern_t;
```

### 4.3 Niveles de alarma y salud

```c
typedef enum {
    ALARM_LEVEL_NONE = 0,        /**< Sin alarma activa */
    ALARM_LEVEL_INFO,            /**< Informativa (solo LED suave) */
    ALARM_LEVEL_WARNING,         /**< Advertencia (LED + beep intermitente) */
    ALARM_LEVEL_CRITICAL,        /**< Crítica (LED rápido + sonido continuo o intermitente rápido) */
} alarm_level_t;

typedef enum {
    SYSTEM_HEALTH_HEALTHY = 0,   /**< Sistema sano */
    SYSTEM_HEALTH_DEGRADED,      /**< Degradado (algún subsistema con problemas) */
    SYSTEM_HEALTH_FAULT,         /**< Fallo (sistema no operativo plenamente) */
} system_health_t;
```

### 4.4 Comando manual (desde Shell/CLI/HMI)

```c
typedef enum {
    INDICATOR_CMD_AUTO = 0,      /**< Volver a control automático por alarma/health */
    INDICATOR_CMD_FORCE_LED,     /**< Forzar estado del LED */
    INDICATOR_CMD_FORCE_BUZZER,  /**< Forzar patrón del buzzer */
    INDICATOR_CMD_TEST,          /**< Test: secuencia completa LED+buzzer */
} indicator_cmd_t;

typedef struct {
    indicator_cmd_t      cmd;
    led_state_t          led_state;           /* usado si cmd == FORCE_LED */
    buzzer_pattern_t     buzzer_pattern;      /* usado si cmd == FORCE_BUZZER */
    uint32_t             duration_ms;         /* duración del forzado (0 = permanente) */
} indicator_cmd_msg_t;
```

### 4.5 Estado actual de indicadores (para consulta)

```c
typedef struct {
    led_state_t         led_state;
    buzzer_pattern_t    buzzer_pattern;
    alarm_level_t       active_alarm_level;
    system_health_t     system_health;
    bool                manual_override;  /* true si hay comando FORCE activo */
} indicator_status_t;
```

**Invariantes del dominio:**
- Solo headers de libc (`stdint.h`, `stdbool.h`, `stddef.h`). Cero `<zephyr/...>`.
- Structs planos, sin punteros.

---

## 5. Nivel de Puerto — `i_led.h` y `i_buzzer.h`

Siguiendo la regla obligatoria de `doc/STRUCTURE.md §4.1.D` (v-table + handle + wrappers `static inline`).

### 5.1 `app/include/interfaces/i_led.h`

```c
typedef struct {
    int (*set_state)(void *impl, led_state_t state);
    int (*get_state)(void *impl, led_state_t *out_state);
    int (*set_blink_rate)(void *impl, uint32_t period_ms, uint32_t duty_cycle_pct);
} i_led_api_t;

typedef struct {
    const i_led_api_t *api;
    void *impl;
} i_led_t;

static inline bool i_led_is_bound(const i_led_t *led);
static inline int i_led_set_state(const i_led_t *led, led_state_t state);
static inline int i_led_get_state(const i_led_t *led, led_state_t *out_state);
static inline int i_led_set_blink_rate(const i_led_t *led, uint32_t period_ms,
                                       uint32_t duty_cycle_pct);
```

### 5.2 `app/include/interfaces/i_buzzer.h`

```c
typedef struct {
    int (*play_pattern)(void *impl, buzzer_pattern_t pattern, uint32_t frequency_hz);
    int (*stop)(void *impl);
    int (*get_active_pattern)(void *impl, buzzer_pattern_t *out);
} i_buzzer_api_t;

typedef struct {
    const i_buzzer_api_t *api;
    void *impl;
} i_buzzer_t;

static inline bool i_buzzer_is_bound(const i_buzzer_t *buzzer);
static inline int i_buzzer_play_pattern(const i_buzzer_t *buzzer,
                                        buzzer_pattern_t pattern,
                                        uint32_t frequency_hz);
static inline int i_buzzer_stop(const i_buzzer_t *buzzer);
static inline int i_buzzer_get_active_pattern(const i_buzzer_t *buzzer,
                                              buzzer_pattern_t *out);
```

**Nota:** El parámetro `frequency_hz` en `play_pattern` permite generar tonos distintos según la alarma (ej. tono grave para CRITICAL, tono agudo para INFO).

**Invariante:** `handle->api->...` solo se invoca dentro del adaptador y en `composition_root.c`.

---

## 6. Nivel de Adaptadores (Infraestructura)

### 6.1 `adapter_led_gpio.c`

Archivos: `app/src/infrastructure/adapters/adapter_led_gpio.c/.h`

Convención de nomenclatura: `adapter_led_gpio_init()`, `adapter_led_gpio_port()`.

| Operación | Implementación Zephyr |
|-----------|----------------------|
| `init` | `GPIO_DT_SPEC_GET_OR(DT_NODELABEL(user_led), gpios, {0})`, `device_is_ready()`, `gpio_pin_configure_dt()` como salida |
| `set_state` | `gpio_pin_set_dt()` con valor según `led_state_t` (ON/OFF). Para estados BLINK, el adaptador solo guarda el estado solicitado; el parpadeo real lo ejecuta `ao_system_indicator` alternando `set_state` |
| `get_state` | Leer estado actual guardado en contexto del adaptador |
| `set_blink_rate` | Guardar parámetros de parpadeo (periodo y duty cycle) en el contexto del adaptador |

Contexto del adaptador (`impl`):

```c
struct adapter_led_gpio_ctx {
    struct gpio_dt_spec gpio_spec;
    led_state_t         current_state;
    uint32_t            blink_period_ms;
    uint32_t            blink_duty_cycle_pct;
    bool                is_ready;
};
```

### 6.2 `adapter_buzzer_pwm.c`

Archivos: `app/src/infrastructure/adapters/adapter_buzzer_pwm.c/.h`

Convención: `adapter_buzzer_pwm_init()`, `adapter_buzzer_pwm_port()`.

| Operación | Implementación Zephyr |
|-----------|----------------------|
| `init` | Obtener dispositivo PWM desde DTS (`buzzer_pwm`), `pwm_configure()` |
| `play_pattern` | Configurar PWM con frecuencia y duty cycle según `buzzer_pattern_t` y `frequency_hz`. Para patrones intermitentes, `ao_system_indicator` enciende/apaga el buzzer periódicamente |
| `stop` | `pwm_set_cycles(dev, chan, 0, 0)` o duty cycle a 0 |
| `get_active_pattern` | Devolver patrón actual guardado en contexto |

Contexto del adaptador:

```c
struct adapter_buzzer_pwm_ctx {
    const struct device *pwm_dev;
    uint32_t             pwm_channel;
    buzzer_pattern_t     current_pattern;
    uint32_t             current_frequency_hz;
    bool                 is_ready;
};
```

---

## 7. Nivel de Aplicación — `ao_system_indicator` (Active Object único)

Archivos: `app/src/application/active_objects/ao_system_indicator.c/.h`

### 7.1 Responsabilidades

1. **Máquina de estados con prioridad:** Determinar qué mostrar en LED y buzzer según la alarma de mayor prioridad activa.
2. **Temporización por software:** Usar ticks del sistema (`k_uptime_get_32()` o `k_timer`) para alternar estados de LED y buzzer sin bloquear el hilo.
3. **Comandos manuales:** Procesar `chan_indicator_command` (forzar LED/buzzer, test).
4. **Publicación de estado:** Publicar `indicator_status_t` en `chan_indicator_status` para consulta de otros subsistemas.

### 7.2 Máquina de Estados — Prioridad de Alarma

```
Entrada: alarma_level + system_health + comando manual + ticks del sistema

Prioridad (de mayor a menor):
1. Comando manual FORCE (si activo y dentro de duración)
2. ALARM_LEVEL_CRITICAL      → LED BLINK_FAST + Buzzer ALARM_CONTINUOUS (tono bajo)
3. ALARM_LEVEL_WARNING       → LED BLINK_SLOW  + Buzzer ALARM_INTERMITTENT (tono medio)
4. ALARM_LEVEL_INFO          → LED BLINK_HEARTBEAT + Buzzer SILENT
5. SYSTEM_HEALTH_DEGRADED    → LED BLINK_SLOW + Buzzer SILENT
6. SYSTEM_HEALTH_HEALTHY     → LED ON (fijo) + Buzzer SILENT
7. Default                   → LED OFF + Buzzer SILENT
```

El AO mantiene una tabla interna de alarmas activas (alarm_id → level). Cuando llega un evento de alarma, actualiza la tabla y recalcula el nivel máximo activo.

### 7.3 Temporización por Software

El AO ejecuta un bucle principal que:

1. Espera eventos en su cola de mensajes (`k_msgq`) con timeout corto.
2. Al despertar (por evento o por timeout), calcula el tiempo transcurrido desde la última actualización.
3. Si el estado actual requiere parpadeo (LED BLINK_* o buzzer intermitente), alterna el estado según el tiempo transcurrido.
4. Llama a `i_led_set_state()` o `i_buzzer_play_pattern()`/`i_buzzer_stop()` según corresponda.

Esto evita bloqueos y permite que el hilo responda rápidamente a nuevos eventos Zbus.

### 7.4 Eventos de Entrada (Zbus)

| Canal | Mensaje | Origen típico |
|-------|---------|---------------|
| `chan_system_alarm` | `alarm_msg_t { alarm_id, level, is_active, timestamp }` | modbus_registry, slave_scheduler, ao_network, etc. |
| `chan_system_health` | `system_health_t { health, timestamp }` | Orquestador de health global (futuro) |
| `chan_indicator_command` | `indicator_cmd_msg_t` | Shell, HMI, CLI |

### 7.5 Salidas (Zbus)

| Canal | Mensaje | Propósito |
|-------|---------|-----------|
| `chan_indicator_status` | `indicator_status_t` | Consulta externa de qué se está mostrando |

---

## 8. Canales Zbus

Archivos: `app/src/application/zbus/indicator_channels.{c,h}` (por crear)

```c
/* Mensaje de alarma (publicado por cualquier subsistema) */
typedef struct {
    uint16_t      alarm_id;
    alarm_level_t level;
    bool          is_active;
    uint32_t      timestamp_ms;
} alarm_msg_t;

/* Mensaje de health del sistema */
typedef struct {
    system_health_t health;
    uint32_t        timestamp_ms;
} system_health_msg_t;

/* Canales */
ZBUS_CHAN_DECLARE(chan_system_alarm);
ZBUS_CHAN_DECLARE(chan_system_health);
ZBUS_CHAN_DECLARE(chan_indicator_command);
ZBUS_CHAN_DECLARE(chan_indicator_status);
```

Suscriptores de `chan_system_alarm` y `chan_system_health`: `ao_system_indicator`.
Publicadores: cualquier subsistema que detecte una condición de alarma (ej. `slave_scheduler` si un esclavo Modbus se desconecta, `ao_network` si Ethernet pierde enlace).

---

## 9. Ejemplos de Uso

### 9.1 Esclavo Modbus desconectado

```c
/* En slave_scheduler.c o modbus_registry.c */
alarm_msg_t msg = {
    .alarm_id = ALARM_ID_MODBUS_SLAVE_OFFLINE,
    .level = ALARM_LEVEL_WARNING,
    .is_active = true,
    .timestamp_ms = k_uptime_get_32(),
};
zbus_chan_pub(&chan_system_alarm, &msg, K_MSEC(10));
```

`ao_system_indicator` recibe el mensaje, actualiza su tabla de alarmas activas, determina que WARNING es la prioridad más alta, y activa LED BLINK_SLOW + buzzer ALARM_INTERMITTENT.

### 9.2 Ethernet recupera conexión

```c
/* En ao_network.c */
alarm_msg_t msg = {
    .alarm_id = ALARM_ID_ETHERNET_LINK,
    .level = ALARM_LEVEL_NONE,
    .is_active = false,
    .timestamp_ms = k_uptime_get_32(),
};
zbus_chan_pub(&chan_system_alarm, &msg, K_MSEC(10));
```

Si no hay otras alarmas activas, `ao_system_indicator` vuelve a estado HEALTHY: LED ON fijo, buzzer SILENT.

### 9.3 Shell fuerza parpadeo para identificar dispositivo

```c
/* En telemetry_shell.c o comando dedicado */
indicator_cmd_msg_t cmd = {
    .cmd = INDICATOR_CMD_FORCE_LED,
    .led_state = LED_STATE_BLINK_FAST,
    .buzzer_pattern = BUZZER_PATTERN_SILENT,
    .duration_ms = 10000,  /* 10 segundos */
};
zbus_chan_pub(&chan_indicator_command, &cmd, K_MSEC(10));
```

---

## 10. Composición Root — Inicialización

Archivo: `app/src/infrastructure/di/composition_root.c` (modificar)

Único lugar con `DEVICE_DT_GET()` para LED y buzzer:

```c
static struct adapter_led_gpio_ctx s_led_ctx;
static struct adapter_buzzer_pwm_ctx s_buzzer_ctx;

static int init_indicators(app_container_t *self)
{
    const struct device *led_dev = DEVICE_DT_GET(DT_NODELABEL(user_led));
    const struct device *pwm_dev = DEVICE_DT_GET(DT_NODELABEL(buzzer_pwm));

    if (!device_is_ready(led_dev)) return -ENODEV;
    if (!device_is_ready(pwm_dev)) return -ENODEV;

    int err = adapter_led_gpio_init(&s_led_ctx, led_dev);
    if (err != 0) return err;

    err = adapter_buzzer_pwm_init(&s_buzzer_ctx, pwm_dev);
    if (err != 0) return err;

    adapter_led_gpio_port(&s_led_ctx, &self->led);
    adapter_buzzer_pwm_port(&s_buzzer_ctx, &self->buzzer);

    return 0;
}
```

En `composition_root_init()` se llama a `init_indicators()` y luego se inicia `ao_system_indicator`.

---

## 11. Secuencia de Arranque

```
power-on
  │
  ▼
composition_root_init(&g_app)
  ├─ ... (inicializaciones existentes: modbus, red, storage)
  ├─ adapter_led_gpio_init()     ← NUEVO: DEVICE_DT_GET(user_led)
  ├─ adapter_buzzer_pwm_init()   ← NUEVO: DEVICE_DT_GET(buzzer_pwm)
  ├─ adapter_led_gpio_port()     ← NUEVO: bind i_led_t
  ├─ adapter_buzzer_pwm_port()   ← NUEVO: bind i_buzzer_t
  └─ ao_system_indicator_init()  ← NUEVO: crear hilo AO, suscribirse a Zbus
  │
  ▼
composition_root_start(&g_app)
  └─ ao_system_indicator_start() ← NUEVO: lanzar hilo de indicadores
  │
  ▼
bucle principal (sleep)
```

Estado inicial tras arranque: LED ON (sistema sano), buzzer SILENT, hasta que llegue el primer evento de alarma o health.

---

## 12. Stacks (Invariante MPU)

La stack del hilo `ao_system_indicator` se define a **file scope** con `K_THREAD_STACK_DEFINE`, nunca dentro de `app_container_t`, para preservar el alineamiento MPU en Cortex-M7 con `CONFIG_HW_STACK_PROTECTION`.

---

## 13. Brechas por Implementar

| # | Elemento | Archivo destino | Estado |
|---|----------|-----------------|--------|
| 1 | `indicator_types.h` | `app/include/common/indicator_types.h` | Faltante |
| 2 | `i_led.h` | `app/include/interfaces/i_led.h` | Faltante |
| 3 | `i_buzzer.h` | `app/include/interfaces/i_buzzer.h` | Faltante |
| 4 | `adapter_led_gpio.c/.h` | `app/src/infrastructure/adapters/` | Faltante |
| 5 | `adapter_buzzer_pwm.c/.h` | `app/src/infrastructure/adapters/` | Faltante |
| 6 | `ao_system_indicator.c/.h` | `app/src/application/active_objects/` | Faltante |
| 7 | `indicator_channels.{c,h}` (Zbus) | `app/src/application/zbus/` | Faltante |
| 8 | Integración en `composition_root.c` | `app/src/infrastructure/di/` | Por modificar |
| 9 | Publicadores de alarma en subsistemas existentes | modbus, network, etc. | Por agregar (pendiente motor de alarmas) |
| 10 | Pruebas unitarias (fakes LED/buzzer) | `tests/unit/` | Faltante |

---

## 14. Decisiones Pendientes (para futuro)

1. **Motor de alarmas completo:** Aún no definido. Queda pendiente definir `alarm_id_t` completo y qué subsistemas publican en `chan_system_alarm`.
2. **Persistencia de configuración de indicadores:** Opcional. Podría agregarse un dominio `CONFIG_DOMAIN_INDICATORS` en storage para guardar patrones por defecto.
3. **Integración con Shell:** Comandos de CLI para consultar estado de indicadores y forzar pruebas manuales.

---

**Estado del documento**: Especificación técnica lista para implementación futura.  
**Estado del código**: Ningún archivo ha sido modificado aún. Todo está por construir sobre las bases definidas (DTS con `user_led` y `buzzer_pwm`, arquitectura limpia, patrón de puertos y adaptadores).