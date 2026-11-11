/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file predictive_analytics.c
 * @brief Pure domain implementation of Ordinary Least Squares (OLS) multi-variable
 *        regression and predictive HVAC degradation diagnostics (Revision F Baseline).
 *
 * Pure C99/C11 Domain Layer:
 * - Zero Zephyr RTOS dependencies
 * - Zero hardware register dependencies
 * - Zero dynamic memory allocation
 * - Zero file-scope mutable state
 */

#include "domain/thermal_energy/predictive_analytics.h"

#include <errno.h>
#include <math.h>
#include <string.h>

/* ========================================================================= */
/* OLS Model Management & Solvers                                            */
/* ========================================================================= */

void ols_model_init(ols_model_t *self)
{
    if (self == NULL) {
        return;
    }

    memset(self, 0, sizeof(*self));
}

void ols_model_accumulate(ols_model_t *self, double t_ret, double t_ext, double f_carga, double q_total)
{
    if (self == NULL) {
        return;
    }

    /* Reject non-finite input observations */
    if (isnan(t_ret) || isnan(t_ext) || isnan(f_carga) || isnan(q_total) ||
        isinf(t_ret) || isinf(t_ext) || isinf(f_carga) || isinf(q_total)) {
        return;
    }

    /* Feature vector: x = [1.0, t_ret, t_ext, f_carga] */
    const double x[4] = {1.0, t_ret, t_ext, f_carga};

    /* Accumulate moment matrix X^T * X (symmetric) */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            self->xtx[i][j] += x[i] * x[j];
        }
    }

    /* Accumulate moment vector X^T * Y */
    for (int i = 0; i < 4; i++) {
        self->xty[i] += x[i] * q_total;
    }

    self->sample_count++;
}

int ols_model_solve(ols_model_t *self, double lambda)
{
    if (self == NULL) {
        return -EINVAL;
    }

    /* Underdetermined system: at least 4 independent observations required */
    if (self->sample_count < 4U) {
        self->ols_trained = false;
        return -EDOM;
    }

    /* Default Tikhonov regularization factor if non-positive */
    if (lambda <= 0.0) {
        lambda = OLS_DEFAULT_TIKHONOV_LAMBDA;
    }

    /*
     * Build augmented matrix [A | I_4] of size 4 x 8 where A = X^T * X + lambda * I_4.
     * Gauss-Jordan elimination with partial pivoting inverts A and solves A * beta = X^T * Y.
     */
    double aug[4][8];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            aug[i][j] = self->xtx[i][j];
        }
        aug[i][i] += lambda;

        for (int j = 0; j < 4; j++) {
            aug[i][4 + j] = (i == j) ? 1.0 : 0.0;
        }
    }

    /* Compute 1-norm of regularized matrix A: ||A||_1 = max_j sum_i |A_ij| */
    double norm_a = 0.0;
    for (int j = 0; j < 4; j++) {
        double col_sum = 0.0;
        for (int i = 0; i < 4; i++) {
            col_sum += fabs(aug[i][j]);
        }
        if (col_sum > norm_a) {
            norm_a = col_sum;
        }
    }

    /* Gauss-Jordan elimination with row partial pivoting */
    for (int k = 0; k < 4; k++) {
        /* Find pivot row */
        int pivot_row = k;
        double max_val = fabs(aug[k][k]);
        for (int r = k + 1; r < 4; r++) {
            double val = fabs(aug[r][k]);
            if (val > max_val) {
                max_val = val;
                pivot_row = r;
            }
        }

        /* Check for singularity / degeneracy */
        if (max_val < 1.0e-12 || isnan(max_val) || isinf(max_val)) {
            self->ols_trained = false;
            return -EDOM;
        }

        /* Swap pivot row into diagonal position */
        if (pivot_row != k) {
            for (int c = 0; c < 8; c++) {
                double tmp = aug[k][c];
                aug[k][c] = aug[pivot_row][c];
                aug[pivot_row][c] = tmp;
            }
        }

        /* Scale pivot row so diagonal element equals 1.0 */
        double pivot_val = aug[k][k];
        for (int c = 0; c < 8; c++) {
            aug[k][c] /= pivot_val;
        }

        /* Eliminate column k from all other rows */
        for (int r = 0; r < 4; r++) {
            if (r == k) {
                continue;
            }
            double factor = aug[r][k];
            for (int c = 0; c < 8; c++) {
                aug[r][c] -= factor * aug[k][c];
            }
        }
    }

    /* Compute 1-norm of inverse matrix A^-1 (right half of augmented matrix) */
    double norm_inv = 0.0;
    for (int j = 0; j < 4; j++) {
        double col_sum = 0.0;
        for (int i = 0; i < 4; i++) {
            col_sum += fabs(aug[i][4 + j]);
        }
        if (col_sum > norm_inv) {
            norm_inv = col_sum;
        }
    }

    /* Condition number kappa_1(A) = ||A||_1 * ||A^-1||_1 */
    double cond = norm_a * norm_inv;
    if (isnan(cond) || isinf(cond) || cond > 1.0e16) {
        self->ols_trained = false;
        return -EDOM;
    }
    self->condition_number = (float)cond;

    /* Compute solved coefficients: beta = A^-1 * (X^T * Y) */
    for (int i = 0; i < 4; i++) {
        double sum = 0.0;
        for (int j = 0; j < 4; j++) {
            sum += aug[i][4 + j] * self->xty[j];
        }
        if (isnan(sum) || isinf(sum)) {
            self->ols_trained = false;
            return -EDOM;
        }
        self->beta[i] = sum;
    }

    self->ols_trained = true;
    return 0;
}

float ols_model_predict_q(const ols_model_t *self, float t_ret, float t_ext, float f_carga)
{
    if (self == NULL || !self->ols_trained) {
        return 0.0f;
    }

    if (isnan(t_ret) || isnan(t_ext) || isnan(f_carga) ||
        isinf(t_ret) || isinf(t_ext) || isinf(f_carga)) {
        return 0.0f;
    }

    double q = self->beta[0] +
               self->beta[1] * (double)t_ret +
               self->beta[2] * (double)t_ext +
               self->beta[3] * (double)f_carga;

    if (isnan(q) || isinf(q) || q < 0.0) {
        return 0.0f;
    }

    return (float)q;
}

/* ========================================================================= */
/* Predictive Diagnostic Calculations                                       */
/* ========================================================================= */

float predictive_calc_expected_cop(float q_expected_kw, float p_elec_kw)
{
    if (p_elec_kw < PREDICTIVE_MIN_PELEC_KW || q_expected_kw <= 0.0f ||
        isnan(q_expected_kw) || isnan(p_elec_kw) ||
        isinf(q_expected_kw) || isinf(p_elec_kw)) {
        return 0.0f;
    }

    return q_expected_kw / p_elec_kw;
}

float predictive_calc_mechanical_degradation(float cop_real, float cop_expected, float eta_ii, float eta_ii_base)
{
    if (isnan(cop_real) || isnan(cop_expected) || isnan(eta_ii) || isnan(eta_ii_base) ||
        isinf(cop_real) || isinf(cop_expected) || isinf(eta_ii) || isinf(eta_ii_base)) {
        return 0.0f;
    }

    float term_cop = 0.0f;
    if (cop_expected > 0.001f) {
        term_cop = (cop_expected - cop_real) / cop_expected;
    }

    float term_eta = 0.0f;
    if (eta_ii_base > 0.001f) {
        term_eta = (eta_ii_base - eta_ii) / eta_ii_base;
    }

    float deg = (term_cop > term_eta) ? term_cop : term_eta;
    deg *= 100.0f;

    if (deg < 0.0f) {
        deg = 0.0f;
    } else if (deg > 100.0f) {
        deg = 100.0f;
    }

    return deg;
}

float predictive_calc_filter_restriction(float t_inj_base, float t_inj, float delta_t_base, float delta_t)
{
    if (isnan(t_inj_base) || isnan(t_inj) || isnan(delta_t_base) || isnan(delta_t) ||
        isinf(t_inj_base) || isinf(t_inj) || isinf(delta_t_base) || isinf(delta_t)) {
        return 0.0f;
    }

    float term1 = 60.0f * ((t_inj_base - t_inj) / 3.0f);
    float term2 = 40.0f * ((delta_t - delta_t_base) / 4.0f);
    float pct = term1 + term2;

    if (pct < 0.0f) {
        pct = 0.0f;
    } else if (pct > 100.0f) {
        pct = 100.0f;
    }

    return pct;
}

uint16_t predictive_calc_filter_days_remaining(uint16_t training_hours, float pct_filter, float t_inj, float slope_c_per_day)
{
    /* Priority 1: Window incomplete (< 168h) -> Lock to initialization code 100 days */
    if (training_hours < OLS_MIN_TRAINING_HOURS) {
        return PREDICTIVE_FILTER_DAYS_INITIALIZING;
    }

    /* Priority 2: Clean filter or flat degradation slope -> Healthy ceiling 90 days */
    if (pct_filter < PREDICTIVE_FILTER_CLEAN_PCT_THRESHOLD ||
        slope_c_per_day >= PREDICTIVE_FILTER_SLOPE_THRESHOLD) {
        return PREDICTIVE_FILTER_DAYS_HEALTHY_MAX;
    }

    /* Priority 3: Active degradation */
    float temp_diff = t_inj - 7.5f;
    if (temp_diff < 0.0f) {
        temp_diff = 0.0f;
    }

    float abs_slope = fabsf(slope_c_per_day);
    if (abs_slope < 0.05f) {
        abs_slope = 0.05f;
    }

    float days = temp_diff / abs_slope;
    if (days > (float)PREDICTIVE_FILTER_DAYS_HEALTHY_MAX) {
        days = (float)PREDICTIVE_FILTER_DAYS_HEALTHY_MAX;
    }

    return (uint16_t)(days + 0.5f);
}

float predictive_calc_refrigerant_leak(float delta_t_base, float delta_t, float f_carga, float eta_ii, float eta_ii_base, float pct_filter)
{
    /* Lockout when filter fouling is high or unit is running under light load */
    if (pct_filter >= PREDICTIVE_LEAK_FILTER_LOCKOUT_PCT ||
        f_carga < PREDICTIVE_LEAK_MIN_LOAD_FRACTION) {
        return 0.0f;
    }

    if (isnan(delta_t_base) || isnan(delta_t) || isnan(f_carga) ||
        isnan(eta_ii) || isnan(eta_ii_base) || isnan(pct_filter) ||
        isinf(delta_t_base) || isinf(delta_t) || isinf(f_carga) ||
        isinf(eta_ii) || isinf(eta_ii_base) || isinf(pct_filter)) {
        return 0.0f;
    }

    float delta_t_target = delta_t_base * f_carga;
    float denom1 = 0.5f * delta_t_target;
    if (denom1 < 0.5f) {
        denom1 = 0.5f;
    }
    float term1 = 50.0f * ((delta_t_target - delta_t) / denom1);

    float denom2 = 0.35f * eta_ii_base;
    if (denom2 < 1.0f) {
        denom2 = 1.0f;
    }
    float term2 = 50.0f * ((eta_ii_base - eta_ii) / denom2);

    float leak = term1 + term2;
    if (leak < 0.0f) {
        leak = 0.0f;
    } else if (leak > 100.0f) {
        leak = 100.0f;
    }

    return leak;
}

/* ========================================================================= */
/* Room & Environmental Anomaly Predicates                                   */
/* ========================================================================= */

bool predictive_check_room_hotspot(float t_sala_max, float t_sala_min, float *out_spread)
{
    if (isnan(t_sala_max) || isnan(t_sala_min) || isinf(t_sala_max) || isinf(t_sala_min)) {
        if (out_spread != NULL) {
            *out_spread = 0.0f;
        }
        return false;
    }

    float spread = t_sala_max - t_sala_min;
    if (out_spread != NULL) {
        *out_spread = spread;
    }

    return (spread > PREDICTIVE_HOTSPOT_SPREAD_THRESHOLD);
}

bool predictive_check_airflow_bypass(float t_sala_mean, float t_ret_mean, float t_sala_max, float *out_bypass)
{
    if (isnan(t_sala_mean) || isnan(t_ret_mean) || isnan(t_sala_max) ||
        isinf(t_sala_mean) || isinf(t_ret_mean) || isinf(t_sala_max)) {
        if (out_bypass != NULL) {
            *out_bypass = 0.0f;
        }
        return false;
    }

    float bypass = t_sala_mean - t_ret_mean;
    if (out_bypass != NULL) {
        *out_bypass = bypass;
    }

    return (t_sala_max > PREDICTIVE_BYPASS_TSALA_MAX_THRESHOLD ||
            bypass > PREDICTIVE_BYPASS_GRADIENT_THRESHOLD);
}

bool predictive_check_moisture_ingress(float pct_latent, float shf, float omega_ret)
{
    if (isnan(pct_latent) || isnan(shf) || isnan(omega_ret) ||
        isinf(pct_latent) || isinf(shf) || isinf(omega_ret)) {
        return false;
    }

    return (pct_latent > PREDICTIVE_LATENT_PCT_THRESHOLD ||
            shf < PREDICTIVE_SHF_MIN_THRESHOLD ||
            omega_ret > PREDICTIVE_OMEGA_MAX_THRESHOLD);
}

/* ========================================================================= */
/* Weather Derating & Free Cooling Reserves                                  */
/* ========================================================================= */

float predictive_calc_weather_corrected_capacity(float q_nominal_kw, float t_outdoor)
{
    if (isnan(q_nominal_kw) || isnan(t_outdoor) || isinf(q_nominal_kw) || isinf(t_outdoor) ||
        q_nominal_kw <= 0.0f) {
        return 0.0f;
    }

    float delta_temp = t_outdoor - PREDICTIVE_WEATHER_DERATE_TEMP_C;
    if (delta_temp < 0.0f) {
        delta_temp = 0.0f;
    }

    float factor = 1.0f - PREDICTIVE_WEATHER_DERATE_SLOPE * delta_temp;
    if (factor < 0.0f) {
        factor = 0.0f;
    }

    return q_nominal_kw * factor;
}

float predictive_calc_safe_redundant_capacity(const float q_nom_corr[], uint8_t u_req, float pct_degradation)
{
    if (q_nom_corr == NULL || u_req == 0U) {
        return 0.0f;
    }

    if (isnan(pct_degradation) || isinf(pct_degradation)) {
        pct_degradation = 0.0f;
    }
    if (pct_degradation < 0.0f) {
        pct_degradation = 0.0f;
    } else if (pct_degradation > 100.0f) {
        pct_degradation = 100.0f;
    }

    float sum = 0.0f;
    for (uint8_t u = 0; u < u_req; u++) {
        if (!isnan(q_nom_corr[u]) && !isinf(q_nom_corr[u]) && q_nom_corr[u] > 0.0f) {
            sum += q_nom_corr[u];
        }
    }

    float safe = sum * (1.0f - pct_degradation / 100.0f);
    if (safe < 0.0f) {
        safe = 0.0f;
    }

    return safe;
}

float predictive_calc_free_cooling_margin(float q_max_safe, float q_sensible_total)
{
    if (isnan(q_max_safe) || isnan(q_sensible_total) ||
        isinf(q_max_safe) || isinf(q_sensible_total)) {
        return 0.0f;
    }

    float free_margin = q_max_safe - q_sensible_total;
    if (free_margin < 0.0f) {
        free_margin = 0.0f;
    }

    return free_margin;
}
