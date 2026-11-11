/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_alarm_manager.c
 * @brief Implementation of the Centralized Alarm Lifecycle Manager Active Object.
 */

#include <zephyr/logging/log.h>
#include <string.h>

#include "application/active_objects/ao_alarm_manager.h"
#include "application/zbus/alarm_channels.h"
#include "application/zbus/indicator_channels.h"
#include "common/indicator_types.h"

LOG_MODULE_REGISTER(ao_alarm_mgr, CONFIG_LOG_DEFAULT_LEVEL);

/*============================================================================*
 * INTERNAL FORWARD DECLARATIONS & QUERY API IMPLEMENTATION
 *============================================================================*/

static void ao_alarm_manager_thread_entry(void *p1, void *p2, void *p3);
static const i_alarm_query_api_t g_alarm_query_api;

/*============================================================================*
 * CANONICAL 5-PHASE LIFECYCLE
 *============================================================================*/

int ao_alarm_manager_init(ao_alarm_manager_t *self,
                          k_thread_stack_t *stack,
                          size_t stack_size,
                          int priority)
{
    if (self == NULL || stack == NULL || stack_size == 0) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));

    self->stack = stack;
    self->stack_size = stack_size;
    self->priority = priority;
    self->is_running = false;
    self->active_alarm_count = 0;
    self->highest_active_severity = ALARM_SEVERITY_INFO;

    k_msgq_init(&self->msgq, (char *)self->msgq_buffer, sizeof(ao_alarm_msg_t), AO_ALARM_MANAGER_QUEUE_DEPTH);
    k_sem_init(&self->stopped_sem, 0, 1);
    k_mutex_init(&self->lock);

    /* Initialize descriptor registry with well-known catalog */
    alarm_registry_init(&self->registry);

    /* Initialize entities according to registry descriptors */
    for (size_t i = 0; i < self->registry.count; i++) {
        const alarm_descriptor_t *d = &self->registry.descriptors[i];
        if (d->is_aggregated) {
            /* Default aggregate mask covering all 32 possible sources */
            uint32_t mask = 0xFFFFFFFFU;
            alarm_aggregate_init(&self->aggregates[i], d->alarm_id, d->source_category,
                                 d->default_severity, d->default_priority, mask, d->is_latch_supported);
        } else {
            alarm_entity_init(&self->entities[i], d->alarm_id, d->source_category,
                              d->default_severity, d->default_priority,
                              d->is_latch_supported, d->is_ack_supported);
        }

        /* Initialize snapshot */
        self->snapshot[i].alarm_id = d->alarm_id;
        self->snapshot[i].state = ALARM_STATE_INACTIVE;
        self->snapshot[i].severity = d->default_severity;
        self->snapshot[i].contributing_sources_mask = 0;
        self->snapshot[i].last_trigger_value = 0.0f;
        self->snapshot[i].triggered_timestamp_ms = 0;
        self->snapshot[i].cleared_timestamp_ms = 0;
        self->snapshot[i].trigger_count = 0;
    }

    /* Bind query port handle */
    self->query_port.api = &g_alarm_query_api;
    self->query_port.impl = self;

    /* Create thread in dormant state */
    self->tid = k_thread_create(&self->thread,
                                self->stack,
                                self->stack_size,
                                ao_alarm_manager_thread_entry,
                                self, NULL, NULL,
                                self->priority,
                                0,
                                K_FOREVER);
    k_thread_name_set(self->tid, "ao_alarm_mgr");

    LOG_INF("ao_alarm_mgr initialized (catalog: %zu alarms)", self->registry.count);
    return 0;
}

int ao_alarm_manager_start(ao_alarm_manager_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }
    if (self->is_running) {
        return -EALREADY;
    }

    if (self->tid == NULL) {
        self->tid = k_thread_create(&self->thread,
                                    self->stack,
                                    self->stack_size,
                                    ao_alarm_manager_thread_entry,
                                    self, NULL, NULL,
                                    self->priority,
                                    0,
                                    K_FOREVER);
        k_thread_name_set(self->tid, "ao_alarm_mgr");
    }

    self->is_running = true;
    k_thread_start(self->tid);
    LOG_INF("ao_alarm_mgr thread started");
    return 0;
}

int ao_alarm_manager_stop(ao_alarm_manager_t *self)
{
    if (self == NULL || !self->is_running) {
        return -EINVAL;
    }

    ao_alarm_msg_t msg = {
        .cmd = AO_ALARM_CMD_STOP
    };
    int err = k_msgq_put(&self->msgq, &msg, K_NO_WAIT);
    if (err == -ENOMSG) {
        /* Guarantee STOP delivery under queue saturation (ALARM-FU-010) */
        ao_alarm_msg_t dropped;
        (void)k_msgq_get(&self->msgq, &dropped, K_NO_WAIT);
        err = k_msgq_put(&self->msgq, &msg, K_NO_WAIT);
    }
    return err;
}

int ao_alarm_manager_wait_stopped(ao_alarm_manager_t *self, k_timeout_t timeout)
{
    if (self == NULL) {
        return -EINVAL;
    }
    int rc = k_sem_take(&self->stopped_sem, timeout);
    if (rc == 0 && self->tid != NULL) {
        (void)k_thread_join(self->tid, timeout);
        self->tid = NULL;
    }
    return rc;
}

int ao_alarm_manager_deinit(ao_alarm_manager_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }

    int rc = 0;
    if (self->is_running) {
        (void)ao_alarm_manager_stop(self);
        rc = ao_alarm_manager_wait_stopped(self, K_MSEC(500));
        if (rc != 0) {
            LOG_WRN("ao_alarm_mgr failed to stop cleanly within timeout (%d), forcing abort", rc);
            if (self->tid != NULL) {
                k_thread_abort(self->tid);
                self->tid = NULL;
            }
        }
    } else if (self->tid != NULL) {
        (void)k_thread_join(self->tid, K_MSEC(200));
        self->tid = NULL;
    }

    self->is_running = false;
    return rc;
}

/*============================================================================*
 * PUBLIC NON-BLOCKING CONDITION POSTING APIS
 *============================================================================*/

int ao_alarm_manager_post_condition(ao_alarm_manager_t *self,
                                    uint16_t alarm_id,
                                    bool is_asserted,
                                    float trigger_val)
{
    if (self == NULL || !self->is_running || alarm_id == ALARM_ID_NONE) {
        return -EINVAL;
    }
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, alarm_id);
    k_mutex_unlock(&self->lock);
    if (idx < 0) {
        return -ENOENT;
    }

    ao_alarm_msg_t msg = {
        .cmd = AO_ALARM_CMD_UPDATE_DISCRETE,
        .alarm_id = alarm_id,
        .source_index = 0,
        .is_asserted = is_asserted,
        .trigger_val = trigger_val,
        .timestamp_ms = (uint64_t)k_uptime_get()
    };

    int err = k_msgq_put(&self->msgq, &msg, K_NO_WAIT);
    if (err != 0) {
        LOG_WRN("alarm queue full (%d), dropped cmd=%d alarm=0x%04X", err, msg.cmd, msg.alarm_id);
    }
    return err;
}

int ao_alarm_manager_post_source_condition(ao_alarm_manager_t *self,
                                           uint16_t agg_alarm_id,
                                           uint8_t source_idx,
                                           bool is_asserted,
                                           float trigger_val)
{
    if (self == NULL || !self->is_running || agg_alarm_id == ALARM_ID_NONE || source_idx >= 32) {
        return -EINVAL;
    }
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, agg_alarm_id);
    bool is_agg = (idx >= 0) ? self->registry.descriptors[idx].is_aggregated : false;
    k_mutex_unlock(&self->lock);
    if (idx < 0) {
        return -ENOENT;
    }
    if (!is_agg) {
        return -ENOTSUP;
    }

    ao_alarm_msg_t msg = {
        .cmd = AO_ALARM_CMD_UPDATE_AGGREGATE,
        .alarm_id = agg_alarm_id,
        .source_index = source_idx,
        .is_asserted = is_asserted,
        .trigger_val = trigger_val,
        .timestamp_ms = (uint64_t)k_uptime_get()
    };

    int err = k_msgq_put(&self->msgq, &msg, K_NO_WAIT);
    if (err != 0) {
        LOG_WRN("alarm queue full (%d), dropped cmd=%d alarm=0x%04X", err, msg.cmd, msg.alarm_id);
    }
    return err;
}

int ao_alarm_manager_post_ack(ao_alarm_manager_t *self, uint16_t alarm_id)
{
    if (self == NULL || !self->is_running || alarm_id == ALARM_ID_NONE) {
        return -EINVAL;
    }
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, alarm_id);
    bool ack_sup = (idx >= 0) ? (self->registry.descriptors[idx].is_ack_supported &&
                                 !self->registry.descriptors[idx].is_aggregated) : false;
    k_mutex_unlock(&self->lock);
    if (idx < 0) {
        return -ENOENT;
    }
    if (!ack_sup) {
        return -ENOTSUP;
    }

    ao_alarm_msg_t msg = {
        .cmd = AO_ALARM_CMD_ACKNOWLEDGE,
        .alarm_id = alarm_id,
        .timestamp_ms = (uint64_t)k_uptime_get()
    };

    return k_msgq_put(&self->msgq, &msg, K_NO_WAIT);
}

int ao_alarm_manager_post_reset_latch(ao_alarm_manager_t *self, uint16_t alarm_id)
{
    if (self == NULL || !self->is_running || alarm_id == ALARM_ID_NONE) {
        return -EINVAL;
    }
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, alarm_id);
    bool latch_sup = (idx >= 0) ? self->registry.descriptors[idx].is_latch_supported : false;
    k_mutex_unlock(&self->lock);
    if (idx < 0) {
        return -ENOENT;
    }
    if (!latch_sup) {
        return -ENOTSUP;
    }

    ao_alarm_msg_t msg = {
        .cmd = AO_ALARM_CMD_RESET_LATCH,
        .alarm_id = alarm_id,
        .timestamp_ms = (uint64_t)k_uptime_get()
    };

    return k_msgq_put(&self->msgq, &msg, K_NO_WAIT);
}

i_alarm_query_t ao_alarm_manager_get_query_port(ao_alarm_manager_t *self)
{
    if (self == NULL) {
        i_alarm_query_t empty = { NULL, NULL };
        return empty;
    }
    return self->query_port;
}

int ao_alarm_manager_register_descriptor(ao_alarm_manager_t *self,
                                         const alarm_descriptor_t *desc)
{
    if (self == NULL || desc == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->lock, K_FOREVER);
    int rc = alarm_registry_register(&self->registry, desc);
    if (rc != 0) {
        k_mutex_unlock(&self->lock);
        return rc;
    }

    int idx = alarm_registry_find_index(&self->registry, desc->alarm_id);
    if (idx >= 0) {
        if (desc->is_aggregated) {
            alarm_aggregate_init(&self->aggregates[idx], desc->alarm_id, desc->source_category,
                                 desc->default_severity, desc->default_priority, 0xFFFFFFFFU,
                                 desc->is_latch_supported);
        } else {
            alarm_entity_init(&self->entities[idx], desc->alarm_id, desc->source_category,
                              desc->default_severity, desc->default_priority,
                              desc->is_latch_supported, desc->is_ack_supported);
        }

        self->snapshot[idx].alarm_id = desc->alarm_id;
        self->snapshot[idx].state = ALARM_STATE_INACTIVE;
        self->snapshot[idx].severity = desc->default_severity;
        self->snapshot[idx].contributing_sources_mask = 0;
        self->snapshot[idx].last_trigger_value = 0.0f;
        self->snapshot[idx].triggered_timestamp_ms = 0;
        self->snapshot[idx].cleared_timestamp_ms = 0;
        self->snapshot[idx].trigger_count = 0;
        self->snapshot[idx].active_duration_ms = 0;
    }
    k_mutex_unlock(&self->lock);
    return 0;
}

uint32_t ao_alarm_manager_get_active_duration(ao_alarm_manager_t *self, uint16_t alarm_id)
{
    if (self == NULL || alarm_id == ALARM_ID_NONE) {
        return 0;
    }

    uint32_t duration = 0;
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, alarm_id);
    if (idx >= 0) {
        alarm_state_t st = self->snapshot[idx].state;
        if (st == ALARM_STATE_ACTIVE || st == ALARM_STATE_ACKNOWLEDGED || st == ALARM_STATE_LATCHED) {
            uint64_t now_ms = (uint64_t)k_uptime_get();
            if (now_ms >= self->snapshot[idx].triggered_timestamp_ms) {
                duration = (uint32_t)(now_ms - self->snapshot[idx].triggered_timestamp_ms);
            }
        } else {
            duration = 0;
        }
    }
    k_mutex_unlock(&self->lock);
    return duration;
}

/*============================================================================*
 * INTERNAL DISPATCH & PUBLISHING HELPERS
 *============================================================================*/

static void recalculate_active_metrics_locked(ao_alarm_manager_t *self)
{
    size_t count = 0;
    alarm_severity_t highest = ALARM_SEVERITY_INFO;

    for (size_t i = 0; i < self->registry.count; i++) {
        alarm_state_t st = self->snapshot[i].state;
        if (st == ALARM_STATE_ACTIVE || st == ALARM_STATE_ACKNOWLEDGED || st == ALARM_STATE_LATCHED) {
            count++;
            if (self->snapshot[i].severity > highest) {
                highest = self->snapshot[i].severity;
            }
        }
    }

    self->active_alarm_count = count;
    self->highest_active_severity = highest;
}

static void publish_transition_event(const alarm_transition_event_t *evt)
{
    /* 1. Broadcast detailed transition over Zbus */
    int pub_err = zbus_chan_pub(&chan_alarm_transition, evt, K_NO_WAIT);
    if (pub_err != 0) {
        LOG_WRN("zbus publish chan_alarm_transition failed (%d)", pub_err);
    }

    /* 2. Map severity and notify System Indicators via chan_system_alarm */
    alarm_level_t ind_level = ALARM_LEVEL_NONE;
    if (evt->new_state == ALARM_STATE_ACTIVE ||
        evt->new_state == ALARM_STATE_ACKNOWLEDGED ||
        evt->new_state == ALARM_STATE_LATCHED) {

        if (evt->severity == ALARM_SEVERITY_FATAL || evt->severity == ALARM_SEVERITY_CRITICAL) {
            ind_level = ALARM_LEVEL_CRITICAL;
        } else if (evt->severity == ALARM_SEVERITY_WARNING) {
            ind_level = ALARM_LEVEL_WARNING;
        } else {
            ind_level = ALARM_LEVEL_INFO;
        }
    }

    alarm_msg_t ind_msg = {
        .alarm_id = evt->alarm_id,
        .level = ind_level,
        .is_active = (evt->new_state != ALARM_STATE_INACTIVE),
        .timestamp_ms = (uint32_t)evt->timestamp_ms
    };

    (void)zbus_chan_pub(&chan_system_alarm, &ind_msg, K_NO_WAIT);
}

/*============================================================================*
 * THREAD INNER LOOP
 *============================================================================*/

static void ao_alarm_manager_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    ao_alarm_manager_t *self = (ao_alarm_manager_t *)p1;
    ao_alarm_msg_t msg;
    alarm_transition_event_t evt;

    LOG_INF("ao_alarm_mgr thread entered inner loop");

    while (k_msgq_get(&self->msgq, &msg, K_FOREVER) == 0) {
        if (msg.cmd == AO_ALARM_CMD_STOP) {
            LOG_INF("ao_alarm_mgr received STOP command");
            break;
        }

        k_mutex_lock(&self->lock, K_FOREVER);
        int idx = alarm_registry_find_index(&self->registry, msg.alarm_id);
        if (idx < 0) {
            k_mutex_unlock(&self->lock);
            LOG_WRN("Unknown alarm ID 0x%04x received", msg.alarm_id);
            continue;
        }

        bool changed = false;
        alarm_descriptor_t desc_copy = self->registry.descriptors[idx];
        k_mutex_unlock(&self->lock);
        const alarm_descriptor_t *desc = &desc_copy;

        if (msg.cmd == AO_ALARM_CMD_UPDATE_DISCRETE) {
            if (!desc->is_aggregated) {
                changed = alarm_entity_update_condition(&self->entities[idx],
                                                        msg.is_asserted,
                                                        msg.trigger_val,
                                                        msg.timestamp_ms,
                                                        &evt);
            } else {
                changed = alarm_aggregate_update_source(&self->aggregates[idx],
                                                        0,
                                                        msg.is_asserted,
                                                        msg.trigger_val,
                                                        msg.timestamp_ms,
                                                        &evt);
            }
        } else if (msg.cmd == AO_ALARM_CMD_UPDATE_AGGREGATE) {
            if (desc->is_aggregated) {
                changed = alarm_aggregate_update_source(&self->aggregates[idx],
                                                        msg.source_index,
                                                        msg.is_asserted,
                                                        msg.trigger_val,
                                                        msg.timestamp_ms,
                                                        &evt);
            }
        } else if (msg.cmd == AO_ALARM_CMD_ACKNOWLEDGE) {
            if (!desc->is_aggregated) {
                changed = alarm_entity_acknowledge(&self->entities[idx],
                                                    msg.timestamp_ms,
                                                    &evt);
            }
        } else if (msg.cmd == AO_ALARM_CMD_RESET_LATCH) {
            if (desc->is_aggregated) {
                changed = alarm_aggregate_reset_latch(&self->aggregates[idx],
                                                      msg.timestamp_ms,
                                                      &evt);
            } else {
                changed = alarm_entity_reset_latch(&self->entities[idx],
                                                    msg.timestamp_ms,
                                                    &evt);
            }
        }

        if (changed) {
            /* Update snapshot cache atomically */
            k_mutex_lock(&self->lock, K_FOREVER);
            self->snapshot[idx].state = evt.new_state;
            self->snapshot[idx].contributing_sources_mask = evt.contributing_sources_mask;
            if (evt.event_type == ALARM_EVENT_ACTIVATED || evt.event_type == ALARM_EVENT_SOURCE_CHANGED) {
                self->snapshot[idx].last_trigger_value = evt.trigger_value;
            }
            if (evt.event_type == ALARM_EVENT_ACTIVATED) {
                self->snapshot[idx].triggered_timestamp_ms = evt.timestamp_ms;
                self->snapshot[idx].trigger_count++;
            } else if (evt.event_type == ALARM_EVENT_CLEARED) {
                self->snapshot[idx].cleared_timestamp_ms = evt.timestamp_ms;
                if (evt.timestamp_ms >= self->snapshot[idx].triggered_timestamp_ms) {
                    self->snapshot[idx].active_duration_ms =
                        (uint32_t)(evt.timestamp_ms - self->snapshot[idx].triggered_timestamp_ms);
                }
            }
            recalculate_active_metrics_locked(self);
            k_mutex_unlock(&self->lock);

            /* Publish event to observers */
            publish_transition_event(&evt);
        }
    }

    self->is_running = false;
    k_sem_give(&self->stopped_sem);
    LOG_INF("ao_alarm_mgr thread terminated");
}

/*============================================================================*
 * QUERY PORT V-TABLE IMPLEMENTATION
 *============================================================================*/

static size_t query_get_active_count(void *impl)
{
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    k_mutex_lock(&self->lock, K_FOREVER);
    size_t count = self->active_alarm_count;
    k_mutex_unlock(&self->lock);
    return count;
}

static alarm_severity_t query_get_highest_active_severity(void *impl)
{
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    k_mutex_lock(&self->lock, K_FOREVER);
    alarm_severity_t sev = self->highest_active_severity;
    k_mutex_unlock(&self->lock);
    return sev;
}

static bool query_is_active(void *impl, uint16_t alarm_id)
{
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, alarm_id);
    if (idx < 0) {
        k_mutex_unlock(&self->lock);
        return false;
    }

    alarm_state_t st = self->snapshot[idx].state;
    bool active = (st == ALARM_STATE_ACTIVE || st == ALARM_STATE_ACKNOWLEDGED || st == ALARM_STATE_LATCHED);
    k_mutex_unlock(&self->lock);
    return active;
}

static int query_get_alarm_state(void *impl, uint16_t alarm_id, alarm_state_t *out_state)
{
    if (out_state == NULL) {
        return -EINVAL;
    }
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, alarm_id);
    if (idx < 0) {
        k_mutex_unlock(&self->lock);
        return -ENOENT;
    }

    *out_state = self->snapshot[idx].state;
    k_mutex_unlock(&self->lock);
    return 0;
}

static int query_get_alarm_snapshot(void *impl, uint16_t alarm_id, alarm_snapshot_entry_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return -EINVAL;
    }
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    k_mutex_lock(&self->lock, K_FOREVER);
    int idx = alarm_registry_find_index(&self->registry, alarm_id);
    if (idx < 0) {
        k_mutex_unlock(&self->lock);
        return -ENOENT;
    }

    *out_snapshot = self->snapshot[idx];
    alarm_state_t st = self->snapshot[idx].state;
    if (st == ALARM_STATE_ACTIVE || st == ALARM_STATE_ACKNOWLEDGED || st == ALARM_STATE_LATCHED) {
        uint64_t now_ms = (uint64_t)k_uptime_get();
        if (now_ms >= self->snapshot[idx].triggered_timestamp_ms) {
            out_snapshot->active_duration_ms =
                (uint32_t)(now_ms - self->snapshot[idx].triggered_timestamp_ms);
        }
    }
    k_mutex_unlock(&self->lock);
    return 0;
}

static size_t query_get_active_alarm_ids(void *impl, uint16_t *out_ids, size_t max_count)
{
    if (out_ids == NULL || max_count == 0) {
        return 0;
    }
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    size_t copied = 0;

    k_mutex_lock(&self->lock, K_FOREVER);
    for (size_t i = 0; i < self->registry.count && copied < max_count; i++) {
        alarm_state_t st = self->snapshot[i].state;
        if (st == ALARM_STATE_ACTIVE || st == ALARM_STATE_ACKNOWLEDGED || st == ALARM_STATE_LATCHED) {
            out_ids[copied++] = self->snapshot[i].alarm_id;
        }
    }
    k_mutex_unlock(&self->lock);
    return copied;
}

static int query_get_descriptor(void *impl, uint16_t alarm_id, alarm_descriptor_t *out_desc)
{
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    k_mutex_lock(&self->lock, K_FOREVER);
    int rc = alarm_registry_get(&self->registry, alarm_id, out_desc);
    k_mutex_unlock(&self->lock);
    return rc;
}

static uint32_t query_get_active_duration(void *impl, uint16_t alarm_id)
{
    ao_alarm_manager_t *self = (ao_alarm_manager_t *)impl;
    return ao_alarm_manager_get_active_duration(self, alarm_id);
}

static const i_alarm_query_api_t g_alarm_query_api = {
    .get_active_count = query_get_active_count,
    .get_highest_active_severity = query_get_highest_active_severity,
    .is_active = query_is_active,
    .get_alarm_state = query_get_alarm_state,
    .get_alarm_snapshot = query_get_alarm_snapshot,
    .get_active_alarm_ids = query_get_active_alarm_ids,
    .get_descriptor = query_get_descriptor,
    .get_active_duration = query_get_active_duration
};
