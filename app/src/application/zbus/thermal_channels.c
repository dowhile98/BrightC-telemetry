/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file thermal_channels.c
 * @brief Zbus channel definition for Thermal and Energy telemetry update events.
 */

#include "application/zbus/thermal_channels.h"

ZBUS_CHAN_DEFINE(
    chan_thermal_energy_update,
    thermal_energy_event_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);
