/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file digital_input_channels.c
 * @brief Zbus channel definitions for Digital Input state transitions.
 */

#include "application/zbus/digital_input_channels.h"

ZBUS_CHAN_DEFINE(
    chan_digital_input_state,
    di_transition_event_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(
        .role = DI_ROLE_FAN_1_AUX,
        .new_state = DI_STATE_INACTIVE,
        .prev_state = DI_STATE_INACTIVE,
        .timestamp_ms = 0ULL
    )
);
