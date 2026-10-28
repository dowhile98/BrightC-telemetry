/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * 4-20 mA Current Loop Transducer & NAMUR NE43 Diagnostic.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef LIB_TRANSDUCER_CURRENT_LOOP_H_
#define LIB_TRANSDUCER_CURRENT_LOOP_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** NAMUR NE43 Thresholds in Milliamperes */
#define NAMUR_NE43_FAIL_LOW_MAX_MA    3.6f
#define NAMUR_NE43_UNDER_RANGE_MAX_MA 3.8f
#define NAMUR_NE43_OVER_RANGE_MIN_MA  20.5f
#define NAMUR_NE43_FAIL_HIGH_MIN_MA   21.0f

typedef enum {
    NAMUR_STATUS_NORMAL = 0,
    NAMUR_STATUS_UNDER_RANGE,
    NAMUR_STATUS_OVER_RANGE,
    NAMUR_STATUS_FAILURE_LOW_WIRE_BREAK,
    NAMUR_STATUS_FAILURE_HIGH_SHORT_CIRCUIT
} namur_ne43_status_t;

typedef struct {
    float shunt_ohms; /**< Shunt resistor in Ohms (e.g. 220.0f) */
    float gain;       /**< Circuit gain (e.g. 1.98f) */
    float v_ref;      /**< ADC reference voltage (e.g. 3.3f) */
    float adc_max;    /**< Full scale ADC counts (e.g. 65535.0f) */
    float scale_factor; /**< Prescaler factor (e.g. 3.0f in legacy circuit) */
} current_loop_circuit_cfg_t;

typedef struct {
    float ma_at_min;   /**< Current at minimum physical value (usually 4.0 mA) */
    float physical_min;/**< Minimum physical value (e.g. 0.0 Bar, -14.5 Bar) */
    float ma_at_max;   /**< Current at maximum physical value (usually 20.0 mA) */
    float physical_max;/**< Maximum physical value (e.g. 600.0 Bar, 800.0 Bar) */
} current_loop_scale_cfg_t;

/**
 * @brief Initialize default circuit parameters for BrightC V3 board.
 */
void transducer_current_loop_get_brightc_v3_default_config(current_loop_circuit_cfg_t *cfg);

/**
 * @brief Convert raw ADC counts to loop current in milliamperes.
 */
int transducer_current_loop_adc_to_ma(const current_loop_circuit_cfg_t *cfg, float adc_raw, float *out_ma);

/**
 * @brief Evaluate loop current against NAMUR NE43 standard.
 */
namur_ne43_status_t transducer_current_loop_diagnose_namur(float current_ma);

/**
 * @brief Scale loop current (mA) to engineering units (e.g. Bar, meters, PSI).
 */
int transducer_current_loop_ma_to_physical(const current_loop_scale_cfg_t *scale_cfg, float current_ma, float *out_physical);

/**
 * @brief Direct conversion from ADC counts to physical unit with NAMUR check.
 */
int transducer_current_loop_adc_to_physical(const current_loop_circuit_cfg_t *circ_cfg,
                                            const current_loop_scale_cfg_t *scale_cfg,
                                            float adc_raw,
                                            float *out_physical,
                                            namur_ne43_status_t *out_namur);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TRANSDUCER_CURRENT_LOOP_H_ */
