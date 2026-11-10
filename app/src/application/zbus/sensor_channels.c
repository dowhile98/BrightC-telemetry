/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file sensor_channels.c
 * @brief Zbus channel definitions for sensor snapshot and thermal matrix telemetry.
 */

#include "application/zbus/sensor_channels.h"

ZBUS_CHAN_DEFINE(
    chan_sensor_snapshot,
    sensor_snapshot_msg_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(lis_hvac_sensor_feed, lis_alarm_sensor_feed),
    ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(
    chan_thermal_matrix,
    thermal_matrix_frame_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);
