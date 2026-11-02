/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_led.h
 * @brief In-memory Fake LED port implementation for unit testing.
 */

#ifndef TESTS_FAKES_FAKE_LED_H_
#define TESTS_FAKES_FAKE_LED_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/indicator_types.h"
#include "interfaces/i_led.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Call counters */
    uint32_t set_state_calls;
    uint32_t get_state_calls;
    uint32_t set_blink_rate_calls;

    /* Error injection */
    int simulated_set_state_error;
    int simulated_get_state_error;
    int simulated_set_blink_rate_error;

    /* Observable state */
    led_state_t current_state;
    uint32_t blink_period_ms;
    uint32_t blink_duty_cycle_pct;
} fake_led_state_t;

/**
 * @brief Reset the fake LED state and clear all counters.
 */
void fake_led_reset(void);

/**
 * @brief Obtain an i_led_t port handle bound to the fake LED adapter.
 */
i_led_t fake_led_get_port(void);

/**
 * @brief Direct inspection of the internal fake LED state.
 */
const fake_led_state_t *fake_led_get_state(void);

/**
 * @brief Inject simulated error return codes.
 */
void fake_led_set_errors(int set_state_err, int get_state_err, int set_blink_rate_err);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_FAKES_FAKE_LED_H_ */
