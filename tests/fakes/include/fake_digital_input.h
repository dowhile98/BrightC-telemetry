/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_digital_input.h
 * @brief In-memory Fake Digital Input port implementation for unit testing.
 */

#ifndef TESTS_FAKES_FAKE_DIGITAL_INPUT_H_
#define TESTS_FAKES_FAKE_DIGITAL_INPUT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/digital_input_types.h"
#include "interfaces/i_digital_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Call counters */
    uint32_t init_calls;
    uint32_t scan_calls;
    uint32_t get_state_calls;
    uint32_t get_raw_level_calls;
    uint32_t get_status_calls;
    uint32_t get_active_mask_calls;

    /* Error injection */
    int simulated_init_error;
    int simulated_scan_error;
    int simulated_get_state_error;
    int simulated_get_raw_level_error;
    int simulated_get_status_error;
    int simulated_get_active_mask_error;

    /* Channel state simulation */
    size_t              channel_count;
    di_channel_config_t configs[DI_CHANNEL_MAX];
    di_channel_status_t statuses[DI_CHANNEL_MAX];
    bool                raw_levels[DI_CHANNEL_MAX];
    debounce_filter_t   filters[DI_CHANNEL_MAX];
    uint32_t            active_accum_ms[DI_CHANNEL_MAX];
    uint64_t            current_time_ms;
} fake_digital_input_state_t;

/**
 * @brief Reset the fake digital input state and clear all counters.
 */
void fake_digital_input_reset(void);

/**
 * @brief Obtain an i_digital_input_t port handle bound to the fake adapter.
 */
i_digital_input_t fake_digital_input_port(void);

/**
 * @brief Read internal fake digital input state.
 */
const fake_digital_input_state_t *fake_digital_input_get_state(void);

/**
 * @brief Inject simulated error codes into fake operations.
 */
void fake_digital_input_set_errors(int init_err, int scan_err, int get_state_err);

/**
 * @brief Set raw physical pin level for a role in the fake.
 */
int fake_digital_input_set_raw_level(di_role_t role, bool raw_level);

/**
 * @brief Force logical debounced active state for a role directly in the fake.
 */
int fake_digital_input_force_state(di_role_t role, bool is_active);

/**
 * @brief Step simulated time and run debounce scan in the fake.
 */
int fake_digital_input_step_ms(uint32_t step_ms);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_FAKES_FAKE_DIGITAL_INPUT_H_ */
