/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_relay.c
 * @brief In-memory Fake discrete relay port implementation for unit testing.
 */

#include "fake_relay.h"

#include <errno.h>
#include <string.h>

static fake_relay_state_t s_fake_state;

void fake_relay_reset(void)
{
    memset(&s_fake_state, 0, sizeof(s_fake_state));
    for (size_t i = 0U; i < RELAY_CHANNEL_MAX; i++) {
        s_fake_state.states[i] = RELAY_STATE_OPEN;
    }
}

static int fake_relay_api_set_state(void *impl, relay_channel_t channel, relay_state_t state)
{
    (void)impl;

    if (!relay_channel_is_valid(channel)) {
        return -EINVAL;
    }

    s_fake_state.set_state_calls++;

    if (s_fake_state.simulated_error != 0) {
        return s_fake_state.simulated_error;
    }

    s_fake_state.states[channel] = state;
    return 0;
}

static int fake_relay_api_get_state(void *impl, relay_channel_t channel, relay_state_t *out_state)
{
    (void)impl;

    if (!relay_channel_is_valid(channel) || out_state == NULL) {
        return -EINVAL;
    }

    s_fake_state.get_state_calls++;
    *out_state = s_fake_state.states[channel];
    return 0;
}

static int fake_relay_api_get_status(void *impl, relay_channel_t channel, relay_channel_status_t *out_status)
{
    (void)impl;

    if (!relay_channel_is_valid(channel) || out_status == NULL) {
        return -EINVAL;
    }

    out_status->state = s_fake_state.states[channel];
    out_status->cycle_count = s_fake_state.set_state_calls;
    out_status->last_toggle_ms = 0U;
    out_status->is_fault = (s_fake_state.simulated_error != 0);
    return 0;
}

static const i_relay_api_t s_fake_relay_api = {
    .set_state = fake_relay_api_set_state,
    .get_state = fake_relay_api_get_state,
    .get_status = fake_relay_api_get_status,
};

i_relay_t fake_relay_get_port(void)
{
    i_relay_t port = {
        .api = &s_fake_relay_api,
        .impl = &s_fake_state,
    };
    return port;
}

const fake_relay_state_t *fake_relay_get_state(void)
{
    return &s_fake_state;
}

relay_state_t fake_relay_get_channel_state(relay_channel_t ch)
{
    if (!relay_channel_is_valid(ch)) {
        return RELAY_STATE_OPEN;
    }
    return s_fake_state.states[ch];
}

void fake_relay_set_simulated_error(int err)
{
    s_fake_state.simulated_error = err;
}
