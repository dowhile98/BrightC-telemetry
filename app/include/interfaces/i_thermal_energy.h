/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_thermal_energy.h
 * @brief Abstract Port Contract for Thermal and Energy Monitoring Subsystem.
 *
 * Follows Rule §4.1.D Port Contract & Fat-Pointer Encapsulation (Invariant 1):
 * 1. V-table i_thermal_energy_api_t
 * 2. Fat-pointer handle i_thermal_energy_t
 * 3. Safety validation predicate i_thermal_energy_is_bound()
 * 4. Static inline defensive wrappers
 *
 * Pure ISO C99/C11 domain declarations:
 * - Zero Zephyr RTOS dependencies
 * - Zero vendor HAL or hardware register dependencies
 * - Zero dynamic memory allocation
 */

#ifndef APP_INTERFACES_I_THERMAL_ENERGY_H_
#define APP_INTERFACES_I_THERMAL_ENERGY_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/config_types.h"
#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* 1. V-Table Contract                                                       */
/* ========================================================================= */

/**
 * @brief Virtual method table for Thermal and Energy queries and calibration.
 *
 * Implementation detail: MUST NOT be dereferenced directly by port consumers.
 */
typedef struct {
    int (*get_latest_result)(void *impl, thermal_energy_result_t *out_result);
    int (*get_latest_capture)(void *impl, thermal_energy_capture_t *out_capture);
    int (*get_hourly_history)(void *impl, uint16_t hours_ago, thermal_hourly_record_t *out_record);
    int (*get_baseline)(void *impl, config_thermal_baseline_t *out_baseline);
    int (*trigger_baseline_recalibration)(void *impl);
} i_thermal_energy_api_t;

/* ========================================================================= */
/* 2. Fat-Pointer Handle                                                     */
/* ========================================================================= */

/**
 * @brief Fat-pointer handle combining v-table pointer and instance context pointer.
 */
typedef struct {
    const i_thermal_energy_api_t *api;
    void                         *impl;
} i_thermal_energy_t;

/* ========================================================================= */
/* 3. Safety Predicate                                                       */
/* ========================================================================= */

/**
 * @brief Validate whether the thermal energy port handle is bound.
 *
 * @param[in] port Pointer to port handle.
 * @return true if handle is valid, api table is populated, and impl is non-NULL.
 */
static inline bool i_thermal_energy_is_bound(const i_thermal_energy_t *port)
{
    return (port != NULL &&
            port->api != NULL &&
            port->impl != NULL &&
            port->api->get_latest_result != NULL &&
            port->api->get_latest_capture != NULL &&
            port->api->get_hourly_history != NULL &&
            port->api->get_baseline != NULL &&
            port->api->trigger_baseline_recalibration != NULL);
}

/* ========================================================================= */
/* 4. Defensive Inline Wrappers                                              */
/* ========================================================================= */

/**
 * @brief Retrieve the most recent thermodynamic calculation result snapshot.
 *
 * @param[in]  port       Bound thermal energy port handle.
 * @param[out] out_result Destination buffer for thermodynamic result snapshot.
 * @return 0 on success, -EINVAL on invalid handle or NULL buffer, or adapter error.
 */
static inline int i_thermal_energy_get_latest_result(const i_thermal_energy_t *port,
                                                     thermal_energy_result_t *out_result)
{
    if (!i_thermal_energy_is_bound(port) || out_result == NULL) {
        return -EINVAL;
    }
    return port->api->get_latest_result(port->impl, out_result);
}

/**
 * @brief Retrieve the most recent raw multi-source telemetry capture snapshot.
 *
 * @param[in]  port        Bound thermal energy port handle.
 * @param[out] out_capture Destination buffer for telemetry capture snapshot.
 * @return 0 on success, -EINVAL on invalid handle or NULL buffer, or adapter error.
 */
static inline int i_thermal_energy_get_latest_capture(const i_thermal_energy_t *port,
                                                      thermal_energy_capture_t *out_capture)
{
    if (!i_thermal_energy_is_bound(port) || out_capture == NULL) {
        return -EINVAL;
    }
    return port->api->get_latest_capture(port->impl, out_capture);
}

/**
 * @brief Retrieve a compressed hourly history log record from the 720h ring buffer.
 *
 * @param[in]  port       Bound thermal energy port handle.
 * @param[in]  hours_ago  History offset (0 = current/most recent closed hour, up to 719).
 * @param[out] out_record Destination buffer for hourly log record.
 * @return 0 on success, -EINVAL on invalid handle or NULL buffer, -ENOENT if index out of range, or adapter error.
 */
static inline int i_thermal_energy_get_hourly_history(const i_thermal_energy_t *port,
                                                      uint16_t hours_ago,
                                                      thermal_hourly_record_t *out_record)
{
    if (!i_thermal_energy_is_bound(port) || out_record == NULL) {
        return -EINVAL;
    }
    return port->api->get_hourly_history(port->impl, hours_ago, out_record);
}

/**
 * @brief Retrieve the active steady-state machine-learned baseline parameters.
 *
 * @param[in]  port         Bound thermal energy port handle.
 * @param[out] out_baseline Destination buffer for baseline configuration.
 * @return 0 on success, -EINVAL on invalid handle or NULL buffer, or adapter error.
 */
static inline int i_thermal_energy_get_baseline(const i_thermal_energy_t *port,
                                                config_thermal_baseline_t *out_baseline)
{
    if (!i_thermal_energy_is_bound(port) || out_baseline == NULL) {
        return -EINVAL;
    }
    return port->api->get_baseline(port->impl, out_baseline);
}

/**
 * @brief Trigger recalibration and re-training of the steady-state baseline.
 *
 * @param[in] port Bound thermal energy port handle.
 * @return 0 on success, -EINVAL on invalid handle, or adapter error.
 */
static inline int i_thermal_energy_trigger_baseline_recalibration(const i_thermal_energy_t *port)
{
    if (!i_thermal_energy_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->trigger_baseline_recalibration(port->impl);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_THERMAL_ENERGY_H_ */
