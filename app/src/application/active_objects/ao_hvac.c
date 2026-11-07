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
                hvac_unit_entity_init(&self->units[u], u, 3U, 10U, 30U, 180U, true);
            }
        }
    } else {
        /* Default configuration: Group 0 active with all units assigned */
        self->config.total_groups = 1U;
        self->config.total_units = HVAC_MAX_UNITS_TOTAL;
        self->config.groups[0].sensor_mask = 0xFFFFU;
        self->config.groups[0].unit_mask = 0xFFU;
        self->config.groups[0].active_turn_count = 1U;

        self->group_active[0] = true;
        hvac_group_entity_init(&self->groups[0], 0U);

        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            self->unit_active[u] = true;
            hvac_unit_entity_init(&self->units[u], u, 3U, 10U, 30U, 180U, true);
            (void)hvac_group_entity_add_unit(&self->groups[0], &self->units[u]);
        }
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

int ao_hvac_step(ao_hvac_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);

    /* 1. Evaluate and tick each active group */
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_active[g]) {
            hvac_group_entity_evaluate(&self->groups[g], &self->group_metrics[g]);
            hvac_group_entity_tick_1s(&self->groups[g]);
        }
    }

    /* 2. For each active unit, query synthesized tri-state and actuate */
    for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
        if (self->unit_active[u]) {
            hvac_tri_state_t tri = hvac_unit_entity_get_tri_state(&self->units[u]);
            if (i_hvac_unit_is_bound(&self->actuator_port)) {
                int act_err = i_hvac_unit_set_tri_state(&self->actuator_port, u, tri);
                if (act_err != 0) {
                    LOG_ERR("Actuator failed for unit %u: %d", u, act_err);
                    self->actuator_fault_count++;
                    hvac_unit_entity_increment_fault(&self->units[u]);
                }
            }
        }
    }

    /* 3. Snapshot status and publish to Zbus */
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
    k_mutex_unlock(&self->lock);

    return 0;
}

int ao_hvac_feed_temperatures(ao_hvac_t *self,
                              const float *temperatures,
                              uint16_t valid_mask,
                              size_t count)
{
    if (self == NULL || !self->is_initialized || temperatures == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    for (uint8_t g = 0U; g < HVAC_MAX_GROUPS; g++) {
        if (self->group_active[g]) {
            (void)hvac_compute_group_metrics(temperatures,
                                             valid_mask,
                                             count,
                                             self->config.groups[g].sensor_mask,
                                             &self->group_metrics[g]);
        }
    }
    k_mutex_unlock(&self->lock);

    return 0;
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
    for (uint8_t g = 0U; g < count; g++) {
        hvac_group_entity_set_config(&self->groups[g], &cfg->groups[g]);
    }

    /* Synchronize physical relay routing on runtime profile update */
    if (self->actuator_adapter != NULL) {
        for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
            (void)adapter_hvac_actuator_set_hw_config(self->actuator_adapter, u, &cfg->units[u]);
        }
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

int ao_hvac_set_unit_manual(ao_hvac_t *self, uint8_t unit_id, bool enable, bool run_compressor, bool run_fan)
{
    ARG_UNUSED(run_fan);
    if (self == NULL || !self->is_initialized || unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }
    k_mutex_lock(&self->lock, K_FOREVER);
    if (enable) {
        hvac_unit_entity_set_manual(&self->units[unit_id], run_compressor);
    } else {
        hvac_unit_entity_request_stop(&self->units[unit_id], false);
    }
    k_mutex_unlock(&self->lock);
    return 0;
}

