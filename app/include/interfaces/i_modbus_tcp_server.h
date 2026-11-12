/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_modbus_tcp_server.h
 * @brief Abstract port interface for the Industrial Modbus TCP Server.
 *
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-table (i_modbus_tcp_server_api_t)
 * 2) Handle (i_modbus_tcp_server_t)
 * 3) Safety validation predicate (i_modbus_tcp_server_is_bound())
 * 4) Validated wrappers (static inline i_modbus_tcp_server_*())
 *
 * Pure C99/C11 - zero Zephyr headers, zero vendor HAL, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_MODBUS_TCP_SERVER_H_
#define APP_INTERFACES_I_MODBUS_TCP_SERVER_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/modbus_tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (Implementation Detail, Never Called Directly by Application)
 *============================================================================*/

/**
 * @brief Operations exposed by a Modbus TCP server implementation.
 */
typedef struct {
    /**
     * @brief Initialize Modbus TCP server adapter context with configuration.
     *
     * @param[in] impl    Implementation context.
     * @param[in] config  Server configuration (port, max clients, timeouts).
     * @return 0 on success, negative errno on failure.
     */
    int (*init)(void *impl, const modbus_tcp_server_config_t *config);

    /**
     * @brief Bind listening socket on port 502 and begin serving clients.
     *
     * @param[in] impl  Implementation context.
     * @return 0 on success, negative errno on failure.
     */
    int (*start)(void *impl);

    /**
     * @brief Stop listening socket and disconnect all active client sessions.
     *
     * @param[in] impl  Implementation context.
     * @return 0 on success, negative errno on failure.
     */
    int (*stop)(void *impl);

    /**
     * @brief Query cumulative diagnostic statistics from server instance.
     *
     * @param[in]  impl       Implementation context.
     * @param[out] out_stats  Pointer to statistics output structure.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_stats)(void *impl, modbus_tcp_server_stats_t *out_stats);

    /**
     * @brief Query current operational lifecycle state.
     *
     * @param[in]  impl       Implementation context.
     * @param[out] out_state  Pointer to state output variable.
     * @return 0 on success, negative errno on failure.
     */
    int (*get_state)(void *impl, modbus_tcp_server_state_t *out_state);

    /**
     * @brief Query detailed status of a specific client session slot.
     *
     * @param[in]  impl        Implementation context.
     * @param[in]  slot_index  Session slot index (0..max_clients-1).
     * @param[out] out_info    Pointer to session info output structure.
     * @return 0 on success, negative errno on failure (-ENOENT if idle).
     */
    int (*get_session_info)(void *impl, uint8_t slot_index, modbus_tcp_session_info_t *out_info);

    /**
     * @brief Forcibly close and evict a specific client session.
     *
     * @param[in] impl        Implementation context.
     * @param[in] slot_index  Session slot index to disconnect.
     * @return 0 on success, negative errno on failure.
     */
    int (*disconnect_client)(void *impl, uint8_t slot_index);
} i_modbus_tcp_server_api_t;

/*============================================================================*
 * 2. PORT HANDLE (Injected into Consumers / Composition Root)
 *============================================================================*/

/**
 * @brief Fat-pointer handle encapsulating Modbus TCP Server operations and context.
 */
typedef struct {
    const i_modbus_tcp_server_api_t *api;  /**< V-table pointer */
    void                            *impl; /**< Concrete adapter context pointer */
} i_modbus_tcp_server_t;

/**
 * @brief Verify that a Modbus TCP server port handle is bound and valid.
 *
 * @param[in] port Port handle to check (may be NULL).
 * @return true if both api and impl pointers are non-NULL and core operations are present.
 */
static inline bool i_modbus_tcp_server_is_bound(const i_modbus_tcp_server_t *port)
{
    return (port != NULL &&
            port->api != NULL &&
            port->impl != NULL &&
            port->api->init != NULL &&
            port->api->start != NULL &&
            port->api->stop != NULL &&
            port->api->get_stats != NULL &&
            port->api->get_state != NULL &&
            port->api->get_session_info != NULL &&
            port->api->disconnect_client != NULL);
}

/*============================================================================*
 * 3. VALIDATED WRAPPERS (Public API Called by Application & Active Objects)
 *============================================================================*/

/**
 * @brief Initialize the Modbus TCP Server adapter.
 *
 * @param[in] port    Port handle.
 * @param[in] config  Server configuration parameters.
 * @return 0 on success, -EINVAL on invalid handle/params, or negative errno.
 */
static inline int i_modbus_tcp_server_init(const i_modbus_tcp_server_t *port,
                                           const modbus_tcp_server_config_t *config)
{
    if (!i_modbus_tcp_server_is_bound(port) || config == NULL) {
        return -EINVAL;
    }
    return port->api->init(port->impl, config);
}

/**
 * @brief Start the Modbus TCP Server listener.
 *
 * @param[in] port  Port handle.
 * @return 0 on success, -EINVAL on invalid handle, or negative errno.
 */
static inline int i_modbus_tcp_server_start(const i_modbus_tcp_server_t *port)
{
    if (!i_modbus_tcp_server_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->start(port->impl);
}

/**
 * @brief Stop the Modbus TCP Server listener and terminate active sessions.
 *
 * @param[in] port  Port handle.
 * @return 0 on success, -EINVAL on invalid handle, or negative errno.
 */
static inline int i_modbus_tcp_server_stop(const i_modbus_tcp_server_t *port)
{
    if (!i_modbus_tcp_server_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->stop(port->impl);
}

/**
 * @brief Retrieve cumulative diagnostic metrics from the server.
 *
 * @param[in]  port       Port handle.
 * @param[out] out_stats  Destination buffer for statistics.
 * @return 0 on success, -EINVAL on invalid handle/buffer, or negative errno.
 */
static inline int i_modbus_tcp_server_get_stats(const i_modbus_tcp_server_t *port,
                                                modbus_tcp_server_stats_t *out_stats)
{
    if (!i_modbus_tcp_server_is_bound(port) || out_stats == NULL) {
        return -EINVAL;
    }
    return port->api->get_stats(port->impl, out_stats);
}

/**
 * @brief Query current lifecycle state of the server.
 *
 * @param[in]  port       Port handle.
 * @param[out] out_state  Destination variable for server state.
 * @return 0 on success, -EINVAL on invalid handle/output, or negative errno.
 */
static inline int i_modbus_tcp_server_get_state(const i_modbus_tcp_server_t *port,
                                                modbus_tcp_server_state_t *out_state)
{
    if (!i_modbus_tcp_server_is_bound(port) || out_state == NULL) {
        return -EINVAL;
    }
    return port->api->get_state(port->impl, out_state);
}

/**
 * @brief Query status information for a specific client session slot.
 *
 * @param[in]  port        Port handle.
 * @param[in]  slot_index  Slot index (0..max_clients-1).
 * @param[out] out_info    Destination buffer for session info.
 * @return 0 on success, -EINVAL on invalid handle/output, -ENOENT if idle, or negative errno.
 */
static inline int i_modbus_tcp_server_get_session_info(const i_modbus_tcp_server_t *port,
                                                       uint8_t slot_index,
                                                       modbus_tcp_session_info_t *out_info)
{
    if (!i_modbus_tcp_server_is_bound(port) || out_info == NULL) {
        return -EINVAL;
    }
    return port->api->get_session_info(port->impl, slot_index, out_info);
}

/**
 * @brief Forcibly disconnect an active client session.
 *
 * @param[in] port        Port handle.
 * @param[in] slot_index  Slot index to disconnect.
 * @return 0 on success, -EINVAL on invalid handle, or negative errno.
 */
static inline int i_modbus_tcp_server_disconnect_client(const i_modbus_tcp_server_t *port,
                                                        uint8_t slot_index)
{
    if (!i_modbus_tcp_server_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->disconnect_client(port->impl, slot_index);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_MODBUS_TCP_SERVER_H_ */
