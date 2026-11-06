/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thermodynamic and Psychrometric Engine (Revision F Engineering Baseline).
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies / Zero Dynamic Memory.
 */

#include "thermodynamic_engine.h"

#include <errno.h>
#include <math.h>
#include <string.h>

/* ========================================================================= */
/* Static Constant Tables (Stored in Flash / ROM)                            */
/* ========================================================================= */

/**
 * @brief 46-point Flash LUT for R410A dew-point saturation temperature (Change F-02).
 * Spans 0.0 to 45.0 bar gauge in 1.0 bar increments (NIST REFPROP standard).
 */
const float R410A_TSAT_LUT[THERMO_R410A_LUT_POINTS] = {
    -51.40f, -37.50f, -27.80f, -20.20f, -13.90f, /* 0 .. 4 bar */
     -8.40f,  -3.60f,   0.80f,   4.80f,   8.50f, /* 5 .. 9 bar */
     11.90f,  15.10f,  18.10f,  20.90f,  23.60f, /* 10 .. 14 bar */
     26.10f,  28.60f,  30.90f,  33.10f,  35.30f, /* 15 .. 19 bar */
     37.30f,  39.30f,  41.30f,  43.10f,  45.00f, /* 20 .. 24 bar */
     46.80f,  48.50f,  50.20f,  51.90f,  53.50f, /* 25 .. 29 bar */
     55.10f,  56.70f,  58.20f,  59.70f,  61.20f, /* 30 .. 34 bar */
     62.60f,  64.00f,  65.40f,  66.80f,  68.10f, /* 35 .. 39 bar */
     69.40f,  70.70f,  72.00f,  73.30f,  74.50f, /* 40 .. 44 bar */
     75.70f                                      /* 45 bar */
};

/* ========================================================================= */
/* Static Helper Functions                                                   */
/* ========================================================================= */

static inline float clamp_f(float val, float min_val, float max_val)
{
    if (val < min_val) {
        return min_val;
    }
    if (val > max_val) {
        return max_val;
    }
    return val;
}

static inline float clamp_pct(float val)
{
    return clamp_f(val, 0.0f, 100.0f);
}

/* ========================================================================= */
/* Standalone Mathematical & Psychrometric Helpers                           */
/* ========================================================================= */

float thermo_calc_p_atm_kpa(float altitude_m)
{
    if (altitude_m <= 0.0f) {
        return 101.325f;
    }
    float base = 1.0f - 2.25577e-5f * altitude_m;
    if (base < 0.01f) {
        base = 0.01f;
    }
    return 101.325f * powf(base, 5.25588f);
}

float thermo_calc_p_ws_kpa(float temp_c)
{
    float denom = temp_c + 237.3f;
    if (denom < 1.0f) {
        denom = 1.0f;
    }
    return 0.61078f * expf((17.27f * temp_c) / denom);
}

float thermo_calc_p_w_kpa(float p_ws_kpa, float rh_pct)
{
    if (p_ws_kpa <= 0.0f) {
        return 0.0f;
    }
    float rh = clamp_pct(rh_pct);
    return p_ws_kpa * (rh / 100.0f);
}

float thermo_calc_humidity_ratio(float p_w_kpa, float p_atm_kpa)
{
    if (p_w_kpa <= 0.0f) {
        return 0.0f;
    }
    float denom = p_atm_kpa - p_w_kpa;
    if (denom < 0.01f) {
        denom = 0.01f;
    }
    return 0.62198f * (p_w_kpa / denom);
}

float thermo_calc_dry_air_density(float p_atm_kpa, float p_w_kpa, float temp_c)
{
    float p_diff = p_atm_kpa - p_w_kpa;
    if (p_diff < 0.01f) {
        p_diff = 0.01f;
    }
    float t_abs = temp_c + THERMO_KELVIN_OFFSET_C;
    if (t_abs < 100.0f) {
        t_abs = 100.0f;
    }
    return p_diff / (THERMO_R_DRY_AIR_KJ_KG_K * t_abs);
}

float thermo_calc_dew_point_c(float p_w_kpa)
{
    if (p_w_kpa <= 0.001f) {
        return -50.0f;
    }
    float ratio = p_w_kpa / 0.61078f;
    if (ratio < 1.0e-4f) {
        ratio = 1.0e-4f;
    }
    float gamma = logf(ratio);
    float denom = 17.27f - gamma;
    if (fabsf(denom) < 1.0e-4f) {
        denom = 1.0e-4f;
    }
    return (237.3f * gamma) / denom;
}

float thermo_calc_carnot_cop(float t_evap_sat_k, float t_cond_sat_k)
{
    float lift = t_cond_sat_k - t_evap_sat_k;
    float denom = fmaxf(THERMO_CARNOT_MIN_LIFT_K, lift);
    return t_evap_sat_k / denom;
}

float thermo_calc_exergetic_efficiency(float cop_real, float cop_carnot)
{
    if (cop_carnot <= 0.01f || cop_real <= 0.0f) {
        return 0.0f;
    }
    return clamp_pct((cop_real / cop_carnot) * 100.0f);
}

/* ========================================================================= */
/* Phase 2 Static R410A Flash Lookup Table API (Change F-02)                 */
/* ========================================================================= */

int thermo_r410a_sat_temp_c(float p_gauge_bar, float *out_t_sat_c)
{
    if (out_t_sat_c == NULL) {
        return -EINVAL;
    }

    float p_clamped = clamp_f(p_gauge_bar, 0.0f, (float)(THERMO_R410A_LUT_POINTS - 1U));
    int i = (int)p_clamped;
    if (i >= (int)(THERMO_R410A_LUT_POINTS - 1U)) {
        i = (int)(THERMO_R410A_LUT_POINTS - 2U);
    }
    float f = p_clamped - (float)i;

    *out_t_sat_c = R410A_TSAT_LUT[i] + f * (R410A_TSAT_LUT[i + 1] - R410A_TSAT_LUT[i]);
    return 0;
}

int thermo_r410a_superheat_k(float p_gauge_bar, float t_suction_c, float *out_sh_k)
{
    if (out_sh_k == NULL) {
        return -EINVAL;
    }
    float t_sat = 0.0f;
    int rc = thermo_r410a_sat_temp_c(p_gauge_bar, &t_sat);
    if (rc != 0) {
        return rc;
    }
    *out_sh_k = t_suction_c - t_sat;
    return 0;
}

/* ========================================================================= */
/* Double-Precision Ordinary Least Squares (OLS) Solver API (Change F-03)     */
/* ========================================================================= */

int thermo_ols_init(thermo_ols_t *ols)
{
    if (ols == NULL) {
        return -EINVAL;
    }
    memset(ols, 0, sizeof(*ols));
    return 0;
}

int thermo_ols_add_sample(thermo_ols_t *ols,
                          double t_ret,
                          double t_ext,
                          double f_carga,
                          double q_total)
{
    if (ols == NULL) {
        return -EINVAL;
    }

    double x[4] = {1.0, t_ret, t_ext, f_carga};
    double y = q_total;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ols->xtx[i][j] += x[i] * x[j];
        }
        ols->xty[i] += x[i] * y;
    }
    ols->sample_count++;
    return 0;
}

int thermo_ols_solve(const thermo_ols_t *ols, double lambda_reg, double out_beta[4])
{
    if (ols == NULL || out_beta == NULL || ols->sample_count < 4U) {
        return -EINVAL;
    }

    double a[4][8];
    memset(a, 0, sizeof(a));

    /* Populate augmented matrix [ (X^T * X + lambda * I) | I ] */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            a[i][j] = ols->xtx[i][j];
        }
        a[i][i] += (lambda_reg > 0.0) ? lambda_reg : THERMO_OLS_DEFAULT_LAMBDA;
        a[i][4 + i] = 1.0;
    }

    /* Gauss-Jordan elimination with partial pivoting in 64-bit precision */
    for (int col = 0; col < 4; col++) {
        /* Pivot selection */
        int max_row = col;
        double max_val = fabs(a[col][col]);
        for (int row = col + 1; row < 4; row++) {
            double v = fabs(a[row][col]);
            if (v > max_val) {
                max_val = v;
                max_row = row;
            }
        }
        if (max_val < 1.0e-12) {
            return -EDOM; /* Ill-conditioned / singular */
        }
        if (max_row != col) {
            for (int k = 0; k < 8; k++) {
                double tmp = a[col][k];
                a[col][k] = a[max_row][k];
                a[max_row][k] = tmp;
            }
        }

        /* Scale pivot row */
        double pivot = a[col][col];
        for (int k = 0; k < 8; k++) {
            a[col][k] /= pivot;
        }

        /* Eliminate column */
        for (int row = 0; row < 4; row++) {
            if (row != col) {
                double factor = a[row][col];
                for (int k = 0; k < 8; k++) {
                    a[row][k] -= factor * a[col][k];
                }
            }
        }
    }

    /* Multiply inverted matrix A^(-1) (stored in columns 4..7) by X^T * Y */
    for (int i = 0; i < 4; i++) {
        double sum = 0.0;
        for (int j = 0; j < 4; j++) {
            sum += a[i][4 + j] * ols->xty[j];
        }
        out_beta[i] = sum;
    }

    return 0;
}

/* ========================================================================= */
/* Initialization Functions                                                  */
/* ========================================================================= */

int thermo_engine_init_default_config(thermo_config_t *cfg)
{
    if (cfg == NULL) {
        return -EINVAL;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->site_altitude_m = 0.0f;
    cfg->configured_units = 3U;
    for (uint8_t u = 0; u < HVAC_MAX_UNITS_TOTAL; u++) {
        cfg->v_nom_m3h[u] = 1200.0f;         /* 1200 m3/h typical per unit */
        cfg->q_nominal_unit_kw[u] = 10.5f;   /* ~3 Tons / 10.5 kW */
    }
    cfg->delta_t_pinch_evap_k = THERMO_DEFAULT_PINCH_EVAP_K;
    cfg->delta_t_pinch_cond_k = THERMO_DEFAULT_PINCH_COND_K;
    cfg->t_ext_design_c = THERMO_DEFAULT_T_EXT_DESIGN_C;
    cfg->k_ext_derating = THERMO_DEFAULT_K_EXT_DERATING;
    cfg->u_req = 2U; /* 2+1 redundancy */

    return 0;
}

int thermo_engine_init_default_baseline(thermo_baseline_cfg_t *baseline)
{
    if (baseline == NULL) {
        return -EINVAL;
    }

    memset(baseline, 0, sizeof(*baseline));
    for (uint8_t k = 0; k < SENSOR_MATRIX_MAX_PAIRS; k++) {
        baseline->t_inj_base_c[k] = 13.0f;
        baseline->delta_t_base_c[k] = 10.0f;
        baseline->m_t_day_c[k] = 0.0f;
    }
    baseline->eta_ii_base_pct = 40.0f;
    baseline->hours_ram_history = 0U;
    baseline->ols_valid = false;

    return 0;
}

/* ========================================================================= */
/* Core Thermodynamic Computation Cycle                                      */
/* ========================================================================= */

int thermo_engine_compute(const thermo_config_t *cfg,
                          const thermo_baseline_cfg_t *baseline,
                          const thermo_input_t *input,
                          thermo_output_t *output)
{
    if (cfg == NULL || input == NULL || output == NULL || input->matrix == NULL) {
        return -EINVAL;
    }

    const thermal_matrix_frame_t *matrix = input->matrix;
    memset(output, 0, sizeof(*output));

    /* --------------------------------------------------------------------- */
    /* 1. Atmospheric Pressure & Spatial Temperatures                        */
    /* --------------------------------------------------------------------- */
    output->p_atm_kpa = thermo_calc_p_atm_kpa(cfg->site_altitude_m);

    /* 1.1 Process Room Ambient Probes (M) */
    uint8_t m_probes = matrix->configured_room_probes;
    if (m_probes > SENSOR_MATRIX_MAX_ROOM_PROBES) {
        m_probes = SENSOR_MATRIX_MAX_ROOM_PROBES;
    }

    float t_room_sum = 0.0f;
    float t_room_max = -100.0f;
    float t_room_min = 100.0f;
    uint8_t valid_m_count = 0;

    for (uint8_t m = 0; m < m_probes; m++) {
        if ((matrix->room_valid_mask & (1U << m)) != 0U) {
            float t_val = matrix->t_room[m];
            t_room_sum += t_val;
            if (t_val > t_room_max) {
                t_room_max = t_val;
            }
            if (t_val < t_room_min) {
                t_room_min = t_val;
            }
            valid_m_count++;
        }
    }

    output->valid_room_probes_count = valid_m_count;
    if (valid_m_count > 0U) {
        output->t_room_avg_c = t_room_sum / (float)valid_m_count;
        output->t_room_max_c = t_room_max;
        output->t_room_min_c = t_room_min;
        output->delta_t_room_max_c = (valid_m_count >= 2U) ? (t_room_max - t_room_min) : 0.0f;
    }

    /* 1.2 Process Outdoor Ambient Probes (L) */
    uint8_t l_probes = matrix->configured_outdoor_probes;
    if (l_probes > SENSOR_MATRIX_MAX_OUTDOOR_PROBES) {
        l_probes = SENSOR_MATRIX_MAX_OUTDOOR_PROBES;
    }

    float t_ext_sum = 0.0f;
    float t_ext_max = -100.0f;
    float t_ext_min = 100.0f;
    uint8_t valid_l_count = 0;

    for (uint8_t l = 0; l < l_probes; l++) {
        if ((matrix->outdoor_valid_mask & (1U << l)) != 0U) {
            float t_val = matrix->t_outdoor[l];
            t_ext_sum += t_val;
            if (t_val > t_ext_max) {
                t_ext_max = t_val;
            }
            if (t_val < t_ext_min) {
                t_ext_min = t_val;
            }
            valid_l_count++;
        }
    }

    output->valid_outdoor_probes_count = valid_l_count;
    if (valid_l_count == 0U) {
        output->t_outdoor_eff_c = cfg->t_ext_design_c;
    } else if (valid_l_count == 1U) {
        output->t_outdoor_eff_c = t_ext_sum;
    } else {
        if ((t_ext_max - t_ext_min) <= 4.0f) {
            output->t_outdoor_eff_c = t_ext_sum / (float)valid_l_count;
        } else {
            output->t_outdoor_eff_c = t_ext_min;
        }
    }

    /* --------------------------------------------------------------------- */
    /* 2. Determine Pair Activity and Compute Average Air Temperatures       */
    /* --------------------------------------------------------------------- */
    uint8_t n_pairs = matrix->configured_pairs;
    if (n_pairs > SENSOR_MATRIX_MAX_PAIRS) {
        n_pairs = SENSOR_MATRIX_MAX_PAIRS;
    }

    /* Count active valid pairs per unit to distribute nominal airflows */
    uint8_t active_pairs_per_unit[HVAC_MAX_UNITS_TOTAL + 1U];
    memset(active_pairs_per_unit, 0, sizeof(active_pairs_per_unit));

    float t_inj_sum = 0.0f;
    float t_ret_sum = 0.0f;
    uint8_t active_pairs_count = 0;

    for (uint8_t k = 0; k < n_pairs; k++) {
        bool pair_healthy = (matrix->pair_valid_mask & (1U << k)) != 0U;
        uint8_t unit_id = matrix->pair_hvac_unit_map[k];
        bool unit_fan_on = false;

        if (unit_id >= 1U && unit_id <= cfg->configured_units) {
            uint8_t unit_idx = unit_id - 1U;
            unit_fan_on = (input->fan_active_mask & (1U << unit_idx)) != 0U;
        }

        output->pairs[k].t_inj_c = matrix->t_inj[k];
        output->pairs[k].t_ret_c = matrix->t_ret[k];
        output->pairs[k].delta_t_c = matrix->t_ret[k] - matrix->t_inj[k];

        if (pair_healthy && unit_fan_on) {
            output->pairs[k].is_active = true;
            active_pairs_per_unit[unit_id]++;
            t_inj_sum += matrix->t_inj[k];
            t_ret_sum += matrix->t_ret[k];
            active_pairs_count++;
        } else {
            output->pairs[k].is_active = false;
        }
    }

    output->active_pairs_count = active_pairs_count;

    if (active_pairs_count > 0U) {
        output->t_inj_avg_c = t_inj_sum / (float)active_pairs_count;
        output->t_ret_avg_c = t_ret_sum / (float)active_pairs_count;
        output->delta_t_clima_avg_c = output->t_ret_avg_c - output->t_inj_avg_c;
    } else {
        /* Fallback: if all fans off, use all valid pairs for static thermal reference */
        uint8_t valid_k = 0;
        float fallback_inj = 0.0f;
        float fallback_ret = 0.0f;
        for (uint8_t k = 0; k < n_pairs; k++) {
            if ((matrix->pair_valid_mask & (1U << k)) != 0U) {
                fallback_inj += matrix->t_inj[k];
                fallback_ret += matrix->t_ret[k];
                valid_k++;
            }
        }
        if (valid_k > 0U) {
            output->t_inj_avg_c = fallback_inj / (float)valid_k;
            output->t_ret_avg_c = fallback_ret / (float)valid_k;
            output->delta_t_clima_avg_c = output->t_ret_avg_c - output->t_inj_avg_c;
        }
    }

    if (valid_m_count > 0U && (active_pairs_count > 0U || output->t_ret_avg_c != 0.0f)) {
        output->delta_t_bypass_c = output->t_room_avg_c - output->t_ret_avg_c;
    }

    /* --------------------------------------------------------------------- */
    /* 3. Return Psychrometrics & Fan-Governed Dry Air Mass Flow (F-01)      */
    /* --------------------------------------------------------------------- */
    float rh_ret = matrix->humidity_return_valid ? matrix->humidity_return_pct : 50.0f;

    output->p_ws_ret_kpa = thermo_calc_p_ws_kpa(output->t_ret_avg_c);
    output->p_w_ret_kpa = thermo_calc_p_w_kpa(output->p_ws_ret_kpa, rh_ret);
    output->omega_ret = thermo_calc_humidity_ratio(output->p_w_ret_kpa, output->p_atm_kpa);
    output->rho_da_ret = thermo_calc_dry_air_density(output->p_atm_kpa,
                                                    output->p_w_ret_kpa,
                                                    output->t_ret_avg_c);
    output->t_rocio_ret_c = thermo_calc_dew_point_c(output->p_w_ret_kpa);

    /* Compute fan-governed dry air mass flow per unit and distribute to pairs */
    float m_da_total = 0.0f;

    for (uint8_t k = 0; k < n_pairs; k++) {
        if (!output->pairs[k].is_active) {
            output->pairs[k].m_da_kg_s = 0.0f;
            continue;
        }

        uint8_t unit_id = matrix->pair_hvac_unit_map[k];
        uint8_t unit_idx = (unit_id >= 1U) ? (unit_id - 1U) : 0U;
        float v_nom = (unit_idx < cfg->configured_units) ? cfg->v_nom_m3h[unit_idx] : 1200.0f;

        float m_da_unit = (v_nom / 3600.0f) * output->rho_da_ret;
        uint8_t pairs_in_unit = active_pairs_per_unit[unit_id];
        if (pairs_in_unit == 0U) {
            pairs_in_unit = 1U;
        }

        output->pairs[k].m_da_kg_s = m_da_unit / (float)pairs_in_unit;
        m_da_total += output->pairs[k].m_da_kg_s;
    }

    output->m_da_total_kg_s = m_da_total;

    /* --------------------------------------------------------------------- */
    /* 4. Evaporator Balances: Sensible, Latent, Enthalpies, Condensation    */
    /* --------------------------------------------------------------------- */
    float cp_m = THERMO_CP_DRY_AIR_KJ_KG_C + THERMO_CP_VAPOR_KJ_KG_C * output->omega_ret;
    float q_sensible_total = 0.0f;
    float q_latent_total = 0.0f;

    for (uint8_t k = 0; k < n_pairs; k++) {
        if (!output->pairs[k].is_active) {
            output->pairs[k].omega_inj = output->omega_ret;
            output->pairs[k].h_ret_kj_kg = 0.0f;
            output->pairs[k].h_inj_kj_kg = 0.0f;
            continue;
        }

        float t_inj_k = output->pairs[k].t_inj_c;
        float t_ret_k = output->pairs[k].t_ret_c;
        float m_da_k = output->pairs[k].m_da_kg_s;

        /* Sensible capacity */
        output->pairs[k].q_sensible_kw = m_da_k * cp_m * (t_ret_k - t_inj_k);
        q_sensible_total += output->pairs[k].q_sensible_kw;

        /* Condensation regime check */
        if (t_inj_k >= output->t_rocio_ret_c) {
            /* Dry regime */
            output->pairs[k].is_condensing = false;
            output->pairs[k].p_w_inj_kpa = output->p_w_ret_kpa;
            output->pairs[k].omega_inj = output->omega_ret;
            output->pairs[k].q_latent_kw = 0.0f;
        } else {
            /* Active Condensation regime */
            output->pairs[k].is_condensing = true;
            float p_ws_inj = thermo_calc_p_ws_kpa(t_inj_k);
            output->pairs[k].p_w_inj_kpa = THERMO_COIL_DISCHARGE_RH_FACTOR * p_ws_inj;
            float omega_calc = thermo_calc_humidity_ratio(output->pairs[k].p_w_inj_kpa,
                                                          output->p_atm_kpa);
            output->pairs[k].omega_inj = fminf(output->omega_ret, omega_calc);

            float delta_omega = fmaxf(0.0f, output->omega_ret - output->pairs[k].omega_inj);
            output->pairs[k].q_latent_kw = m_da_k * THERMO_H_FG_COIL_KJ_KG * delta_omega;
            q_latent_total += output->pairs[k].q_latent_kw;
        }

        output->pairs[k].q_total_kw = output->pairs[k].q_sensible_kw + output->pairs[k].q_latent_kw;

        /* Specific Enthalpies per pair */
        output->pairs[k].h_ret_kj_kg = THERMO_CP_DRY_AIR_KJ_KG_C * t_ret_k +
                                       output->omega_ret * (THERMO_H_V0_KJ_KG + THERMO_CP_VAPOR_KJ_KG_C * t_ret_k);
        output->pairs[k].h_inj_kj_kg = THERMO_CP_DRY_AIR_KJ_KG_C * t_inj_k +
                                       output->pairs[k].omega_inj * (THERMO_H_V0_KJ_KG + THERMO_CP_VAPOR_KJ_KG_C * t_inj_k);
    }

    output->q_sensible_total_kw = q_sensible_total;
    output->q_latent_total_kw = q_latent_total;
    output->q_total_kw = q_sensible_total + q_latent_total;

    /* Condensate rate in L/h */
    output->m_cond_l_h = (output->q_latent_total_kw / THERMO_H_FG_COIL_KJ_KG) * 3600.0f;

    /* Sensible Heat Factor (SHF) */
    if (output->q_total_kw <= 0.001f) {
        output->shf = 1.0f;
        output->latent_heat_pct = 0.0f;
    } else {
        output->shf = clamp_f(output->q_sensible_total_kw / output->q_total_kw, 0.0f, 1.0f);
        output->latent_heat_pct = (1.0f - output->shf) * 100.0f;
    }

    /* --------------------------------------------------------------------- */
    /* 5. First & Second Law Thermodynamic Performance (Carnot & Exergy)     */
    /* --------------------------------------------------------------------- */
    if (input->p_elec_total_kw > 0.01f) {
        output->cop_real = output->q_total_kw / input->p_elec_total_kw;
        output->cop_sensible = output->q_sensible_total_kw / input->p_elec_total_kw;
    } else {
        output->cop_real = 0.0f;
        output->cop_sensible = 0.0f;
    }

    output->t_evap_sat_k = (output->t_inj_avg_c - cfg->delta_t_pinch_evap_k) + THERMO_KELVIN_OFFSET_C;
    output->t_cond_sat_k = (output->t_outdoor_eff_c + cfg->delta_t_pinch_cond_k) + THERMO_KELVIN_OFFSET_C;
    output->cop_carnot = thermo_calc_carnot_cop(output->t_evap_sat_k, output->t_cond_sat_k);
    output->eta_ii_pct = thermo_calc_exergetic_efficiency(output->cop_real, output->cop_carnot);

    /* --------------------------------------------------------------------- */
    /* 6. Predictive Diagnostics & Mechanical Degradation                    */
    /* --------------------------------------------------------------------- */
    /* 6.1 Room Hotspot & Airflow Bypass */
    output->diagnostics.room_hotspot = (valid_m_count >= 2U && output->delta_t_room_max_c > 3.5f);
    output->diagnostics.airflow_bypass = (valid_m_count >= 1U && active_pairs_count >= 1U &&
                                          (output->t_room_max_c > 27.0f || output->delta_t_bypass_c > 3.0f));

    /* 6.2 Moisture Ingress */
    output->diagnostics.moisture_ingress = ((output->q_total_kw > 0.1f && output->shf < 0.70f) ||
                                            (output->omega_ret > 0.012f));

    /* 6.3 OLS Capacity Prediction & Mechanical Degradation */
    float base_eta = (baseline != NULL && baseline->eta_ii_base_pct > 0.0f) ? baseline->eta_ii_base_pct : 40.0f;
    float f_c = clamp_f(input->f_carga, 0.0f, 1.0f);

    if (baseline != NULL && baseline->ols_valid) {
        double q_ols = baseline->ols_beta[0] +
                       baseline->ols_beta[1] * (double)output->t_ret_avg_c +
                       baseline->ols_beta[2] * (double)output->t_outdoor_eff_c +
                       baseline->ols_beta[3] * (double)f_c;
        output->q_expected_kw = (float)fmax(0.0, q_ols);

        if (input->p_elec_total_kw > 0.01f) {
            output->cop_expected = output->q_expected_kw / input->p_elec_total_kw;
        } else {
            output->cop_expected = 0.0f;
        }

        float deg_cop = 0.0f;
        if (output->cop_expected > 0.01f) {
            deg_cop = (output->cop_expected - output->cop_real) / output->cop_expected;
        }
        float deg_eta = (base_eta - output->eta_ii_pct) / base_eta;
        output->diagnostics.mech_degradation_pct = clamp_pct(fmaxf(deg_cop, deg_eta) * 100.0f);
    } else {
        output->q_expected_kw = 0.0f;
        output->cop_expected = 0.0f;
        if (base_eta > 0.01f && output->eta_ii_pct > 0.0f) {
            float deg_eta = (base_eta - output->eta_ii_pct) / base_eta;
            output->diagnostics.mech_degradation_pct = clamp_pct(deg_eta * 100.0f);
        } else {
            output->diagnostics.mech_degradation_pct = 0.0f;
        }
    }

    /* 6.4 Weather-Corrected Free Cooling Reserve (kW_free) */
    float q_max_safe = 0.0f;
    uint8_t u_limit = (cfg->u_req > 0U && cfg->u_req <= cfg->configured_units) ? cfg->u_req : cfg->configured_units;

    for (uint8_t u = 0; u < u_limit; u++) {
        float temp_excess = fmaxf(0.0f, output->t_outdoor_eff_c - cfg->t_ext_design_c);
        float derate = 1.0f - cfg->k_ext_derating * temp_excess;
        if (derate < 0.2f) {
            derate = 0.2f;
        }
        q_max_safe += cfg->q_nominal_unit_kw[u] * derate;
    }

    q_max_safe *= (1.0f - (output->diagnostics.mech_degradation_pct / 100.0f));
    output->q_max_safe_kw = q_max_safe;
    output->kw_free = fmaxf(0.0f, q_max_safe - output->q_sensible_total_kw);

    /* 6.5 Per-Pair Predictive Maintenance Rules (Filter & Leak & Days Remaining) */
    uint32_t ram_hours = (baseline != NULL) ? baseline->hours_ram_history : 0U;

    for (uint8_t k = 0; k < n_pairs; k++) {
        if (!output->pairs[k].is_active) {
            output->diagnostics.filter_fouling_pct[k] = 0.0f;
            output->diagnostics.days_remaining[k] = THERMO_DAYS_REMAINING_CEILING;
            output->diagnostics.days_remaining_raw[k] = (uint16_t)THERMO_DAYS_REMAINING_CEILING;
            output->diagnostics.leak_probability_pct[k] = 0.0f;
            continue;
        }

        float t_inj_base = (baseline != NULL) ? baseline->t_inj_base_c[k] : 13.0f;
        float dt_base = (baseline != NULL) ? baseline->delta_t_base_c[k] : 10.0f;
        float m_t_slope = (baseline != NULL) ? baseline->m_t_day_c[k] : 0.0f;

        /* Filter fouling probability */
        float filter_term1 = 60.0f * ((t_inj_base - output->pairs[k].t_inj_c) / 3.0f);
        float filter_term2 = 40.0f * ((output->pairs[k].delta_t_c - dt_base) / 4.0f);
        float filter_prob = clamp_pct(filter_term1 + filter_term2);
        output->diagnostics.filter_fouling_pct[k] = filter_prob;

        /* Days Remaining Precedence Hierarchy (Correction F-04) */
        if (ram_hours < THERMO_DAYS_REMAINING_RAM_GATE) {
            /* Condition 1: Initialization window */
            output->diagnostics.days_remaining[k] = THERMO_DAYS_REMAINING_INIT_DAYS;
            output->diagnostics.days_remaining_raw[k] = (uint16_t)THERMO_DAYS_REMAINING_INIT_RAW;
        } else if (filter_prob < 25.0f || m_t_slope >= -THERMO_MIN_DRIFT_SLOPE_C_DAY) {
            /* Condition 2: Clean filter or positive/flat drift */
            output->diagnostics.days_remaining[k] = THERMO_DAYS_REMAINING_CEILING;
            output->diagnostics.days_remaining_raw[k] = (uint16_t)THERMO_DAYS_REMAINING_CEILING;
        } else {
            /* Condition 3: Active fouling degradation */
            float numer = fmaxf(0.0f, output->pairs[k].t_inj_c - THERMO_COIL_FREEZE_THRESHOLD_C);
            float denom = fmaxf(fabsf(m_t_slope), THERMO_MIN_DRIFT_SLOPE_C_DAY);
            float days = fminf(THERMO_DAYS_REMAINING_CEILING, numer / denom);
            output->diagnostics.days_remaining[k] = days;
            output->diagnostics.days_remaining_raw[k] = (uint16_t)(days + 0.5f);
        }

        /* Leak / compressor fatigue probability (evaluated if %Filter < 50%) */
        if (filter_prob < 50.0f && f_c > 0.01f && dt_base > 0.01f && base_eta > 0.01f) {
            float exp_dt = dt_base * f_c;
            float denom_dt = 0.5f * exp_dt;
            float denom_eta = 0.35f * base_eta;

            float leak_term1 = 50.0f * ((exp_dt - output->pairs[k].delta_t_c) / denom_dt);
            float leak_term2 = 50.0f * ((base_eta - output->eta_ii_pct) / denom_eta);
            output->diagnostics.leak_probability_pct[k] = clamp_pct(leak_term1 + leak_term2);
        } else {
            output->diagnostics.leak_probability_pct[k] = 0.0f;
        }
    }

    return 0;
}
