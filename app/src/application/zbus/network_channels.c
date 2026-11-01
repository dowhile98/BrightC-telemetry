/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Network state Zbus channel definitions.
 */

#include "application/zbus/network_channels.h"

ZBUS_CHAN_DEFINE(
    chan_network_eth_state,
    net_state_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(
        .interface = NETWORK_INTERFACE_ETHERNET,
        .status = {
            .state = NET_IF_STATE_DOWN,
            .ip_addr = 0,
            .subnet_mask = 0,
            .gateway = 0,
            .rssi = -128,
            .link_up_ms = 0
        },
        .timestamp_ms = 0
    )
);

ZBUS_CHAN_DEFINE(
    chan_network_wifi_state,
    net_state_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(
        .interface = NETWORK_INTERFACE_WIFI,
        .status = {
            .state = NET_IF_STATE_DOWN,
            .ip_addr = 0,
            .subnet_mask = 0,
            .gateway = 0,
            .rssi = -128,
            .link_up_ms = 0
        },
        .timestamp_ms = 0
    )
);
