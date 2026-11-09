/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_hvac_unit.h
 * @brief HVAC Unit Actuator Port Interface.
 *
 * Abstract hardware contract for driving and reading individual AC unit
 * tri-state actuators (relay_aa, relay_sm, relay_g) following Rule §4.1.D.
 */

#ifndef APP_INTERFACES_I_HVAC_UNIT_H_
#define APP_INTERFACES_I_HVAC_UNIT_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/hvac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * V-TABLE (implementation detail)
 *============================================================================*/

/**
 * @brief Operations exposed by an HVAC unit actuator adapter.
 *
 * Application and domain code reaches actuators exclusively through the
 * `i_hvac_unit_*()` wrappers. Direct access to the v-table is strictly forbidden.
 */
typedef struct {
    /**
     * @brief Set the tri-state output of a specified AC unit.
     *
     * @param[in] impl     Adapter implementation context.
     * @param[in] unit_id  Zero-indexed unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
     * @param[in] tri      Target tri-state level.
     * @return 0 on success, negative errno on failure.
     */
    int (*set_tri_state)(void *impl, uint8_t unit_id, hvac_tri_state_t tri);

    /**
     * @brief Read the current tri-state output of a specified AC unit.
     *
     * @param[in]  impl     Adapter implementation context.
     * @param[in]  unit_id  Zero-indexed unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
     * @param[out] out_tri  Pointer to receive the observed tri-state level.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_tri_state)(void *impl, uint8_t unit_id, hvac_tri_state_t *out_tri);

    /**
     * @brief Set the bypass alarm contact state (Alarm 1).
     *
     * @param[in] impl    Adapter implementation context.
     * @param[in] active  true to energize Alarm 1 relay, false to de-energize.
     * @return 0 on success, negative errno on failure.
     */
    int (*set_bypass_alarm)(void *impl, bool active);
} i_hvac_unit_api_t;

/**
 * @brief Polymorphic HVAC unit handle: v-table plus opaque implementation pointer.
 */
typedef struct {
    const i_hvac_unit_api_t *api;  /**< Operation table (never NULL when bound) */
    void *impl;                    /**< Concrete adapter instance */
} i_hvac_unit_t;

/*============================================================================*
 * VALIDATED INLINE WRAPPERS
 *============================================================================*/

/**
 * @brief Check that an HVAC unit handle is bound to an implementation.
 *
 * @param[in] port Handle to test (may be NULL).
 * @return true when both the v-table, implementation, and mandatory methods are non-NULL.
 */
static inline bool i_hvac_unit_is_bound(const i_hvac_unit_t *port)
{
    return (port != NULL) &&
           (port->api != NULL) &&
           (port->impl != NULL) &&
           (port->api->set_tri_state != NULL) &&
           (port->api->get_tri_state != NULL);
}

/**
 * @brief Set the tri-state output level for an AC unit.
 *
 * @param[in] port     HVAC unit port handle.
 * @param[in] unit_id  Zero-indexed unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
 * @param[in] tri      Target tri-state command level.
 * @return 0 on success, -EINVAL on invalid handle/parameters, or adapter errno.
 */
static inline int i_hvac_unit_set_tri_state(const i_hvac_unit_t *port, uint8_t unit_id, hvac_tri_state_t tri)
{
    if (!i_hvac_unit_is_bound(port)) {
        return -EINVAL;
    }
    if (unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }
    if (!hvac_tri_state_is_valid(tri)) {
        return -EINVAL;
    }
    return port->api->set_tri_state(port->impl, unit_id, tri);
}

/**
 * @brief Read the current tri-state output level of an AC unit.
 *
 * @param[in]  port     HVAC unit port handle.
 * @param[in]  unit_id  Zero-indexed unit ID (0 .. HVAC_MAX_UNITS_TOTAL - 1).
 * @param[out] out_tri  Pointer to store the current tri-state level.
 * @return 0 on success, -EINVAL on invalid handle/parameters, or adapter errno.
 */
static inline int i_hvac_unit_get_tri_state(const i_hvac_unit_t *port, uint8_t unit_id, hvac_tri_state_t *out_tri)
{
    if (!i_hvac_unit_is_bound(port) || out_tri == NULL) {
        return -EINVAL;
    }
    if (unit_id >= HVAC_MAX_UNITS_TOTAL) {
        return -EINVAL;
    }
    return port->api->get_tri_state(port->impl, unit_id, out_tri);
}

/**
 * @brief Set the bypass alarm output state.
 *
 * @param[in] port    HVAC unit port handle.
 * @param[in] active  true to energize Alarm 1 relay, false to de-energize.
 * @return 0 on success, -EINVAL on invalid handle/parameters, or adapter errno.
 */
static inline int i_hvac_unit_set_bypass_alarm(const i_hvac_unit_t *port, bool active)
{
    if (!i_hvac_unit_is_bound(port)) {
        return -EINVAL;
    }
    if (port->api->set_bypass_alarm == NULL) {
        return -ENOTSUP;
    }
    return port->api->set_bypass_alarm(port->impl, active);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_HVAC_UNIT_H_ */
