/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file psychrometrics.c
 * @brief Moist air psychrometrics, barometric pressure, vapor pressures, dew point,
 *        air density, and mass flow calculations (Revision F Baseline).
 */

#include "domain/thermal_energy/psychrometrics.h"

#include <math.h>

float psychrometrics_p_atm(int16_t altitude_m)
{
    float alt = (float)altitude_m;
    if (alt < -100.0f) {
        alt = -100.0f;
    } else if (alt > 4000.0f) {
        alt = 4000.0f;
    }

    float base = 1.0f - 2.25577e-5f * alt;
    if (base < 0.01f) {
        base = 0.01f;
    }

    return PSYCHRO_P_ATM_SEA_LEVEL_KPA * powf(base, 5.25588f);
}

float psychrometrics_p_ws(float temp_c)
{
    float denom = temp_c + 237.3f;
    if (denom <= 0.01f) {
        denom = 0.01f;
    }

    return 0.61078f * expf((17.27f * temp_c) / denom);
}

float psychrometrics_p_w(float p_ws_kpa, float rh_pct)
{
    if (p_ws_kpa <= 0.0f) {
        return 0.0f;
    }

    float rh = rh_pct;
    if (rh < 0.0f) {
        rh = 0.0f;
    } else if (rh > 100.0f) {
        rh = 100.0f;
    }

    return p_ws_kpa * (rh / 100.0f);
}

float psychrometrics_humidity_ratio(float p_atm_kpa, float p_w_kpa)
{
    if (p_w_kpa <= 0.0f) {
        return 0.0f;
    }

    float denom = p_atm_kpa - p_w_kpa;
    if (denom <= PSYCHRO_MIN_P_DIFF_KPA) {
        return 0.0f;
    }

    return 0.62198f * (p_w_kpa / denom);
}

float psychrometrics_dry_air_density(float p_atm_kpa, float p_w_kpa, float temp_c)
{
    float p_diff = p_atm_kpa - p_w_kpa;
    if (p_diff <= 0.0f) {
        return 0.0f;
    }

    float t_kelvin = temp_c + PSYCHRO_KELVIN_OFFSET_C;
    if (t_kelvin <= 1.0f) {
        return 0.0f;
    }

    return p_diff / (PSYCHRO_R_DRY_AIR_KJ_KG_K * t_kelvin);
}

float psychrometrics_dew_point(float p_w_kpa)
{
    if (p_w_kpa <= 0.001f) {
        return -50.0f;
    }

    float ratio = p_w_kpa / 0.61078f;
    if (ratio <= 0.0f) {
        return -50.0f;
    }

    float gamma = logf(ratio);
    float denom = 17.27f - gamma;
    if (fabsf(denom) <= 0.01f) {
        return -50.0f;
    }

    return (237.3f * gamma) / denom;
}

float psychrometrics_air_mass_flow(float nominal_airflow_m3h, float rho_da, bool fan_active)
{
    if (!fan_active || nominal_airflow_m3h <= 0.0f || rho_da <= 0.0f) {
        return 0.0f;
    }

    return (nominal_airflow_m3h / 3600.0f) * rho_da;
}
