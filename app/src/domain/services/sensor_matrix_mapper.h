/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor matrix domain ingestion, calibration and mapping engine.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef APP_DOMAIN_SERVICES_SENSOR_MATRIX_MAPPER_H_
#define APP_DOMAIN_SERVICES_SENSOR_MATRIX_MAPPER_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "common/sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a default pilot profile channel configuration table.
 *
 * Maps CWT CH1-16 to 6 pairs + 2 room + 2 outdoor probes,
 * disables internal RTD 1-5 with 0 offset/gain,
 * configures J9 CH1 for return humidity.
 *
 * @param[out] configs  Destination array of at least @ref SENSOR_PHYS_CH_MAX entries.
 * @param[in]  max_entries Available capacity in @p configs.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int sensor_matrix_init_default_pilot_config(sensor_channel_cfg_t *configs, size_t max_entries);

/**
 * @brief Map raw peripheral readings into a semantic thermal matrix frame.
 *
 * Consumes raw physical channels (external Modbus CWT, internal RTD/ADC, J9 current loop),
 * evaluates validity and open probe status, converts internal RTD ADC counts using
 * Callendar-Van Dusen equation, applies 2-point calibration offset and gain, routes channels
 * to their configured semantic role, and computes pair consensus masks.
 *
 * @param[in]  raw         Raw sensor readings bank.
 * @param[in]  configs     Channel configuration table.
 * @param[in]  cfg_count   Number of configuration entries in @p configs.
 * @param[out] out_frame   Semantic thermal matrix frame destination.
 *
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int sensor_matrix_map(const raw_sensor_readings_t *raw,
                      const sensor_channel_cfg_t *configs,
                      size_t cfg_count,
                      thermal_matrix_frame_t *out_frame);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_SENSOR_MATRIX_MAPPER_H_ */
