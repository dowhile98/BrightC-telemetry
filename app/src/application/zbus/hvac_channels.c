/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hvac_channels.c
 * @brief Zbus channel definition for HVAC Subsystem status snapshots.
 */

#include "application/zbus/hvac_channels.h"

ZBUS_CHAN_DEFINE(
    chan_hvac_status,
    hvac_status_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(lis_alarm_hvac_feed),
    ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(
    chan_hvac_event,
    hvac_event_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(lis_hvac_event_logger),
    ZBUS_MSG_INIT(0)
);
