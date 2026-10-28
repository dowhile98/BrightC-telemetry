/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "transducer_rtd.h"
#include <math.h>

void transducer_rtd_get_brightc_v3_default_config(rtd_circuit_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->r0 = 100.0f;
    cfg->r_ref = BRIGHTC_RTD_RREF_DEFAULT;
    cfg->gain = BRIGHTC_RTD_GAIN_DEFAULT;
    cfg->v_bias = BRIGHTC_RTD_VBIAS_DEFAULT;
    cfg->v_ref = BRIGHTC_RTD_VREF_DEFAULT;
    cfg->adc_max = BRIGHTC_RTD_ADC_MAX_COUNTS;
}

int transducer_rtd_adc_to_ohms(const rtd_circuit_config_t *cfg, float adc_raw, float *out_ohms)
{
    if (out_ohms == NULL) {
        return -1;
    }

    rtd_circuit_config_t local_cfg;
    if (cfg == NULL) {
        transducer_rtd_get_brightc_v3_default_config(&local_cfg);
        cfg = &local_cfg;
    }

    if (cfg->gain <= 0.0f || cfg->adc_max <= 0.0f) {
        return -1;
    }

    float a = ((cfg->v_ref / cfg->adc_max) * adc_raw) / cfg->gain + cfg->v_bias;
    if (a >= cfg->v_ref || (cfg->v_ref - a) <= 1e-6f) {
        *out_ohms = 9999.0f; /* Saturated / open circuit */
        return -1;
    }

    float rtd = (a * cfg->r_ref) / (cfg->v_ref - a);
    *out_ohms = rtd;
    return 0;
}

int transducer_rtd_ohms_to_celsius(float r0, float rtd_ohms, float *out_temp_c)
{
    if (out_temp_c == NULL || r0 <= 0.0f || rtd_ohms <= 0.0f) {
        return -1;
    }

    /* Inverse Callendar-Van Dusen for T >= 0°C:
     * R(T) = R0 * (1 + A*T + B*T^2)
     * Discriminant = A^2 - 4*B*(1 - R/R0)
     */
    float discriminant = (RTD_IEC60751_A * RTD_IEC60751_A) - 
                         (4.0f * RTD_IEC60751_B * (1.0f - (rtd_ohms / r0)));

    if (discriminant < 0.0f) {
        *out_temp_c = -999.0f;
        return -1;
    }

    *out_temp_c = (-RTD_IEC60751_A + sqrtf(discriminant)) / (2.0f * RTD_IEC60751_B);
    return 0;
}

int transducer_rtd_adc_to_celsius(const rtd_circuit_config_t *cfg, float adc_raw, float *out_temp_c)
{
    float ohms = 0.0f;
    int err = transducer_rtd_adc_to_ohms(cfg, adc_raw, &ohms);
    if (err) {
        return err;
    }
    float r0 = (cfg != NULL) ? cfg->r0 : 100.0f;
    return transducer_rtd_ohms_to_celsius(r0, ohms, out_temp_c);
}
