/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract port for Sensor Matrix Ingestion Subsystem.
 * Follows doc/STRUCTURE.md rule §4.1.D:
 * 1. V-table
 * 2. Handle
 * 3. Static inline validated wrappers
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef APP_INTERFACES_I_SENSOR_MATRIX_H_
#define APP_INTERFACES_I_SENSOR_MATRIX_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "common/sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1. V-table: implementation detail, never called directly from the application */
typedef struct {
    int (*get_frame)(void *impl, thermal_matrix_frame_t *out_frame);
    int (*get_raw_readings)(void *impl, raw_sensor_readings_t *out_raw);
    int (*set_channel_config)(void *impl, physical_channel_id_t ch, const sensor_channel_cfg_t *cfg);
    int (*get_channel_config)(void *impl, physical_channel_id_t ch, sensor_channel_cfg_t *out_cfg);
    int (*update)(void *impl);
} i_sensor_matrix_api_t;

/* 2. Handle injected into the port consumer */
typedef struct {
    const i_sensor_matrix_api_t *api;
    void *impl;
} i_sensor_matrix_t;

/**
 * @brief Check whether port handle is properly bound to an adapter implementation.
 */
static inline bool i_sensor_matrix_is_bound(const i_sensor_matrix_t *matrix)
{
    return matrix != NULL && matrix->api != NULL && matrix->impl != NULL;
}

/* 3. Validated wrappers: the only allowed path to the v-table */

/**
 * @brief Retrieve the latest semantic thermal matrix frame.
 *
 * @param[in]  matrix    Bound port handle.
 * @param[out] out_frame Output destination frame.
 * @return 0 on success, -EINVAL if unbound or null pointer, or adapter error.
 */
static inline int i_sensor_matrix_get_frame(const i_sensor_matrix_t *matrix,
                                            thermal_matrix_frame_t *out_frame)
{
    if (!i_sensor_matrix_is_bound(matrix) || out_frame == NULL) {
        return -EINVAL;
    }
    if (matrix->api->get_frame == NULL) {
        return -ENOSYS;
    }
    return matrix->api->get_frame(matrix->impl, out_frame);
}

/**
 * @brief Retrieve the latest raw physical readings bank.
 *
 * @param[in]  matrix  Bound port handle.
 * @param[out] out_raw Output destination bank.
 * @return 0 on success, -EINVAL if unbound or null pointer, or adapter error.
 */
static inline int i_sensor_matrix_get_raw_readings(const i_sensor_matrix_t *matrix,
                                                   raw_sensor_readings_t *out_raw)
{
    if (!i_sensor_matrix_is_bound(matrix) || out_raw == NULL) {
        return -EINVAL;
    }
    if (matrix->api->get_raw_readings == NULL) {
        return -ENOSYS;
    }
    return matrix->api->get_raw_readings(matrix->impl, out_raw);
}

/**
 * @brief Configure or hot-swap a physical channel mapping descriptor.
 *
 * @param[in] matrix Bound port handle.
 * @param[in] ch     Physical channel identifier.
 * @param[in] cfg    Channel configuration descriptor.
 * @return 0 on success, -EINVAL if invalid channel or arguments, or adapter error.
 */
static inline int i_sensor_matrix_set_channel_config(const i_sensor_matrix_t *matrix,
                                                     physical_channel_id_t ch,
                                                     const sensor_channel_cfg_t *cfg)
{
    if (!i_sensor_matrix_is_bound(matrix) || !physical_channel_is_valid(ch) || cfg == NULL) {
        return -EINVAL;
    }
    if (matrix->api->set_channel_config == NULL) {
        return -ENOSYS;
    }
    return matrix->api->set_channel_config(matrix->impl, ch, cfg);
}

/**
 * @brief Read the active configuration descriptor of a physical channel.
 *
 * @param[in]  matrix  Bound port handle.
 * @param[in]  ch      Physical channel identifier.
 * @param[out] out_cfg Channel configuration destination buffer.
 * @return 0 on success, -EINVAL if invalid channel or arguments, or adapter error.
 */
static inline int i_sensor_matrix_get_channel_config(const i_sensor_matrix_t *matrix,
                                                     physical_channel_id_t ch,
                                                     sensor_channel_cfg_t *out_cfg)
{
    if (!i_sensor_matrix_is_bound(matrix) || !physical_channel_is_valid(ch) || out_cfg == NULL) {
        return -EINVAL;
    }
    if (matrix->api->get_channel_config == NULL) {
        return -ENOSYS;
    }
    return matrix->api->get_channel_config(matrix->impl, ch, out_cfg);
}

/**
 * @brief Trigger sensor matrix mapping update from latest acquired physical banks.
 *
 * @param[in] matrix Bound port handle.
 * @return 0 on success, -EINVAL if unbound, or adapter error.
 */
static inline int i_sensor_matrix_update(const i_sensor_matrix_t *matrix)
{
    if (!i_sensor_matrix_is_bound(matrix)) {
        return -EINVAL;
    }
    if (matrix->api->update == NULL) {
        return -ENOSYS;
    }
    return matrix->api->update(matrix->impl);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_SENSOR_MATRIX_H_ */
