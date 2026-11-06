/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Comprehensive unit tests for Thermodynamic and Psychrometric Engine (Revision F).
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "domain/services/thermodynamic_engine.h"

ZTEST_SUITE(thermodynamic_engine_suite, NULL, NULL, NULL, NULL, NULL);

/* ========================================================================= */
/* Test Helpers                                                              */
/* ========================================================================= */

static void setup_test_matrix_nominal(thermal_matrix_frame_t *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->timestamp_us = 1000000000ULL;

    /* 6 pairs (N=6): 2 pairs per unit for 3 units */
    matrix->configured_pairs = 6U;
    matrix->pair_valid_mask = 0x003FU; /* Pairs 0..5 valid */
    matrix->pair_open_probe_mask = 0x0000U;

    for (uint8_t k = 0; k < 6; k++) {
        matrix->t_inj[k] = 13.0f + 0.2f * (float)k; /* 13.0 to 14.0 C */
        matrix->t_ret[k] = 23.0f + 0.2f * (float)k; /* 23.0 to 24.0 C */
        matrix->pair_hvac_unit_map[k] = (k / 2U) + 1U; /* Unit 1, 1, 2, 2, 3, 3 */
    }

    /* 2 room probes (M=2) */
    matrix->configured_room_probes = 2U;
    matrix->room_valid_mask = 0x0003U;
    matrix->t_room[0] = 22.0f;
    matrix->t_room[1] = 22.5f;

    /* 2 outdoor probes (L=2) */
    matrix->configured_outdoor_probes = 2U;
    matrix->outdoor_valid_mask = 0x03U;
    matrix->t_outdoor[0] = 32.0f;
    matrix->t_outdoor[1] = 33.0f;

    /* Humidity Return */
    matrix->humidity_return_valid = true;
    matrix->humidity_return_pct = 50.0f; /* 50 % HR */
}

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

/**
 * @brief Test standalone mathematical psychrometric formulations.
 */
ZTEST(thermodynamic_engine_suite, test_standalone_psychrometric_math)
{
    /* 1. Barometric atmospheric pressure */
    float p_sea = thermo_calc_p_atm_kpa(0.0f);
    zassert_within(p_sea, 101.325f, 0.01f, "Sea level pressure mismatch");

    float p_bogota = thermo_calc_p_atm_kpa(2600.0f);
    zassert_within(p_bogota, 73.86f, 0.5f, "Altitude pressure mismatch");

    /* 2. Saturation vapor pressure (Magnus-Tetens) */
    float p_ws_0 = thermo_calc_p_ws_kpa(0.0f);
    zassert_within(p_ws_0, 0.61078f, 0.005f, "P_ws at 0 C mismatch");

    float p_ws_20 = thermo_calc_p_ws_kpa(20.0f);
    zassert_within(p_ws_20, 2.338f, 0.05f, "P_ws at 20 C mismatch");

    /* 3. Partial vapor pressure */
    float p_w_50 = thermo_calc_p_w_kpa(p_ws_20, 50.0f);
    zassert_within(p_w_50, 1.169f, 0.03f, "P_w at 50% HR mismatch");

    /* 4. Humidity ratio */
    float omega = thermo_calc_humidity_ratio(1.49f, 101.325f);
    zassert_within(omega, 0.0093f, 0.0005f, "Humidity ratio mismatch");

    /* 5. Dry air density */
    float rho_da = thermo_calc_dry_air_density(101.325f, 1.49f, 24.0f);
    zassert_within(rho_da, 1.170f, 0.01f, "Dry air density mismatch");

    /* 6. Dew point */
    float t_dew = thermo_calc_dew_point_c(1.49f);
    zassert_within(t_dew, 12.9f, 0.3f, "Dew point mismatch");

    /* 7. Carnot COP */
    float cop_c = thermo_calc_carnot_cop(278.15f, 318.15f); /* Lift = 40 K */
    zassert_within(cop_c, 6.953f, 0.05f, "Carnot COP mismatch");

    /* 8. Exergetic efficiency */
    float eta_ii = thermo_calc_exergetic_efficiency(3.0f, cop_c);
    zassert_within(eta_ii, 43.14f, 0.1f, "Exergetic efficiency mismatch");
}

/**
 * @brief Test Phase 2 R410A Flash Lookup Table and Superheat API (F-02).
 */
ZTEST(thermodynamic_engine_suite, test_r410a_flash_lut_and_superheat)
{
    float t_sat = 0.0f;
    float sh = 0.0f;

    /* Exact knot points */
    int rc = thermo_r410a_sat_temp_c(0.0f, &t_sat);
    zassert_equal(rc, 0, "Lookup failed");
    zassert_within(t_sat, -51.40f, 0.01f, "R410A 0 bar sat temp mismatch");

    rc = thermo_r410a_sat_temp_c(7.0f, &t_sat);
    zassert_equal(rc, 0, "Lookup failed");
    zassert_within(t_sat, 0.80f, 0.01f, "R410A 7 bar sat temp mismatch");

    rc = thermo_r410a_sat_temp_c(15.0f, &t_sat);
    zassert_equal(rc, 0, "Lookup failed");
    zassert_within(t_sat, 26.10f, 0.01f, "R410A 15 bar sat temp mismatch");

    /* Linear interpolation: 7.5 bar -> halfway between 0.80 and 4.80 = 2.80 C */
    rc = thermo_r410a_sat_temp_c(7.5f, &t_sat);
    zassert_equal(rc, 0, "Lookup failed");
    zassert_within(t_sat, 2.80f, 0.01f, "R410A 7.5 bar interpolation mismatch");

    /* Suction superheat calculation */
    rc = thermo_r410a_superheat_k(7.0f, 10.80f, &sh);
    zassert_equal(rc, 0, "Superheat failed");
    zassert_within(sh, 10.00f, 0.01f, "Superheat calculation mismatch");

    /* NULL guards and boundary clamping */
    zassert_equal(thermo_r410a_sat_temp_c(5.0f, NULL), -EINVAL, "Null guard failed");
    zassert_equal(thermo_r410a_superheat_k(5.0f, 10.0f, NULL), -EINVAL, "Null guard failed");

    thermo_r410a_sat_temp_c(-5.0f, &t_sat);
    zassert_within(t_sat, -51.40f, 0.01f, "Lower clamp mismatch");

    thermo_r410a_sat_temp_c(50.0f, &t_sat);
    zassert_within(t_sat, 75.70f, 0.01f, "Upper clamp mismatch");
}

/**
 * @brief Test Double-Precision OLS Accumulator and Solver with Tikhonov Regularization (F-03).
 */
ZTEST(thermodynamic_engine_suite, test_ols_accumulation_and_solver)
{
    thermo_ols_t ols;
    int rc = thermo_ols_init(&ols);
    zassert_equal(rc, 0, "OLS init failed");
    zassert_equal(ols.sample_count, 0U, "Count not zero");

    /* Insufficient samples should fail */
    double beta[4];
    rc = thermo_ols_solve(&ols, 1.0e-6, beta);
    zassert_equal(rc, -EINVAL, "Should fail on 0 samples");

    /* Synthetic ground truth linear relationship:
     * Q = 5.0 + 0.4 * T_ret - 0.1 * T_ext + 12.0 * F_carga
     */
    double true_b0 = 5.0;
    double true_b1 = 0.4;
    double true_b2 = -0.1;
    double true_b3 = 12.0;

    double samples[8][3] = {
        {22.0, 25.0, 0.50},
        {23.0, 28.0, 0.75},
        {24.0, 30.0, 1.00},
        {22.5, 32.0, 0.60},
        {23.5, 34.0, 0.80},
        {24.5, 26.0, 0.90},
        {22.8, 29.0, 0.70},
        {23.2, 31.0, 0.85}
    };

    for (int i = 0; i < 8; i++) {
        double tr = samples[i][0];
        double te = samples[i][1];
        double fc = samples[i][2];
        double q = true_b0 + true_b1 * tr + true_b2 * te + true_b3 * fc;
        rc = thermo_ols_add_sample(&ols, tr, te, fc, q);
        zassert_equal(rc, 0, "Sample add failed");
    }

    zassert_equal(ols.sample_count, 8U, "Count mismatch");

    rc = thermo_ols_solve(&ols, 1.0e-6, beta);
    zassert_equal(rc, 0, "OLS solve failed");

    zassert_within(beta[0], true_b0, 0.2, "Beta 0 mismatch");
    zassert_within(beta[1], true_b1, 0.05, "Beta 1 mismatch");
    zassert_within(beta[2], true_b2, 0.05, "Beta 2 mismatch");
    zassert_within(beta[3], true_b3, 0.05, "Beta 3 mismatch");
}

/**
 * @brief Test Fan-Governed Dry Air Mass Flow Decoupling (Correction F-01).
 */
ZTEST(thermodynamic_engine_suite, test_fan_governed_mass_flow_decoupling)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    thermo_engine_init_default_baseline(&baseline);
    setup_test_matrix_nominal(&matrix);

    input.matrix = &matrix;
    input.p_elec_total_kw = 5.0f;
    input.f_carga = 1.0f;

    /* Case A: All 3 fans ON (fan_active_mask = 0x07 -> units 1, 2, 3 active) */
    input.fan_active_mask = 0x07U;
    int rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Engine compute failed");

    zassert_equal(out.active_pairs_count, 6U, "All 6 pairs should be active");
    /* 3 units * (1200 m3/h / 3600) * rho (~1.17) approx 1.17 kg/s */
    zassert_within(out.m_da_total_kg_s, 1.17f, 0.1f, "Total mass flow mismatch");
    zassert_true(out.q_sensible_total_kw > 10.0f, "Sensible capacity expected > 10 kW");

    /* Case B: Only Unit 1 and Unit 2 fans ON (fan_active_mask = 0x03 -> Unit 3 OFF) */
    input.fan_active_mask = 0x03U;
    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Engine compute failed");

    zassert_equal(out.active_pairs_count, 4U, "Only 4 pairs should be active");
    /* Pairs 4 and 5 belong to Unit 3 -> must have m_da = 0 and q = 0 */
    zassert_equal(out.pairs[4].is_active, false, "Pair 4 must be inactive");
    zassert_equal(out.pairs[5].is_active, false, "Pair 5 must be inactive");
    zassert_within(out.pairs[4].m_da_kg_s, 0.0f, 0.0001f, "Pair 4 m_da must be 0");
    zassert_within(out.pairs[4].q_sensible_kw, 0.0f, 0.0001f, "Pair 4 Q must be 0");

    /* Case C: Pure ventilation mode (Compressors OFF, f_carga = 0, but fans ON) */
    input.fan_active_mask = 0x07U;
    input.f_carga = 0.0f;
    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Engine compute failed");
    zassert_within(out.m_da_total_kg_s, 1.17f, 0.1f, "Ventilation mass flow must remain active");
}

/**
 * @brief Test Psychrometric Dry vs. Condensing Regimes.
 */
ZTEST(thermodynamic_engine_suite, test_psychrometric_dry_vs_condensing_regimes)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    thermo_engine_init_default_baseline(&baseline);
    setup_test_matrix_nominal(&matrix);

    input.matrix = &matrix;
    input.p_elec_total_kw = 6.0f;
    input.fan_active_mask = 0x07U;
    input.f_carga = 1.0f;

    /* Dew point at 23.5 C, 50% RH is approx 12.5 C */
    /* Case A: Warm discharge (T_inj = 16.0 C >= T_dew -> Dry regime) */
    for (uint8_t k = 0; k < 6; k++) {
        matrix.t_inj[k] = 16.0f;
        matrix.t_ret[k] = 24.0f;
    }

    int rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");
    zassert_within(out.q_latent_total_kw, 0.0f, 0.001f, "Dry regime must have Q_latent = 0");
    zassert_within(out.m_cond_l_h, 0.0f, 0.001f, "Dry regime must have zero condensate");
    zassert_within(out.shf, 1.0f, 0.001f, "Dry regime SHF must be 1.0");
    for (uint8_t k = 0; k < 6; k++) {
        zassert_false(out.pairs[k].is_condensing, "Pair should not be condensing");
        zassert_within(out.pairs[k].omega_inj, out.omega_ret, 0.0001f, "Omega inj must equal omega ret");
    }

    /* Case B: Cold discharge (T_inj = 8.0 C < T_dew -> Active Condensing regime) */
    for (uint8_t k = 0; k < 6; k++) {
        matrix.t_inj[k] = 8.0f;
        matrix.t_ret[k] = 24.0f;
    }

    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");
    zassert_true(out.q_latent_total_kw > 0.5f, "Condensing regime must have positive Q_latent");
    zassert_true(out.m_cond_l_h > 0.5f, "Condensing regime must have positive condensate rate");
    zassert_true(out.shf < 1.0f, "SHF must be less than 1.0 under condensing");
    for (uint8_t k = 0; k < 6; k++) {
        zassert_true(out.pairs[k].is_condensing, "Pair should be condensing");
        zassert_true(out.pairs[k].omega_inj < out.omega_ret, "Omega inj must be less than omega ret");
    }
}

/**
 * @brief Test Spatial Temperature Metrics and Outdoor Reference Consensus.
 */
ZTEST(thermodynamic_engine_suite, test_spatial_temperature_metrics)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    thermo_engine_init_default_baseline(&baseline);
    setup_test_matrix_nominal(&matrix);

    input.matrix = &matrix;
    input.p_elec_total_kw = 5.0f;
    input.fan_active_mask = 0x07U;
    input.f_carga = 1.0f;

    /* Set Room probes to 21.0 C and 25.0 C (Delta T = 4.0 C > 3.5 C hotspot) */
    matrix.t_room[0] = 21.0f;
    matrix.t_room[1] = 25.0f;

    /* Set Outdoor probes within 4 C spread (30.0 C and 33.0 C -> mean = 31.5 C) */
    matrix.t_outdoor[0] = 30.0f;
    matrix.t_outdoor[1] = 33.0f;

    int rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");

    zassert_within(out.t_room_avg_c, 23.0f, 0.01f, "Room avg mismatch");
    zassert_within(out.delta_t_room_max_c, 4.0f, 0.01f, "Room delta max mismatch");
    zassert_true(out.diagnostics.room_hotspot, "Room hotspot alarm should trigger");
    zassert_within(out.t_outdoor_eff_c, 31.5f, 0.01f, "Outdoor mean consensus mismatch");

    /* Case B: Outdoor probes diverge > 4 C (e.g. 28.0 C and 36.0 C -> min = 28.0 C) */
    matrix.t_outdoor[0] = 28.0f;
    matrix.t_outdoor[1] = 36.0f;

    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");
    zassert_within(out.t_outdoor_eff_c, 28.0f, 0.01f, "Outdoor min consensus mismatch on high spread");
}

/**
 * @brief Test First and Second Law Thermodynamic Performance (COP and Exergy).
 */
ZTEST(thermodynamic_engine_suite, test_cop_carnot_and_exergy)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    thermo_engine_init_default_baseline(&baseline);
    setup_test_matrix_nominal(&matrix);

    input.matrix = &matrix;
    input.fan_active_mask = 0x07U;
    input.f_carga = 1.0f;

    /* Case A: Power is zero -> COP and exergy should be 0 (no division by zero) */
    input.p_elec_total_kw = 0.0f;
    int rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");
    zassert_within(out.cop_real, 0.0f, 0.001f, "COP real should be 0");
    zassert_within(out.eta_ii_pct, 0.0f, 0.001f, "Eta II should be 0");

    /* Case B: Normal active cooling */
    input.p_elec_total_kw = 4.0f;
    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");

    zassert_true(out.cop_real > 2.0f, "COP real expected > 2.0");
    zassert_true(out.cop_carnot > 4.0f, "Carnot COP expected > 4.0");
    zassert_true(out.eta_ii_pct >= 25.0f && out.eta_ii_pct <= 65.0f,
                 "Second law exergetic efficiency expected in reasonable range (25..65 %)");
}

/**
 * @brief Test Predictive Maintenance Rules and Days Remaining Precedence Hierarchy (F-04).
 */
ZTEST(thermodynamic_engine_suite, test_predictive_rules_and_days_remaining_hierarchy)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    thermo_engine_init_default_baseline(&baseline);
    setup_test_matrix_nominal(&matrix);

    input.matrix = &matrix;
    input.p_elec_total_kw = 4.0f;
    input.fan_active_mask = 0x07U;
    input.f_carga = 1.0f;

    /* Condition 1: RAM hours < 168 -> must lock to 100 days (0xFFFF raw) */
    baseline.hours_ram_history = 100U;
    int rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");

    for (uint8_t k = 0; k < 6; k++) {
        zassert_within(out.diagnostics.days_remaining[k], THERMO_DAYS_REMAINING_INIT_DAYS, 0.01f,
                       "Condition 1 days remaining mismatch");
        zassert_equal(out.diagnostics.days_remaining_raw[k], THERMO_DAYS_REMAINING_INIT_RAW,
                      "Condition 1 raw code mismatch");
    }

    /* Condition 2: RAM hours >= 168, but filter < 25% -> clamp to 90 days */
    baseline.hours_ram_history = 200U;
    baseline.m_t_day_c[0] = 0.0f;
    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");

    zassert_within(out.diagnostics.days_remaining[0], THERMO_DAYS_REMAINING_CEILING, 0.01f,
                   "Condition 2 clean filter ceiling mismatch");
    zassert_equal(out.diagnostics.days_remaining_raw[0], (uint16_t)THERMO_DAYS_REMAINING_CEILING,
                  "Condition 2 raw code mismatch");

    /* Condition 3: Active degradation (%Filter >= 25% and negative drift m_T < -0.05 C/day) */
    /* Drive T_inj down to simulate severe fouling: T_inj = 9.0 C, delta_T = 15.0 C */
    matrix.t_inj[0] = 9.0f;
    matrix.t_ret[0] = 24.0f;
    baseline.t_inj_base_c[0] = 13.0f;
    baseline.delta_t_base_c[0] = 10.0f;
    baseline.m_t_day_c[0] = -0.10f; /* -0.10 C/day */

    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");

    zassert_true(out.diagnostics.filter_fouling_pct[0] >= 25.0f, "Filter fouling should exceed 25%");
    /* Numer = 9.0 - 7.5 = 1.5; Denom = 0.10 -> 15.0 days remaining */
    zassert_within(out.diagnostics.days_remaining[0], 15.0f, 0.5f,
                   "Condition 3 days remaining degradation mismatch");
    zassert_equal(out.diagnostics.days_remaining_raw[0], 15U, "Raw code mismatch");
}

/**
 * @brief Test Weather-Corrected Free Capacity Margin (kW_free).
 */
ZTEST(thermodynamic_engine_suite, test_weather_corrected_free_capacity)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    thermo_engine_init_default_baseline(&baseline);
    setup_test_matrix_nominal(&matrix);

    /* 2 redundant units required, 10.5 kW each -> 21.0 kW base */
    cfg.u_req = 2U;
    cfg.q_nominal_unit_kw[0] = 10.5f;
    cfg.q_nominal_unit_kw[1] = 10.5f;
    cfg.t_ext_design_c = 35.0f;
    cfg.k_ext_derating = 0.005f;

    input.matrix = &matrix;
    input.p_elec_total_kw = 4.0f;
    input.fan_active_mask = 0x07U;
    input.f_carga = 1.0f;

    /* Case A: Outdoor temp = 35.0 C (design temp -> no thermal derating) */
    matrix.t_outdoor[0] = 35.0f;
    matrix.t_outdoor[1] = 35.0f;

    int rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");

    zassert_within(out.q_max_safe_kw, 21.0f, 0.5f, "Safe max capacity mismatch");
    zassert_true(out.kw_free > 0.0f, "Free capacity should be positive");
    zassert_within(out.kw_free, out.q_max_safe_kw - out.q_sensible_total_kw, 0.01f,
                   "kW_free margin formula mismatch");

    /* Case B: High ambient 45.0 C (10 C above design -> 5% derating) */
    matrix.t_outdoor[0] = 45.0f;
    matrix.t_outdoor[1] = 45.0f;

    rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Compute failed");

    /* 21.0 * (1 - 0.05) = 19.95 kW */
    zassert_within(out.q_max_safe_kw, 19.95f, 0.5f, "Derated max capacity mismatch");
}

/**
 * @brief Test Multi-Concentrator Scalability (16 pairs, 16 room, 8 outdoor probes).
 */
ZTEST(thermodynamic_engine_suite, test_multi_concentrator_scalability)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    cfg.configured_units = 8U; /* Scale up to 8 HVAC units */
    for (uint8_t u = 0; u < 8; u++) {
        cfg.v_nom_m3h[u] = 1500.0f;
        cfg.q_nominal_unit_kw[u] = 12.0f;
    }
    thermo_engine_init_default_baseline(&baseline);

    memset(&matrix, 0, sizeof(matrix));
    matrix.configured_pairs = SENSOR_MATRIX_MAX_PAIRS;          /* 16 pairs */
    matrix.pair_valid_mask = 0xFFFFU;                           /* All 16 valid */
    matrix.configured_room_probes = SENSOR_MATRIX_MAX_ROOM_PROBES; /* 16 room probes */
    matrix.room_valid_mask = 0xFFFFU;
    matrix.configured_outdoor_probes = SENSOR_MATRIX_MAX_OUTDOOR_PROBES; /* 8 outdoor probes */
    matrix.outdoor_valid_mask = 0xFFU;

    /* Distribute 16 pairs across 8 units (2 pairs per unit) */
    for (uint8_t k = 0; k < 16; k++) {
        matrix.t_inj[k] = 12.5f + 0.1f * (float)k;
        matrix.t_ret[k] = 22.5f + 0.1f * (float)k;
        matrix.pair_hvac_unit_map[k] = (k / 2U) + 1U;
    }

    for (uint8_t m = 0; m < 16; m++) {
        matrix.t_room[m] = 22.0f + 0.05f * (float)m;
    }

    for (uint8_t l = 0; l < 8; l++) {
        matrix.t_outdoor[l] = 31.0f + 0.1f * (float)l;
    }

    matrix.humidity_return_valid = true;
    matrix.humidity_return_pct = 48.0f;

    input.matrix = &matrix;
    input.p_elec_total_kw = 25.0f;
    input.fan_active_mask = 0xFFU; /* All 8 units fans ON */
    input.f_carga = 1.0f;

    int rc = thermo_engine_compute(&cfg, &baseline, &input, &out);
    zassert_equal(rc, 0, "Scalability compute failed");

    zassert_equal(out.active_pairs_count, 16U, "All 16 pairs must be active");
    zassert_equal(out.valid_room_probes_count, 16U, "All 16 room probes must be valid");
    zassert_equal(out.valid_outdoor_probes_count, 8U, "All 8 outdoor probes must be valid");

    /* 8 units * (1500 / 3600) * ~1.17 = ~3.9 kg/s */
    zassert_within(out.m_da_total_kg_s, 3.9f, 0.3f, "16-pair scaled mass flow mismatch");
    zassert_true(out.q_sensible_total_kw > 35.0f, "Scaled capacity expected > 35 kW");
    zassert_true(out.cop_real > 1.5f, "Scaled COP real expected > 1.5");
}

/**
 * @brief Test Error Handling and Null Pointer Guards.
 */
ZTEST(thermodynamic_engine_suite, test_null_and_invalid_arguments)
{
    thermo_config_t cfg;
    thermo_baseline_cfg_t baseline;
    thermo_output_t out;
    thermal_matrix_frame_t matrix;
    thermo_input_t input;

    thermo_engine_init_default_config(&cfg);
    thermo_engine_init_default_baseline(&baseline);
    setup_test_matrix_nominal(&matrix);
    input.matrix = &matrix;

    zassert_equal(thermo_engine_init_default_config(NULL), -EINVAL, "Null config guard failed");
    zassert_equal(thermo_engine_init_default_baseline(NULL), -EINVAL, "Null baseline guard failed");

    zassert_equal(thermo_engine_compute(NULL, &baseline, &input, &out), -EINVAL, "Null cfg compute guard failed");
    zassert_equal(thermo_engine_compute(&cfg, &baseline, NULL, &out), -EINVAL, "Null input compute guard failed");
    zassert_equal(thermo_engine_compute(&cfg, &baseline, &input, NULL), -EINVAL, "Null output compute guard failed");

    input.matrix = NULL;
    zassert_equal(thermo_engine_compute(&cfg, &baseline, &input, &out), -EINVAL, "Null matrix compute guard failed");
}
