/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake implementation of i_energy_meter port.
 */

#include "fake_energy_meter.h"
#include <string.h>

static fake_energy_meter_state_t s_fake_state;

static int fake_get_reading(void *impl, energy_reading_t *out_reading)
{
    fake_energy_meter_state_t *s = (fake_energy_meter_state_t *)impl;
    s->get_reading_calls++;
    if (s->get_reading_ret != 0) {
        return s->get_reading_ret;
    }
    memcpy(out_reading, &s->reading, sizeof(energy_reading_t));
    return 0;
}

static const i_energy_meter_api_t s_fake_api = {
    .get_reading = fake_get_reading,
};

void fake_energy_meter_reset(void)
{
    memset(&s_fake_state, 0, sizeof(s_fake_state));
}

i_energy_meter_t fake_energy_meter_get_port(void)
{
    i_energy_meter_t port = {
        .api = &s_fake_api,
        .impl = &s_fake_state,
    };
    return port;
}

void fake_energy_meter_set_reading(const energy_reading_t *reading)
{
    if (reading != NULL) {
        memcpy(&s_fake_state.reading, reading, sizeof(energy_reading_t));
    }
}

void fake_energy_meter_set_return_code(int ret)
{
    s_fake_state.get_reading_ret = ret;
}

const fake_energy_meter_state_t *fake_energy_meter_get_state(void)
{
    return &s_fake_state;
}
