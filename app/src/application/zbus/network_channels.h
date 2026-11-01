/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Network state Zbus message definition and channel declarations.
 */

#ifndef APP_APPLICATION_ZBUS_NETWORK_CHANNELS_H_
#define APP_APPLICATION_ZBUS_NETWORK_CHANNELS_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#include "common/network_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Message payload published when network interface state changes.
 */
typedef struct {
    network_interface_t interface;    /**< NETWORK_INTERFACE_ETHERNET or NETWORK_INTERFACE_WIFI */
    net_status_t        status;       /**< Full runtime status (state, ip, mask, gw, rssi) */
    uint32_t            timestamp_ms; /**< Kernel uptime in milliseconds when state was captured */
} net_state_msg_t;

ZBUS_CHAN_DECLARE(chan_network_eth_state);
ZBUS_CHAN_DECLARE(chan_network_wifi_state);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_NETWORK_CHANNELS_H_ */
