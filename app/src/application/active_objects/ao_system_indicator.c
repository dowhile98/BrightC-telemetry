/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_system_indicator.c
 * @brief Active Object coordinating Status LED and Buzzer indicators.
 */

#include "application/active_objects/ao_system_indicator.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#if defined(CONFIG_ZBUS)
#include "application/zbus/indicator_channels.h"
#endif

LOG_MODULE_REGISTER(ao_system_indicator, LOG_LEVEL_INF);

/* Smart Industrial Startup Chime Parameters */
#define STARTUP_CHIME_FREQ_1_HZ    1500U
#define STARTUP_CHIME_DUR_1_MS     80U
#define STARTUP_CHIME_SILENCE_MS   30U
#define STARTUP_CHIME_FREQ_2_HZ    2500U
#define STARTUP_CHIME_DUR_2_MS     150U
#define STARTUP_CHIME_TOTAL_MS     (STARTUP_CHIME_DUR_1_MS + STARTUP_CHIME_SILENCE_MS + STARTUP_CHIME_DUR_2_MS)

static void recalculate_alarm_level(ao_system_indicator_t *self)
{
    alarm_level_t max_level = ALARM_LEVEL_NONE;
    for (size_t i = 0; i < AO_INDICATOR_MAX_ALARMS; i++) {
        if (self->alarms[i].is_active) {
            if (self->alarms[i].level > max_level) {
                max_level = self->alarms[i].level;
            }
        }
    }
    self->highest_alarm_level = max_level;
}

static void update_alarm_entry(ao_system_indicator_t *self, const alarm_msg_t *msg)
{
    /* Find existing entry */
    for (size_t i = 0; i < AO_INDICATOR_MAX_ALARMS; i++) {
        if (self->alarms[i].alarm_id == msg->alarm_id && self->alarms[i].is_active) {
            self->alarms[i].level = msg->level;
            self->alarms[i].is_active = msg->is_active;
            recalculate_alarm_level(self);
            return;
        }
    }

    /* If not active and wasn't found, nothing to do */
    if (!msg->is_active) {
        return;
    }

    /* Find empty or inactive slot */
    for (size_t i = 0; i < AO_INDICATOR_MAX_ALARMS; i++) {
        if (!self->alarms[i].is_active) {
            self->alarms[i].alarm_id = msg->alarm_id;
            self->alarms[i].level = msg->level;
            self->alarms[i].is_active = true;
            recalculate_alarm_level(self);
            return;
        }
    }

    LOG_WRN("indicator alarm table full, cannot record alarm %u", msg->alarm_id);
}

int ao_system_indicator_process_event(ao_system_indicator_t *self, const ao_indicator_event_t *evt)
{
    if (self == NULL || evt == NULL) {
        return -EINVAL;
    }

    switch (evt->type) {
    case AO_INDICATOR_EVT_ALARM:
        update_alarm_entry(self, &evt->data.alarm);
        break;

    case AO_INDICATOR_EVT_HEALTH:
        if (system_health_is_valid(evt->data.health.health)) {
            self->current_health = evt->data.health.health;
        }
        break;

    case AO_INDICATOR_EVT_COMMAND: {
        const indicator_cmd_msg_t *cmd = &evt->data.cmd;
        if (!indicator_cmd_is_valid(cmd->cmd)) {
            return -EINVAL;
        }
        if (cmd->cmd == INDICATOR_CMD_AUTO) {
            self->manual_override = false;
        } else if (cmd->cmd == INDICATOR_CMD_FORCE_LED) {
            self->manual_override = true;
            self->manual_cmd = cmd->cmd;
            self->manual_led_state = cmd->led_state;
            self->manual_duration_ms = cmd->duration_ms;
            self->manual_start_ms = (uint32_t)k_uptime_get();
        } else if (cmd->cmd == INDICATOR_CMD_FORCE_BUZZER) {
            self->manual_override = true;
            self->manual_cmd = cmd->cmd;
            self->manual_buzzer_pattern = cmd->buzzer_pattern;
            self->manual_duration_ms = cmd->duration_ms;
            self->manual_start_ms = (uint32_t)k_uptime_get();
        } else if (cmd->cmd == INDICATOR_CMD_TEST) {
            self->manual_override = true;
            self->manual_cmd = cmd->cmd;
            self->manual_led_state = LED_STATE_BLINK_FAST;
            self->manual_buzzer_pattern = BUZZER_PATTERN_ALARM_INTERMITTENT;
            self->manual_duration_ms = (cmd->duration_ms > 0U) ? cmd->duration_ms : 3000U;
            self->manual_start_ms = (uint32_t)k_uptime_get();
        }
        break;
    }

    case AO_INDICATOR_EVT_STARTUP_CHIME:
        self->startup_chime_active = true;
        self->chime_start_ms = (uint32_t)k_uptime_get();
        break;

    case AO_INDICATOR_EVT_STOP:
        self->is_running = false;
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

int ao_system_indicator_step(ao_system_indicator_t *self, uint32_t now_ms)
{
    if (self == NULL || !i_led_is_bound(self->led) || !i_buzzer_is_bound(self->buzzer)) {
        return -EINVAL;
    }

    /* 1. Handle manual override expiration */
    if (self->manual_override && self->manual_duration_ms > 0U) {
        if ((now_ms - self->manual_start_ms) >= self->manual_duration_ms) {
            self->manual_override = false;
        }
    }

    led_state_t desired_led = LED_STATE_OFF;
    buzzer_pattern_t desired_buzzer = BUZZER_PATTERN_SILENT;
    uint32_t desired_freq = 0U;
    bool buzzer_sound_active = false;

    /* 2. Determine State Machine Priority */
    if (self->manual_override) {
        if (self->manual_cmd == INDICATOR_CMD_FORCE_LED) {
            desired_led = self->manual_led_state;
            desired_buzzer = BUZZER_PATTERN_SILENT;
        } else if (self->manual_cmd == INDICATOR_CMD_FORCE_BUZZER) {
            desired_led = LED_STATE_ON;
            desired_buzzer = self->manual_buzzer_pattern;
            desired_freq = 2000U;
        } else if (self->manual_cmd == INDICATOR_CMD_TEST) {
            desired_led = LED_STATE_BLINK_FAST;
            desired_buzzer = BUZZER_PATTERN_ALARM_INTERMITTENT;
            desired_freq = 2000U;
        }
    } else if (self->startup_chime_active) {
        /* Abort startup chime if an active fault/critical alarm is present */
        if (self->highest_alarm_level == ALARM_LEVEL_CRITICAL ||
            self->highest_alarm_level == ALARM_LEVEL_WARNING ||
            self->current_health == SYSTEM_HEALTH_FAULT) {
            self->startup_chime_active = false;
        } else {
            uint32_t elapsed = now_ms - self->chime_start_ms;
            desired_led = LED_STATE_ON; /* Transitioning to steady healthy LED */
            desired_buzzer = BUZZER_PATTERN_STARTUP_CHIME;

            if (elapsed < STARTUP_CHIME_DUR_1_MS) {
                /* Tone 1: 1500 Hz for 80 ms */
                desired_freq = STARTUP_CHIME_FREQ_1_HZ;
                buzzer_sound_active = true;
            } else if (elapsed < (STARTUP_CHIME_DUR_1_MS + STARTUP_CHIME_SILENCE_MS)) {
                /* Silence: 30 ms */
                buzzer_sound_active = false;
            } else if (elapsed < STARTUP_CHIME_TOTAL_MS) {
                /* Tone 2: 2500 Hz for 150 ms */
                desired_freq = STARTUP_CHIME_FREQ_2_HZ;
                buzzer_sound_active = true;
            } else {
                /* Chime finished */
                self->startup_chime_active = false;
                buzzer_sound_active = false;
                desired_buzzer = BUZZER_PATTERN_SILENT;
                self->current_health = SYSTEM_HEALTH_HEALTHY;
            }
        }
    }

    if (!self->manual_override && !self->startup_chime_active) {
        /* Normal Alarm & Health Priority Mapping */
        if (self->highest_alarm_level == ALARM_LEVEL_CRITICAL) {
            desired_led = LED_STATE_BLINK_FAST;
            desired_buzzer = BUZZER_PATTERN_ALARM_CONTINUOUS;
            desired_freq = 2000U;
        } else if (self->highest_alarm_level == ALARM_LEVEL_WARNING) {
            desired_led = LED_STATE_BLINK_SLOW;
            desired_buzzer = BUZZER_PATTERN_ALARM_INTERMITTENT;
            desired_freq = 1500U;
        } else if (self->highest_alarm_level == ALARM_LEVEL_INFO) {
            desired_led = LED_STATE_BLINK_HEARTBEAT;
            desired_buzzer = BUZZER_PATTERN_SILENT;
        } else if (self->current_health == SYSTEM_HEALTH_FAULT) {
            desired_led = LED_STATE_BLINK_FAST;
            desired_buzzer = BUZZER_PATTERN_ALARM_INTERMITTENT;
            desired_freq = 1500U;
        } else if (self->current_health == SYSTEM_HEALTH_DEGRADED) {
            desired_led = LED_STATE_BLINK_SLOW;
            desired_buzzer = BUZZER_PATTERN_SILENT;
        } else if (self->current_health == SYSTEM_HEALTH_HEALTHY) {
            desired_led = LED_STATE_ON;
            desired_buzzer = BUZZER_PATTERN_SILENT;
        } else {
            desired_led = LED_STATE_OFF;
            desired_buzzer = BUZZER_PATTERN_SILENT;
        }
    }

    /* 3. Software Timing for LED */
    bool hw_led_on = false;
    switch (desired_led) {
    case LED_STATE_OFF:
        hw_led_on = false;
        break;
    case LED_STATE_ON:
        hw_led_on = true;
        break;
    case LED_STATE_BLINK_SLOW:
        hw_led_on = ((now_ms % 1000U) < 500U);
        break;
    case LED_STATE_BLINK_FAST:
        hw_led_on = ((now_ms % 200U) < 100U);
        break;
    case LED_STATE_BLINK_HEARTBEAT: {
        uint32_t ph = now_ms % 1000U;
        hw_led_on = ((ph < 80U) || (ph >= 200U && ph < 280U));
        break;
    }
    default:
        hw_led_on = false;
        break;
    }

    if (hw_led_on != self->last_hw_led_on) {
        int err = i_led_set_state(self->led, hw_led_on ? LED_STATE_ON : LED_STATE_OFF);
        if (err == 0) {
            self->last_hw_led_on = hw_led_on;
        }
    }

    /* 4. Software Timing for Buzzer (if not already dictated by chime) */
    if (desired_buzzer != BUZZER_PATTERN_STARTUP_CHIME) {
        switch (desired_buzzer) {
        case BUZZER_PATTERN_SILENT:
            buzzer_sound_active = false;
            break;
        case BUZZER_PATTERN_ALARM_CONTINUOUS:
            buzzer_sound_active = true;
            if (desired_freq == 0U) {
                desired_freq = 2000U;
            }
            break;
        case BUZZER_PATTERN_ALARM_INTERMITTENT:
            if (desired_freq == 0U) {
                desired_freq = 1500U;
            }
            buzzer_sound_active = ((now_ms % 500U) < 250U);
            break;
        case BUZZER_PATTERN_BEEP_SHORT:
            if (!self->beep_active) {
                self->beep_active = true;
                self->beep_start_ms = now_ms;
                self->beep_duration_ms = 100U;
            }
            if ((now_ms - self->beep_start_ms) < self->beep_duration_ms) {
                buzzer_sound_active = true;
                desired_freq = 2000U;
            } else {
                self->beep_active = false;
                buzzer_sound_active = false;
            }
            break;
        case BUZZER_PATTERN_BEEP_LONG:
            if (!self->beep_active) {
                self->beep_active = true;
                self->beep_start_ms = now_ms;
                self->beep_duration_ms = 500U;
            }
            if ((now_ms - self->beep_start_ms) < self->beep_duration_ms) {
                buzzer_sound_active = true;
                desired_freq = 2000U;
            } else {
                self->beep_active = false;
                buzzer_sound_active = false;
            }
            break;
        default:
            buzzer_sound_active = false;
            break;
        }
    }

    if (buzzer_sound_active != self->last_hw_buzzer_on ||
        (buzzer_sound_active && (desired_freq != self->last_hw_buzzer_freq))) {
        if (buzzer_sound_active) {
            int err = i_buzzer_play_tone(self->buzzer, desired_freq, 0U);
            if (err == 0) {
                self->last_hw_buzzer_on = true;
                self->last_hw_buzzer_freq = desired_freq;
            }
        } else {
            int err = i_buzzer_stop(self->buzzer);
            if (err == 0) {
                self->last_hw_buzzer_on = false;
                self->last_hw_buzzer_freq = 0U;
            }
        }
    }

    self->target_led_state = desired_led;
    self->target_buzzer_pattern = desired_buzzer;

    return 0;
}

static void ao_system_indicator_thread_entry(void *p1, void *p2, void *p3)
{
    ao_system_indicator_t *self = (ao_system_indicator_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("ao_system_indicator thread started");

    bool should_terminate = false;
    while (!should_terminate) {
        ao_indicator_event_t evt;
        int ret = k_msgq_get(self->queue, &evt, K_FOREVER);
        if (ret != 0) {
            continue;
        }
        if (evt.type == AO_INDICATOR_EVT_TERMINATE) {
            break;
        }
        if (evt.type != AO_INDICATOR_EVT_START) {
            continue;
        }

        LOG_INF("ao_system_indicator thread entered inner loop");

        while (self->is_running) {
            ret = k_msgq_get(self->queue, &evt, K_MSEC(AO_INDICATOR_TICK_MS));
            if (ret == 0) {
                if (evt.type == AO_INDICATOR_EVT_STOP) {
                    break;
                }
                if (evt.type == AO_INDICATOR_EVT_TERMINATE) {
                    should_terminate = true;
                    break;
                }
                (void)ao_system_indicator_process_event(self, &evt);
            }

            uint32_t now = (uint32_t)k_uptime_get();
            (void)ao_system_indicator_step(self, now);
        }

        /* Silence and turn off on inner loop exit */
        if (i_led_is_bound(self->led)) {
            (void)i_led_set_state(self->led, LED_STATE_OFF);
        }
        if (i_buzzer_is_bound(self->buzzer)) {
            (void)i_buzzer_stop(self->buzzer);
        }

        self->is_running = false;
        LOG_INF("ao_system_indicator thread exited inner loop");
        if (!should_terminate) {
            (void)k_sem_give(&self->stopped_sem);
        }
    }

    LOG_INF("ao_system_indicator thread terminated");
    (void)k_sem_give(&self->stopped_sem);
}

int ao_system_indicator_init(ao_system_indicator_t *self,
                             const i_led_t *led,
                             const i_buzzer_t *buzzer,
                             struct k_msgq *queue,
                             k_thread_stack_t *stack,
                             size_t stack_size)
{
    if (self == NULL || !i_led_is_bound(led) || !i_buzzer_is_bound(buzzer) ||
        queue == NULL || stack == NULL || stack_size == 0U) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->led = led;
    self->buzzer = buzzer;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->current_health = SYSTEM_HEALTH_HEALTHY;
    self->highest_alarm_level = ALARM_LEVEL_NONE;
    self->target_led_state = LED_STATE_OFF;
    self->target_buzzer_pattern = BUZZER_PATTERN_SILENT;

    k_sem_init(&self->stopped_sem, 0, 1);
    self->is_initialized = true;

    k_thread_create(&self->thread, self->stack, self->stack_size,
                    ao_system_indicator_thread_entry,
                    self, NULL, NULL,
                    AO_INDICATOR_PRIORITY, 0, K_NO_WAIT);

    k_thread_name_set(&self->thread, "ao_indicator");

    return 0;
}

int ao_system_indicator_start(ao_system_indicator_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EALREADY;
    }

    self->is_running = true;

    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_START,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_system_indicator_stop(ao_system_indicator_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return 0;
    }

    self->is_running = false;

    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_STOP,
    };
    (void)k_msgq_put(self->queue, &evt, K_NO_WAIT);

    return 0;
}

int ao_system_indicator_wait_stopped(ao_system_indicator_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_system_indicator_deinit(ao_system_indicator_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EBUSY;
    }

    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_TERMINATE,
    };
    (void)k_msgq_put(self->queue, &evt, K_NO_WAIT);

    int err = k_thread_join(&self->thread, K_MSEC(1000));
    if (err != 0) {
        LOG_WRN("ao_system_indicator deinit timeout, forcing abort");
        k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}

int ao_system_indicator_post_alarm(ao_system_indicator_t *self, const alarm_msg_t *msg)
{
    if (self == NULL || msg == NULL || self->queue == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_ALARM,
        .data.alarm = *msg,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_system_indicator_post_health(ao_system_indicator_t *self, const system_health_msg_t *msg)
{
    if (self == NULL || msg == NULL || self->queue == NULL) {
        return -EINVAL;
    }

    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_HEALTH,
        .data.health = *msg,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_system_indicator_post_command(ao_system_indicator_t *self, const indicator_cmd_msg_t *cmd)
{
    if (self == NULL || cmd == NULL || self->queue == NULL) {
        return -EINVAL;
    }

    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_COMMAND,
        .data.cmd = *cmd,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_system_indicator_trigger_startup_chime(ao_system_indicator_t *self)
{
    if (self == NULL || self->queue == NULL) {
        return -EINVAL;
    }

    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_STARTUP_CHIME,
    };
    return k_msgq_put(self->queue, &evt, K_NO_WAIT);
}

int ao_system_indicator_get_status(const ao_system_indicator_t *self, indicator_status_t *out_status)
{
    if (self == NULL || out_status == NULL) {
        return -EINVAL;
    }

    out_status->led_state = self->target_led_state;
    out_status->buzzer_pattern = self->target_buzzer_pattern;
    out_status->active_alarm_level = self->highest_alarm_level;
    out_status->system_health = self->current_health;
    out_status->manual_override = self->manual_override;

    return 0;
}
