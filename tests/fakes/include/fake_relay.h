/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_relay.h
 * @brief In-memory Fake discrete relay port implementation for unit testing.
 */

#ifndef TESTS_FAKES_FAKE_RELAY_H_
#define TESTS_FAKES_FAKE_RELAY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "interfaces/i_relay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    relay_state_t states[RELAY_CHANNEL_MAX];
    uint32_t      set_state_calls;
    uint32_t      get_state_calls;
    int           simulated_error;
} fake_relay_state_t;

/**
 * @brief Reset the fake relay state and clear all channel states.
 */
void fake_relay_reset(void);

/**
 * @brief Obtain an i_relay_t port handle bound to the fake adapter.
 */
i_relay_t fake_relay_get_port(void);

/**
 * @brief Direct inspection of the internal fake state.
 */
const fake_relay_state_t *fake_relay_get_state(void);

/**
 * @brief Inspect the current state of a specific channel.
 */
relay_state_t fake_relay_get_channel_state(relay_channel_t ch);

/**
 * @brief Inject simulated error on set_state.
 */
void fake_relay_set_simulated_error(int err);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_FAKES_FAKE_RELAY_H_ */
