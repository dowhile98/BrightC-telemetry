/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hvac_channels.h
 * @brief Zbus channel declarations for HVAC Subsystem status snapshots.
 */

#ifndef APP_APPLICATION_ZBUS_HVAC_CHANNELS_H_
#define APP_APPLICATION_ZBUS_HVAC_CHANNELS_H_

#include <zephyr/zbus/zbus.h>

#include "common/hvac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HVAC subsystem consolidated status telemetry snapshot message.
 */
typedef struct {
    hvac_group_state_t group_state[HVAC_MAX_GROUPS];
    hvac_unit_state_t  unit_state[HVAC_MAX_UNITS_TOTAL];
    hvac_tri_state_t   unit_tri_state[HVAC_MAX_UNITS_TOTAL];
    uint32_t           bypass_causes[HVAC_MAX_GROUPS];
    float              highest_temp[HVAC_MAX_GROUPS];
    uint8_t            current_turn_offset[HVAC_MAX_GROUPS];
} hvac_status_msg_t;

/**
 * @brief Zbus channel broadcasting periodic HVAC status telemetry snapshots.
 *
 * Payloads are hvac_status_msg_t structures.
 */
ZBUS_CHAN_DECLARE(chan_hvac_status);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_HVAC_CHANNELS_H_ */
