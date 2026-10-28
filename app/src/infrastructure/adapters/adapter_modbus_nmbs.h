/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Infrastructure adapter: nanoMODBUS serial RTU client behind i_modbus_client.
 * This is the only file of the application that knows about nanoMODBUS.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_MODBUS_NMBS_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_MODBUS_NMBS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/nanomodbus/nanomodbus_z.h>
#include <zephyr/nanomodbus/nanomodbus_z_serial.h>

#include "common/modbus_types.h"
#include "interfaces/i_modbus_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One nanoMODBUS client bound to one RS-485 bus.
 *
 * Stored by value inside the composition-root container; the structure owns
 * the transport ring buffer, so it must not be copied after init.
 */
typedef struct {
    nmbs_z_serial_transport_t transport; /**< Serial RTU transport */
    nmbs_z_client_t client;              /**< Thread-safe Modbus client */
    nmbs_platform_conf platform;         /**< Platform callbacks from the transport */
    const struct device *uart_dev;       /**< UART backing this bus */
    modbus_bus_cfg_t cfg;                /**< Line configuration currently applied */
    bool is_ready;                       /**< Transport and client initialised */
} adapter_modbus_nmbs_t;

/**
 * @brief Initialise the transport and the client from a devicetree configuration.
 *
 * @param[out] self Adapter instance (static storage, never copied).
 * @param[in]  cfg  Transport configuration, usually built with
 *                  NMBS_Z_SERIAL_TRANSPORT_CONFIG_DT().
 *
 * @return 0 on success, negative errno on failure.
 */
int adapter_modbus_nmbs_init(adapter_modbus_nmbs_t *self,
                             const nmbs_z_serial_transport_config_t *cfg);

/**
 * @brief Expose the adapter through the client port.
 *
 * @return 0 on success, -EINVAL on bad arguments or an uninitialised adapter.
 */
int adapter_modbus_nmbs_port(adapter_modbus_nmbs_t *self, i_modbus_client_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_MODBUS_NMBS_H_ */
