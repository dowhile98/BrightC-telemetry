# System Indicators Architecture (LED + Buzzer)

- **Status**: Technical Architecture Specification
- **Platform**: DAECGE BrightC Controller V3 (STM32H743ZIT6, Cortex-M7 @ 480 MHz)
- **Execution Environment**: Zephyr RTOS (West T2 Topology)
- **Author**: BrightC Firmware Team
- **Document language**: Spanish (architecture documentation); code in English

---

## 1. General Overview

### 1.1 What is this subsystem for?

The BrightC Controller V3 has two physical signaling devices:

| Device | Medium | DTS Node | Alias / Label |
|--------|--------|----------|---------------|
| **User LED** | GPIO (compat `gpio-leds`) | `user_led: led_0` in `leds` | `led0 = &user_led` |
| **Buzzer** | PWM (compat `pwm-leds`) | `buzzer_pwm: buzzer` in `buzzer` | PWM15, channel 1 |

The system indicators subsystem exists to **communicate device status to the operator through visual and acoustic signals**, without the emitting subsystems knowing how the LED or buzzer is physically implemented.

It is used for:

- **System health status** (healthy, degraded, failure)
- **Active alarms** (informative, warning, critical)
- **Transient events** (firmware startup, configuration saved, communication error)
- **Device identification** (forced blinking to physically locate the unit)

### 1.2 What this subsystem does NOT do

- Does not detect sensors or make control decisions by itself.
- Does not persist alarm states in EEPROM (alarms are transient).
- Does not implement the complete alarm engine (pending future definition). It is limited to consuming alarm and health events published by other subsystems.
- Does not replace Zbus as telemetry data transport; it only consumes status events.

---

## 2. Design Decisions (Closed)

| # | Decision | Justification |
|---|----------|---------------|
| D1 | **A single `ao_system_indicator` Active Object** coordinating LED and buzzer | Avoids two threads, two stacks (MPU risk on Cortex-M7 with `CONFIG_HW_STACK_PROTECTION`) and race conditions acting on shared physical devices. |
| D2 | **Two separate ports**: `i_led.h` and `i_buzzer.h` | They are physically distinct devices with different APIs (GPIO vs PWM). A common port would force artificial abstractions. |
| D3 | **State machine with alarm priority** | CRITICAL > WARNING > INFO > HEALTHY. When multiple alarms are active, the highest priority one is shown. |
| D4 | **Zbus as input channel** | `ao_system_indicator` subscribes to `chan_system_alarm` and `chan_system_health`. Any component publishes there without knowing the indicator. |
| D5 | **Manual commands via `chan_indicator_command`** | Allows Shell or HMI to force a state (e.g., blinking to identify device) independently of alarms. |
| D6 | **Software timing within the AO** | The AO uses system ticks (`k_uptime_get_32()` or `k_timer`) to handle LED blinking and buzzer tones without blocking the thread. |
| D7 | **Buzzer with configurable frequency via PWM** | Allows generating different tones according to alarm type or event. |

---

## 3. Layered Architecture (Clean Architecture)

```
┌─────────────────────────────────────────────────────────────────────┐
│  EVENT EMITTERS (any subsystem)                                    │
│  - modbus_registry, ao_network, ao_storage, slave_scheduler, etc. │
│  - Publish on chan_system_alarm / chan_system_health              │
└─────────────────────────────────────────────────────────────────────┘
                              │ Zbus
┌─────────────────────────────────────────────────────────────────────┐
│  APPLICATION LAYER — ao_system_indicator (single Active Object)    │
│  - Alarm priority state machine                                    │
│  - Software timing for LED blinking and buzzer tones               │
│  - Orchestrates LED and buzzer according to current state          │
│  - Applies manual commands from Shell/HMI                          │
└─────────────────────────────────────────────────────────────────────┘
                              │ i_led_t / i_buzzer_t (ports)
┌─────────────────────────────────────────────────────────────────────┐
│  PORTS — i_led.h  and  i_buzzer.h                                  │
│  - Independent v-tables and handles                                │
│  - static inline validated wrappers                                │
└─────────────────────────────────────────────────────────────────────┘
                              │ implements
┌─────────────────────────────────────────────────────────────────────┐
│  INFRASTRUCTURE LAYER — Adapters                                   │
│  - adapter_led_gpio.c     → <zephyr/drivers/gpio.h>                │
│  - adapter_buzzer_pwm.c   → <zephyr/drivers/pwm.h>                 │
└─────────────────────────────────────────────────────────────────────┘
                              │ DEVICE_DT_GET()
┌─────────────────────────────────────────────────────────────────────┐
│  HARDWARE / DEVICETREE                                              │
│  - user_led: led_0 (gpio-leds)                                     │
│  - buzzer_pwm: buzzer (pwm-leds, PWM15 channel 1)                 │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 4. Domain Level — Data Types (Pure C99)

File: `app/include/common/indicator_types.h` (to be created)

### 4.1 LED States

```c
typedef enum {
    LED_STATE_OFF = 0,           /**< LED off */
    LED_STATE_ON,                /**< LED steady on */
    LED_STATE_BLINK_SLOW,        /**< Slow blinking (~1 Hz) */
    LED_STATE_BLINK_FAST,        /**< Fast blinking (~5 Hz) */
    LED_STATE_BLINK_HEARTBEAT,   /**< Soft heartbeat (indicates system alive) */
} led_state_t;
```

### 4.2 Buzzer Patterns

```c
typedef enum {
    BUZZER_PATTERN_SILENT = 0,           /**< No sound */
    BUZZER_PATTERN_BEEP_SHORT,           /**< A short beep */
    BUZZER_PATTERN_BEEP_LONG,            /**< A long beep */
    BUZZER_PATTERN_ALARM_INTERMITTENT,   /**< Repeated beeps with pause */
    BUZZER_PATTERN_ALARM_CONTINUOUS,     /**< Continuous sound */
} buzzer_pattern_t;
```

### 4.3 Alarm Levels and Health

```c
typedef enum {
    ALARM_LEVEL_NONE = 0,        /**< No active alarm */
    ALARM_LEVEL_INFO,            /**< Informative (soft LED only) */
    ALARM_LEVEL_WARNING,         /**< Warning (LED + intermittent beep) */
    ALARM_LEVEL_CRITICAL,        /**< Critical (fast LED + continuous or fast intermittent sound) */
} alarm_level_t;

typedef enum {
    SYSTEM_HEALTH_HEALTHY = 0,   /**< Healthy system */
    SYSTEM_HEALTH_DEGRADED,      /**< Degraded (some subsystem with problems) */
    SYSTEM_HEALTH_FAULT,         /**< Fault (system not fully operational) */
} system_health_t;
```

### 4.4 Manual Command (from Shell/CLI/HMI)

```c
typedef enum {
    INDICATOR_CMD_AUTO = 0,      /**< Return to automatic control by alarm/health */
    INDICATOR_CMD_FORCE_LED,     /**< Force LED state */
    INDICATOR_CMD_FORCE_BUZZER,  /**< Force buzzer pattern */
    INDICATOR_CMD_TEST,          /**< Test: full LED+buzzer sequence */
} indicator_cmd_t;

typedef struct {
    indicator_cmd_t      cmd;
    led_state_t          led_state;           /* used if cmd == FORCE_LED */
    buzzer_pattern_t     buzzer_pattern;      /* used if cmd == FORCE_BUZZER */
    uint32_t             duration_ms;         /* forced duration (0 = permanent) */
} indicator_cmd_msg_t;
```

### 4.5 Current Indicator Status (for query)

```c
typedef struct {
    led_state_t         led_state;
    buzzer_pattern_t    buzzer_pattern;
    alarm_level_t       active_alarm_level;
    system_health_t     system_health;
    bool                manual_override;  /* true if a FORCE command is active */
} indicator_status_t;
```

**Domain invariants:**
- Only libc headers (`stdint.h`, `stdbool.h`, `stddef.h`). Zero `<zephyr/...>`.
- Flat structs, no pointers.

---

## 5. Port Level — `i_led.h` and `i_buzzer.h`

Following the mandatory rule of `doc/STRUCTURE.md §4.1.D` (v-table + handle + `static inline` wrappers).

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

**Note:** The `frequency_hz` parameter in `play_pattern` allows generating different tones according to the alarm (e.g. low tone for CRITICAL, high tone for INFO).

**Invariant:** `handle->api->...` is only invoked within the adapter and in `composition_root.c`.

---

## 6. Adapter Level (Infrastructure)

### 6.1 `adapter_led_gpio.c`

Files: `app/src/infrastructure/adapters/adapter_led_gpio.c/.h`

Naming convention: `adapter_led_gpio_init()`, `adapter_led_gpio_port()`.

| Operation | Zephyr Implementation |
|-----------|----------------------|
| `init` | `GPIO_DT_SPEC_GET_OR(DT_NODELABEL(user_led), gpios, {0})`, `device_is_ready()`, `gpio_pin_configure_dt()` as output |
| `set_state` | `gpio_pin_set_dt()` with value according to `led_state_t` (ON/OFF). For BLINK states, the adapter only saves the requested state; actual blinking is executed by `ao_system_indicator` alternating `set_state` |
| `get_state` | Read current state saved in adapter context |
| `set_blink_rate` | Save blink parameters (period and duty cycle) in adapter context |

Adapter context (`impl`):

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

Files: `app/src/infrastructure/adapters/adapter_buzzer_pwm.c/.h`

Convention: `adapter_buzzer_pwm_init()`, `adapter_buzzer_pwm_port()`.

| Operation | Zephyr Implementation |
|-----------|----------------------|
| `init` | Get PWM device from DTS (`buzzer_pwm`), `pwm_configure()` |
| `play_pattern` | Configure PWM with frequency and duty cycle according to `buzzer_pattern_t` and `frequency_hz`. For intermittent patterns, `ao_system_indicator` turns the buzzer on/off periodically |
| `stop` | `pwm_set_cycles(dev, chan, 0, 0)` or duty cycle to 0 |
| `get_active_pattern` | Return current pattern saved in context |

Adapter context:

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

## 7. Application Level — `ao_system_indicator` (single Active Object)

Files: `app/src/application/active_objects/ao_system_indicator.c/.h`

### 7.1 Responsibilities

1. **Priority state machine**: Determine what to show on LED and buzzer according to the highest priority active alarm.
2. **Software timing**: Use system ticks (`k_uptime_get_32()` or `k_timer`) to alternate LED and buzzer states without blocking the thread.
3. **Manual commands**: Process `chan_indicator_command` (force LED/buzzer, test).
4. **Status publication**: Publish `indicator_status_t` on `chan_indicator_status` for query by other subsystems.

### 7.2 State Machine — Alarm Priority

```
Input: alarm_level + system_health + manual command + system ticks

Priority (highest to lowest):
1. Manual FORCE command (if active and within duration)
2. ALARM_LEVEL_CRITICAL      → LED BLINK_FAST + Buzzer ALARM_CONTINUOUS (low tone)
3. ALARM_LEVEL_WARNING       → LED BLINK_SLOW  + Buzzer ALARM_INTERMITTENT (medium tone)
4. ALARM_LEVEL_INFO          → LED BLINK_HEARTBEAT + Buzzer SILENT
5. SYSTEM_HEALTH_DEGRADED    → LED BLINK_SLOW + Buzzer SILENT
6. SYSTEM_HEALTH_HEALTHY     → LED ON (steady) + Buzzer SILENT
7. Default                   → LED OFF + Buzzer SILENT
```

The AO maintains an internal table of active alarms (alarm_id → level). When an alarm event arrives, it updates the table and recalculates the maximum active level.

### 7.3 Software Timing

The AO executes a main loop that:

1. Waits for events in its message queue (`k_msgq`) with a short timeout.
2. Upon waking (by event or timeout), calculates elapsed time since last update.
3. If current state requires blinking (LED BLINK_* or intermittent buzzer), alternates state according to elapsed time.
4. Calls `i_led_set_state()` or `i_buzzer_play_pattern()`/`i_buzzer_stop()` as appropriate.

This avoids blocking and allows the thread to respond quickly to new Zbus events.

### 7.4 Input Events (Zbus)

| Channel | Message | Typical Origin |
|---------|---------|----------------|
| `chan_system_alarm` | `alarm_msg_t { alarm_id, level, is_active, timestamp }` | modbus_registry, slave_scheduler, ao_network, etc. |
| `chan_system_health` | `system_health_t { health, timestamp }` | Global health orchestrator (future) |
| `chan_indicator_command` | `indicator_cmd_msg_t` | Shell, HMI, CLI |

### 7.5 Outputs (Zbus)

| Channel | Message | Purpose |
|---------|---------|---------|
| `chan_indicator_status` | `indicator_status_t` | External query of what is being displayed |

---

## 8. Zbus Channels

Files: `app/src/application/zbus/indicator_channels.{c,h}` (to be created)

```c
/* Alarm message (published by any subsystem) */
typedef struct {
    uint16_t      alarm_id;
    alarm_level_t level;
    bool          is_active;
    uint32_t      timestamp_ms;
} alarm_msg_t;

/* System health message */
typedef struct {
    system_health_t health;
    uint32_t        timestamp_ms;
} system_health_msg_t;

/* Channels */
ZBUS_CHAN_DECLARE(chan_system_alarm);
ZBUS_CHAN_DECLARE(chan_system_health);
ZBUS_CHAN_DECLARE(chan_indicator_command);
ZBUS_CHAN_DECLARE(chan_indicator_status);
```

Subscribers of `chan_system_alarm` and `chan_system_health`: `ao_system_indicator`.
Publishers: any subsystem detecting an alarm condition (e.g. `slave_scheduler` if a Modbus slave disconnects, `ao_network` if Ethernet loses link).

---

## 9. Usage Examples

### 9.1 Modbus slave disconnected

```c
/* In slave_scheduler.c or modbus_registry.c */
alarm_msg_t msg = {
    .alarm_id = ALARM_ID_MODBUS_SLAVE_OFFLINE,
    .level = ALARM_LEVEL_WARNING,
    .is_active = true,
    .timestamp_ms = k_uptime_get_32(),
};
zbus_chan_pub(&chan_system_alarm, &msg, K_MSEC(10));
```

`ao_system_indicator` receives the message, updates its active alarms table, determines that WARNING is the highest priority, and activates LED BLINK_SLOW + buzzer ALARM_INTERMITTENT.

### 9.2 Ethernet recovers connection

```c
/* In ao_network.c */
alarm_msg_t msg = {
    .alarm_id = ALARM_ID_ETHERNET_LINK,
    .level = ALARM_LEVEL_NONE,
    .is_active = false,
    .timestamp_ms = k_uptime_get_32(),
};
zbus_chan_pub(&chan_system_alarm, &msg, K_MSEC(10));
```

If there are no other active alarms, `ao_system_indicator` returns to HEALTHY state: LED ON steady, buzzer SILENT.

### 9.3 Shell forces blinking to identify device

```c
/* In telemetry_shell.c or dedicated command */
indicator_cmd_msg_t cmd = {
    .cmd = INDICATOR_CMD_FORCE_LED,
    .led_state = LED_STATE_BLINK_FAST,
    .buzzer_pattern = BUZZER_PATTERN_SILENT,
    .duration_ms = 10000,  /* 10 seconds */
};
zbus_chan_pub(&chan_indicator_command, &cmd, K_MSEC(10));
```

---

## 10. Composition Root — Initialization

File: `app/src/infrastructure/di/composition_root.c` (modify)

Only place with `DEVICE_DT_GET()` for LED and buzzer:

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

In `composition_root_init()`, `init_indicators()` is called and then `ao_system_indicator` is started.

---

## 11. Startup Sequence

```
power-on
  │
  ▼
composition_root_init(&g_app)
  ├─ ... (existing initializations: modbus, network, storage)
  ├─ adapter_led_gpio_init()     ← NEW: DEVICE_DT_GET(user_led)
  ├─ adapter_buzzer_pwm_init()   ← NEW: DEVICE_DT_GET(buzzer_pwm)
  ├─ adapter_led_gpio_port()     ← NEW: bind i_led_t
  ├─ adapter_buzzer_pwm_port()   ← NEW: bind i_buzzer_t
  └─ ao_system_indicator_init()  ← NEW: create AO thread, subscribe to Zbus
  │
  ▼
composition_root_start(&g_app)
  └─ ao_system_indicator_start() ← NEW: launch indicator thread
  │
  ▼
main loop (sleep)
```

Initial state after boot: LED ON (system healthy), buzzer SILENT, until the first alarm or health event arrives.

---

## 12. Stacks (MPU Invariant)

The stack of the `ao_system_indicator` thread is defined at **file scope** with `K_THREAD_STACK_DEFINE`, never inside `app_container_t`, to preserve MPU alignment on Cortex-M7 with `CONFIG_HW_STACK_PROTECTION`.

---

## 13. Gaps to Implement

| # | Element | Destination File | Status |
|---|---------|-----------------|--------|
| 1 | `indicator_types.h` | `app/include/common/indicator_types.h` | Missing |
| 2 | `i_led.h` | `app/include/interfaces/i_led.h` | Missing |
| 3 | `i_buzzer.h` | `app/include/interfaces/i_buzzer.h` | Missing |
| 4 | `adapter_led_gpio.c/.h` | `app/src/infrastructure/adapters/` | Missing |
| 5 | `adapter_buzzer_pwm.c/.h` | `app/src/infrastructure/adapters/` | Missing |
| 6 | `ao_system_indicator.c/.h` | `app/src/application/active_objects/` | Missing |
| 7 | `indicator_channels.{c,h}` (Zbus) | `app/src/application/zbus/` | Missing |
| 8 | Integration in `composition_root.c` | `app/src/infrastructure/di/` | To modify |
| 9 | Alarm publishers in existing subsystems | modbus, network, etc. | To add (pending alarm engine) |
| 10 | Unit tests (LED/buzzer fakes) | `tests/unit/` | Missing |

---

## 14. Pending Decisions (for future)

1. **Complete alarm engine**: Not yet defined. Pending definition of full `alarm_id_t` and which subsystems publish on `chan_system_alarm`.
2. **Persistence of indicator configuration**: Optional. A `CONFIG_DOMAIN_INDICATORS` domain could be added in storage to save default patterns.
3. **Shell integration**: CLI commands to query indicator status and force manual tests.

---

**Document status**: Technical specification ready for future implementation.  
**Code status**: No file has been modified yet. Everything is to be built on the defined foundations (DTS with `user_led` and `buzzer_pwm`, clean architecture, ports and adapters pattern).
