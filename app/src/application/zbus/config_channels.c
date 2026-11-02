/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Configuration and storage subsystem Zbus channel definitions.
 */

#include "application/zbus/config_channels.h"

ZBUS_CHAN_DEFINE(
    chan_system_config,
    system_config_event_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(
        .domain = CONFIG_DOMAIN_DEVICE,
        .event_type = CONFIG_EVENT_DIRTY,
        .sub_id = 0,
        .status = 0
    )
);
