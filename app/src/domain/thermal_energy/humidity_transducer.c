/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file humidity_transducer.c
 * @brief 4-20 mA NAMUR NE43 Return Relative Humidity Transducer Driver.
 */

#include "domain/thermal_energy/humidity_transducer.h"

#include <math.h>

quality_status_t humidity_transducer_process(float current_ma, float *out_rh_pct)
{
    if (out_rh_pct == NULL) {
        return QUALITY_FAILED;
    }

    if (isnan(current_ma) || isinf(current_ma)) {
        *out_rh_pct = 0.0f;
        return QUALITY_FAILED;
    }

    /* NAMUR NE43 loop diagnostics */
    if (current_ma < HUMIDITY_LOOP_BREAK_THRESHOLD_MA) {
        *out_rh_pct = 0.0f;
        return QUALITY_FAILED;
    }

    if (current_ma < HUMIDITY_LOOP_ZERO_SCALE_MA) {
        *out_rh_pct = 0.0f;
        return QUALITY_SUSPECT;
    }

    if (current_ma <= HUMIDITY_LOOP_FULL_SCALE_MA) {
        float rh = ((current_ma - HUMIDITY_LOOP_ZERO_SCALE_MA) /
                    (HUMIDITY_LOOP_FULL_SCALE_MA - HUMIDITY_LOOP_ZERO_SCALE_MA)) * 100.0f;
        if (rh < 0.0f) {
            rh = 0.0f;
        } else if (rh > 100.0f) {
            rh = 100.0f;
        }
        *out_rh_pct = rh;
        return QUALITY_VALID;
    }

    if (current_ma <= HUMIDITY_LOOP_OVERCURRENT_THRESHOLD_MA) {
        *out_rh_pct = 100.0f;
        return QUALITY_SUSPECT;
    }

    /* Over-current fault: current_ma > 20.5 mA */
    *out_rh_pct = 100.0f;
    return QUALITY_FAILED;
}

quality_status_t humidity_transducer_from_adc(uint16_t adc_counts,
                                             float shunt_resistance_ohms,
                                             float *out_current_ma,
                                             float *out_rh_pct)
{
    if (shunt_resistance_ohms <= 0.0f) {
        if (out_current_ma != NULL) {
            *out_current_ma = 0.0f;
        }
        if (out_rh_pct != NULL) {
            *out_rh_pct = 0.0f;
        }
        return QUALITY_FAILED;
    }

    float v_adc = ((float)adc_counts) * (HUMIDITY_ADC_VREF_VOLTS / HUMIDITY_ADC_16BIT_FULL_SCALE_COUNTS);
    float current_ma = (v_adc / shunt_resistance_ohms) * 1000.0f;

    if (out_current_ma != NULL) {
        *out_current_ma = current_ma;
    }

    return humidity_transducer_process(current_ma, out_rh_pct);
}
