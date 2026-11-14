/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file system_channels.c
 * @brief Zbus channel definition for operational system events.
 */

#include "application/zbus/system_channels.h"

ZBUS_CHAN_DEFINE(
    chan_system_event,
    system_event_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(lis_system_event_logger),
    ZBUS_MSG_INIT(
        .type = SYSTEM_EVENT_BOOT,
        .severity = ALARM_SEVERITY_INFO,
        .subsystem = "SYSTEM",
        .message = "System initialized",
        .data_u32 = 0,
        .data_i64 = 0,
        .timestamp_ms = 0
    )
);
