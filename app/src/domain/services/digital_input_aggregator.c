/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file digital_input_aggregator.c
 * @brief Pure domain aggregator service for digital inputs and contact alarms.
 *
 * Clean Architecture (§4.1.D): pure ISO C99/C11, zero Zephyr headers.
 */

#include "domain/services/digital_input_aggregator.h"

#include <errno.h>
#include <string.h>

static int di_aggregator_find_index(const di_aggregator_t *self, di_role_t role)
{
    if (self == NULL || !di_role_is_valid(role)) {
        return -EINVAL;
    }
    for (size_t i = 0; i < self->channel_count; i++) {
        if (self->configs[i].enabled && self->configs[i].role == role) {
            return (int)i;
        }
    }
    return -ENOENT;
}

int di_aggregator_init(di_aggregator_t *self,
                       const di_channel_config_t *configs,
                       size_t count,
                       di_raw_reader_fn_t reader,
                       void *reader_ctx)
{
    if (self == NULL || configs == NULL || count == 0U || count > DI_CHANNEL_MAX) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->channel_count = count;
    self->raw_reader = reader;
    self->reader_ctx = reader_ctx;
    self->current_time_ms = 0ULL;

    for (size_t i = 0; i < count; i++) {
        self->configs[i] = configs[i];
        debounce_filter_init(&self->filters[i], configs[i].debounce_ms, false);
        self->statuses[i].role = configs[i].role;
        self->statuses[i].raw_level = false;
        self->statuses[i].is_active = false;
        self->statuses[i].active_duration_s = 0U;
        self->statuses[i].transition_count = 0U;
        self->statuses[i].last_transition_ms = 0ULL;
        self->raw_inputs[i] = false;
        self->active_accum_ms[i] = 0U;
    }

    return 0;
}

int di_aggregator_reconfigure(di_aggregator_t *self,
                              const di_channel_config_t *configs,
                              size_t count)
{
    if (self == NULL || configs == NULL || count == 0U || count > DI_CHANNEL_MAX) {
        return -EINVAL;
    }

    self->channel_count = count;

    for (size_t i = 0; i < count; i++) {
        self->configs[i] = configs[i];
        debounce_filter_init(&self->filters[i], configs[i].debounce_ms, false);
        self->statuses[i].role = configs[i].role;
        self->statuses[i].raw_level = false;
        self->statuses[i].is_active = false;
        self->statuses[i].active_duration_s = 0U;
        self->statuses[i].transition_count = 0U;
        self->statuses[i].last_transition_ms = self->current_time_ms;
        self->raw_inputs[i] = false;
        self->active_accum_ms[i] = 0U;
    }

    return 0;
}

int di_aggregator_scan(di_aggregator_t *self,
                       uint32_t elapsed_ms,
                       di_transition_event_t *out_events,
                       size_t max_events,
                       size_t *out_event_count)
{
    if (self == NULL) {
        return -EINVAL;
    }

    self->current_time_ms += elapsed_ms;
    size_t event_count = 0U;

    for (size_t i = 0; i < self->channel_count; i++) {
        if (!self->configs[i].enabled) {
            continue;
        }

        /* Sample raw hardware pin if reader callback is bound */
        if (self->raw_reader != NULL) {
            bool pin_level = false;
            int ret = self->raw_reader(self->reader_ctx,
                                       self->configs[i].source_type,
                                       self->configs[i].source_dev_id,
                                       self->configs[i].pin_index,
                                       &pin_level);
            if (ret == 0) {
                self->raw_inputs[i] = pin_level;
            }
        }

        bool raw_pin = self->raw_inputs[i];
        self->statuses[i].raw_level = raw_pin;

        /* Normalize electrical polarity and contact geometry */
        bool is_raw_asserted = di_normalize_raw_level(raw_pin,
                                                      self->configs[i].active_level,
                                                      self->configs[i].contact_type);

        /* Pass into software integrator debounce filter */
        bool stable_active = false;
        bool transitioned = debounce_filter_update(&self->filters[i],
                                                   is_raw_asserted,
                                                   elapsed_ms,
                                                   &stable_active);

        if (transitioned) {
            self->statuses[i].transition_count++;
            self->statuses[i].last_transition_ms = self->current_time_ms;
            self->statuses[i].is_active = stable_active;
            self->active_accum_ms[i] = 0U;
            self->statuses[i].active_duration_s = 0U;

            if (out_events != NULL && event_count < max_events) {
                out_events[event_count].role = self->configs[i].role;
                out_events[event_count].new_state = stable_active ? DI_STATE_ACTIVE : DI_STATE_INACTIVE;
                out_events[event_count].prev_state = stable_active ? DI_STATE_INACTIVE : DI_STATE_ACTIVE;
                out_events[event_count].timestamp_ms = self->current_time_ms;
                event_count++;
            }
        } else {
            if (self->statuses[i].is_active) {
                self->active_accum_ms[i] += elapsed_ms;
                self->statuses[i].active_duration_s = self->active_accum_ms[i] / 1000U;
            } else {
                self->active_accum_ms[i] = 0U;
                self->statuses[i].active_duration_s = 0U;
            }
        }
    }

    if (out_event_count != NULL) {
        *out_event_count = event_count;
    }

    return 0;
}

int di_aggregator_get_state(const di_aggregator_t *self, di_role_t role, bool *out_is_active)
{
    if (self == NULL || !di_role_is_valid(role) || out_is_active == NULL) {
        return -EINVAL;
    }
    int idx = di_aggregator_find_index(self, role);
    if (idx < 0) {
        return idx;
    }
    *out_is_active = self->statuses[idx].is_active;
    return 0;
}

int di_aggregator_get_raw(const di_aggregator_t *self, di_role_t role, bool *out_raw)
{
    if (self == NULL || !di_role_is_valid(role) || out_raw == NULL) {
        return -EINVAL;
    }
    int idx = di_aggregator_find_index(self, role);
    if (idx < 0) {
        return idx;
    }
    *out_raw = self->statuses[idx].raw_level;
    return 0;
}

int di_aggregator_get_status(const di_aggregator_t *self, di_role_t role, di_channel_status_t *out_status)
{
    if (self == NULL || !di_role_is_valid(role) || out_status == NULL) {
        return -EINVAL;
    }
    int idx = di_aggregator_find_index(self, role);
    if (idx < 0) {
        return idx;
    }
    *out_status = self->statuses[idx];
    return 0;
}

int di_aggregator_get_active_mask(const di_aggregator_t *self, uint64_t *out_mask)
{
    if (self == NULL || out_mask == NULL) {
        return -EINVAL;
    }

    uint64_t mask = 0ULL;
    for (size_t i = 0; i < self->channel_count; i++) {
        if (self->configs[i].enabled && self->statuses[i].is_active) {
            if ((unsigned int)self->configs[i].role < 64U) {
                mask |= (1ULL << (unsigned int)self->configs[i].role);
            }
        }
    }
    *out_mask = mask;
    return 0;
}

int di_aggregator_set_raw_by_index(di_aggregator_t *self, size_t index, bool raw_level)
{
    if (self == NULL || index >= self->channel_count) {
        return -EINVAL;
    }
    self->raw_inputs[index] = raw_level;
    return 0;
}

int di_aggregator_set_raw_by_role(di_aggregator_t *self, di_role_t role, bool raw_level)
{
    if (self == NULL || !di_role_is_valid(role)) {
        return -EINVAL;
    }
    int idx = di_aggregator_find_index(self, role);
    if (idx < 0) {
        return idx;
    }
    self->raw_inputs[idx] = raw_level;
    return 0;
}

/*============================================================================*
 * i_digital_input_t PORT ADAPTER IMPLEMENTATION
 *============================================================================*/

static int aggregator_port_init(void *impl, const di_channel_config_t *configs, size_t count)
{
    di_aggregator_t *self = (di_aggregator_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }
    return di_aggregator_init(self, configs, count, self->raw_reader, self->reader_ctx);
}

static int aggregator_port_scan(void *impl, uint32_t elapsed_ms)
{
    di_aggregator_t *self = (di_aggregator_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }
    return di_aggregator_scan(self, elapsed_ms, NULL, 0U, NULL);
}

static int aggregator_port_get_state(void *impl, di_role_t role, bool *out_is_active)
{
    const di_aggregator_t *self = (const di_aggregator_t *)impl;
    return di_aggregator_get_state(self, role, out_is_active);
}

static int aggregator_port_get_raw(void *impl, di_role_t role, bool *out_raw)
{
    const di_aggregator_t *self = (const di_aggregator_t *)impl;
    return di_aggregator_get_raw(self, role, out_raw);
}

static int aggregator_port_get_status(void *impl, di_role_t role, di_channel_status_t *out_status)
{
    const di_aggregator_t *self = (const di_aggregator_t *)impl;
    return di_aggregator_get_status(self, role, out_status);
}

static int aggregator_port_get_active_mask(void *impl, uint64_t *out_mask)
{
    const di_aggregator_t *self = (const di_aggregator_t *)impl;
    return di_aggregator_get_active_mask(self, out_mask);
}

static const i_digital_input_api_t s_di_aggregator_api = {
    .init = aggregator_port_init,
    .scan = aggregator_port_scan,
    .get_state = aggregator_port_get_state,
    .get_raw_level = aggregator_port_get_raw,
    .get_status = aggregator_port_get_status,
    .get_active_mask = aggregator_port_get_active_mask,
};

i_digital_input_t di_aggregator_port(di_aggregator_t *self)
{
    i_digital_input_t port = {
        .api = (self != NULL) ? &s_di_aggregator_api : NULL,
        .impl = self,
    };
    return port;
}
