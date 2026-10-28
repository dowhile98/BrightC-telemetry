/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTD PT100/PT1000 Temperature Transducer & Callendar-Van Dusen equation.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef LIB_TRANSDUCER_RTD_H_
#define LIB_TRANSDUCER_RTD_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Callendar-Van Dusen IEC 60751 coefficients */
#define RTD_IEC60751_A  3.9083E-3f
#define RTD_IEC60751_B -5.775E-7f
#define RTD_IEC60751_C -4.183E-12f

/** Default BrightC V3 hardware circuit parameters */
#define BRIGHTC_RTD_GAIN_DEFAULT 80.8061f
#define BRIGHTC_RTD_VBIAS_DEFAULT 0.0579f
#define BRIGHTC_RTD_RREF_DEFAULT 5100.0f
#define BRIGHTC_RTD_VREF_DEFAULT 3.3f
#define BRIGHTC_RTD_ADC_MAX_COUNTS 65535.0f

typedef struct {
    float r0;        /**< Nominal resistance at 0°C (e.g. 100.0f for PT100, 1000.0f for PT1000) */
    float r_ref;     /**< Reference resistor value in Ohms (e.g. 5100.0f) */
    float gain;      /**< Amplifier gain */
    float v_bias;    /**< Amplifier bias offset */
    float v_ref;     /**< ADC reference voltage (e.g. 3.3f) */
    float adc_max;   /**< Full scale ADC counts (e.g. 65535.0f for 16-bit) */
} rtd_circuit_config_t;

/**
 * @brief Initialize default configuration for BrightC V3 board (PT100).
 */
void transducer_rtd_get_brightc_v3_default_config(rtd_circuit_config_t *cfg);

/**
 * @brief Convert raw ADC reading to RTD resistance in Ohms.
 *
 * @param cfg Circuit configuration parameters (NULL uses BrightC V3 defaults).
 * @param adc_raw Raw ADC counts.
 * @param out_ohms Pointer to output resistance in Ohms.
 * @return 0 on success, -1 if circuit open/saturated.
 */
int transducer_rtd_adc_to_ohms(const rtd_circuit_config_t *cfg, float adc_raw, float *out_ohms);

/**
 * @brief Convert RTD resistance in Ohms to Temperature in Celsius (Callendar-Van Dusen).
 *
 * @param r0 Nominal resistance at 0°C (usually 100.0f for PT100).
 * @param rtd_ohms Measured RTD resistance.
 * @param out_temp_c Pointer to output temperature in degrees Celsius.
 * @return 0 on success, -1 if resistance is invalid.
 */
int transducer_rtd_ohms_to_celsius(float r0, float rtd_ohms, float *out_temp_c);

/**
 * @brief Combined helper: Convert raw ADC reading directly to Celsius.
 */
int transducer_rtd_adc_to_celsius(const rtd_circuit_config_t *cfg, float adc_raw, float *out_temp_c);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TRANSDUCER_RTD_H_ */
