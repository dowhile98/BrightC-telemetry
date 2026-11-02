/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_led_gpio.c
 * @brief Infrastructure adapter for Status LED using Zephyr GPIO driver.
 */

#include "infrastructure/adapters/adapter_led_gpio.h"

#include <errno.h>

static int adapter_led_gpio_impl_set_state(void *impl, led_state_t state)
{
    adapter_led_gpio_t *self = (adapter_led_gpio_t *)impl;
    if (self == NULL || !self->is_ready || !led_state_is_valid(state)) {
        return -EINVAL;
    }

    int pin_val = (state == LED_STATE_OFF) ? 0 : 1;
    int err = gpio_pin_set_dt(&self->gpio_spec, pin_val);
    if (err != 0) {
        return err;
    }

    self->current_state = state;
    return 0;
}

static int adapter_led_gpio_impl_get_state(void *impl, led_state_t *out_state)
{
    adapter_led_gpio_t *self = (adapter_led_gpio_t *)impl;
    if (self == NULL || !self->is_ready || out_state == NULL) {
        return -EINVAL;
    }

    *out_state = self->current_state;
    return 0;
}

static int adapter_led_gpio_impl_set_blink_rate(void *impl, uint32_t period_ms, uint32_t duty_cycle_pct)
{
    adapter_led_gpio_t *self = (adapter_led_gpio_t *)impl;
    if (self == NULL || !self->is_ready || period_ms == 0U || duty_cycle_pct > 100U) {
        return -EINVAL;
    }

    self->blink_period_ms = period_ms;
    self->blink_duty_cycle_pct = duty_cycle_pct;
    return 0;
}

static const i_led_api_t s_adapter_led_gpio_api = {
    .set_state = adapter_led_gpio_impl_set_state,
    .get_state = adapter_led_gpio_impl_get_state,
    .set_blink_rate = adapter_led_gpio_impl_set_blink_rate,
};

int adapter_led_gpio_init(adapter_led_gpio_t *self, const struct gpio_dt_spec *gpio_spec)
{
    if (self == NULL || gpio_spec == NULL) {
        return -EINVAL;
    }

    self->gpio_spec = *gpio_spec;
    self->current_state = LED_STATE_OFF;
    self->blink_period_ms = 1000U;
    self->blink_duty_cycle_pct = 50U;
    self->is_ready = false;

    if (!gpio_is_ready_dt(&self->gpio_spec)) {
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&self->gpio_spec, GPIO_OUTPUT_INACTIVE);
    if (err != 0) {
        return err;
    }

    self->is_ready = true;
    return 0;
}

int adapter_led_gpio_port(adapter_led_gpio_t *self, i_led_t *out_port)
{
    if (self == NULL || !self->is_ready || out_port == NULL) {
        return -EINVAL;
    }

    out_port->api = &s_adapter_led_gpio_api;
    out_port->impl = self;
    return 0;
}
