/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Median Filter for Impulse Noise and Spike Rejection.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef LIB_FILTER_MEDIAN_H_
#define LIB_FILTER_MEDIAN_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_MEDIAN_MAX_WINDOW 9U
#define FILTER_MEDIAN_MIN_WINDOW 3U

/**
 * @brief Median filter state structure.
 */
typedef struct {
    float buffer[FILTER_MEDIAN_MAX_WINDOW];
    uint8_t window_size; /**< Odd number between 3 and 9 */
    uint8_t write_idx;   /**< Circular write pointer */
    uint8_t count;       /**< Number of samples gathered so far */
} filter_median_t;

/**
 * @brief Initialize a median filter instance.
 *
 * @param f Pointer to filter state.
 * @param window_size Window size (must be odd, between 3 and 9).
 * @return 0 on success, -1 on invalid parameter.
 */
int filter_median_init(filter_median_t *f, uint8_t window_size);

/**
 * @brief Update median filter with a new sample.
 *
 * @param f Pointer to filter state.
 * @param sample New sample value.
 * @return Filtered median value.
 */
float filter_median_update(filter_median_t *f, float sample);

/**
 * @brief Reset median filter state.
 *
 * @param f Pointer to filter state.
 */
void filter_median_reset(filter_median_t *f);

#ifdef __cplusplus
}
#endif

#endif /* LIB_FILTER_MEDIAN_H_ */
