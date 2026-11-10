/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_alarm_detector_volt.c
 * @brief Implementation of 3-Phase Line Voltage Alarm Ingestion Detector.
 */

#include "infrastructure/adapters/adapter_alarm_detector_volt.h"

void adapter_alarm_detector_volt_init(adapter_alarm_detector_volt_t *self,
                                      ao_alarm_manager_t *mgr,
                                      float nominal_v,
                                      float dev_pct,
                                      float hyst_v)
{
    if (self == NULL) {
        return;
    }

    self->manager = mgr;
    self->is_low_active = false;
    self->is_high_active = false;
    self->comm_fault_active = false;

    alarm_eval_voltage_low_init(&self->eval_low, nominal_v, dev_pct, hyst_v);
    alarm_eval_voltage_high_init(&self->eval_high, nominal_v, dev_pct, hyst_v);
}

void adapter_alarm_detector_volt_process_reading(adapter_alarm_detector_volt_t *self,
                                                 const energy_reading_t *reading)
{
    if (self == NULL || self->manager == NULL || reading == NULL) {
        return;
    }

    if (!reading->is_valid) {
        /* Meter communication invalid; assert communication alarm and freeze voltage state */
        if (!self->comm_fault_active) {
            self->comm_fault_active = true;
            (void)ao_alarm_manager_post_condition(self->manager,
                                                  ALARM_ID_ENERGY_METER_COMM_FAULT,
                                                  true, 0.0f);
        }
        return;
    }

    /* Clear communication fault if previously active */
    if (self->comm_fault_active) {
        self->comm_fault_active = false;
        (void)ao_alarm_manager_post_condition(self->manager,
                                              ALARM_ID_ENERGY_METER_COMM_FAULT,
                                              false, 0.0f);
    }

    /* Find min and max phase voltages for accurate trigger reporting */
    float min_v = reading->voltage_v[0];
    float max_v = reading->voltage_v[0];
    for (size_t p = 1; p < ENERGY_PHASE_MAX; p++) {
        if (reading->voltage_v[p] < min_v) {
            min_v = reading->voltage_v[p];
        }
        if (reading->voltage_v[p] > max_v) {
            max_v = reading->voltage_v[p];
        }
    }

    /* Evaluate Under-Voltage (< 220V - 8% with hysteresis) */
    bool is_low = alarm_eval_voltage_low_update(&self->eval_low, reading->voltage_v, ENERGY_PHASE_MAX);
    if (is_low != self->is_low_active) {
        self->is_low_active = is_low;
        (void)ao_alarm_manager_post_condition(self->manager,
                                              ALARM_ID_LINE_UNDER_VOLTAGE,
                                              is_low, min_v);
    }

    /* Evaluate Over-Voltage (> 220V + 8% with hysteresis) */
    bool is_high = alarm_eval_voltage_high_update(&self->eval_high, reading->voltage_v, ENERGY_PHASE_MAX);
    if (is_high != self->is_high_active) {
        self->is_high_active = is_high;
        (void)ao_alarm_manager_post_condition(self->manager,
                                              ALARM_ID_LINE_OVER_VOLTAGE,
                                              is_high, max_v);
    }
}
