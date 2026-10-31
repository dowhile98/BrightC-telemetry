/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake implementation of i_sensor_matrix port.
 * Pure C99/C11 for fast host unit testing without hardware.
 */

#ifndef FAKE_SENSOR_MATRIX_H_
#define FAKE_SENSOR_MATRIX_H_

#include "interfaces/i_sensor_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    thermal_matrix_frame_t frame;
    raw_sensor_readings_t raw;
    sensor_channel_cfg_t configs[PHYS_CH_MAX];
    bool channel_configured[PHYS_CH_MAX];

    /* Call counters */
    uint32_t get_frame_calls;
    uint32_t get_raw_calls;
    uint32_t set_cfg_calls;
    uint32_t get_cfg_calls;
    uint32_t update_calls;

    /* Override return codes */
    int get_frame_ret;
    int get_raw_ret;
    int set_cfg_ret;
    int get_cfg_ret;
    int update_ret;

    /* Options */
    bool auto_map_on_update;
} fake_sensor_matrix_state_t;

/**
 * @brief Reset fake state, zeroing counters, frame and raw data.
 */
void fake_sensor_matrix_reset(void);

/**
 * @brief Get the port handle bound to the fake adapter.
 */
i_sensor_matrix_t fake_sensor_matrix_get_port(void);

/**
 * @brief Set the thermal matrix frame to be returned by get_frame.
 */
void fake_sensor_matrix_set_frame(const thermal_matrix_frame_t *frame);

/**
 * @brief Set the raw sensor readings to be returned by get_raw_readings or used on update.
 */
void fake_sensor_matrix_set_raw_readings(const raw_sensor_readings_t *raw);

/**
 * @brief Configure whether update() automatically executes domain sensor_matrix_map.
 */
void fake_sensor_matrix_set_auto_map(bool enable);

/**
 * @brief Inspect the internal state of the fake.
 */
const fake_sensor_matrix_state_t *fake_sensor_matrix_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_SENSOR_MATRIX_H_ */
