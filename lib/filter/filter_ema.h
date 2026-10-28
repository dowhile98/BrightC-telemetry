/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Exponential Moving Average (EMA) Digital Low-Pass Filter.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef LIB_FILTER_EMA_H_
#define LIB_FILTER_EMA_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief EMA filter state structure.
 */
typedef struct {
    float alpha;         /**< Smoothing factor (0.0 < alpha <= 1.0) */
    float filtered_val;  /**< Current filtered output value */
    bool is_initialized; /**< True if initial sample has been processed */
} filter_ema_t;

/**
 * @brief Initialize the EMA filter.
 *
 * @param f Pointer to filter state.
 * @param alpha Smoothing factor (0.0 < alpha <= 1.0).
 */
static inline void filter_ema_init(filter_ema_t *f, float alpha)
{
    if (f == NULL) {
        return;
    }
    if (alpha <= 0.0f) {
        f->alpha = 0.01f;
    } else if (alpha > 1.0f) {
        f->alpha = 1.0f;
    } else {
        f->alpha = alpha;
    }
    f->filtered_val = 0.0f;
    f->is_initialized = false;
}

/**
 * @brief Update the EMA filter with a new sample.
 *
 * @param f Pointer to filter state.
 * @param sample New measurement sample.
 * @return Filtered output value.
 */
static inline float filter_ema_update(filter_ema_t *f, float sample)
{
    if (f == NULL) {
        return sample;
    }
    if (!f->is_initialized) {
        f->filtered_val = sample;
        f->is_initialized = true;
        return sample;
    }
    f->filtered_val = (f->alpha * sample) + ((1.0f - f->alpha) * f->filtered_val);
    return f->filtered_val;
}

/**
 * @brief Reset the filter state.
 *
 * @param f Pointer to filter state.
 */
static inline void filter_ema_reset(filter_ema_t *f)
{
    if (f != NULL) {
        f->filtered_val = 0.0f;
        f->is_initialized = false;
    }
}

/**
 * @brief Get the current filtered value without updating.
 *
 * @param f Pointer to filter state.
 * @return Current filtered value.
 */
static inline float filter_ema_get(const filter_ema_t *f)
{
    return (f != NULL) ? f->filtered_val : 0.0f;
}

#ifdef __cplusplus
}
#endif

#endif /* LIB_FILTER_EMA_H_ */
