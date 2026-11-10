/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file indicator_channels.c
 * @brief Zbus channel definitions for system indicators and alarms.
 */

#include "application/zbus/indicator_channels.h"

ZBUS_CHAN_DEFINE(
    chan_system_alarm,
    alarm_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(lis_indicator_alarm_feed),
    ZBUS_MSG_INIT(
        .alarm_id = 0,
        .level = ALARM_LEVEL_NONE,
        .is_active = false,
        .timestamp_ms = 0
    )
);

ZBUS_CHAN_DEFINE(
    chan_system_health,
    system_health_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(
        .health = SYSTEM_HEALTH_HEALTHY,
        .timestamp_ms = 0
    )
);

ZBUS_CHAN_DEFINE(
    chan_indicator_command,
    indicator_cmd_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(
        .cmd = INDICATOR_CMD_AUTO,
        .led_state = LED_STATE_OFF,
        .buzzer_pattern = BUZZER_PATTERN_SILENT,
        .duration_ms = 0
    )
);

ZBUS_CHAN_DEFINE(
    chan_indicator_status,
    indicator_status_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(
        .led_state = LED_STATE_OFF,
        .buzzer_pattern = BUZZER_PATTERN_SILENT,
        .active_alarm_level = ALARM_LEVEL_NONE,
        .system_health = SYSTEM_HEALTH_HEALTHY,
        .manual_override = false
    )
);
