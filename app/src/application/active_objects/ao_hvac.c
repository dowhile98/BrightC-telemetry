/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_hvac.c
 * @brief Active Object coordinator for Industrial HVAC Subsystem.
 */

#include "application/active_objects/ao_hvac.h"
#include "infrastructure/adapters/adapter_hvac_actuator.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

#if defined(CONFIG_ZBUS)
#include <zephyr/zbus/zbus.h>
#include "application/zbus/hvac_channels.h"
#endif

LOG_MODULE_REGISTER(ao_hvac, LOG_LEVEL_INF);

static const char *ao_hvac_unit_state_name(hvac_unit_state_t state)
{
    switch (state) {
    case UNIT_STATE_OFF:          return "OFF";
    case UNIT_STATE_STARTING:     return "STARTING";
    case UNIT_STATE_RUNNING:      return "RUNNING";
    case UNIT_STATE_STOPPING:     return "STOPPING";
    case UNIT_STATE_BLOCKED_ALARM:return "BLOCKED_ALARM";
    case UNIT_STATE_FAULT:        return "FAULT";
    case UNIT_STATE_BYPASS:       return "BYPASS";
    default:                      return "UNKNOWN";
    }
}

static void ao_hvac_thread_entry(void *p1, void *p2, void *p3)
{
    ao_hvac_t *self = (ao_hvac_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (!self->terminate) {
        /* 1. Gated state: blocked until ao_hvac_start() or deinit() */
        (void)k_sem_take(&self->run_sem, K_FOREVER);
        if (self->terminate) {
            break;
        }

        LOG_INF("ao_hvac thread entered inner loop");

        /* 2. Inner periodic evaluation loop */
        while (self->is_running) {
            if (self->queue != NULL) {
                ao_hvac_event_t evt;
                int ret = k_msgq_get(self->queue, &evt, K_MSEC(AO_HVAC_PERIOD_MS));
                if (ret == 0) {
                    if (evt.type == AO_HVAC_EVENT_STOP) {
                        break;
                    }
                }
            } else {
                k_msleep(AO_HVAC_PERIOD_MS);
            }

            if (!self->is_running) {
                break;
            }

            (void)ao_hvac_step(self);
        }

        LOG_INF("ao_hvac thread exited inner loop");
        self->is_running = false;
        (void)ao_hvac_safe_state(self);
        (void)k_sem_give(&self->stopped_sem);
    }

    LOG_INF("ao_hvac thread terminated");
    (void)k_sem_give(&self->stopped_sem);
}

int ao_hvac_init(ao_hvac_t *self,
                 const config_hvac_t *config,
                 const i_hvac_unit_t *actuator_port,
                 struct k_msgq *queue,
                 k_thread_stack_t *stack,
                 size_t stack_size)
{
    if (self == NULL || actuator_port == NULL || queue == NULL ||
        stack == NULL || stack_size == 0U) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->actuator_port = *actuator_port;
    self->queue = queue;
    self->stack = stack;
    self->stack_size = stack_size;
    self->is_running = false;
    self->terminate = false;

    k_sem_init(&self->run_sem, 0, 1);
    k_sem_init(&self->stopped_sem, 0, 1);
    k_mutex_init(&self->lock);

    self->boot_stabilization_s = 5U;
    self->sensor_snapshot_age_s = 0U;
    memset(&self->last_sensor_snapshot, 0, sizeof(self->last_sensor_snapshot));

    /* Initialize entities */
    if (config != NULL) {
        self->config = *config;
        uint8_t groups_to_init = (config->total_groups > 0U && config->total_groups <= HVAC_MAX_GROUPS)
                                 ? config->total_groups : 1U;

        for (uint8_t g = 0U; g < groups_to_init; g++) {
            self->group_active[g] = true;
            hvac_group_entity_init(&self->groups[g], g);
            hvac_group_entity_set_config(&self->groups[g], &config->groups[g]);

            for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
                if ((config->groups[g].unit_mask & (1U << u)) != 0U) {
                    self->unit_active[u] = true;
                    hvac_unit_entity_init(&self->units[u],
                                          u,
                                          config->groups[g].max_allowed_faults,
                                          config->groups[g].delay_on_s,
                                          config->groups[g].fan_delay_s,
                                          config->groups[g].pressure_retry_s,
                                          config->groups[g].enable_fan_delay != 0U);
                    (void)hvac_group_entity_add_unit(&self->groups[g], &self->units[u]);
                }
            }
        }

        /* Ensure all remaining units are initialized safely */
        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            if (!self->unit_active[u]) {
                hvac_unit_entity_init(&self->units[u], u, 3U, 10U, 30U, 180U, false);
            }
        }
    } else {
        /* Default configuration: Group 0 active with all units assigned */
        self->config.total_groups = 1U;
        self->config.total_units = HVAC_MAX_UNITS_TOTAL;
        self->config.groups[0].sensor_mask = 0xFFFFU;
        self->config.groups[0].unit_mask = 0xFFU;
        self->config.groups[0].active_turn_count = 1U;
        self->config.groups[0].enable_fan_delay = 0U;

        self->group_active[0] = true;
        hvac_group_entity_init(&self->groups[0], 0U);

        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            self->unit_active[u] = true;
            hvac_unit_entity_init(&self->units[u], u, 3U, 10U, 30U, 180U, false);
            (void)hvac_group_entity_add_unit(&self->groups[0], &self->units[u]);
        }
    }

    /* Initialize transition tracking baselines */
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        self->prev_group_state[g] = self->groups[g].state;
        self->prev_turn_offset[g] = self->groups[g].current_turn_offset;
        self->prev_manual_active[g] = self->groups[g].is_manual_active;
    }
    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        self->prev_unit_state[u] = self->units[u].state;
    }

    self->is_initialized = true;

    /* Spawn thread in gated state */
    k_thread_create(&self->thread,
                    self->stack,
                    self->stack_size,
                    ao_hvac_thread_entry,
                    self, NULL, NULL,
                    K_PRIO_PREEMPT(AO_HVAC_PRIORITY),
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&self->thread, "ao_hvac");

    return 0;
}

int ao_hvac_start(ao_hvac_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EALREADY;
    }

    if (self->queue != NULL) {
        k_msgq_purge(self->queue);
    }

    self->is_running = true;
    k_sem_give(&self->run_sem);

    return 0;
}

#if defined(CONFIG_ZBUS)
static void publish_hvac_event(uint8_t group_id, uint8_t unit_id, hvac_event_type_t type)
{
    hvac_event_t evt = {
        .group_id = group_id,
        .unit_id = unit_id,
        .type = type,
        .timestamp_ms = k_uptime_get_32(),
    };
    (void)zbus_chan_pub(&chan_hvac_event, &evt, K_MSEC(50));
}
#else
#define publish_hvac_event(g, u, t) ((void)0)
#endif

int ao_hvac_step(ao_hvac_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    /* Boot stabilization guard for sensor acquisition on startup */
    bool in_grace_period = false;
    if (self->boot_stabilization_s > 0U) {
        if (self->last_sensor_snapshot.valid_mask != 0ULL) {
            self->boot_stabilization_s = 0U;
        } else {
            self->boot_stabilization_s--;
            in_grace_period = true;
        }
    }

    /* Sensor data freshness watchdog (staleness check) */
    if (self->sensor_snapshot_age_s < UINT32_MAX) {
        self->sensor_snapshot_age_s++;
    }
    if (!in_grace_period && (self->boot_stabilization_s == 0U)) {
        if (self->sensor_snapshot_age_s >= HVAC_SENSOR_STALENESS_TIMEOUT_S) {
            self->last_sensor_snapshot.valid_mask = 0ULL;
            for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
                self->group_metrics[g].valid_sensors = 0U;
            }
            if (self->sensor_snapshot_age_s == HVAC_SENSOR_STALENESS_TIMEOUT_S) {
                LOG_WRN("Sensor snapshot stale (%u s >= %u s), invalidating readings",
                        self->sensor_snapshot_age_s, HVAC_SENSOR_STALENESS_TIMEOUT_S);
            }
        }
    }

    /* 1. Evaluate and tick each active group */
    if (self->has_test_temp_override) {
        for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
            if (self->group_active[g]) {
                self->group_metrics[g].highest_temp = self->test_temp_override;
                self->group_metrics[g].average_temp = self->test_temp_override;
                self->group_metrics[g].valid_sensors = 1U;
                self->group_metrics[g].total_sensors = 1U;
            }
        }
        in_grace_period = false;
    }

    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_active[g]) {
            /* Tick 1s first so timers from previous step advance before evaluating current step */
            hvac_group_entity_tick_1s(&self->groups[g]);

            if (in_grace_period && (self->group_metrics[g].valid_sensors == 0U)) {
                /* In boot grace window without valid sensor data: keep group in IDLE and do not trip SENSOR_FAULT */
                self->groups[g].state = GROUP_STATE_IDLE;
            } else {
                hvac_group_entity_evaluate(&self->groups[g], &self->group_metrics[g]);
            }

            hvac_group_state_t g_after = self->groups[g].state;
            bool manual_after = self->groups[g].is_manual_active;
            uint8_t turn_after = self->groups[g].current_turn_offset;

            /* Check group state transitions */
            if (g_after != self->prev_group_state[g]) {
                if (g_after == GROUP_STATE_COOLING) {
                    publish_hvac_event(g, 0U, HVAC_EVENT_COOLING_STARTED);
                } else if (g_after == GROUP_STATE_COOL_BOOST) {
                    publish_hvac_event(g, 0U, HVAC_EVENT_COOL_BOOST_STARTED);
                } else if (g_after == GROUP_STATE_IDLE) {
                    publish_hvac_event(g, 0U, HVAC_EVENT_IDLE_ENTERED);
                } else if (g_after == GROUP_STATE_BYPASS) {
                    publish_hvac_event(g, 0U, HVAC_EVENT_BYPASS_ENTERED);
                } else if (g_after == GROUP_STATE_FIRE) {
                    publish_hvac_event(g, 0U, HVAC_EVENT_FIRE_ACTIVATED);
                }

                if (self->prev_group_state[g] == GROUP_STATE_BYPASS && g_after != GROUP_STATE_BYPASS) {
                    publish_hvac_event(g, 0U, HVAC_EVENT_BYPASS_CLEARED);
                }
                if (self->prev_group_state[g] == GROUP_STATE_FIRE && g_after != GROUP_STATE_FIRE) {
                    publish_hvac_event(g, 0U, HVAC_EVENT_FIRE_CLEARED);
                }

                self->prev_group_state[g] = g_after;
            }

            /* Check shift rotation */
            if (turn_after != self->prev_turn_offset[g]) {
                publish_hvac_event(g, 0U, HVAC_EVENT_SHIFT_ROTATED);
                self->prev_turn_offset[g] = turn_after;
            }

            /* Check manual mode timeout */
            if (self->prev_manual_active[g] && !manual_after && self->groups[g].manual_timer_s == 0U) {
                publish_hvac_event(g, 0U, HVAC_EVENT_MANUAL_TIMEOUT);
            }
            self->prev_manual_active[g] = manual_after;
        }
    }

    /* 2. For each active unit, query synthesized tri-state and actuate */
    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        if (self->unit_active[u]) {
            hvac_unit_state_t u_after = self->units[u].state;
            if (u_after != self->prev_unit_state[u]) {
                hvac_tri_state_t transition_tri = hvac_unit_entity_get_tri_state(&self->units[u]);
                LOG_INF("HVAC unit %u state: %s -> %s (AA=%u SM=%u G=%u, startup=%us fan_delay=%us)",
                        (unsigned int)(u + 1U),
                        ao_hvac_unit_state_name(self->prev_unit_state[u]),
                        ao_hvac_unit_state_name(u_after),
                        (unsigned int)transition_tri.relay_aa,
                        (unsigned int)transition_tri.relay_sm,
                        (unsigned int)transition_tri.relay_g,
                        (unsigned int)self->units[u].startup_timer_s,
                        (unsigned int)self->units[u].fan_delay_timer_s);
                if (u_after == UNIT_STATE_FAULT) {
                    publish_hvac_event(0U, u, HVAC_EVENT_UNIT_FAULT);
                }
                self->prev_unit_state[u] = u_after;
            }

            hvac_tri_state_t tri = hvac_unit_entity_get_tri_state(&self->units[u]);
            if (i_hvac_unit_is_bound(&self->actuator_port)) {
                int act_err = i_hvac_unit_set_tri_state(&self->actuator_port, u, tri);
                if (act_err != 0) {
                    LOG_ERR("Actuator failed for unit %u: %d", u, act_err);
                    self->actuator_fault_count++;
                }
            }
        }
    }

    /* 3. Evaluate global bypass condition and drive Alarm 1 relay */
    bool any_bypass = false;
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_active[g] && (self->groups[g].state == GROUP_STATE_BYPASS || self->groups[g].bypass_causes_mask != 0U)) {
            any_bypass = true;
            break;
        }
    }
    if (i_hvac_unit_is_bound(&self->actuator_port)) {
        (void)i_hvac_unit_set_bypass_alarm(&self->actuator_port, any_bypass);
    }

    /* 4. Snapshot status and publish to Zbus */
    hvac_status_msg_t status;
    memset(&status, 0, sizeof(status));

    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        status.group_state[g] = self->groups[g].state;
        status.bypass_causes[g] = self->groups[g].bypass_causes_mask;
        status.highest_temp[g] = self->group_metrics[g].highest_temp;
        status.current_turn_offset[g] = self->groups[g].current_turn_offset;
    }

    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        status.unit_state[u] = self->units[u].state;
        status.unit_tri_state[u] = hvac_unit_entity_get_tri_state(&self->units[u]);
    }

#if defined(CONFIG_ZBUS)
    (void)zbus_chan_pub(&chan_hvac_status, &status, K_MSEC(50));
#endif

    k_mutex_unlock(&self->lock);
    return 0;
}

int ao_hvac_safe_state(ao_hvac_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        if (self->unit_active[u]) {
            hvac_unit_entity_request_stop(&self->units[u], false);
            if (i_hvac_unit_is_bound(&self->actuator_port)) {
                int err = i_hvac_unit_set_tri_state(&self->actuator_port, u, HVAC_TRI_STATE_OFF);
                if (err != 0) {
                    LOG_ERR("Failed to force safe state on unit %u: %d", u, err);
                }
            }
        }
    }

    if (i_hvac_unit_is_bound(&self->actuator_port)) {
        (void)i_hvac_unit_set_bypass_alarm(&self->actuator_port, false);
    }

    k_mutex_unlock(&self->lock);
    return 0;
}

int ao_hvac_stop(ao_hvac_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return 0;
    }

    self->is_running = false;

    if (self->queue != NULL) {
        ao_hvac_event_t evt = {
            .type = AO_HVAC_EVENT_STOP,
        };
        (void)k_msgq_put(self->queue, &evt, K_NO_WAIT);
    }

    (void)ao_hvac_safe_state(self);

    return 0;
}

int ao_hvac_wait_stopped(ao_hvac_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    return k_sem_take(&self->stopped_sem, timeout);
}

int ao_hvac_deinit(ao_hvac_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EBUSY;
    }

    self->terminate = true;
    k_sem_give(&self->run_sem);

    int err = k_thread_join(&self->thread, K_MSEC(1000));
    if (err != 0) {
        LOG_WRN("ao_hvac deinit timeout, forcing abort");
        k_thread_abort(&self->thread);
    }

    self->is_initialized = false;
    return 0;
}

int ao_hvac_set_group_metrics(ao_hvac_t *self,
                              uint8_t group_id,
                              const group_temperature_metrics_t *metrics)
{
    if (self == NULL || !self->is_initialized || group_id >= HVAC_MAX_GROUPS || metrics == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->group_metrics[group_id] = *metrics;
    if (metrics->valid_sensors > 0U) {
        if (self->sensor_snapshot_age_s >= HVAC_SENSOR_STALENESS_TIMEOUT_S) {
            LOG_INF("Sensor snapshot recovered after %u s", self->sensor_snapshot_age_s);
        }
        self->last_sensor_snapshot.valid_mask |= (1ULL << group_id);
        self->sensor_snapshot_age_s = 0U;
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_feed_sensor_snapshot(ao_hvac_t *self,
                                 const float *temperatures,
                                 uint64_t valid_mask,
                                 size_t count)
{
    if (self == NULL || !self->is_initialized || temperatures == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->last_sensor_snapshot.valid_mask = valid_mask;
    if (valid_mask != 0ULL) {
        if (self->sensor_snapshot_age_s >= HVAC_SENSOR_STALENESS_TIMEOUT_S) {
            LOG_INF("Sensor snapshot recovered after %u s", self->sensor_snapshot_age_s);
        }
        self->sensor_snapshot_age_s = 0U;
    }
    if (count > 0U) {
        size_t to_copy = (count <= 64U) ? count : 64U;
        memcpy(self->last_sensor_snapshot.temperatures, temperatures, to_copy * sizeof(float));
    }
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_active[g]) {
            const config_hvac_group_t *grp = &self->config.groups[g];
            if (grp->sensor_start > 0U && grp->sensor_end >= grp->sensor_start) {
                (void)hvac_compute_group_metrics_range(temperatures,
                                                       valid_mask,
                                                       count,
                                                       grp->sensor_start,
                                                       grp->sensor_end,
                                                       &self->group_metrics[g]);
            } else {
                (void)hvac_compute_group_metrics(temperatures,
                                                 (uint16_t)(valid_mask & 0xFFFFU),
                                                 count,
                                                 grp->sensor_mask,
                                                 &self->group_metrics[g]);
            }
        }
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_feed_temperatures(ao_hvac_t *self,
                              const float *temperatures,
                              uint16_t valid_mask,
                              size_t count)
{
    return ao_hvac_feed_sensor_snapshot(self, temperatures, (uint64_t)valid_mask, count);
}

int ao_hvac_set_unit_alarms(ao_hvac_t *self,
                            uint8_t unit_id,
                            bool hp,
                            bool lp,
                            bool flow,
                            bool volt)
{
    if (self == NULL || !self->is_initialized || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    hvac_unit_entity_update_alarms(&self->units[unit_id], hp, lp, flow, volt);
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_set_fire(ao_hvac_t *self, bool active)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->is_fire_active = active;
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_active[g]) {
            hvac_group_entity_set_fire(&self->groups[g], active);
        }
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_set_manual(ao_hvac_t *self, uint8_t group_id, bool active)
{
    if (self == NULL || !self->is_initialized || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    if (self->group_active[group_id]) {
        hvac_group_entity_set_manual(&self->groups[group_id], active);
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_set_group_manual(ao_hvac_t *self, uint8_t group_id, bool active)
{
    return ao_hvac_set_manual(self, group_id, active);
}

int ao_hvac_set_group_mode_off(ao_hvac_t *self, uint8_t group_id, bool off)
{
    if (self == NULL || !self->is_initialized || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    if (self->group_active[group_id]) {
        hvac_group_entity_set_mode_off(&self->groups[group_id], off);
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_set_bypass_cause(ao_hvac_t *self, uint8_t group_id, hvac_bypass_cause_t cause, bool active)
{
    if (self == NULL || !self->is_initialized || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    if (self->group_active[group_id]) {
        hvac_group_entity_set_bypass_cause(&self->groups[group_id], cause, active);
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_get_status(ao_hvac_t *self, hvac_status_msg_t *out_status)
{
    if (self == NULL || out_status == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        out_status->group_state[g] = self->groups[g].state;
        out_status->bypass_causes[g] = self->groups[g].bypass_causes_mask;
        out_status->highest_temp[g] = self->group_metrics[g].highest_temp;
        out_status->current_turn_offset[g] = self->groups[g].current_turn_offset;
    }

    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        out_status->unit_state[u] = self->units[u].state;
        out_status->unit_tri_state[u] = hvac_unit_entity_get_tri_state(&self->units[u]);
    }

    k_mutex_unlock(&self->lock);
    return 0;
}

int ao_hvac_set_group_config(ao_hvac_t *self, uint8_t group_id, const config_hvac_group_t *cfg)
{
    if (self == NULL || !self->is_initialized || group_id >= HVAC_MAX_GROUPS || cfg == NULL) {
        return -EINVAL;
    }

    if (!config_hvac_group_is_valid(cfg)) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->config.groups[group_id] = *cfg;
    hvac_group_entity_set_config(&self->groups[group_id], cfg);
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_apply_config(ao_hvac_t *self, const config_hvac_t *cfg)
{
    if (self == NULL || !self->is_initialized || cfg == NULL) {
        return -EINVAL;
    }

    if (!config_hvac_is_valid(cfg)) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->config = *cfg;
    uint8_t count = (cfg->total_groups > 0U && cfg->total_groups <= HVAC_MAX_GROUPS)
                    ? cfg->total_groups : 1U;

    /* Hot rebuild of group-unit bindings and active status */
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        hvac_group_entity_clear_units(&self->groups[g]);
        self->group_active[g] = false;
    }
    memset(self->unit_active, 0, sizeof(self->unit_active));

    for (uint8_t g = 0U; g < count; g++) {
        self->group_active[g] = true;
        hvac_group_entity_set_config(&self->groups[g], &cfg->groups[g]);

        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            if ((cfg->groups[g].unit_mask & (1U << u)) != 0U) {
                self->unit_active[u] = true;
                (void)hvac_group_entity_add_unit(&self->groups[g], &self->units[u]);
            }
        }
    }

    /* Synchronize physical relay routing on runtime profile update */
    if (self->actuator_adapter != NULL) {
        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            (void)adapter_hvac_actuator_set_hw_config(self->actuator_adapter, u, &cfg->units[u]);
        }
        (void)adapter_hvac_actuator_set_bypass_alarm_contact_type(
            self->actuator_adapter,
            (hvac_alarm_contact_type_t)cfg->bypass_alarm_contact_type);
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_get_config(ao_hvac_t *self, config_hvac_t *out_cfg)
{
    if (self == NULL || out_cfg == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    *out_cfg = self->config;
    k_mutex_unlock(&self->lock);

    return 0;
}

void ao_hvac_set_actuator_adapter(ao_hvac_t *self, adapter_hvac_actuator_t *actuator_adapter)
{
    if (self != NULL) {
        self->actuator_adapter = actuator_adapter;
    }
}

int ao_hvac_force_rotation(ao_hvac_t *self, uint8_t group_id)
{
    if (self == NULL || !self->is_initialized || group_id >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }
    k_mutex_lock(&self->lock, K_FOREVER);
    if (self->group_active[group_id]) {
        hvac_group_entity_rotate_shift(&self->groups[group_id]);
    }
    k_mutex_unlock(&self->lock);
    return 0;
}

int ao_hvac_set_group_rotation(ao_hvac_t *self, uint8_t group_id, uint16_t days)
{
    if (self == NULL || !self->is_initialized || group_id >= HVAC_MAX_GROUPS || days == 0U) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->config.groups[group_id].rotation_interval_days = days;
    if (self->group_active[group_id]) {
        hvac_group_entity_set_rotation_interval_days(&self->groups[group_id], days);
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_set_unit_manual(ao_hvac_t *self, uint8_t unit_id, bool enable, bool run_compressor, bool run_fan)
{
    if (self == NULL || !self->is_initialized || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }
    k_mutex_lock(&self->lock, K_FOREVER);
    if (enable) {
        hvac_unit_entity_set_manual(&self->units[unit_id], run_compressor);
        if (!run_compressor) {
            self->units[unit_id].fan_state = run_fan
                                                 ? HVAC_FAN_STATE_COMPRESSOR_RUNNING
                                                 : HVAC_FAN_STATE_SMART_IDLE_ON;
        }
    } else {
        bool enable_fan_delay = false;
        for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
            if (self->group_active[g] && ((self->config.groups[g].unit_mask & (1U << unit_id)) != 0U)) {
                enable_fan_delay = self->groups[g].enable_fan_delay;
                break;
            }
        }
        hvac_unit_entity_set_manual(&self->units[unit_id], false);
        hvac_unit_entity_request_stop(&self->units[unit_id], enable_fan_delay);
    }
    k_mutex_unlock(&self->lock);
    return 0;
}

int ao_hvac_set_test_temp(ao_hvac_t *self, float temp)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->test_temp_override = temp;
    self->has_test_temp_override = true;
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_active[g]) {
            self->group_metrics[g].highest_temp = temp;
            self->group_metrics[g].average_temp = temp;
            self->group_metrics[g].valid_sensors = 1U;
            self->group_metrics[g].total_sensors = 1U;
        }
    }
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_clear_test_temp(ao_hvac_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    self->has_test_temp_override = false;
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_get_timers(ao_hvac_t *self,
                       uint8_t group_idx,
                       uint32_t *cooling_timer_s,
                       uint32_t *rotation_timer_s,
                       uint16_t *startup_timers,
                       uint16_t *fan_timers)
{
    if (self == NULL || !self->is_initialized || group_idx >= HVAC_MAX_GROUPS) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    if (cooling_timer_s != NULL) {
        *cooling_timer_s = (uint32_t)self->groups[group_idx].cooling_timer_s;
    }
    if (rotation_timer_s != NULL) {
        *rotation_timer_s = self->groups[group_idx].rotation_timer_s;
    }

    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        if (startup_timers != NULL) {
            startup_timers[u] = self->units[u].startup_timer_s;
        }
        if (fan_timers != NULL) {
            fan_timers[u] = self->units[u].fan_delay_timer_s;
        }
    }

    k_mutex_unlock(&self->lock);
    return 0;
}

