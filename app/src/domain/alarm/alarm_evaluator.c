/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_evaluator.c
 * @brief Implementation of Pure Domain Threshold & Hysteresis Algorithms.
 */

#include "domain/alarm/alarm_evaluator.h"

/*============================================================================*
 * LOW TEMPERATURE
 *============================================================================*/

void alarm_eval_temp_low_init(alarm_eval_temp_low_t *self, float threshold_c, float hysteresis_c)
{
    if (self == NULL) {
        return;
    }
    self->threshold_c = threshold_c;
    self->hysteresis_c = (hysteresis_c > 0.0f) ? hysteresis_c : 1.0f;
    self->is_in_alarm = false;
}

bool alarm_eval_temp_low_update(alarm_eval_temp_low_t *self, float current_temp_c)
{
    if (self == NULL) {
        return false;
    }

    if (!self->is_in_alarm) {
        /* Trip condition: strictly below threshold */
        if (current_temp_c < self->threshold_c) {
            self->is_in_alarm = true;
        }
    } else {
        /* Recovery condition: must rise above threshold + hysteresis */
        if (current_temp_c >= (self->threshold_c + self->hysteresis_c)) {
            self->is_in_alarm = false;
        }
    }

    return self->is_in_alarm;
}

/*============================================================================*
 * HIGH TEMPERATURE
 *============================================================================*/

void alarm_eval_temp_high_init(alarm_eval_temp_high_t *self, float threshold_c, float hysteresis_c)
{
    if (self == NULL) {
        return;
    }
    self->threshold_c = threshold_c;
    self->hysteresis_c = (hysteresis_c > 0.0f) ? hysteresis_c : 1.0f;
    self->is_in_alarm = false;
}

bool alarm_eval_temp_high_update(alarm_eval_temp_high_t *self, float current_temp_c)
{
    if (self == NULL) {
        return false;
    }

    if (!self->is_in_alarm) {
        /* Trip condition: strictly above threshold */
        if (current_temp_c > self->threshold_c) {
            self->is_in_alarm = true;
        }
    } else {
        /* Recovery condition: must drop below threshold - hysteresis */
        if (current_temp_c <= (self->threshold_c - self->hysteresis_c)) {
            self->is_in_alarm = false;
        }
    }

    return self->is_in_alarm;
}

/*============================================================================*
 * 3-PHASE LINE UNDER-VOLTAGE
 *============================================================================*/

void alarm_eval_voltage_low_init(alarm_eval_voltage_low_t *self, float nominal_v, float deviation_pct, float hysteresis_v)
{
    if (self == NULL) {
        return;
    }
    self->nominal_v = (nominal_v > 0.0f) ? nominal_v : 220.0f;
    self->deviation_pct = (deviation_pct > 0.0f) ? deviation_pct : 8.0f;
    self->hysteresis_v = (hysteresis_v > 0.0f) ? hysteresis_v : 2.0f;
    self->is_in_alarm = false;
}

bool alarm_eval_voltage_low_update(alarm_eval_voltage_low_t *self, const float voltages[3], size_t phase_count)
{
    if (self == NULL || voltages == NULL || phase_count == 0) {
        return false;
    }

    float trip_voltage = self->nominal_v * (1.0f - (self->deviation_pct / 100.0f));
    float clear_voltage = trip_voltage + self->hysteresis_v;

    size_t n = (phase_count <= 3) ? phase_count : 3;

    if (!self->is_in_alarm) {
        /* Trip if ANY line falls below trip voltage */
        for (size_t i = 0; i < n; i++) {
            if (voltages[i] > 10.0f && voltages[i] < trip_voltage) { /* ignore disconnected 0V test phase */
                self->is_in_alarm = true;
                break;
            }
        }
    } else {
        /* Clear if ALL valid active lines recover above clear voltage */
        bool all_recovered = true;
        for (size_t i = 0; i < n; i++) {
            if (voltages[i] > 10.0f && voltages[i] < clear_voltage) {
                all_recovered = false;
                break;
            }
        }
        if (all_recovered) {
            self->is_in_alarm = false;
        }
    }

    return self->is_in_alarm;
}

/*============================================================================*
 * 3-PHASE LINE OVER-VOLTAGE
 *============================================================================*/

void alarm_eval_voltage_high_init(alarm_eval_voltage_high_t *self, float nominal_v, float deviation_pct, float hysteresis_v)
{
    if (self == NULL) {
        return;
    }
    self->nominal_v = (nominal_v > 0.0f) ? nominal_v : 220.0f;
    self->deviation_pct = (deviation_pct > 0.0f) ? deviation_pct : 8.0f;
    self->hysteresis_v = (hysteresis_v > 0.0f) ? hysteresis_v : 2.0f;
    self->is_in_alarm = false;
}

bool alarm_eval_voltage_high_update(alarm_eval_voltage_high_t *self, const float voltages[3], size_t phase_count)
{
    if (self == NULL || voltages == NULL || phase_count == 0) {
        return false;
    }

    float trip_voltage = self->nominal_v * (1.0f + (self->deviation_pct / 100.0f));
    float clear_voltage = trip_voltage - self->hysteresis_v;

    size_t n = (phase_count <= 3) ? phase_count : 3;

    if (!self->is_in_alarm) {
        /* Trip if ANY line exceeds trip voltage */
        for (size_t i = 0; i < n; i++) {
            if (voltages[i] > trip_voltage) {
                self->is_in_alarm = true;
                break;
            }
        }
    } else {
        /* Clear if ALL active lines drop below clear voltage */
        bool all_recovered = true;
        for (size_t i = 0; i < n; i++) {
            if (voltages[i] > clear_voltage) {
                all_recovered = false;
                break;
            }
        }
        if (all_recovered) {
            self->is_in_alarm = false;
        }
    }

    return self->is_in_alarm;
}
