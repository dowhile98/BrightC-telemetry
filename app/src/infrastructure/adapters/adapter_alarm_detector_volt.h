/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_volt.h
 * @brief Adapter: 3-Phase Electrical Line Voltage Alarm Ingestion Detector.
 *
 * Evaluates periodic energy meter readings against under/over voltage trip bands.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_VOLT_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_VOLT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/energy_types.h"
#include "application/active_objects/ao_alarm_manager.h"
#include "domain/alarm/alarm_evaluator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ao_alarm_manager_t         *manager;
    alarm_eval_voltage_low_t   eval_low;
    alarm_eval_voltage_high_t  eval_high;
    bool                       is_low_active;
    bool                       is_high_active;
    bool                       comm_fault_active;
} adapter_alarm_detector_volt_t;

/**
 * @brief Initialize the 3-phase voltage alarm detector adapter.
 */
void adapter_alarm_detector_volt_init(adapter_alarm_detector_volt_t *self,
                                      ao_alarm_manager_t *mgr,
                                      float nominal_v,
                                      float dev_pct,
                                      float hyst_v);

/**
 * @brief Ingest a periodic electrical energy reading.
 */
void adapter_alarm_detector_volt_process_reading(adapter_alarm_detector_volt_t *self,
                                                 const energy_reading_t *reading);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_ALARM_DETECTOR_VOLT_H_ */
