/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_hvac_unit.h
 * @brief In-memory Fake HVAC unit actuator port implementation for unit testing.
 */

#ifndef TESTS_FAKES_FAKE_HVAC_UNIT_H_
#define TESTS_FAKES_FAKE_HVAC_UNIT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/hvac_types.h"
#include "interfaces/i_hvac_unit.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal observable state of the fake HVAC unit adapter.
 */
typedef struct {
    /* Call counters */
    uint32_t set_tri_state_calls;
    uint32_t get_tri_state_calls;
    uint32_t set_bypass_alarm_calls;

    /* Error injection */
    int simulated_set_tri_state_error;
    int simulated_get_tri_state_error;

    /* Observable outputs */
    hvac_tri_state_t unit_tri_states[HVAC_MAX_UNITS_TOTAL];
    uint32_t         unit_set_count[HVAC_MAX_UNITS_TOTAL];
    bool             bypass_alarm_active;
} fake_hvac_unit_state_t;

/**
 * @brief Reset the fake HVAC unit state and clear all counters.
 */
void fake_hvac_unit_reset(void);

/**
 * @brief Obtain an i_hvac_unit_t port handle bound to the fake adapter.
 */
i_hvac_unit_t fake_hvac_unit_get_port(void);

/**
 * @brief Direct inspection of the internal fake state.
 */
const fake_hvac_unit_state_t *fake_hvac_unit_get_state(void);

/**
 * @brief Inspect the current tri-state of a specific unit.
 */
hvac_tri_state_t fake_hvac_unit_get_unit_state(uint8_t unit_id);

/**
 * @brief Pre-load the tri-state of a specific unit.
 */
void fake_hvac_unit_set_unit_state(uint8_t unit_id, hvac_tri_state_t state);

/**
 * @brief Inject simulated error return codes.
 */
void fake_hvac_unit_set_errors(int set_err, int get_err);

/**
 * @brief Inspect if the bypass alarm contact is currently active.
 */
bool fake_hvac_unit_is_bypass_alarm_active(void);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_FAKES_FAKE_HVAC_UNIT_H_ */
