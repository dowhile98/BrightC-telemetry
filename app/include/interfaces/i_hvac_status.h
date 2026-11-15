/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_hvac_status.h
 * @brief Abstract Port Contract for querying HVAC Subsystem status snapshots.
 *
 * Adheres strictly to Clean Architecture (Rule §4.1.D Port Contract & Fat-Pointer Encapsulation).
 * External services (such as Telemetry Sampler, HMI, Modbus) query HVAC runtime
 * status and control temperatures exclusively through this fat-pointer handle.
 */

#ifndef APP_INTERFACES_I_HVAC_STATUS_H_
#define APP_INTERFACES_I_HVAC_STATUS_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/hvac_types.h"
#include "application/zbus/hvac_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct i_hvac_status_api i_hvac_status_api_t;

/**
 * @brief Fat-pointer handle encapsulating the HVAC status query port.
 */
typedef struct {
    const i_hvac_status_api_t *api;   /**< Operation function table */
    void                      *impl;  /**< Private instance context */
} i_hvac_status_t;

/**
 * @brief Operation function table (v-table) for HVAC status queries.
 */
struct i_hvac_status_api {
    int (*get_status)(void *impl, hvac_status_msg_t *out_status);
};

/*============================================================================*
 * SAFETY VALIDATION PREDICATE & PUBLIC TYPE-SAFE WRAPPERS
 *============================================================================*/

/**
 * @brief Verify that the query port handle is properly bound to an implementation.
 */
static inline bool i_hvac_status_is_bound(const i_hvac_status_t *port)
{
    return (port != NULL && port->api != NULL && port->impl != NULL &&
            port->api->get_status != NULL);
}

/**
 * @brief Read the consolidated HVAC status snapshot.
 *
 * @param[in]  port       Port handle.
 * @param[out] out_status Destination snapshot structure.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_hvac_status_get(const i_hvac_status_t *port, hvac_status_msg_t *out_status)
{
    if (!i_hvac_status_is_bound(port)) {
        return -ENODEV;
    }
    if (out_status == NULL) {
        return -EINVAL;
    }
    return port->api->get_status(port->impl, out_status);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_HVAC_STATUS_H_ */
