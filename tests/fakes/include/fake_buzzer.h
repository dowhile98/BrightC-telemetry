/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_buzzer.h
 * @brief In-memory Fake Buzzer port implementation for unit testing.
 */

#ifndef TESTS_FAKES_FAKE_BUZZER_H_
#define TESTS_FAKES_FAKE_BUZZER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/indicator_types.h"
#include "interfaces/i_buzzer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Call counters */
    uint32_t play_tone_calls;
    uint32_t play_pattern_calls;
    uint32_t stop_calls;
    uint32_t get_active_pattern_calls;

    /* Error injection */
    int simulated_play_tone_error;
    int simulated_play_pattern_error;
    int simulated_stop_error;
    int simulated_get_active_pattern_error;

    /* Observable state */
    buzzer_pattern_t active_pattern;
    uint32_t last_frequency_hz;
    uint32_t last_duration_ms;
    bool is_sounding;
} fake_buzzer_state_t;

/**
 * @brief Reset fake buzzer state and clear all counters.
 */
void fake_buzzer_reset(void);

/**
 * @brief Obtain an i_buzzer_t port handle bound to the fake buzzer adapter.
 */
i_buzzer_t fake_buzzer_get_port(void);

/**
 * @brief Direct inspection of the internal fake buzzer state.
 */
const fake_buzzer_state_t *fake_buzzer_get_state(void);

/**
 * @brief Inject simulated error return codes.
 */
void fake_buzzer_set_errors(int play_tone_err,
                            int play_pattern_err,
                            int stop_err,
                            int get_pattern_err);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_FAKES_FAKE_BUZZER_H_ */
