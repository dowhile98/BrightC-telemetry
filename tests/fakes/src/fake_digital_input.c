/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_digital_input.c
 * @brief In-memory Fake Digital Input port implementation for unit tests.
 */

#include "fake_digital_input.h"

#include <errno.h>
#include <string.h>

static fake_digital_input_state_t s_fake_di;

static int fake_di_find_index(di_role_t role)
{
    if (!di_role_is_valid(role)) {
        return -EINVAL;
    }
    for (size_t i = 0; i < s_fake_di.channel_count; i++) {
        if (s_fake_di.configs[i].enabled && s_fake_di.configs[i].role == role) {
            return (int)i;
        }
    }
    return -ENOENT;
}

static int fake_di_impl_init(void *impl, const di_channel_config_t *configs, size_t count)
{
    fake_digital_input_state_t *s = (fake_digital_input_state_t *)impl;
    if (s == NULL || configs == NULL || count == 0U || count > DI_CHANNEL_MAX) {
        return -EINVAL;
    }

    s->init_calls++;
    if (s->simulated_init_error != 0) {
        return s->simulated_init_error;
    }

    s->channel_count = count;
    for (size_t i = 0; i < count; i++) {
        s->configs[i] = configs[i];
        debounce_filter_init(&s->filters[i], configs[i].debounce_ms, false);
        s->statuses[i].role = configs[i].role;
        s->statuses[i].raw_level = false;
        s->statuses[i].is_active = false;
        s->statuses[i].active_duration_s = 0U;
        s->statuses[i].transition_count = 0U;
        s->statuses[i].last_transition_ms = 0ULL;
        s->raw_levels[i] = false;
        s->active_accum_ms[i] = 0U;
    }

    return 0;
}

static int fake_di_impl_scan(void *impl, uint32_t elapsed_ms)
{
    fake_digital_input_state_t *s = (fake_digital_input_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }

    s->scan_calls++;
    if (s->simulated_scan_error != 0) {
        return s->simulated_scan_error;
    }

    s->current_time_ms += elapsed_ms;

    for (size_t i = 0; i < s->channel_count; i++) {
        if (!s->configs[i].enabled) {
            continue;
        }

        s->statuses[i].raw_level = s->raw_levels[i];
        bool is_asserted = di_normalize_raw_level(s->raw_levels[i],
                                                  s->configs[i].active_level,
                                                  s->configs[i].contact_type);
        bool stable = false;
        bool changed = debounce_filter_update(&s->filters[i], is_asserted, elapsed_ms, &stable);

        if (changed) {
            s->statuses[i].transition_count++;
            s->statuses[i].last_transition_ms = s->current_time_ms;
            s->statuses[i].is_active = stable;
            s->active_accum_ms[i] = 0U;
            s->statuses[i].active_duration_s = 0U;
        } else {
            if (s->statuses[i].is_active) {
                s->active_accum_ms[i] += elapsed_ms;
                s->statuses[i].active_duration_s = s->active_accum_ms[i] / 1000U;
            } else {
                s->active_accum_ms[i] = 0U;
                s->statuses[i].active_duration_s = 0U;
            }
        }
    }

    return 0;
}

static int fake_di_impl_get_state(void *impl, di_role_t role, bool *out_is_active)
{
    fake_digital_input_state_t *s = (fake_digital_input_state_t *)impl;
    if (s == NULL || !di_role_is_valid(role) || out_is_active == NULL) {
        return -EINVAL;
    }

    s->get_state_calls++;
    if (s->simulated_get_state_error != 0) {
        return s->simulated_get_state_error;
    }

    int idx = fake_di_find_index(role);
    if (idx < 0) {
        return idx;
    }

    *out_is_active = s->statuses[idx].is_active;
    return 0;
}

static int fake_di_impl_get_raw_level(void *impl, di_role_t role, bool *out_raw_level)
{
    fake_digital_input_state_t *s = (fake_digital_input_state_t *)impl;
    if (s == NULL || !di_role_is_valid(role) || out_raw_level == NULL) {
        return -EINVAL;
    }

    s->get_raw_level_calls++;
    if (s->simulated_get_raw_level_error != 0) {
        return s->simulated_get_raw_level_error;
    }

    int idx = fake_di_find_index(role);
    if (idx < 0) {
        return idx;
    }

    *out_raw_level = s->statuses[idx].raw_level;
    return 0;
}

static int fake_di_impl_get_status(void *impl, di_role_t role, di_channel_status_t *out_status)
{
    fake_digital_input_state_t *s = (fake_digital_input_state_t *)impl;
    if (s == NULL || !di_role_is_valid(role) || out_status == NULL) {
        return -EINVAL;
    }

    s->get_status_calls++;
    if (s->simulated_get_status_error != 0) {
        return s->simulated_get_status_error;
    }

    int idx = fake_di_find_index(role);
    if (idx < 0) {
        return idx;
    }

    *out_status = s->statuses[idx];
    return 0;
}

static int fake_di_impl_get_active_mask(void *impl, uint64_t *out_mask)
{
    fake_digital_input_state_t *s = (fake_digital_input_state_t *)impl;
    if (s == NULL || out_mask == NULL) {
        return -EINVAL;
    }

    s->get_active_mask_calls++;
    if (s->simulated_get_active_mask_error != 0) {
        return s->simulated_get_active_mask_error;
    }

    uint64_t mask = 0ULL;
    for (size_t i = 0; i < s->channel_count; i++) {
        if (s->configs[i].enabled && s->statuses[i].is_active) {
            if ((unsigned int)s->configs[i].role < 64U) {
                mask |= (1ULL << (unsigned int)s->configs[i].role);
            }
        }
    }

    *out_mask = mask;
    return 0;
}

static const i_digital_input_api_t s_fake_di_api = {
    .init = fake_di_impl_init,
    .scan = fake_di_impl_scan,
    .get_state = fake_di_impl_get_state,
    .get_raw_level = fake_di_impl_get_raw_level,
    .get_status = fake_di_impl_get_status,
    .get_active_mask = fake_di_impl_get_active_mask,
};

void fake_digital_input_reset(void)
{
    memset(&s_fake_di, 0, sizeof(s_fake_di));
}

i_digital_input_t fake_digital_input_port(void)
{
    i_digital_input_t port = {
        .api = &s_fake_di_api,
        .impl = &s_fake_di,
    };
    return port;
}

const fake_digital_input_state_t *fake_digital_input_get_state(void)
{
    return &s_fake_di;
}

void fake_digital_input_set_errors(int init_err, int scan_err, int get_state_err)
{
    s_fake_di.simulated_init_error = init_err;
    s_fake_di.simulated_scan_error = scan_err;
    s_fake_di.simulated_get_state_error = get_state_err;
}

int fake_digital_input_set_raw_level(di_role_t role, bool raw_level)
{
    int idx = fake_di_find_index(role);
    if (idx < 0) {
        return idx;
    }
    s_fake_di.raw_levels[idx] = raw_level;
    return 0;
}

int fake_digital_input_force_state(di_role_t role, bool is_active)
{
    int idx = fake_di_find_index(role);
    if (idx < 0) {
        return idx;
    }
    s_fake_di.statuses[idx].is_active = is_active;
    debounce_filter_init(&s_fake_di.filters[idx], s_fake_di.configs[idx].debounce_ms, is_active);
    /* Align raw level with forced state so subsequent scans preserve it */
    bool raw_assert = is_active;
    if (s_fake_di.configs[idx].contact_type == DI_CONTACT_NORMALLY_CLOSED) {
        raw_assert = !raw_assert;
    }
    s_fake_di.raw_levels[idx] = (s_fake_di.configs[idx].active_level == DI_ACTIVE_HIGH) ? raw_assert : !raw_assert;
    s_fake_di.statuses[idx].raw_level = s_fake_di.raw_levels[idx];
    return 0;
}

int fake_digital_input_step_ms(uint32_t step_ms)
{
    return fake_di_impl_scan(&s_fake_di, step_ms);
}
