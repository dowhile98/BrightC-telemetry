/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract port interface for network management.
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-table (i_network_api_t)
 * 2) Handle (i_network_t)
 * 3) Validated wrappers (static inline i_network_*())
 *
 * Pure C99/C11 - no Zephyr headers, no vendor HAL, no dynamic memory.
 */

#ifndef APP_INTERFACES_I_NETWORK_H_
#define APP_INTERFACES_I_NETWORK_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/network_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (implementation detail, never called directly by application)
 *============================================================================*/

typedef struct {
    /**
     * @brief Initialize hardware and driver context for the network interface.
     */
    int (*init)(void *impl);

    /**
     * @brief Bring interface up and initiate network connection/AP.
     */
    int (*start)(void *impl);

    /**
     * @brief Disconnect / disable interface and bring link down.
     */
    int (*stop)(void *impl);

    /**
     * @brief Read active configuration for the network interface.
     */
    int (*get_config)(void *impl, void *out_data, size_t max_size, size_t *out_size);

    /**
     * @brief Apply new configuration to the network interface.
     */
    int (*set_config)(void *impl, const void *data, size_t size);

    /**
     * @brief Obtain current runtime status (link state, IP, RSSI).
     */
    int (*get_status)(void *impl, net_status_t *out_status);
} i_network_api_t;

/*============================================================================*
 * 2. PORT HANDLE (injected into consumers / active objects)
 *============================================================================*/

typedef struct {
    const i_network_api_t *api; /**< V-table pointer */
    void *impl;                 /**< Concrete adapter context pointer */
} i_network_t;

/**
 * @brief Check that a network port handle is bound to a concrete implementation.
 *
 * @param[in] net Port handle to check (may be NULL).
 * @return true if both api and impl pointers are non-NULL and core operations are present.
 */
static inline bool i_network_is_bound(const i_network_t *net)
{
    return net != NULL && net->api != NULL && net->impl != NULL &&
           net->api->init != NULL && net->api->start != NULL &&
           net->api->stop != NULL && net->api->get_status != NULL;
}

/*============================================================================*
 * 3. VALIDATED WRAPPERS (only allowed path to dispatch through the v-table)
 *============================================================================*/

/**
 * @brief Initialize network interface hardware/driver.
 *
 * @param[in] net Bound network port handle.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_network_init(const i_network_t *net)
{
    if (!i_network_is_bound(net)) {
        return -EINVAL;
    }
    return net->api->init(net->impl);
}

/**
 * @brief Start network interface and begin connection.
 *
 * @param[in] net Bound network port handle.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_network_start(const i_network_t *net)
{
    if (!i_network_is_bound(net)) {
        return -EINVAL;
    }
    return net->api->start(net->impl);
}

/**
 * @brief Stop network interface and release link.
 *
 * @param[in] net Bound network port handle.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_network_stop(const i_network_t *net)
{
    if (!i_network_is_bound(net)) {
        return -EINVAL;
    }
    return net->api->stop(net->impl);
}

/**
 * @brief Retrieve configuration of a network interface.
 *
 * @param[in]  net      Bound network port handle.
 * @param[out] out_data Destination buffer for config struct.
 * @param[in]  max_size Size of destination buffer.
 * @param[out] out_size Actual written size (may be NULL).
 * @return 0 on success, negative errno on failure.
 */
static inline int i_network_get_config(const i_network_t *net,
                                       void *out_data,
                                       size_t max_size,
                                       size_t *out_size)
{
    if (!i_network_is_bound(net) || out_data == NULL || max_size == 0U) {
        return -EINVAL;
    }
    if (net->api->get_config == NULL) {
        return -ENOSYS;
    }
    return net->api->get_config(net->impl, out_data, max_size, out_size);
}

/**
 * @brief Store and apply configuration to a network interface.
 *
 * @param[in] net  Bound network port handle.
 * @param[in] data Source buffer with configuration.
 * @param[in] size Size of configuration structure.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_network_set_config(const i_network_t *net,
                                       const void *data,
                                       size_t size)
{
    if (!i_network_is_bound(net) || data == NULL || size == 0U) {
        return -EINVAL;
    }
    if (net->api->set_config == NULL) {
        return -ENOSYS;
    }
    return net->api->set_config(net->impl, data, size);
}

/**
 * @brief Query runtime status of a network interface.
 *
 * @param[in]  net        Bound network port handle.
 * @param[out] out_status Destination status structure.
 * @return 0 on success, negative errno on failure.
 */
static inline int i_network_get_status(const i_network_t *net,
                                       net_status_t *out_status)
{
    if (!i_network_is_bound(net) || out_status == NULL) {
        return -EINVAL;
    }
    return net->api->get_status(net->impl, out_status);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_NETWORK_H_ */
