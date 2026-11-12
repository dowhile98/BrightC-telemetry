/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_modbus_tcp.h
 * @brief Active Object Coordinator for Industrial Modbus TCP Server.
 *
 * Wraps nanoMODBUS Zephyr TCP Server (nmbs_z_server_tcp_t) behind the
 * i_modbus_tcp_server_t interface contract.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_MODBUS_TCP_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_MODBUS_TCP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#if defined(CONFIG_NANOMODBUS)
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_tcp.h>
#else
typedef struct nmbs_callbacks nmbs_callbacks;
#endif

#include "common/modbus_tcp_types.h"
#include "interfaces/i_modbus_tcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modbus TCP Active Object coordinator context.
 *
 * Wraps nmbs_z_server_tcp_t directly. Because nmbs_z_server_tcp_t embeds
 * the listener thread stack, ao_modbus_tcp_t instances must reside in static storage.
 */
typedef struct ao_modbus_tcp {
    i_modbus_tcp_server_t       port;                     /**< Concrete port handle */
#if defined(CONFIG_NANOMODBUS_SERVER_TCP)
    nmbs_z_server_tcp_t         server;                   /**< nanoMODBUS Zephyr TCP server */
#endif
    modbus_tcp_server_config_t  config;                   /**< Active configuration */
    modbus_tcp_server_state_t   state;                    /**< Operational lifecycle state */
    bool                        is_initialized;           /**< True when initialized */
} ao_modbus_tcp_t;

/**
 * @brief Initialize the Modbus TCP Active Object coordinator.
 *
 * Configures the underlying nmbs_z_server_tcp_t instance with the specified
 * nanoMODBUS callbacks and TCP port.
 *
 * @param[out] self        AO instance to initialize (must reside in static storage).
 * @param[in]  config      Server configuration parameters.
 * @param[in]  callbacks   nanoMODBUS callback function table.
 * @return 0 on success, negative errno on failure.
 */
int ao_modbus_tcp_init(ao_modbus_tcp_t *self,
                       const modbus_tcp_server_config_t *config,
                       const nmbs_callbacks *callbacks);

/**
 * @brief Start the Modbus TCP Active Object thread and begin listening.
 *
 * @param[in,out] self AO instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_modbus_tcp_start(ao_modbus_tcp_t *self);

/**
 * @brief Stop the Modbus TCP Active Object and close listening/client sockets.
 *
 * @param[in,out] self AO instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_modbus_tcp_stop(ao_modbus_tcp_t *self);

/**
 * @brief Retrieve copy of cumulative diagnostic metrics from server instance.
 *
 * @param[in]  self      AO instance.
 * @param[out] out_stats Destination statistics buffer.
 * @return 0 on success, negative errno on failure.
 */
int ao_modbus_tcp_get_stats(ao_modbus_tcp_t *self, modbus_tcp_server_stats_t *out_stats);

/**
 * @brief Check if the Active Object server thread is currently running.
 *
 * @param[in] self AO instance.
 * @return true if running, false otherwise.
 */
bool ao_modbus_tcp_is_running(const ao_modbus_tcp_t *self);

/**
 * @brief Query current operational state.
 *
 * @param[in]  self      AO instance.
 * @param[out] out_state Destination state variable.
 * @return 0 on success, negative errno on error.
 */
int ao_modbus_tcp_get_state(const ao_modbus_tcp_t *self, modbus_tcp_server_state_t *out_state);

/**
 * @brief Get the abstract i_modbus_tcp_server_t port handle.
 *
 * @param[in] self AO instance.
 * @return Fat-pointer port handle.
 */
i_modbus_tcp_server_t ao_modbus_tcp_get_port(ao_modbus_tcp_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_MODBUS_TCP_H_ */
