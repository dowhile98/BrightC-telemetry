/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "transducer_linear.h"

int transducer_linear_convert(const linear_transducer_cfg_t *cfg, float raw_x, float *out_y)
{
    if (cfg == NULL || out_y == NULL) {
        return -1;
    }
    float dx = cfg->x2 - cfg->x1;
    if (dx == 0.0f) {
        return -1;
    }
    float slope = (cfg->y2 - cfg->y1) / dx;
    *out_y = cfg->y1 + (slope * (raw_x - cfg->x1));
    return 0;
}
