/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file humidity_transducer.h
 * @brief 4-20 mA NAMUR NE43 Return Relative Humidity Transducer Driver.
 *
 * Pure C99/C11 Domain Layer:
 * - Zero Zephyr RTOS dependencies
 * - Zero hardware register dependencies
 * - Zero dynamic memory allocation
 */

#ifndef APP_DOMAIN_THERMAL_ENERGY_HUMIDITY_TRANSDUCER_H_
#define APP_DOMAIN_THERMAL_ENERGY_HUMIDITY_TRANSDUCER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Physical Constants & NAMUR NE43 Thresholds                                */
/* ========================================================================= */

#define HUMIDITY_LOOP_BREAK_THRESHOLD_MA       (3.5f)
#define HUMIDITY_LOOP_ZERO_SCALE_MA            (4.0f)
#define HUMIDITY_LOOP_FULL_SCALE_MA            (20.0f)
#define HUMIDITY_LOOP_OVERCURRENT_THRESHOLD_MA (20.5f)
#define HUMIDITY_DEFAULT_SHUNT_RESISTANCE_OHMS (150.0f)
#define HUMIDITY_ADC_16BIT_FULL_SCALE_COUNTS   (65536.0f)
#define HUMIDITY_ADC_VREF_VOLTS                (3.3f)

/* ========================================================================= */
/* Public Domain Functions                                                   */
/* ========================================================================= */

/**
 * @brief Process 4-20 mA current loop reading for return air humidity according to NAMUR NE43.
 *
 * Diagnostics:
 * - I < 3.5 mA: Loop break fault -> returns QUALITY_FAILED, sets *out_rh_pct = 0.0f
 * - 3.5 <= I < 4.0 mA: Deadband clamp -> returns QUALITY_SUSPECT, sets *out_rh_pct = 0.0f
 * - 4.0 <= I <= 20.0 mA: Normal linear -> returns QUALITY_VALID, sets *out_rh_pct = ((I - 4.0f) / 16.0f) * 100.0f
 * - 20.0 < I <= 20.5 mA: Saturation clamp -> returns QUALITY_SUSPECT, sets *out_rh_pct = 100.0f
 * - I > 20.5 mA: Overcurrent fault -> returns QUALITY_FAILED, sets *out_rh_pct = 100.0f
 *
 * @param[in]  current_ma Measured loop current in mA.
 * @param[out] out_rh_pct Output relative humidity in percentage (0.0 to 100.0%).
 * @return Quality status of the measurement.
 */
quality_status_t humidity_transducer_process(float current_ma, float *out_rh_pct);

/**
 * @brief Convert raw ADC counts across a shunt resistor into loop current and relative humidity.
 *
 * Uses 16-bit counts with 3.3V full scale:
 * V = counts * (3.3f / 65536.0f)
 * I = (V / R_shunt) * 1000.0f [mA]
 *
 * @param[in]  adc_counts            16-bit raw ADC counts (0..65535).
 * @param[in]  shunt_resistance_ohms Shunt resistance in Ohms (nominal 150.0 Ohms).
 * @param[out] out_current_ma        Optional output pointer for calculated current in mA (may be NULL).
 * @param[out] out_rh_pct            Output pointer for calculated relative humidity in %.
 * @return Quality status of the measurement.
 */
quality_status_t humidity_transducer_from_adc(uint16_t adc_counts,
                                             float shunt_resistance_ohms,
                                             float *out_current_ma,
                                             float *out_rh_pct);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_THERMAL_ENERGY_HUMIDITY_TRANSDUCER_H_ */
