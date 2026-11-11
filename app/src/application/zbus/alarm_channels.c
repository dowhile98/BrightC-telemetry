/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_channels.c
 * @brief Zbus channel definition for Alarm Management state transitions.
 */

#include "application/zbus/alarm_channels.h"

ZBUS_CHAN_DEFINE(
    chan_alarm_transition,
    alarm_transition_event_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(lis_alarm_logger),
    ZBUS_MSG_INIT(
        .alarm_id = 0,
        .event_type = ALARM_EVENT_NONE,
        .previous_state = ALARM_STATE_INACTIVE,
        .new_state = ALARM_STATE_INACTIVE,
        .severity = ALARM_SEVERITY_INFO,
        .priority = ALARM_PRIORITY_LOW,
        .source_category = ALARM_SOURCE_DIGITAL_INPUT,
        .contributing_sources_mask = 0,
        .trigger_value = 0.0f,
        .timestamp_ms = 0
    )
);
