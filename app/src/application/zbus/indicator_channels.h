/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file indicator_channels.h
 * @brief Zbus channel declarations for system indicators and alarms.
 */

#ifndef APP_APPLICATION_ZBUS_INDICATOR_CHANNELS_H_
#define APP_APPLICATION_ZBUS_INDICATOR_CHANNELS_H_

#include <zephyr/zbus/zbus.h>

#include "common/indicator_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Channel for reporting and clearing subsystem alarms.
 */
ZBUS_CHAN_DECLARE(chan_system_alarm);

/**
 * @brief Channel for global system health reports.
 */
ZBUS_CHAN_DECLARE(chan_system_health);

/**
 * @brief Channel for manual indicator commands (Shell, CLI, test harness).
 */
ZBUS_CHAN_DECLARE(chan_indicator_command);

/**
 * @brief Channel for publishing active visual and acoustic status.
 */
ZBUS_CHAN_DECLARE(chan_indicator_status);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_INDICATOR_CHANNELS_H_ */
