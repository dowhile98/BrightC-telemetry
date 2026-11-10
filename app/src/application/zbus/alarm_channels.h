/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_channels.h
 * @brief Zbus channel declarations for Alarm Management state transitions.
 */

#ifndef APP_APPLICATION_ZBUS_ALARM_CHANNELS_H_
#define APP_APPLICATION_ZBUS_ALARM_CHANNELS_H_

#include <zephyr/zbus/zbus.h>
#include "common/alarm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zbus channel broadcasting detailed alarm transition events (ON_PRESS / ON_RELEASE).
 *
 * Payloads are alarm_transition_event_t structures.
 * Subscribed by Persistent Logger, SNMP Agent, HMI, and telemetry spoolers.
 */
ZBUS_CHAN_DECLARE(chan_alarm_transition);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_ALARM_CHANNELS_H_ */
