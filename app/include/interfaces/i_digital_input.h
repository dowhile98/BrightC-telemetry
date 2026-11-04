/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_digital_input.h
 * @brief Discrete Digital Input port interface following Ports & Adapters (§4.1.D).
 *
 * Provides a clean hardware-agnostic abstraction for querying contact states,
 * raw pin levels, diagnostic status, and active channel bitmasks.
 * Pure C99/C11: zero Zephyr OS headers, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_DIGITAL_INPUT_H_
#define APP_INTERFACES_I_DIGITAL_INPUT_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/digital_input_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (Implementation Detail)
 *============================================================================*/

/**
 * @brief Operations exposed by a digital input subsystem implementation.
 */
typedef struct {
    /**
     * @brief Configure logical input channels.
     *
     * @param[in] impl     Implementation context.
     * @param[in] configs  Array of channel configurations.
     * @param[in] count    Number of configurations in array (1..DI_CHANNEL_MAX).
     * @return 0 on success, negative errno on failure.
     */
    int (*init)(void *impl, const di_channel_config_t *configs, size_t count);

    /**
     * @brief Execute a periodic scan cycle across all configured channels.
     *
     * @param[in] impl        Implementation context.
     * @param[in] elapsed_ms  Time elapsed since previous scan cycle.
     * @return 0 on success, negative errno on failure.
     */
    int (*scan)(void *impl, uint32_t elapsed_ms);

    /**
     * @brief Query debounced logical active state for a semantic role.
     *
     * @param[in]  impl           Implementation context.
     * @param[in]  role           Semantic role to query.
     * @param[out] out_is_active  Destination receiving true if active/asserted, false if inactive.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_state)(void *impl, di_role_t role, bool *out_is_active);

    /**
     * @brief Query most recent raw physical pin level.
     *
     * @param[in]  impl           Implementation context.
     * @param[in]  role           Semantic role to query.
     * @param[out] out_raw_level  Destination receiving physical pin logic level.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_raw_level)(void *impl, di_role_t role, bool *out_raw_level);

    /**
     * @brief Read comprehensive runtime diagnostic metrics for a channel.
     *
     * @param[in]  impl        Implementation context.
     * @param[in]  role        Semantic role to query.
     * @param[out] out_status  Destination receiving status snapshot.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_status)(void *impl, di_role_t role, di_channel_status_t *out_status);

    /**
     * @brief Read 64-bit mask of all currently active channels indexed by role.
     *
     * Bit (1ULL << role) is 1 when role is active, 0 when inactive.
     *
     * @param[in]  impl      Implementation context.
     * @param[out] out_mask  Destination receiving 64-bit active mask.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_active_mask)(void *impl, uint64_t *out_mask);
} i_digital_input_api_t;

/*============================================================================*
 * 2. HANDLE
 *============================================================================*/

/**
 * @brief Polymorphic digital input port handle: v-table plus opaque context pointer.
 */
typedef struct {
    const i_digital_input_api_t *api; /**< Operation table (never NULL when bound) */
    void *impl;                       /**< Concrete adapter/aggregator instance */
} i_digital_input_t;

/*============================================================================*
 * 3. VALIDATED INLINE WRAPPERS
 *============================================================================*/

/**
 * @brief Verify port handle is properly bound to an implementation.
 *
 * @param[in] port Port handle to validate (may be NULL).
 * @return true if bound and all function pointers are valid; false otherwise.
 */
static inline bool i_digital_input_is_bound(const i_digital_input_t *port)
{
    return (port != NULL) &&
           (port->api != NULL) &&
           (port->impl != NULL) &&
           (port->api->init != NULL) &&
           (port->api->scan != NULL) &&
           (port->api->get_state != NULL) &&
           (port->api->get_raw_level != NULL) &&
           (port->api->get_status != NULL) &&
           (port->api->get_active_mask != NULL);
}

/**
 * @brief Initialize digital input channels via port handle.
 */
static inline int i_digital_input_init(const i_digital_input_t *port,
                                      const di_channel_config_t *configs,
                                      size_t count)
{
    if (!i_digital_input_is_bound(port) || configs == NULL || count == 0U || count > DI_CHANNEL_MAX) {
        return -EINVAL;
    }
    return port->api->init(port->impl, configs, count);
}

/**
 * @brief Execute a scan cycle via port handle.
 */
static inline int i_digital_input_scan(const i_digital_input_t *port, uint32_t elapsed_ms)
{
    if (!i_digital_input_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->scan(port->impl, elapsed_ms);
}

/**
 * @brief Read debounced logical active state for a semantic role via port handle.
 */
static inline int i_digital_input_get_state(const i_digital_input_t *port,
                                           di_role_t role,
                                           bool *out_is_active)
{
    if (!i_digital_input_is_bound(port) || !di_role_is_valid(role) || out_is_active == NULL) {
        return -EINVAL;
    }
    return port->api->get_state(port->impl, role, out_is_active);
}

/**
 * @brief Read raw physical pin level for a semantic role via port handle.
 */
static inline int i_digital_input_get_raw_level(const i_digital_input_t *port,
                                               di_role_t role,
                                               bool *out_raw_level)
{
    if (!i_digital_input_is_bound(port) || !di_role_is_valid(role) || out_raw_level == NULL) {
        return -EINVAL;
    }
    return port->api->get_raw_level(port->impl, role, out_raw_level);
}

/**
 * @brief Read runtime status and diagnostics for a channel via port handle.
 */
static inline int i_digital_input_get_status(const i_digital_input_t *port,
                                            di_role_t role,
                                            di_channel_status_t *out_status)
{
    if (!i_digital_input_is_bound(port) || !di_role_is_valid(role) || out_status == NULL) {
        return -EINVAL;
    }
    return port->api->get_status(port->impl, role, out_status);
}

/**
 * @brief Read 64-bit active channel bitmask via port handle.
 */
static inline int i_digital_input_get_active_mask(const i_digital_input_t *port,
                                                 uint64_t *out_mask)
{
    if (!i_digital_input_is_bound(port) || out_mask == NULL) {
        return -EINVAL;
    }
    return port->api->get_active_mask(port->impl, out_mask);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_DIGITAL_INPUT_H_ */
