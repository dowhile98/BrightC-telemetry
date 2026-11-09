/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_hvac_unit.c
 * @brief In-memory Fake HVAC unit actuator port implementation.
 */

#include "fake_hvac_unit.h"

#include <errno.h>
#include <string.h>

static fake_hvac_unit_state_t s_fake_hvac_unit;

static int fake_hvac_unit_impl_set_tri_state(void *impl, uint8_t unit_id, hvac_tri_state_t tri)
{
    fake_hvac_unit_state_t *s = (fake_hvac_unit_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    if (unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    s->set_tri_state_calls++;
    s->unit_set_count[unit_id]++;

    if (s->simulated_set_tri_state_error != 0) {
        return s->simulated_set_tri_state_error;
    }

    s->unit_tri_states[unit_id] = tri;
    return 0;
}

static int fake_hvac_unit_impl_get_tri_state(void *impl, uint8_t unit_id, hvac_tri_state_t *out_tri)
{
    fake_hvac_unit_state_t *s = (fake_hvac_unit_state_t *)impl;
    if (s == NULL || out_tri == NULL) {
        return -EINVAL;
    }
    if (unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }

    s->get_tri_state_calls++;

    if (s->simulated_get_tri_state_error != 0) {
        return s->simulated_get_tri_state_error;
    }

    *out_tri = s->unit_tri_states[unit_id];
    return 0;
}

static int fake_hvac_unit_impl_set_bypass_alarm(void *impl, bool active)
{
    fake_hvac_unit_state_t *s = (fake_hvac_unit_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->set_bypass_alarm_calls++;
    s->bypass_alarm_active = active;
    return 0;
}

static const i_hvac_unit_api_t s_fake_hvac_unit_api = {
    .set_tri_state = fake_hvac_unit_impl_set_tri_state,
    .get_tri_state = fake_hvac_unit_impl_get_tri_state,
    .set_bypass_alarm = fake_hvac_unit_impl_set_bypass_alarm,
};

void fake_hvac_unit_reset(void)
{
    memset(&s_fake_hvac_unit, 0, sizeof(s_fake_hvac_unit));
    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        s_fake_hvac_unit.unit_tri_states[i] = HVAC_TRI_STATE_OFF;
    }
}

i_hvac_unit_t fake_hvac_unit_get_port(void)
{
    i_hvac_unit_t port = {
        .api = &s_fake_hvac_unit_api,
        .impl = &s_fake_hvac_unit,
    };
    return port;
}

const fake_hvac_unit_state_t *fake_hvac_unit_get_state(void)
{
    return &s_fake_hvac_unit;
}

hvac_tri_state_t fake_hvac_unit_get_unit_state(uint8_t unit_id)
{
    if (unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return HVAC_TRI_STATE_OFF;
    }
    return s_fake_hvac_unit.unit_tri_states[unit_id];
}

void fake_hvac_unit_set_unit_state(uint8_t unit_id, hvac_tri_state_t state)
{
    if (unit_id < HVAC_MAX_UNITS_TOTAL) {
        s_fake_hvac_unit.unit_tri_states[unit_id] = state;
    }
}

void fake_hvac_unit_set_errors(int set_err, int get_err)
{
    s_fake_hvac_unit.simulated_set_tri_state_error = set_err;
    s_fake_hvac_unit.simulated_get_tri_state_error = get_err;
}

bool fake_hvac_unit_is_bypass_alarm_active(void)
{
    return s_fake_hvac_unit.bypass_alarm_active;
}
