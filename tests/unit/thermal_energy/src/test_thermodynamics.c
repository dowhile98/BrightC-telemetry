/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_thermodynamics.c
 * @brief Unit tests for thermodynamic balances, COP, Carnot ceiling and exergy (Suite 4).
 */

#include <zephyr/ztest.h>
#include <math.h>

#include "domain/thermal_energy/thermodynamics.h"
#include "domain/thermal_energy/psychrometrics.h"

ZTEST_SUITE(thermodynamics_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(thermodynamics_suite, test_cop_nominal_operation)
{
    quality_status_t q = QUALITY_UNAVAILABLE;
    float cop = thermodynamics_calc_cop(42.0f, 12.0f, &q);

    zassert_equal(q, QUALITY_VALID, "COP must be QUALITY_VALID when P_elec >= 0.5 kW");
    zassert_within(cop, 3.500f, 0.001f, "COP must be exactly 42.0 / 12.0 = 3.500");
}

ZTEST(thermodynamics_suite, test_cop_zero_power_division_guard)
{
    quality_status_t q = QUALITY_VALID;

    /* P_elec = 0.0 kW */
    float cop = thermodynamics_calc_cop(20.0f, 0.0f, &q);
    zassert_equal(q, QUALITY_UNAVAILABLE, "COP must be QUALITY_UNAVAILABLE when P_elec = 0.0");
    zassert_equal(cop, 0.0f, "COP must be 0.0 when P_elec = 0.0");

    /* P_elec = 0.4 kW (< 0.5 kW threshold) */
    cop = thermodynamics_calc_cop(20.0f, 0.4f, &q);
    zassert_equal(q, QUALITY_UNAVAILABLE, "COP must be QUALITY_UNAVAILABLE when P_elec < 0.5 kW");
    zassert_equal(cop, 0.0f, "COP must be 0.0 when P_elec < 0.5 kW");

    /* NaN and Inf */
    cop = thermodynamics_calc_cop(NAN, 10.0f, &q);
    zassert_equal(q, QUALITY_UNAVAILABLE, "NaN capacity must return QUALITY_UNAVAILABLE");
    cop = thermodynamics_calc_cop(20.0f, INFINITY, &q);
    zassert_equal(q, QUALITY_UNAVAILABLE, "Inf power must return QUALITY_UNAVAILABLE");
}

ZTEST(thermodynamics_suite, test_carnot_and_exergy_nominal)
{
    /* T_inj_mean = 12.0 deg C, T_outdoor_eff = 30.0 deg C, COP_real = 3.50 */
    float t_evap_sat_k = (12.0f - 8.0f) + 273.15f;  /* 277.15 K */
    float t_cond_sat_k = (30.0f + 10.0f) + 273.15f; /* 313.15 K */

    float cop_carnot = thermodynamics_calc_carnot_cop(t_evap_sat_k, t_cond_sat_k);
    /* Lift = 36.0 K -> COP_Carnot = 277.15 / 36.0 = 7.6986 */
    zassert_within(cop_carnot, 7.698f, 0.005f, "COP_Carnot must be ~7.698");

    float eta_ii = thermodynamics_calc_eta_ii(3.50f, cop_carnot);
    /* eta_II = (3.50 / 7.6986) * 100 = 45.46% */
    zassert_within(eta_ii, 45.46f, 0.05f, "eta_II must be ~45.46%%");
}

ZTEST(thermodynamics_suite, test_carnot_extreme_cold_ambient)
{
    /* Extreme cold ambient where T_cond would be lower than T_evap:
     * e.g. T_inj = 14.0 C -> T_evap = (14-8) + 273.15 = 279.15 K
     * T_ext = -10.0 C -> T_cond = (-10+10) + 273.15 = 273.15 K
     * Lift = -6.0 K -> Must be clamped to min lift 5.0 K
     */
    float t_evap = 279.15f;
    float t_cond = 273.15f;
    float cop_carnot = thermodynamics_calc_carnot_cop(t_evap, t_cond);

    /* With min lift = 5.0 K, COP_Carnot = 279.15 / 5.0 = 55.83 */
    zassert_within(cop_carnot, 279.15f / 5.0f, 0.01f, "Carnot lift must be clamped to 5.0 K");
}

ZTEST(thermodynamics_suite, test_thermodynamics_dry_regime)
{
    thermodynamics_input_t input;
    thermodynamics_output_t output;
    memset(&input, 0, sizeof(input));

    input.configured_pairs = 1U;
    input.pair_valid_mask = 0x0001U;
    input.t_ret[0] = 24.0f;
    input.t_inj[0] = 14.0f;
    input.m_dot_da[0] = 1.4625f; /* 4500 m3/h at 1.17 kg/m3 */

    input.p_atm_kpa = 101.325f;
    /* 24C, 40% RH -> P_ws = 2.985 kPa, P_w = 1.194 kPa, T_rocio ~ 9.5 C, omega ~ 0.00741 */
    float p_ws = psychrometrics_p_ws(24.0f);
    float p_w = psychrometrics_p_w(p_ws, 40.0f);
    input.omega_ret = psychrometrics_humidity_ratio(input.p_atm_kpa, p_w);
    input.t_rocio_ret = psychrometrics_dew_point(p_w); /* ~9.5 C */

    input.t_inj_mean = 14.0f;
    input.t_outdoor_eff = 30.0f;
    input.total_active_power_kw = 5.0f;
    input.elec_power_valid = true;

    /* T_inj (14.0 C) > T_rocio (~9.5 C) -> Dry regime! */
    int ret = thermodynamics_compute(&input, &output);
    zassert_equal(ret, 0, "thermodynamics_compute must succeed");

    zassert_within(output.delta_t[0], 10.0f, 0.001f, "Delta T must be 10.0 C");
    zassert_true(output.q_sensible_total > 14.0f, "Sensible capacity must be > 14 kW");
    zassert_equal(output.q_latent_total, 0.0f, "Dry regime must have zero latent capacity");
    zassert_within(output.shf, 1.0f, 0.0001f, "SHF must be 1.00 in dry regime");
    zassert_equal(output.m_dot_condensate_lph, 0.0f, "Condensate rate must be 0 in dry regime");
    zassert_equal(output.cop_quality, QUALITY_VALID, "COP must be valid");
    zassert_true(output.cop_real > 2.5f, "COP should be > 2.5");
}

ZTEST(thermodynamics_suite, test_thermodynamics_wet_regime)
{
    thermodynamics_input_t input;
    thermodynamics_output_t output;
    memset(&input, 0, sizeof(input));

    input.configured_pairs = 1U;
    input.pair_valid_mask = 0x0001U;
    input.t_ret[0] = 24.0f;
    input.t_inj[0] = 11.0f;
    input.m_dot_da[0] = 1.4625f;

    input.p_atm_kpa = 101.325f;
    /* 24C, 65% RH -> P_ws = 2.985 kPa, P_w = 1.940 kPa, T_rocio ~ 17.0 C */
    float p_ws = psychrometrics_p_ws(24.0f);
    float p_w = psychrometrics_p_w(p_ws, 65.0f);
    input.omega_ret = psychrometrics_humidity_ratio(input.p_atm_kpa, p_w);
    input.t_rocio_ret = psychrometrics_dew_point(p_w); /* ~17.0 C */

    input.t_inj_mean = 11.0f;
    input.t_outdoor_eff = 30.0f;
    input.total_active_power_kw = 8.0f;
    input.elec_power_valid = true;

    /* T_inj (11.0 C) < T_rocio (~17.0 C) -> Active condensation regime! */
    int ret = thermodynamics_compute(&input, &output);
    zassert_equal(ret, 0, "thermodynamics_compute must succeed");

    zassert_true(output.q_sensible_total > 15.0f, "Sensible capacity must be > 15 kW");
    zassert_true(output.q_latent_total > 0.0f, "Wet regime must have positive latent capacity");
    zassert_true(output.m_dot_condensate_lph > 0.0f, "Condensate rate must be > 0 in wet regime");
    zassert_true(output.shf < 1.0f, "SHF must be < 1.0 in wet regime");
    zassert_true(output.q_total > output.q_sensible_total, "Q_total must exceed Q_sensible");
}

ZTEST(thermodynamics_suite, test_thermodynamics_defensive_null_and_bounds)
{
    thermodynamics_input_t input;
    thermodynamics_output_t output;
    memset(&input, 0, sizeof(input));

    zassert_equal(thermodynamics_compute(NULL, &output), -EINVAL, "NULL input must return -EINVAL");
    zassert_equal(thermodynamics_compute(&input, NULL), -EINVAL, "NULL output must return -EINVAL");

    /* Pair mask 0 -> all pairs inactive */
    input.configured_pairs = 4U;
    input.pair_valid_mask = 0U;
    int ret = thermodynamics_compute(&input, &output);
    zassert_equal(ret, 0, "All-inactive pairs must succeed");
    zassert_equal(output.q_total, 0.0f, "Total capacity must be 0 when no pairs active");
    zassert_equal(output.cop_quality, QUALITY_UNAVAILABLE, "COP must be unavailable without power");
}
