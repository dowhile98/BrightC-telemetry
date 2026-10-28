/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "transducer_current_loop.h"

void transducer_current_loop_get_brightc_v3_default_config(current_loop_circuit_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->shunt_ohms = 220.0f;
    cfg->gain = 1.98f;
    cfg->v_ref = 3.3f;
    cfg->adc_max = 65535.0f;
    cfg->scale_factor = 3.0f;
}

int transducer_current_loop_adc_to_ma(const current_loop_circuit_cfg_t *cfg, float adc_raw, float *out_ma)
{
    if (out_ma == NULL) {
        return -1;
    }

    current_loop_circuit_cfg_t local_cfg;
    if (cfg == NULL) {
        transducer_current_loop_get_brightc_v3_default_config(&local_cfg);
        cfg = &local_cfg;
    }

    float denom = cfg->shunt_ohms * cfg->gain * cfg->adc_max;
    if (denom <= 0.0f) {
        return -1;
    }

    float numer = cfg->scale_factor * adc_raw * cfg->v_ref;
    *out_ma = (numer / denom) * 1000.0f;
    return 0;
}

namur_ne43_status_t transducer_current_loop_diagnose_namur(float current_ma)
{
    if (current_ma < NAMUR_NE43_FAIL_LOW_MAX_MA) {
        return NAMUR_STATUS_FAILURE_LOW_WIRE_BREAK;
    }
    if (current_ma < NAMUR_NE43_UNDER_RANGE_MAX_MA) {
        return NAMUR_STATUS_UNDER_RANGE;
    }
    if (current_ma > NAMUR_NE43_FAIL_HIGH_MIN_MA) {
        return NAMUR_STATUS_FAILURE_HIGH_SHORT_CIRCUIT;
    }
    if (current_ma > NAMUR_NE43_OVER_RANGE_MIN_MA) {
        return NAMUR_STATUS_OVER_RANGE;
    }
    return NAMUR_STATUS_NORMAL;
}

int transducer_current_loop_ma_to_physical(const current_loop_scale_cfg_t *scale_cfg, float current_ma, float *out_physical)
{
    if (scale_cfg == NULL || out_physical == NULL) {
        return -1;
    }

    float delta_ma = scale_cfg->ma_at_max - scale_cfg->ma_at_min;
    if (delta_ma == 0.0f) {
        return -1;
    }

    float slope = (scale_cfg->physical_max - scale_cfg->physical_min) / delta_ma;
    *out_physical = scale_cfg->physical_min + (slope * (current_ma - scale_cfg->ma_at_min));
    return 0;
}

int transducer_current_loop_adc_to_physical(const current_loop_circuit_cfg_t *circ_cfg,
                                            const current_loop_scale_cfg_t *scale_cfg,
                                            float adc_raw,
                                            float *out_physical,
                                            namur_ne43_status_t *out_namur)
{
    float ma = 0.0f;
    int err = transducer_current_loop_adc_to_ma(circ_cfg, adc_raw, &ma);
    if (err) {
        return err;
    }

    namur_ne43_status_t status = transducer_current_loop_diagnose_namur(ma);
    if (out_namur != NULL) {
        *out_namur = status;
    }

    if (scale_cfg != NULL && out_physical != NULL) {
        return transducer_current_loop_ma_to_physical(scale_cfg, ma, out_physical);
    }

    return 0;
}
