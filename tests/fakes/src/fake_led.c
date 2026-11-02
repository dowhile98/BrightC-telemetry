/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_led.c
 * @brief In-memory Fake LED port implementation.
 */

#include "fake_led.h"

#include <errno.h>
#include <string.h>

static fake_led_state_t s_fake_led;

static int fake_led_impl_set_state(void *impl, led_state_t state)
{
    fake_led_state_t *s = (fake_led_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->set_state_calls++;
    if (s->simulated_set_state_error != 0) {
        return s->simulated_set_state_error;
    }
    s->current_state = state;
    return 0;
}

static int fake_led_impl_get_state(void *impl, led_state_t *out_state)
{
    fake_led_state_t *s = (fake_led_state_t *)impl;
    if (s == NULL || out_state == NULL) {
        return -EINVAL;
    }
    s->get_state_calls++;
    if (s->simulated_get_state_error != 0) {
        return s->simulated_get_state_error;
    }
    *out_state = s->current_state;
    return 0;
}

static int fake_led_impl_set_blink_rate(void *impl, uint32_t period_ms, uint32_t duty_cycle_pct)
{
    fake_led_state_t *s = (fake_led_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->set_blink_rate_calls++;
    if (s->simulated_set_blink_rate_error != 0) {
        return s->simulated_set_blink_rate_error;
    }
    s->blink_period_ms = period_ms;
    s->blink_duty_cycle_pct = duty_cycle_pct;
    return 0;
}

static const i_led_api_t s_fake_led_api = {
    .set_state = fake_led_impl_set_state,
    .get_state = fake_led_impl_get_state,
    .set_blink_rate = fake_led_impl_set_blink_rate,
};

void fake_led_reset(void)
{
    memset(&s_fake_led, 0, sizeof(s_fake_led));
    s_fake_led.current_state = LED_STATE_OFF;
}

i_led_t fake_led_get_port(void)
{
    i_led_t port = {
        .api = &s_fake_led_api,
        .impl = &s_fake_led,
    };
    return port;
}

const fake_led_state_t *fake_led_get_state(void)
{
    return &s_fake_led;
}

void fake_led_set_errors(int set_state_err, int get_state_err, int set_blink_rate_err)
{
    s_fake_led.simulated_set_state_error = set_state_err;
    s_fake_led.simulated_get_state_error = get_state_err;
    s_fake_led.simulated_set_blink_rate_error = set_blink_rate_err;
}
