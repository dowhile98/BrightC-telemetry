/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_psychrometrics.c
 * @brief Unit tests for moist air psychrometrics, pressure and density (Suite 3).
 */

#include <zephyr/ztest.h>
#include <math.h>

#include "domain/thermal_energy/psychrometrics.h"

ZTEST_SUITE(psychrometrics_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(psychrometrics_suite, test_psychrometric_sea_level_nominal)
{
    /* Z = 0 m, T_ret = 24.0 deg C, HR_ret = 50.0 % */
    float p_atm = psychrometrics_p_atm(0);
    zassert_within(p_atm, 101.325f, 0.001f, "P_atm at sea level must be 101.325 kPa");

    float p_ws = psychrometrics_p_ws(24.0f);
    zassert_within(p_ws, 2.985f, 0.005f, "P_ws at 24C must be 2.985 kPa");

    float p_w = psychrometrics_p_w(p_ws, 50.0f);
    zassert_within(p_w, 1.493f, 0.005f, "P_w at 24C 50%% must be 1.493 kPa");

    float omega = psychrometrics_humidity_ratio(p_atm, p_w);
    zassert_within(omega, 0.00931f, 0.00005f, "omega at 24C 50%% must be 0.00931 kg/kg");

    float t_dew = psychrometrics_dew_point(p_w);
    zassert_within(t_dew, 12.93f, 0.05f, "T_dew at P_w=1.493 must be 12.93 deg C");

    float rho_da = psychrometrics_dry_air_density(p_atm, p_w, 24.0f);
    zassert_within(rho_da, 1.170f, 0.005f, "rho_da at 24C 50%% must be 1.170 kg/m3");
}

ZTEST(psychrometrics_suite, test_psychrometric_high_altitude_bogota)
{
    /* Z = 2600 m, T_ret = 22.0 deg C, HR_ret = 60.0 % */
    float p_atm = psychrometrics_p_atm(2600);
    zassert_within(p_atm, 73.75f, 0.40f, "P_atm at 2600 m must be ~73.75 kPa");

    float p_ws = psychrometrics_p_ws(22.0f);
    zassert_within(p_ws, 2.645f, 0.005f, "P_ws at 22C must be 2.645 kPa");

    float p_w = psychrometrics_p_w(p_ws, 60.0f);
    zassert_within(p_w, 1.587f, 0.005f, "P_w at 22C 60%% must be 1.587 kPa");

    float omega = psychrometrics_humidity_ratio(p_atm, p_w);
    zassert_within(omega, 0.01368f, 0.00010f, "omega at 2600m 22C 60%% must be ~0.01368 kg/kg");

    float rho_da = psychrometrics_dry_air_density(p_atm, p_w, 22.0f);
    zassert_within(rho_da, 0.854f, 0.005f, "rho_da at 2600m 22C must be 0.854 kg/m3");
}

ZTEST(psychrometrics_suite, test_psychrometric_ashrae_25c_50rh)
{
    /* Standard ASHRAE Fundamentals test point: 25.0 deg C, 50.0 % RH at sea level */
    float p_atm = psychrometrics_p_atm(0);
    float p_ws = psychrometrics_p_ws(25.0f);
    zassert_within(p_ws, 3.169f, 0.010f, "P_ws at 25C must be ~3.169 kPa");

    float p_w = psychrometrics_p_w(p_ws, 50.0f);
    zassert_within(p_w, 1.585f, 0.010f, "P_w at 25C 50%% must be ~1.585 kPa");

    float omega = psychrometrics_humidity_ratio(p_atm, p_w);
    zassert_within(omega, 0.00988f, 0.0001f, "omega at 25C 50%% must be ~0.00988 kg/kg");

    float t_dew = psychrometrics_dew_point(p_w);
    zassert_within(t_dew, 13.87f, 0.10f, "T_dew at 25C 50%% must be ~13.87 deg C");

    float rho_da = psychrometrics_dry_air_density(p_atm, p_w, 25.0f);
    zassert_within(rho_da, 1.165f, 0.005f, "rho_da at 25C 50%% must be ~1.165 kg/m3");
}

ZTEST(psychrometrics_suite, test_psychrometric_air_mass_flow)
{
    float nominal_airflow = 4500.0f; /* m3/h */
    float rho_da = 1.170f;           /* kg/m3 */

    /* Active fan */
    float m_dot_active = psychrometrics_air_mass_flow(nominal_airflow, rho_da, true);
    float expected = (4500.0f / 3600.0f) * 1.170f; /* 1.4625 kg/s */
    zassert_within(m_dot_active, expected, 0.0001f, "m_dot when fan active must be 1.4625 kg/s");

    /* Inactive fan -> must strictly return 0.0 kg/s (Decoupling Rule F-01) */
    float m_dot_inactive = psychrometrics_air_mass_flow(nominal_airflow, rho_da, false);
    zassert_equal(m_dot_inactive, 0.0f, "m_dot must be 0.0 kg/s when fan inactive");
}

ZTEST(psychrometrics_suite, test_psychrometric_defensive_bounds)
{
    /* Altitude clamping [-100, 4000] */
    float p_below = psychrometrics_p_atm(-500);
    float p_min_clamp = psychrometrics_p_atm(-100);
    zassert_equal(p_below, p_min_clamp, "Altitude below -100m must be clamped");

    float p_above = psychrometrics_p_atm(6000);
    float p_max_clamp = psychrometrics_p_atm(4000);
    zassert_equal(p_above, p_max_clamp, "Altitude above 4000m must be clamped");

    /* Negative / zero vapor pressure */
    zassert_equal(psychrometrics_humidity_ratio(101.325f, -1.0f), 0.0f,
                  "Negative P_w must return 0.0");
    zassert_equal(psychrometrics_dew_point(0.0f), -50.0f,
                  "Zero P_w must return safe -50C");

    /* Denominator limit: P_atm - P_w <= 1.0 kPa */
    zassert_equal(psychrometrics_humidity_ratio(100.0f, 99.5f), 0.0f,
                  "Denom <= 1.0 kPa must return 0.0");

    /* Extreme cold dew point */
    zassert_equal(psychrometrics_dew_point(0.0005f), -50.0f,
                  "P_w <= 0.001 kPa must return -50.0C");
}
