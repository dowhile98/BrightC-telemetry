/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "transducer_voltage_divider.h"

int transducer_voltage_divider_adc_to_volts(const voltage_divider_cfg_t *cfg, float adc_raw, float *out_volts)
{
    if (cfg == NULL || out_volts == NULL) {
        return -1;
    }
    if (cfg->r_low_ohms <= 0.0f || cfg->adc_max <= 0.0f) {
        return -1;
    }

    float v_pin = (adc_raw / cfg->adc_max) * cfg->v_ref;
    float divider_ratio = (cfg->r_high_ohms + cfg->r_low_ohms) / cfg->r_low_ohms;
    *out_volts = v_pin * divider_ratio;
    return 0;
}
