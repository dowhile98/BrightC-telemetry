/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_buzzer_pwm.c
 * @brief Infrastructure adapter for Buzzer using Zephyr PWM driver.
 */

#include "infrastructure/adapters/adapter_buzzer_pwm.h"

#include <errno.h>

static int adapter_buzzer_pwm_impl_play_tone(void *impl, uint32_t frequency_hz, uint32_t duration_ms)
{
    adapter_buzzer_pwm_t *self = (adapter_buzzer_pwm_t *)impl;
    if (self == NULL || !self->is_ready) {
        return -EINVAL;
    }

    (void)duration_ms; /* Non-blocking: tone duration is managed by caller or Active Object */

    if (frequency_hz == 0U) {
        int err = pwm_set_pulse_dt(&self->pwm_spec, 0U);
        if (err != 0) {
            return err;
        }
        self->current_frequency_hz = 0U;
        self->current_pattern = BUZZER_PATTERN_SILENT;
        return 0;
    }

    uint64_t period_ns = 1000000000ULL / (uint64_t)frequency_hz;
    uint64_t pulse_ns = period_ns / 2ULL;

    int err = pwm_set_dt(&self->pwm_spec, (uint32_t)period_ns, (uint32_t)pulse_ns);
    if (err != 0) {
        return err;
    }

    self->current_frequency_hz = frequency_hz;
    return 0;
}

static int adapter_buzzer_pwm_impl_play_pattern(void *impl, buzzer_pattern_t pattern, uint32_t frequency_hz)
{
    adapter_buzzer_pwm_t *self = (adapter_buzzer_pwm_t *)impl;
    if (self == NULL || !self->is_ready || !buzzer_pattern_is_valid(pattern)) {
        return -EINVAL;
    }

    if (pattern == BUZZER_PATTERN_SILENT) {
        int err = pwm_set_pulse_dt(&self->pwm_spec, 0U);
        if (err != 0) {
            return err;
        }
        self->current_pattern = BUZZER_PATTERN_SILENT;
        self->current_frequency_hz = 0U;
        return 0;
    }

    uint32_t freq = (frequency_hz > 0U) ? frequency_hz : 2000U;
    uint64_t period_ns = 1000000000ULL / (uint64_t)freq;
    uint64_t pulse_ns = period_ns / 2ULL;

    int err = pwm_set_dt(&self->pwm_spec, (uint32_t)period_ns, (uint32_t)pulse_ns);
    if (err != 0) {
        return err;
    }

    self->current_pattern = pattern;
    self->current_frequency_hz = freq;
    return 0;
}

static int adapter_buzzer_pwm_impl_stop(void *impl)
{
    adapter_buzzer_pwm_t *self = (adapter_buzzer_pwm_t *)impl;
    if (self == NULL || !self->is_ready) {
        return -EINVAL;
    }

    int err = pwm_set_pulse_dt(&self->pwm_spec, 0U);
    if (err != 0) {
        return err;
    }

    self->current_pattern = BUZZER_PATTERN_SILENT;
    self->current_frequency_hz = 0U;
    return 0;
}

static int adapter_buzzer_pwm_impl_get_active_pattern(void *impl, buzzer_pattern_t *out_pattern)
{
    adapter_buzzer_pwm_t *self = (adapter_buzzer_pwm_t *)impl;
    if (self == NULL || !self->is_ready || out_pattern == NULL) {
        return -EINVAL;
    }

    *out_pattern = self->current_pattern;
    return 0;
}

static const i_buzzer_api_t s_adapter_buzzer_pwm_api = {
    .play_tone = adapter_buzzer_pwm_impl_play_tone,
    .play_pattern = adapter_buzzer_pwm_impl_play_pattern,
    .stop = adapter_buzzer_pwm_impl_stop,
    .get_active_pattern = adapter_buzzer_pwm_impl_get_active_pattern,
};

int adapter_buzzer_pwm_init(adapter_buzzer_pwm_t *self, const struct pwm_dt_spec *pwm_spec)
{
    if (self == NULL || pwm_spec == NULL) {
        return -EINVAL;
    }

    self->pwm_spec = *pwm_spec;
    self->current_pattern = BUZZER_PATTERN_SILENT;
    self->current_frequency_hz = 0U;
    self->is_ready = false;

    if (!pwm_is_ready_dt(&self->pwm_spec)) {
        return -ENODEV;
    }

    int err = pwm_set_pulse_dt(&self->pwm_spec, 0U);
    if (err != 0) {
        return err;
    }

    self->is_ready = true;
    return 0;
}

int adapter_buzzer_pwm_port(adapter_buzzer_pwm_t *self, i_buzzer_t *out_port)
{
    if (self == NULL || !self->is_ready || out_port == NULL) {
        return -EINVAL;
    }

    out_port->api = &s_adapter_buzzer_pwm_api;
    out_port->impl = self;
    return 0;
}
