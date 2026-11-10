/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_evaluator.h
 * @brief Pure Domain Evaluator: Threshold & Hysteresis Algorithms.
 *
 * ISO C99 only. Zero Zephyr RTOS or vendor hardware dependencies.
 * Encapsulates stateful evaluation for:
 * - Low Temperature (Trip: T < Threshold; Clear: T >= Threshold + Hysteresis)
 * - High Temperature (Trip: T > Threshold; Clear: T <= Threshold - Hysteresis)
 * - 3-Phase AC Line Under-Voltage (Trip: V < V_trip; Clear: V >= V_trip + Hysteresis)
 * - 3-Phase AC Line Over-Voltage (Trip: V > V_trip; Clear: V <= V_trip - Hysteresis)
 */

#ifndef APP_DOMAIN_ALARM_EVALUATOR_H_
#define APP_DOMAIN_ALARM_EVALUATOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Low temperature evaluator context.
 */
typedef struct {
    float threshold_c;   /**< Low temperature trip threshold in degC */
    float hysteresis_c;  /**< Recovery hysteresis band in degC */
    bool  is_in_alarm;   /**< Current internal evaluation state */
} alarm_eval_temp_low_t;

/**
 * @brief High temperature evaluator context.
 */
typedef struct {
    float threshold_c;   /**< High temperature trip threshold in degC */
    float hysteresis_c;  /**< Recovery hysteresis band in degC */
    bool  is_in_alarm;   /**< Current internal evaluation state */
} alarm_eval_temp_high_t;

/**
 * @brief 3-Phase Line Under-Voltage evaluator context.
 */
typedef struct {
    float nominal_v;     /**< Nominal line voltage (e.g. 220.0V) */
    float deviation_pct; /**< Allowed drop percentage (e.g. 8.0%) */
    float hysteresis_v;  /**< Recovery hysteresis (e.g. 2.0V) */
    bool  is_in_alarm;   /**< Current internal evaluation state */
} alarm_eval_voltage_low_t;

/**
 * @brief 3-Phase Line Over-Voltage evaluator context.
 */
typedef struct {
    float nominal_v;     /**< Nominal line voltage (e.g. 220.0V) */
    float deviation_pct; /**< Allowed surge percentage (e.g. 8.0%) */
    float hysteresis_v;  /**< Recovery hysteresis (e.g. 2.0V) */
    bool  is_in_alarm;   /**< Current internal evaluation state */
} alarm_eval_voltage_high_t;

/*============================================================================*
 * LOW TEMPERATURE EVALUATOR APIS
 *============================================================================*/

void alarm_eval_temp_low_init(alarm_eval_temp_low_t *self, float threshold_c, float hysteresis_c);

bool alarm_eval_temp_low_update(alarm_eval_temp_low_t *self, float current_temp_c);

/*============================================================================*
 * HIGH TEMPERATURE EVALUATOR APIS
 *============================================================================*/

void alarm_eval_temp_high_init(alarm_eval_temp_high_t *self, float threshold_c, float hysteresis_c);

bool alarm_eval_temp_high_update(alarm_eval_temp_high_t *self, float current_temp_c);

/*============================================================================*
 * 3-PHASE LINE VOLTAGE EVALUATOR APIS
 *============================================================================*/

void alarm_eval_voltage_low_init(alarm_eval_voltage_low_t *self, float nominal_v, float deviation_pct, float hysteresis_v);

bool alarm_eval_voltage_low_update(alarm_eval_voltage_low_t *self, const float voltages[3], size_t phase_count);

void alarm_eval_voltage_high_init(alarm_eval_voltage_high_t *self, float nominal_v, float deviation_pct, float hysteresis_v);

bool alarm_eval_voltage_high_update(alarm_eval_voltage_high_t *self, const float voltages[3], size_t phase_count);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_ALARM_EVALUATOR_H_ */
