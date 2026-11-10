/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_temp.h
 * @brief Adapter: Environmental & Group Temperature Alarm Ingestion Detector.
 *
 * Evaluates periodic sensor snapshots against low/high temperature thresholds
 * and maintains per-sensor hysteresis state machines across up to HVAC_MAX_GROUPS.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_TEMP_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_TEMP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/sensor_types.h"
#include "common/hvac_types.h"
#include "application/zbus/sensor_channels.h"
#include "application/active_objects/ao_alarm_manager.h"
#include "domain/alarm/alarm_evaluator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ao_alarm_manager_t       *manager;
    uint16_t                  group_sensor_masks[HVAC_MAX_GROUPS];
    alarm_eval_temp_low_t     eval_low[16];
    alarm_eval_temp_high_t    eval_high[16];
    bool                      last_is_low[16];
    bool                      last_is_high[16];
    bool                      group_fault_active[HVAC_MAX_GROUPS];
} adapter_alarm_detector_temp_t;

/**
 * @brief Initialize temperature alarm detector adapter for up to HVAC_MAX_GROUPS.
 *
 * @param self Pointer to adapter context.
 * @param mgr Pointer to Active Object alarm manager.
 * @param group_masks Array of bitmasks for sensor assignment per group (or NULL for default).
 * @param t_low_c Low temperature trip threshold in degrees Celsius.
 * @param t_high_c High temperature trip threshold in degrees Celsius.
 * @param hyst_c Hysteresis band in degrees Celsius.
 */
void adapter_alarm_detector_temp_init(adapter_alarm_detector_temp_t *self,
                                      ao_alarm_manager_t *mgr,
                                      const uint16_t *group_masks,
                                      float t_low_c,
                                      float t_high_c,
                                      float hyst_c);

/**
 * @brief Ingest a periodic sensor snapshot.
 */
void adapter_alarm_detector_temp_process_snapshot(adapter_alarm_detector_temp_t *self,
                                                  const sensor_snapshot_msg_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_TEMP_H_ */
