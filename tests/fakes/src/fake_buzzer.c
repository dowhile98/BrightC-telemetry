/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_buzzer.c
 * @brief In-memory Fake Buzzer port implementation.
 */

#include "fake_buzzer.h"

#include <errno.h>
#include <string.h>

static fake_buzzer_state_t s_fake_buzzer;

static int fake_buzzer_impl_play_tone(void *impl, uint32_t frequency_hz, uint32_t duration_ms)
{
    fake_buzzer_state_t *s = (fake_buzzer_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->play_tone_calls++;
    if (s->simulated_play_tone_error != 0) {
        return s->simulated_play_tone_error;
    }
    s->last_frequency_hz = frequency_hz;
    s->last_duration_ms = duration_ms;
    s->is_sounding = (frequency_hz > 0U);
    if (frequency_hz == 0U) {
        s->active_pattern = BUZZER_PATTERN_SILENT;
    }
    return 0;
}

static int fake_buzzer_impl_play_pattern(void *impl, buzzer_pattern_t pattern, uint32_t frequency_hz)
{
    fake_buzzer_state_t *s = (fake_buzzer_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->play_pattern_calls++;
    if (s->simulated_play_pattern_error != 0) {
        return s->simulated_play_pattern_error;
    }
    s->active_pattern = pattern;
    s->last_frequency_hz = frequency_hz;
    s->is_sounding = (pattern != BUZZER_PATTERN_SILENT);
    return 0;
}

static int fake_buzzer_impl_stop(void *impl)
{
    fake_buzzer_state_t *s = (fake_buzzer_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->stop_calls++;
    if (s->simulated_stop_error != 0) {
        return s->simulated_stop_error;
    }
    s->active_pattern = BUZZER_PATTERN_SILENT;
    s->is_sounding = false;
    return 0;
}

static int fake_buzzer_impl_get_active_pattern(void *impl, buzzer_pattern_t *out_pattern)
{
    fake_buzzer_state_t *s = (fake_buzzer_state_t *)impl;
    if (s == NULL || out_pattern == NULL) {
        return -EINVAL;
    }
    s->get_active_pattern_calls++;
    if (s->simulated_get_active_pattern_error != 0) {
        return s->simulated_get_active_pattern_error;
    }
    *out_pattern = s->active_pattern;
    return 0;
}

static const i_buzzer_api_t s_fake_buzzer_api = {
    .play_tone = fake_buzzer_impl_play_tone,
    .play_pattern = fake_buzzer_impl_play_pattern,
    .stop = fake_buzzer_impl_stop,
    .get_active_pattern = fake_buzzer_impl_get_active_pattern,
};

void fake_buzzer_reset(void)
{
    memset(&s_fake_buzzer, 0, sizeof(s_fake_buzzer));
    s_fake_buzzer.active_pattern = BUZZER_PATTERN_SILENT;
}

i_buzzer_t fake_buzzer_get_port(void)
{
    i_buzzer_t port = {
        .api = &s_fake_buzzer_api,
        .impl = &s_fake_buzzer,
    };
    return port;
}

const fake_buzzer_state_t *fake_buzzer_get_state(void)
{
    return &s_fake_buzzer;
}

void fake_buzzer_set_errors(int play_tone_err,
                            int play_pattern_err,
                            int stop_err,
                            int get_pattern_err)
{
    s_fake_buzzer.simulated_play_tone_error = play_tone_err;
    s_fake_buzzer.simulated_play_pattern_error = play_pattern_err;
    s_fake_buzzer.simulated_stop_error = stop_err;
    s_fake_buzzer.simulated_get_active_pattern_error = get_pattern_err;
}
