/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_temp.h
 * @brief Adapter: Environmental & Group Temperature Alarm Ingestion Detector.
 *
 * Evaluates periodic sensor snapshots against low/high temperature thresholds
 * and maintains per-sensor hysteresis state machines across up to HVAC_MAX_GROUPS.
 * Sensors are assigned to groups via min/max sensor IDs (1-based, 1..64).
 * Supports automatic configuration from persistent HVAC EEPROM settings.
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

/**
 * @brief Sensor ID range assigned to a thermal group (1-based, 1..64).
 */
typedef struct {
    uint8_t min_id; /**< Start sensor flat ID (1-based, 0 = unassigned) */
    uint8_t max_id; /**< End sensor flat ID (1-based, 0 = unassigned) */
} group_sensor_range_t;

/**
 * @brief Thermal group detector runtime configuration.
 */
typedef struct {
    uint8_t min_id;   /**< Start sensor ID (1..64, 0 = unassigned) */
    uint8_t max_id;   /**< End sensor ID (1..64, 0 = unassigned) */
    float   t_low_c;  /**< Low temperature trip threshold in °C */
    float   t_high_c; /**< High temperature trip threshold in °C */
    float   hyst_c;   /**< Hysteresis band in °C */
    bool    enabled;  /**< true if group is active/configured */
} adapter_temp_group_config_t;

/** Maximum number of flat temperature sensors evaluated across groups (IDs 1..64) */
#define ADAPTER_TEMP_MAX_SENSORS 64U

/** Maximum number of sensor sources per group (matches 32-bit uint32_t source mask in alarm_aggregate_t) */
#define ADAPTER_TEMP_MAX_GROUP_SOURCES 32U

typedef struct {
    ao_alarm_manager_t          *manager;
    adapter_temp_group_config_t  groups[HVAC_MAX_GROUPS];
    alarm_eval_temp_low_t        eval_low[HVAC_MAX_GROUPS][ADAPTER_TEMP_MAX_GROUP_SOURCES];
    alarm_eval_temp_high_t       eval_high[HVAC_MAX_GROUPS][ADAPTER_TEMP_MAX_GROUP_SOURCES];
    bool                         last_is_low[HVAC_MAX_GROUPS][ADAPTER_TEMP_MAX_GROUP_SOURCES];
    bool                         last_is_high[HVAC_MAX_GROUPS][ADAPTER_TEMP_MAX_GROUP_SOURCES];
    bool                         group_fault_active[HVAC_MAX_GROUPS];
} adapter_alarm_detector_temp_t;

/**
 * @brief Initialize temperature alarm detector adapter.
 *
 * All groups are initially disabled until configured via
 * adapter_alarm_detector_temp_apply_hvac_config() or
 * adapter_alarm_detector_temp_set_group_config().
 *
 * @param self Pointer to adapter context.
 * @param mgr Pointer to Active Object alarm manager.
 */
void adapter_alarm_detector_temp_init(adapter_alarm_detector_temp_t *self,
                                      ao_alarm_manager_t *mgr);

/**
 * @brief Configure sensor range for a specific thermal group.
 */
int adapter_alarm_detector_temp_set_group_range(adapter_alarm_detector_temp_t *self,
                                                uint8_t group_idx,
                                                uint8_t min_id,
                                                uint8_t max_id);

/**
 * @brief Configure sensor range and temperature thresholds for a specific group.
 */
int adapter_alarm_detector_temp_set_group_config(adapter_alarm_detector_temp_t *self,
                                                 uint8_t group_idx,
                                                 uint8_t min_id,
                                                 uint8_t max_id,
                                                 float t_low_c,
                                                 float t_high_c,
                                                 float hyst_c);

/**
 * @brief Configure a specific group directly from HVAC group configuration.
 */
int adapter_alarm_detector_temp_set_group_from_hvac(adapter_alarm_detector_temp_t *self,
                                                    uint8_t group_idx,
                                                    const config_hvac_group_t *grp);

/**
 * @brief Apply full persistent HVAC configuration to all groups.
 */
int adapter_alarm_detector_temp_apply_hvac_config(adapter_alarm_detector_temp_t *self,
                                                  const config_hvac_t *hvac_cfg);

/**
 * @brief Ingest a periodic sensor snapshot.
 */
void adapter_alarm_detector_temp_process_snapshot(adapter_alarm_detector_temp_t *self,
                                                  const sensor_snapshot_msg_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_TEMP_H_ */
