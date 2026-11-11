/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_predictive_ols.c
 * @brief Unit tests for OLS multi-variable regression, condition number validation,
 *        and predictive HVAC diagnostics (Suite 6).
 */

#include <zephyr/ztest.h>
#include <math.h>
#include <string.h>

#include "domain/thermal_energy/predictive_analytics.h"

ZTEST_SUITE(predictive_ols_suite, NULL, NULL, NULL, NULL, NULL);

/* ========================================================================= */
/* Test Cases: OLS Initialization, Solvers & Regularization                  */
/* ========================================================================= */

ZTEST(predictive_ols_suite, test_ols_initialization_lockout)
{
    ols_model_t model;
    ols_model_init(&model);

    zassert_false(model.ols_trained, "Model must not be trained upon initialization");
    zassert_equal(model.sample_count, 0U, "Sample count must be zero");
    zassert_equal(model.training_hours, 0U, "Training hours must be zero");
    zassert_within(model.condition_number, 0.0f, 0.0001f, "Condition number must be zero");

    /* Accumulate less than 168 hours */
    model.training_hours = 50U;
    zassert_false(model.ols_trained, "Model must remain untrained at 50h (< 168h)");

    /* REQ-PRED-04 Priority 1: When training_hours < 168h, filter days remaining is locked to 100 */
    uint16_t days = predictive_calc_filter_days_remaining(model.training_hours, 40.0f, 10.5f, -0.15f);
    zassert_equal(days, PREDICTIVE_FILTER_DAYS_INITIALIZING,
                  "Filter days remaining must lock to 100 days when training_hours < 168");
}

ZTEST(predictive_ols_suite, test_ols_168h_training_convergence)
{
    ols_model_t model;
    ols_model_init(&model);

    /*
     * True underlying linear model:
     *   Q_total = 5.0 + 1.2 * T_ret - 0.4 * T_ext + 15.0 * F_carga
     *   beta = [5.0, 1.2, -0.4, 15.0]
     */
    const double expected_beta[4] = {5.0, 1.2, -0.4, 15.0};

    /* Generate 168 hourly samples with realistic physical variation */
    for (uint32_t i = 0; i < 168; i++) {
        double t_ret = 22.0 + 3.0 * sin((double)i * 0.1);           /* 19.0 to 25.0 °C */
        double t_ext = 30.0 + 6.0 * cos((double)i * 0.05);          /* 24.0 to 36.0 °C */
        double f_carga = 0.5 + 0.4 * sin((double)i * 0.2 + 0.5);    /* 0.1 to 0.9 */

        double q_total = expected_beta[0] +
                         expected_beta[1] * t_ret +
                         expected_beta[2] * t_ext +
                         expected_beta[3] * f_carga;

        ols_model_accumulate(&model, t_ret, t_ext, f_carga, q_total);
    }

    model.training_hours = 168U;
    zassert_equal(model.sample_count, 168U, "Should have 168 accumulated samples");

    int ret = ols_model_solve(&model, OLS_DEFAULT_TIKHONOV_LAMBDA);
    zassert_equal(ret, 0, "OLS solve must succeed for well-conditioned 168h dataset");
    zassert_true(model.ols_trained, "Model must be marked trained");
    zassert_true(model.condition_number > 0.0f, "Condition number must be positive");

    /* Check convergence to true coefficients within +/- 0.05 */
    for (int j = 0; j < 4; j++) {
        zassert_within(model.beta[j], expected_beta[j], 0.05,
                       "Beta[%d] must match reference within +/- 0.05 (got %f, expected %f)",
                       j, model.beta[j], expected_beta[j]);
    }

    /* Verify prediction with model */
    float pred_q = ols_model_predict_q(&model, 24.0f, 32.0f, 0.8f);
    float manual_q = (float)(5.0 + 1.2 * 24.0 - 0.4 * 32.0 + 15.0 * 0.8); /* 5.0 + 28.8 - 12.8 + 12.0 = 33.0 */
    zassert_within(pred_q, manual_q, 0.2f, "Prediction must match expected model output");
}

ZTEST(predictive_ols_suite, test_ols_singular_matrix_regularization)
{
    ols_model_t model;
    ols_model_init(&model);

    /*
     * Constant return temperature: T_ret = 23.0 for all samples.
     * Intercept (col 0) and T_ret (col 1) are collinear: col 1 = 23.0 * col 0.
     * Without regularization, det(X^T * X) = 0 (singular matrix).
     * With Tikhonov regularization (lambda = 10^-6), matrix must invert cleanly without NaN or crash.
     */
    for (uint32_t i = 0; i < 168; i++) {
        double t_ret = 23.0; /* Constant! Collinear with intercept */
        double t_ext = 28.0 + 5.0 * sin((double)i * 0.1);
        double f_carga = 0.6 + 0.3 * cos((double)i * 0.1);
        double q_total = 20.0 + 0.5 * t_ext + 10.0 * f_carga;

        ols_model_accumulate(&model, t_ret, t_ext, f_carga, q_total);
    }

    model.training_hours = 168U;

    int ret = ols_model_solve(&model, OLS_DEFAULT_TIKHONOV_LAMBDA);
    zassert_equal(ret, 0, "OLS solve must succeed with Tikhonov regularization");
    zassert_true(model.ols_trained, "Model must be marked trained");
    for (int j = 0; j < 4; j++) {
        zassert_false(isnan(model.beta[j]), "Beta[%d] must not be NaN", j);
        zassert_false(isinf(model.beta[j]), "Beta[%d] must not be Inf", j);
    }
}

ZTEST(predictive_ols_suite, test_ols_defensive_null_and_underdetermined)
{
    ols_model_t model;
    ols_model_init(&model);

    /* Defensive NULL checks */
    ols_model_init(NULL);
    ols_model_accumulate(NULL, 24.0, 30.0, 0.8, 30.0);
    zassert_equal(ols_model_solve(NULL, 1.0e-6), -EINVAL, "NULL model must return -EINVAL");
    zassert_equal(ols_model_predict_q(NULL, 24.0f, 30.0f, 0.8f), 0.0f, "NULL model returns 0.0");

    /* Underdetermined system (< 4 samples) */
    ols_model_accumulate(&model, 24.0, 30.0, 0.8, 30.0);
    ols_model_accumulate(&model, 25.0, 31.0, 0.7, 32.0);
    zassert_equal(model.sample_count, 2U, "Only 2 samples accumulated");

    int ret = ols_model_solve(&model, 1.0e-6);
    zassert_equal(ret, -EDOM, "Underdetermined system (< 4 samples) must return -EDOM");
    zassert_false(model.ols_trained, "Model must not be marked trained on error");
    zassert_equal(ols_model_predict_q(&model, 24.0f, 30.0f, 0.8f), 0.0f,
                  "Untrained model prediction must return 0.0");
}

/* ========================================================================= */
/* Test Cases: Filter Diagnostics & Precedence F-04                          */
/* ========================================================================= */

ZTEST(predictive_ols_suite, test_filter_restriction_calculation)
{
    /* T_inj_base = 12.0 C, current T_inj = 9.0 C -> Delta = 3.0 C drop -> 60.0 * 3.0/3.0 = 60%
     * DeltaT_base = 8.0 C, current DeltaT = 12.0 C -> Delta = 4.0 C increase -> 40.0 * 4.0/4.0 = 40%
     * Total = 100.0%
     */
    float pct = predictive_calc_filter_restriction(12.0f, 9.0f, 8.0f, 12.0f);
    zassert_within(pct, 100.0f, 0.01f, "Filter restriction should evaluate to 100.0%%");

    /* Nominal clean filter (no temperature shifts) -> 0.0% */
    pct = predictive_calc_filter_restriction(12.0f, 12.0f, 8.0f, 8.0f);
    zassert_within(pct, 0.0f, 0.01f, "Clean filter should evaluate to 0.0%%");

    /* Negative shifts should clamp to 0.0% */
    pct = predictive_calc_filter_restriction(12.0f, 15.0f, 8.0f, 6.0f);
    zassert_equal(pct, 0.0f, "Negative shift must clamp to 0.0%%");

    /* Extreme shifts should clamp to 100.0% */
    pct = predictive_calc_filter_restriction(12.0f, 4.0f, 8.0f, 20.0f);
    zassert_equal(pct, 100.0f, "Extreme shift must clamp to 100.0%%");
}

ZTEST(predictive_ols_suite, test_filter_days_precedence_clean)
{
    /* Condition 2: Filter clean (< 25%) -> Clamp to 90 days */
    uint16_t days = predictive_calc_filter_days_remaining(200U, 15.0f, 12.0f, 0.0f);
    zassert_equal(days, PREDICTIVE_FILTER_DAYS_HEALTHY_MAX, "Clean filter (%Filter < 25%) must return 90 days");

    /* Condition 2: Clean filter even with negative slope */
    days = predictive_calc_filter_days_remaining(200U, 10.0f, 11.0f, -0.10f);
    zassert_equal(days, PREDICTIVE_FILTER_DAYS_HEALTHY_MAX, "Clean filter with slope must return 90 days");

    /* Condition 2: High loading (40%) but flat slope (m_T >= -0.05 °C/day) -> 90 days */
    days = predictive_calc_filter_days_remaining(200U, 40.0f, 11.0f, -0.02f);
    zassert_equal(days, PREDICTIVE_FILTER_DAYS_HEALTHY_MAX, "Flat degradation slope must return 90 days");
}

ZTEST(predictive_ols_suite, test_filter_days_active_degradation)
{
    /*
     * Condition 3: Active degradation
     * training_hours = 200 >= 168
     * pct_filter = 40% >= 25%
     * slope = -0.15 °C/day < -0.05
     * T_inj = 10.5 °C
     * Days = min(90, (10.5 - 7.5) / 0.15) = 3.0 / 0.15 = 20 days
     */
    uint16_t days = predictive_calc_filter_days_remaining(200U, 40.0f, 10.5f, -0.15f);
    zassert_equal(days, 20U, "Active degradation must project 20 days");

    /* T_inj at or below threshold (7.5 °C) -> 0 days remaining */
    days = predictive_calc_filter_days_remaining(200U, 50.0f, 7.2f, -0.20f);
    zassert_equal(days, 0U, "T_inj <= 7.5 °C must return 0 days");

    /* Slow degradation yielding > 90 days -> clamped to 90 days */
    days = predictive_calc_filter_days_remaining(200U, 30.0f, 15.0f, -0.06f);
    /* (15.0 - 7.5) / 0.06 = 125 days -> clamp to 90 */
    zassert_equal(days, 90U, "Calculated days > 90 must clamp to 90");
}

/* ========================================================================= */
/* Test Cases: Refrigerant Leak & Decoupling                                 */
/* ========================================================================= */

ZTEST(predictive_ols_suite, test_refrigerant_leak_decoupled)
{
    /*
     * Heatwave condition: Outdoor temp rises to 38 °C.
     * Second Law efficiency remains at baseline 44% (eta_II = 44%, eta_II_base = 44%).
     * DeltaT is nominal for the load: DeltaT = DeltaT_base * F_carga = 8.0 * 0.8 = 6.4 °C.
     * Filter is clean (%Filter = 10%).
     * Result must be 0.0% (Zero false alarms during heatwave).
     */
    float leak_pct = predictive_calc_refrigerant_leak(8.0f, 6.4f, 0.8f, 44.0f, 44.0f, 10.0f);
    zassert_within(leak_pct, 0.0f, 0.001f, "Heatwave with normal exergy must yield 0%% leak");
}

ZTEST(predictive_ols_suite, test_refrigerant_leak_true_loss)
{
    /*
     * True refrigerant charge loss:
     * DeltaT_base = 10.0 °C, F_carga = 0.8 -> DeltaT_target = 8.0 °C.
     * Actual DeltaT collapsed to 2.0 °C.
     * Baseline eta_II_base = 45%, actual eta_II collapsed to 20%.
     * Filter loading = 15% (< 50%).
     * Term 1 = 50.0 * (8.0 - 2.0) / (0.5 * 8.0) = 50 * 6.0 / 4.0 = 75.0%
     * Term 2 = 50.0 * (45.0 - 20.0) / (0.35 * 45.0) = 50 * 25.0 / 15.75 = 79.36%
     * Total = clamp(75.0 + 79.36) = 100.0% (> 80%)
     */
    float leak_pct = predictive_calc_refrigerant_leak(10.0f, 2.0f, 0.8f, 20.0f, 45.0f, 15.0f);
    zassert_true(leak_pct > 80.0f, "True refrigerant loss must trigger leak probability > 80%%");
    zassert_within(leak_pct, 100.0f, 0.01f, "Must clamp at 100%%");
}

ZTEST(predictive_ols_suite, test_refrigerant_leak_filter_lockout)
{
    /* When filter fouling is >= 50%, leak calculation is locked out to avoid false diagnosis */
    float leak_pct = predictive_calc_refrigerant_leak(10.0f, 2.0f, 0.8f, 20.0f, 45.0f, 50.0f);
    zassert_equal(leak_pct, 0.0f, "Filter fouling >= 50%% must lock out leak evaluation");

    /* When compressor load fraction < 0.20, locked out */
    leak_pct = predictive_calc_refrigerant_leak(10.0f, 2.0f, 0.15f, 20.0f, 45.0f, 10.0f);
    zassert_equal(leak_pct, 0.0f, "Load fraction < 0.20 must lock out leak evaluation");
}

/* ========================================================================= */
/* Test Cases: Degradation, Alerts & Weather Derating                        */
/* ========================================================================= */

ZTEST(predictive_ols_suite, test_expected_cop_and_mechanical_degradation)
{
    /* Expected COP = Q_exp / P_elec = 30.0 / 10.0 = 3.0 */
    float cop_exp = predictive_calc_expected_cop(30.0f, 10.0f);
    zassert_within(cop_exp, 3.0f, 0.001f, "Expected COP must be 3.0");

    /* P_elec < 0.5 kW guard */
    cop_exp = predictive_calc_expected_cop(30.0f, 0.4f);
    zassert_equal(cop_exp, 0.0f, "Expected COP must be 0 when P_elec < 0.5 kW");

    /* Mechanical degradation:
     * COP_exp = 3.0, COP_real = 2.1 -> (3.0 - 2.1)/3.0 = 0.30 (30%)
     * eta_II_base = 40%, eta_II = 32% -> (40 - 32)/40 = 0.20 (20%)
     * %Deg = max(30%, 20%) = 30.0%
     */
    float deg = predictive_calc_mechanical_degradation(2.1f, 3.0f, 32.0f, 40.0f);
    zassert_within(deg, 30.0f, 0.01f, "Mechanical degradation must be 30.0%%");

    /* Zero degradation when exceeding expectations */
    deg = predictive_calc_mechanical_degradation(3.5f, 3.0f, 42.0f, 40.0f);
    zassert_equal(deg, 0.0f, "Degradation must clamp to 0 when operating better than baseline");
}

ZTEST(predictive_ols_suite, test_room_hotspot_and_bypass)
{
    float spread = 0.0f;
    /* Hotspot: T_max - T_min > 3.5 °C */
    bool hotspot = predictive_check_room_hotspot(26.5f, 22.0f, &spread);
    zassert_true(hotspot, "Spread 4.5 C (> 3.5 C) must trigger hotspot");
    zassert_within(spread, 4.5f, 0.001f, "Spread output must be 4.5 C");

    hotspot = predictive_check_room_hotspot(24.5f, 22.0f, &spread);
    zassert_false(hotspot, "Spread 2.5 C (<= 3.5 C) must not trigger hotspot");

    /* Bypass: T_sala_mean - T_ret_mean > 3.0 C or T_sala_max > 27.0 C */
    float bypass = 0.0f;
    bool alert = predictive_check_airflow_bypass(25.0f, 21.0f, 25.5f, &bypass);
    zassert_true(alert, "Bypass gradient 4.0 C (> 3.0 C) must trigger bypass alert");
    zassert_within(bypass, 4.0f, 0.001f, "Bypass gradient output must be 4.0 C");

    /* High zone hotspot triggering bypass alert even if bypass gradient is small */
    alert = predictive_check_airflow_bypass(24.0f, 22.0f, 27.5f, &bypass);
    zassert_true(alert, "T_sala_max 27.5 C (> 27.0 C) must trigger bypass alert");

    alert = predictive_check_airflow_bypass(24.0f, 22.0f, 25.0f, &bypass);
    zassert_false(alert, "Normal conditions must not trigger bypass alert");
}

ZTEST(predictive_ols_suite, test_moisture_ingress_alert)
{
    /* %Latent > 30% */
    zassert_true(predictive_check_moisture_ingress(35.0f, 0.85f, 0.008f),
                 "Latent 35%% (> 30%%) must trigger moisture alert");

    /* SHF < 0.70 */
    zassert_true(predictive_check_moisture_ingress(20.0f, 0.65f, 0.008f),
                 "SHF 0.65 (< 0.70) must trigger moisture alert");

    /* omega_ret > 0.012 kg/kg */
    zassert_true(predictive_check_moisture_ingress(20.0f, 0.85f, 0.013f),
                 "omega_ret 0.013 (> 0.012) must trigger moisture alert");

    /* All normal */
    zassert_false(predictive_check_moisture_ingress(15.0f, 0.85f, 0.009f),
                  "Normal psychrometrics must not trigger moisture alert");
}

ZTEST(predictive_ols_suite, test_weather_derating_and_free_cooling)
{
    /* Below 35 °C -> No derating */
    float q_corr = predictive_calc_weather_corrected_capacity(50.0f, 32.0f);
    zassert_within(q_corr, 50.0f, 0.001f, "No derating below 35 C");

    /* 45 °C outdoor -> 10 C above 35 C -> 10 * 0.005 = 5% derating -> 50.0 * 0.95 = 47.5 kW */
    q_corr = predictive_calc_weather_corrected_capacity(50.0f, 45.0f);
    zassert_within(q_corr, 47.5f, 0.001f, "45 C outdoor must derate by 5%%");

    /* Safe redundant capacity: 2 units active at 47.5 kW = 95.0 kW; 10% degradation */
    float q_units[2] = {47.5f, 47.5f};
    float q_safe = predictive_calc_safe_redundant_capacity(q_units, 2U, 10.0f);
    /* 95.0 * (1 - 0.10) = 85.5 kW */
    zassert_within(q_safe, 85.5f, 0.001f, "Safe redundant capacity must be 85.5 kW");

    /* Free cooling margin */
    float kw_free = predictive_calc_free_cooling_margin(q_safe, 60.0f);
    zassert_within(kw_free, 25.5f, 0.001f, "Free margin must be 85.5 - 60.0 = 25.5 kW");

    /* Load exceeds capacity -> 0 kW margin */
    kw_free = predictive_calc_free_cooling_margin(q_safe, 90.0f);
    zassert_equal(kw_free, 0.0f, "Overload must clamp free margin to 0.0 kW");
}
