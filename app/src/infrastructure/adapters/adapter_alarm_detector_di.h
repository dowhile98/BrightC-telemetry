/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_di.h
 * @brief Adapter: Digital Input & Contact Alarm Ingestion Detector.
 *
 * Maps incoming di_transition_event_t (from chan_digital_input_state)
 * into alarm conditions posted to ao_alarm_manager.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_DI_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_DI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/digital_input_types.h"
#include "application/active_objects/ao_alarm_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ao_alarm_manager_t *manager;
} adapter_alarm_detector_di_t;

/**
 * @brief Initialize the Digital Input alarm detector adapter.
 */
void adapter_alarm_detector_di_init(adapter_alarm_detector_di_t *self, ao_alarm_manager_t *mgr);

/**
 * @brief Ingest a confirmed digital input transition event.
 */
void adapter_alarm_detector_di_process_transition(adapter_alarm_detector_di_t *self,
                                                  const di_transition_event_t *event);

/**
 * @brief Helper to map a semantic di_role_t to an alarm_id_t.
 *
 * @return alarm_id or ALARM_ID_NONE if not an alarm contact role.
 */
uint16_t adapter_alarm_detector_di_role_to_alarm(di_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_DI_H_ */
