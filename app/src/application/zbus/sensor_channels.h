/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file sensor_channels.h
 * @brief Zbus channel declarations for sensor snapshot and thermal matrix telemetry.
 */

#ifndef APP_APPLICATION_ZBUS_SENSOR_CHANNELS_H_
#define APP_APPLICATION_ZBUS_SENSOR_CHANNELS_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#include "common/sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Consolidated sensor snapshot message.
 *
 * Carries normalized readings across all on-board and bus sensor channels:
 * - Internal RTDs 1..5: indices 0..4 (flat IDs 1..5)
 * - Modbus CWT Concentrator 1 (CH 1..16): indices 9..24 (flat IDs 10..25)
 * - Modbus CWT Concentrator 2 (CH 1..16): indices 29..44 (flat IDs 30..45)
 * - Analog J9 Current Loops (CH 1..10): indices 49..58 (flat IDs 50..59)
 */
typedef struct {
    float    temperatures[64]; /**< Flat sensor readings array (degC or loop mA) */
    uint64_t valid_mask;       /**< 64-bit channel validity bitmask */
    uint64_t timestamp_ms;     /**< Acquisition uptime timestamp in milliseconds */
    uint32_t sequence;         /**< Monotonically increasing sequence number */
} sensor_snapshot_msg_t;

/**
 * @brief Zbus channel broadcasting periodic flat sensor snapshots.
 *
 * Payloads are sensor_snapshot_msg_t structures.
 */
ZBUS_CHAN_DECLARE(chan_sensor_snapshot);

/**
 * @brief Zbus channel broadcasting thermodynamic semantic matrix frames.
 *
 * Payloads are thermal_matrix_frame_t structures.
 */
ZBUS_CHAN_DECLARE(chan_thermal_matrix);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ZBUS_SENSOR_CHANNELS_H_ */
