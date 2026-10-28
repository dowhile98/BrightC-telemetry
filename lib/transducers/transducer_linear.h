/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * General Linear Transducer (y = m*x + b) for generic analog sensors (e.g. 0-3.3V humidity).
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef LIB_TRANSDUCER_LINEAR_H_
#define LIB_TRANSDUCER_LINEAR_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x1; /**< Raw value 1 (e.g. 0.0 counts or 0.0 V) */
    float y1; /**< Physical value 1 (e.g. 0.0 %) */
    float x2; /**< Raw value 2 (e.g. 65535.0 counts or 3.3 V) */
    float y2; /**< Physical value 2 (e.g. 100.0 %) */
} linear_transducer_cfg_t;

/**
 * @brief Convert raw value to physical value using 2-point linear interpolation.
 */
int transducer_linear_convert(const linear_transducer_cfg_t *cfg, float raw_x, float *out_y);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TRANSDUCER_LINEAR_H_ */
