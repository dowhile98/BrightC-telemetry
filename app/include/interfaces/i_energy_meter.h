/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract Port Contract for Electrical Energy Meter Devices.
 * Follows doc/STRUCTURE.md rule §4.1.D:
 * 1. V-table i_energy_meter_api_t
 * 2. Handle i_energy_meter_t
 * 3. Static inline validated wrapper i_energy_meter_get_reading()
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef APP_INTERFACES_I_ENERGY_METER_H_
#define APP_INTERFACES_I_ENERGY_METER_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1. V-table: implementation detail, never called directly from the application */
typedef struct {
    int (*get_reading)(void *impl, energy_reading_t *out_reading);
} i_energy_meter_api_t;

/* 2. Handle injected into the port consumer */
typedef struct {
    const i_energy_meter_api_t *api;
    void *impl;
} i_energy_meter_t;

/**
 * @brief Check whether port handle is properly bound to an adapter implementation.
 *
 * @param[in] meter Pointer to energy meter handle.
 * @return true if handle is valid and fully bound.
 */
static inline bool i_energy_meter_is_bound(const i_energy_meter_t *meter)
{
    return meter != NULL && meter->api != NULL && meter->impl != NULL;
}

/* 3. Validated wrappers: the only allowed path to the v-table */

/**
 * @brief Retrieve the latest energy meter reading.
 *
 * @param[in]  meter       Bound energy meter port handle.
 * @param[out] out_reading Destination buffer for energy reading snapshot.
 * @return 0 on success, -EINVAL on unbound handle or NULL pointer, or adapter error.
 */
static inline int i_energy_meter_get_reading(const i_energy_meter_t *meter,
                                             energy_reading_t *out_reading)
{
    if (!i_energy_meter_is_bound(meter) || out_reading == NULL) {
        return -EINVAL;
    }
    if (meter->api->get_reading == NULL) {
        return -ENOSYS;
    }
    return meter->api->get_reading(meter->impl, out_reading);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_ENERGY_METER_H_ */
