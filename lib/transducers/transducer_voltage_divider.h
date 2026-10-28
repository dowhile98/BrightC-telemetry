/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Resistor Voltage Divider Transducer for DC Voltage Monitoring.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef LIB_TRANSDUCER_VOLTAGE_DIVIDER_H_
#define LIB_TRANSDUCER_VOLTAGE_DIVIDER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float r_high_ohms; /**< High-side resistor in Ohms (connected to input voltage) */
    float r_low_ohms;  /**< Low-side resistor in Ohms (connected to ground) */
    float v_ref;       /**< ADC reference voltage (e.g. 3.3f) */
    float adc_max;     /**< Full scale ADC counts (e.g. 65535.0f) */
} voltage_divider_cfg_t;

/**
 * @brief Convert raw ADC counts to input bus voltage.
 *
 * Vin = Vpin * (R_high + R_low) / R_low
 * where Vpin = (adc_raw / adc_max) * v_ref
 */
int transducer_voltage_divider_adc_to_volts(const voltage_divider_cfg_t *cfg, float adc_raw, float *out_volts);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TRANSDUCER_VOLTAGE_DIVIDER_H_ */
