/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file thermodynamics.h
 * @brief Sensible and latent heat balances, COP, Carnot ceiling, and exergetic efficiency
 *        calculations (Revision F Baseline).
 *
 * Pure C99/C11 Domain Layer:
 * - Zero Zephyr RTOS dependencies
 * - Zero hardware register dependencies
 * - Zero dynamic memory allocation
 */

#ifndef APP_DOMAIN_THERMAL_ENERGY_THERMODYNAMICS_H_
#define APP_DOMAIN_THERMAL_ENERGY_THERMODYNAMICS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Physical Constants & Formulation Parameters                               */
/* ========================================================================= */

/** Specific heat capacity of dry air in kJ/(kg·°C) */
#define THERMO_CP_DRY_AIR_KJ_KG_C        (1.006f)

/** Specific heat capacity of water vapor in kJ/(kg·°C) */
#define THERMO_CP_VAPOR_KJ_KG_C          (1.86f)

/** Water latent heat of vaporization at coil temperature in kJ/kg */
#define THERMO_H_FG_COIL_KJ_KG           (2465.0f)

/** Effective discharge relative humidity factor on coil fins (92% saturation) */
#define THERMO_COIL_DISCHARGE_RH_FACTOR  (0.92f)

/** Default evaporator heat exchanger approach / pinch differential in Kelvin */
#define THERMO_DEFAULT_PINCH_EVAP_K      (8.0f)

/** Default condenser heat exchanger approach / pinch differential in Kelvin */
#define THERMO_DEFAULT_PINCH_COND_K      (10.0f)

/** Minimum temperature lift denominator in Carnot COP to avoid division by zero */
#define THERMO_CARNOT_MIN_LIFT_K         (5.0f)

/** Minimum active electrical power required for valid COP calculation [kW] */
#define THERMO_MIN_ELEC_POWER_FOR_COP_KW (0.5f)

/* ========================================================================= */
/* Input / Output Data Structures                                            */
/* ========================================================================= */

/**
 * @brief Input parameters for thermodynamic evaluation.
 */
typedef struct {
    uint8_t  configured_pairs;                   /**< Number of configured thermal pairs (1..THERMAL_MAX_PAIRS) */
    float    t_inj[THERMAL_MAX_PAIRS];           /**< Cold supply air temperatures [deg C] */
    float    t_ret[THERMAL_MAX_PAIRS];           /**< Warm return air temperatures [deg C] */
    float    m_dot_da[THERMAL_MAX_PAIRS];        /**< Dry air mass flow rate per pair [kg/s] */
    uint16_t pair_valid_mask;                    /**< Bit k = 1 if both probes and fan for pair k are valid/active */

    float    p_atm_kpa;                          /**< Site barometric pressure [kPa] */
    float    omega_ret;                          /**< Return air humidity ratio [kg_vap/kg_da] */
    float    t_rocio_ret;                        /**< Return air dew point temperature [deg C] */

    float    t_inj_mean;                         /**< Airflow-weighted mean supply temperature [deg C] */
    float    t_outdoor_eff;                      /**< Effective outdoor condensing reference temp [deg C] */

    float    total_active_power_kw;              /**< Total electrical active power P_elec [kW] */
    bool     elec_power_valid;                   /**< True if electrical meter is valid */
} thermodynamics_input_t;

/**
 * @brief Output parameters resulting from thermodynamic evaluation.
 */
typedef struct {
    float            delta_t[THERMAL_MAX_PAIRS];        /**< Thermal lift (T_ret - T_inj) per pair [deg C] */
    float            q_sensible_pair[THERMAL_MAX_PAIRS];/**< Sensible cooling capacity per pair [kW] */
    float            q_latent_pair[THERMAL_MAX_PAIRS];  /**< Latent cooling capacity per pair [kW] */
    float            q_total_pair[THERMAL_MAX_PAIRS];   /**< Total cooling capacity per pair [kW] */

    float            q_sensible_total;                  /**< Total room sensible cooling capacity [kW] */
    float            q_latent_total;                    /**< Total room latent cooling capacity [kW] */
    float            q_total;                           /**< Total room cooling capacity (Q_sen + Q_lat) [kW] */

    float            m_dot_condensate_lph;              /**< Condensate drainage rate [Liters/hour] */
    float            shf;                               /**< Sensible Heat Factor (0.0 to 1.0) */

    float            cop_real;                          /**< Actual operational COP (Q_total / P_elec) */
    float            cop_sensible;                      /**< Sensible COP (Q_sensible / P_elec) */
    quality_status_t cop_quality;                       /**< Quality status of COP calculation */

    float            t_evap_sat_k;                      /**< Evaporator saturation temperature [K] */
    float            t_cond_sat_k;                      /**< Condenser saturation temperature [K] */
    float            cop_carnot;                        /**< Theoretical Carnot maximum COP ceiling */
    float            eta_ii_pct;                        /**< Second Law exergetic efficiency [%] (0.0 to 100.0%) */
} thermodynamics_output_t;

/* ========================================================================= */
/* Public Domain Functions                                                   */
/* ========================================================================= */

/**
 * @brief Execute pure thermodynamic calculation cycle according to Revision F baseline.
 *
 * @param[in]  input  Pointer to input telemetry and physical constants.
 * @param[out] output Pointer to resulting thermodynamic metrics.
 * @return 0 on success, negative errno (-EINVAL) on error.
 */
int thermodynamics_compute(const thermodynamics_input_t *input,
                           thermodynamics_output_t *output);

/**
 * @brief Calculate operational COP given cooling capacity and electrical power.
 *
 * Returns Q / P_elec if P_elec >= 0.5 kW, otherwise returns 0.0f and sets quality to QUALITY_UNAVAILABLE.
 *
 * @param[in]  q_kw        Cooling capacity in kW.
 * @param[in]  p_elec_kw   Active electrical power in kW.
 * @param[out] out_quality Pointer to store resulting quality status (optional, may be NULL).
 * @return Computed COP.
 */
float thermodynamics_calc_cop(float q_kw, float p_elec_kw, quality_status_t *out_quality);

/**
 * @brief Calculate theoretical Carnot COP ceiling.
 *
 * COP_Carnot = T_evap_sat / max(5.0 K, T_cond_sat - T_evap_sat)
 *
 * @param[in] t_evap_sat_k Evaporator saturation temperature in Kelvin.
 * @param[in] t_cond_sat_k Condenser saturation temperature in Kelvin.
 * @return Carnot COP ceiling.
 */
float thermodynamics_calc_carnot_cop(float t_evap_sat_k, float t_cond_sat_k);

/**
 * @brief Calculate Second Law exergetic efficiency percentage.
 *
 * eta_II = (COP_real / COP_Carnot) * 100.0%
 *
 * @param[in] cop_real   Actual operational COP.
 * @param[in] cop_carnot Theoretical Carnot COP.
 * @return Exergetic efficiency in percentage (0.0 to 100.0%).
 */
float thermodynamics_calc_eta_ii(float cop_real, float cop_carnot);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_THERMAL_ENERGY_THERMODYNAMICS_H_ */
