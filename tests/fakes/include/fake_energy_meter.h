/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake implementation of i_energy_meter port.
 * Pure C99/C11 for fast host unit testing without hardware.
 */

#ifndef FAKE_ENERGY_METER_H_
#define FAKE_ENERGY_METER_H_

#include "interfaces/i_energy_meter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    energy_reading_t reading;
    uint32_t get_reading_calls;
    int get_reading_ret;
} fake_energy_meter_state_t;

/**
 * @brief Reset fake state, zeroing counters and reading structure.
 */
void fake_energy_meter_reset(void);

/**
 * @brief Get the port handle bound to the fake adapter.
 */
i_energy_meter_t fake_energy_meter_get_port(void);

/**
 * @brief Set the energy reading to be returned by get_reading.
 */
void fake_energy_meter_set_reading(const energy_reading_t *reading);

/**
 * @brief Override return code for get_reading.
 */
void fake_energy_meter_set_return_code(int ret);

/**
 * @brief Inspect the internal state of the fake.
 */
const fake_energy_meter_state_t *fake_energy_meter_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_ENERGY_METER_H_ */
