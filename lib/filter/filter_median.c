/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "filter_median.h"

int filter_median_init(filter_median_t *f, uint8_t window_size)
{
    if (f == NULL) {
        return -1;
    }

    /* Force window size to be odd and clamped between MIN and MAX */
    if (window_size < FILTER_MEDIAN_MIN_WINDOW) {
        window_size = FILTER_MEDIAN_MIN_WINDOW;
    } else if (window_size > FILTER_MEDIAN_MAX_WINDOW) {
        window_size = FILTER_MEDIAN_MAX_WINDOW;
    }

    if ((window_size % 2U) == 0U) {
        window_size++; /* Make odd */
        if (window_size > FILTER_MEDIAN_MAX_WINDOW) {
            window_size -= 2U;
        }
    }

    f->window_size = window_size;
    f->write_idx = 0U;
    f->count = 0U;

    for (uint8_t i = 0U; i < FILTER_MEDIAN_MAX_WINDOW; i++) {
        f->buffer[i] = 0.0f;
    }

    return 0;
}

void filter_median_reset(filter_median_t *f)
{
    if (f == NULL) {
        return;
    }
    f->write_idx = 0U;
    f->count = 0U;
    for (uint8_t i = 0U; i < FILTER_MEDIAN_MAX_WINDOW; i++) {
        f->buffer[i] = 0.0f;
    }
}

float filter_median_update(filter_median_t *f, float sample)
{
    if (f == NULL) {
        return sample;
    }

    /* Insert new sample into circular buffer */
    f->buffer[f->write_idx] = sample;
    f->write_idx = (f->write_idx + 1U) % f->window_size;
    if (f->count < f->window_size) {
        f->count++;
    }

    /* Copy active samples to temp array for sorting */
    float sorted[FILTER_MEDIAN_MAX_WINDOW];
    uint8_t n = f->count;
    for (uint8_t i = 0U; i < n; i++) {
        sorted[i] = f->buffer[i];
    }

    /* Insertion sort */
    for (uint8_t i = 1U; i < n; i++) {
        float key = sorted[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    /* Return middle element */
    return sorted[n / 2U];
}
