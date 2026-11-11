/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file thermal_channels.h
 * @brief Zbus channel declarations for Thermal and Energy telemetry and events.
 */

#ifndef APP_APPLICATION_ZBUS_THERMAL_CHANNELS_H_
#define APP_APPLICATION_ZBUS_THERMAL_CHANNELS_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thermal and energy calculation cycle update event payload.
 */
typedef struct {
    uint32_t         generation_id; /**< Monotonic calculation generation sequence */
    uint64_t         timestamp_ms;  /**< Monotonic timestamp of calculation completion */
    quality_status_t quality;       /**< Overall thermodynamic data quality */
} thermal_energy_event_t;

/**
 * @brief Zbus channel broadcasting periodic thermal and energy update notifications.
 *
 * Payloads are thermal_energy_event_t structures.
 */
ZBUS_CHAN_DECLARE(chan_thermal_energy_update);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_THERMAL_CHANNELS_H_ */
