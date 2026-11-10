/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_hvac.h
 * @brief Adapter: HVAC System Bypass, Cooling Boost, and Unit Lockout Alarm Detector.
 *
 * Ingests periodic hvac_status_msg_t telemetry snapshots and posts
 * Bypass, Cooling Boost, and Lockout alarm conditions to ao_alarm_manager.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_HVAC_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_HVAC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/hvac_types.h"
#include "application/zbus/hvac_channels.h"
#include "application/active_objects/ao_alarm_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ao_alarm_manager_t *manager;
    bool                is_group_bypass_active[HVAC_MAX_GROUPS];
    bool                is_group_boost_active[HVAC_MAX_GROUPS];
    bool                is_unit_lockout[HVAC_MAX_UNITS_TOTAL];
} adapter_alarm_detector_hvac_t;

/**
 * @brief Initialize the HVAC alarm detector adapter.
 */
void adapter_alarm_detector_hvac_init(adapter_alarm_detector_hvac_t *self, ao_alarm_manager_t *mgr);

/**
 * @brief Ingest a periodic HVAC status snapshot.
 */
void adapter_alarm_detector_hvac_process_status(adapter_alarm_detector_hvac_t *self,
                                                const hvac_status_msg_t *status);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_HVAC_H_ */
