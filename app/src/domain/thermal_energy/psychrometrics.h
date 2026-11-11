/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file psychrometrics.h
 * @brief Moist air psychrometrics, barometric pressure, vapor pressures, dew point,
 *        air density, and mass flow calculations (Revision F Baseline).
 *
 * Pure C99/C11 Domain Layer:
 * - Zero Zephyr RTOS dependencies
 * - Zero hardware register dependencies
 * - Zero dynamic memory allocation
 */

#ifndef APP_DOMAIN_THERMAL_ENERGY_PSYCHROMETRICS_H_
#define APP_DOMAIN_THERMAL_ENERGY_PSYCHROMETRICS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Physical Constants & Formulation Parameters                               */
/* ========================================================================= */

/** Ideal gas constant for dry air in kJ/(kg·K) */
#define PSYCHRO_R_DRY_AIR_KJ_KG_K        (0.287055f)

/** Zero degrees Celsius water vapor reference offset in Kelvin */
#define PSYCHRO_KELVIN_OFFSET_C          (273.15f)

/** Sea level standard atmospheric pressure in kPa */
#define PSYCHRO_P_ATM_SEA_LEVEL_KPA      (101.325f)

/** Safe minimum barometric denominator threshold (P_atm - P_w > 1.0 kPa) */
#define PSYCHRO_MIN_P_DIFF_KPA           (1.0f)

/* ========================================================================= */
/* Public Psychrometric Calculation Functions                                */
/* ========================================================================= */

/**
 * @brief Calculate site barometric pressure from altitude above sea level.
 *
 * P_atm = 101.325 * (1.0 - 2.25577e-5 * Z)^5.25588 [kPa]
 * Altitude clamped to [-100, 4000] meters.
 *
 * @param[in] altitude_m Elevation in meters above sea level.
 * @return Atmospheric pressure in kPa.
 */
float psychrometrics_p_atm(int16_t altitude_m);

/**
 * @brief Calculate saturation vapor pressure of moist air via Magnus-Tetens.
 *
 * P_ws = 0.61078 * exp((17.27 * T) / (T + 237.3)) [kPa]
 *
 * @param[in] temp_c Dry-bulb temperature in °C.
 * @return Saturation vapor pressure in kPa.
 */
float psychrometrics_p_ws(float temp_c);

/**
 * @brief Calculate partial vapor pressure of moist air.
 *
 * P_w = P_ws * (rh_pct / 100.0) [kPa]
 *
 * @param[in] p_ws_kpa Saturation vapor pressure in kPa.
 * @param[in] rh_pct   Relative humidity in % (0.0 to 100.0%).
 * @return Partial vapor pressure in kPa.
 */
float psychrometrics_p_w(float p_ws_kpa, float rh_pct);

/**
 * @brief Calculate humidity ratio (absolute humidity omega) of moist air.
 *
 * omega = 0.62198 * P_w / (P_atm - P_w) [kg_vap/kg_da]
 * Safe denominator: P_atm - P_w > 1.0 kPa.
 *
 * @param[in] p_atm_kpa Atmospheric pressure in kPa.
 * @param[in] p_w_kpa   Partial vapor pressure in kPa.
 * @return Humidity ratio in kg_water_vapor / kg_dry_air.
 */
float psychrometrics_humidity_ratio(float p_atm_kpa, float p_w_kpa);

/**
 * @brief Calculate dry air density.
 *
 * rho_da = (P_atm - P_w) / (0.287055 * (T + 273.15)) [kg/m^3]
 *
 * @param[in] p_atm_kpa Atmospheric pressure in kPa.
 * @param[in] p_w_kpa   Partial vapor pressure in kPa.
 * @param[in] temp_c    Dry-bulb temperature in °C.
 * @return Dry air density in kg/m^3.
 */
float psychrometrics_dry_air_density(float p_atm_kpa, float p_w_kpa, float temp_c);

/**
 * @brief Calculate dew point temperature from partial vapor pressure.
 *
 * gamma = ln(P_w / 0.61078)
 * T_dew = (237.3 * gamma) / (17.27 - gamma) [°C]
 * Defended: P_w > 0.001 kPa and |17.27 - gamma| > 0.01.
 *
 * @param[in] p_w_kpa Partial vapor pressure in kPa.
 * @return Dew point temperature in °C.
 */
float psychrometrics_dew_point(float p_w_kpa);

/**
 * @brief Calculate dry air mass flow rate for an evaporator coil or HVAC unit.
 *
 * m_dot_da = (nominal_airflow_m3h / 3600.0) * rho_da [kg/s] if fan_active == true, else 0.0f.
 *
 * @param[in] nominal_airflow_m3h Volumetric design airflow in m^3/h.
 * @param[in] rho_da              Dry air density in kg/m^3.
 * @param[in] fan_active          Confirmed active supply fan status (U_vent_ON).
 * @return Mass flow rate in kg/s.
 */
float psychrometrics_air_mass_flow(float nominal_airflow_m3h, float rho_da, bool fan_active);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_THERMAL_ENERGY_PSYCHROMETRICS_H_ */
