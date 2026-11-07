/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file digital_input_channels.h
 * @brief Zbus channel declarations for Digital Input state transitions and snapshots.
 */

#ifndef APP_APPLICATION_ZBUS_DIGITAL_INPUT_CHANNELS_H_
#define APP_APPLICATION_ZBUS_DIGITAL_INPUT_CHANNELS_H_

#include <zephyr/zbus/zbus.h>

#include "common/digital_input_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zbus channel broadcasting digital input and contact alarm state transitions.
 *
 * Payloads are di_transition_event_t structures.
 */
ZBUS_CHAN_DECLARE(chan_digital_input_state);

/**
 * @brief Zbus channel broadcasting consolidated digital input telemetry snapshots.
 *
 * Payloads are di_snapshot_msg_t structures.
 */
ZBUS_CHAN_DECLARE(chan_digital_input_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_DIGITAL_INPUT_CHANNELS_H_ */
