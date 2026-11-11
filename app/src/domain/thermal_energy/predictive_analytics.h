/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file predictive_analytics.h
 * @brief Pure domain implementation of Ordinary Least Squares (OLS) multi-variable
 *        regression and predictive HVAC degradation diagnostics (Revision F Baseline).
 *
 * Mathematical specifications:
 * - REVF §5.1 / REQ-PRED-01: 64-bit double precision moment accumulation and matrix solve.
 * - REVF §5.1 / REQ-PRED-02: Tikhonov regularization (lambda = 10^-6) for ill-conditioned matrices.
 * - REVF §5.2 / REQ-PRED-03: Expected COP and % mechanical degradation index.
 * - REVF §5.2 / REQ-PRED-04: Filter restriction index and Precedence Hierarchy F-04.
 * - REVF §5.2 / REQ-PRED-05: Outdoor-decoupled refrigerant leak / coil fouling index.
 * - REVF §5.2 / REQ-PRED-06: Room hotspot and airflow bypass anomaly detection.
 * - REVF §5.2 / REQ-PRED-07: Moisture infiltration detection.
 * - REVF §5.4 / REQ-PRED-08: Weather-corrected nominal capacity and free cooling margin.
 *
 * Pure C99/C11 Domain Layer:
 * - Zero Zephyr RTOS dependencies
 * - Zero hardware register dependencies
 * - Zero dynamic memory allocation
 * - Zero file-scope mutable state
 */

#ifndef APP_DOMAIN_THERMAL_ENERGY_PREDICTIVE_ANALYTICS_H_
#define APP_DOMAIN_THERMAL_ENERGY_PREDICTIVE_ANALYTICS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Physical Constants & Diagnostic Thresholds                                */
/* ========================================================================= */

/** Default Tikhonov diagonal regularization parameter lambda = 10^-6 */
#define OLS_DEFAULT_TIKHONOV_LAMBDA          (1.0e-6)

/** Mandatory steady-state training hours before OLS model activation (168 h = 7 days) */
#define OLS_MIN_TRAINING_HOURS               (168U)

/** Initialization code for filter days remaining before 168h training window (100 days) */
#define PREDICTIVE_FILTER_DAYS_INITIALIZING  (100U)

/** Healthy ceiling for filter days remaining when clean or flat slope (90 days) */
#define PREDICTIVE_FILTER_DAYS_HEALTHY_MAX   (90U)

/** Filter loading threshold below which filter is considered clean (25.0 %) */
#define PREDICTIVE_FILTER_CLEAN_PCT_THRESHOLD (25.0f)

/** Filter fouling thermal degradation slope threshold (-0.05 °C/day) */
#define PREDICTIVE_FILTER_SLOPE_THRESHOLD    (-0.05f)

/** Minimum electrical power threshold for COP calculation to avoid division by zero (0.5 kW) */
#define PREDICTIVE_MIN_PELEC_KW              (0.5f)

/** Filter fouling lockout threshold for refrigerant leak detection (50.0 %) */
#define PREDICTIVE_LEAK_FILTER_LOCKOUT_PCT   (50.0f)

/** Minimum compressor load fraction for refrigerant leak detection (20.0 %) */
#define PREDICTIVE_LEAK_MIN_LOAD_FRACTION    (0.20f)

/** Room hotspot maximum temperature spread threshold (3.5 °C) */
#define PREDICTIVE_HOTSPOT_SPREAD_THRESHOLD  (3.5f)

/** Room maximum absolute temperature threshold for airflow bypass alert (27.0 °C) */
#define PREDICTIVE_BYPASS_TSALA_MAX_THRESHOLD (27.0f)

/** Room airflow bypass gradient threshold (T_sala_mean - T_ret_mean > 3.0 °C) */
#define PREDICTIVE_BYPASS_GRADIENT_THRESHOLD (3.0f)

/** Maximum latent load percentage before moisture ingress alert (30.0 %) */
#define PREDICTIVE_LATENT_PCT_THRESHOLD      (30.0f)

/** Minimum Sensible Heat Factor before moisture ingress alert (0.70) */
#define PREDICTIVE_SHF_MIN_THRESHOLD         (0.70f)

/** Maximum return absolute humidity ratio before moisture ingress alert (0.012 kg/kg) */
#define PREDICTIVE_OMEGA_MAX_THRESHOLD       (0.012f)

/** Ambient temperature threshold for weather capacity derating (35.0 °C) */
#define PREDICTIVE_WEATHER_DERATE_TEMP_C     (35.0f)

/** Derating slope per °C above 35.0 °C (0.005 / °C = 0.5% per °C) */
#define PREDICTIVE_WEATHER_DERATE_SLOPE      (0.005f)

/* ========================================================================= */
/* OLS Model Data Structure                                                  */
/* ========================================================================= */

/**
 * @brief Ordinary Least Squares 4-variable regression model context.
 *
 * Model equation:
 *   Q_total = beta_0 + beta_1 * T_ret + beta_2 * T_ext + beta_3 * F_carga
 *
 * Solved via normal equations:
 *   beta = (X^T * X + lambda * I)^-1 * (X^T * Y)
 */
typedef struct {
    double   xtx[4][4];          /**< Accumulated moment matrix X^T * X (double precision) */
    double   xty[4];             /**< Accumulated moment vector X^T * Y (double precision) */
    double   beta[4];            /**< Solved regression coefficients [beta0, beta1, beta2, beta3] */
    uint32_t sample_count;       /**< Total number of accumulated samples */
    uint16_t training_hours;     /**< Accumulated steady-state training hours (0..168+) */
    bool     ols_trained;        /**< True if solved successfully and ready for inference */
    float    condition_number;   /**< Estimated 1-norm condition number of (X^T * X + lambda * I) */
} ols_model_t;

/* ========================================================================= */
/* Public API Function Prototypes                                            */
/* ========================================================================= */

/**
 * @brief Initialize an OLS regression model structure.
 *
 * Zeroes out accumulator matrices, sample counters, and regression flags.
 *
 * @param self Pointer to ols_model_t instance.
 */
void ols_model_init(ols_model_t *self);

/**
 * @brief Accumulate a 4-variable observation into the OLS moment matrices.
 *
 * Feature vector: x = [1.0, t_ret, t_ext, f_carga]
 * Updates:
 *   xtx += x * x^T
 *   xty += x * q_total
 *
 * @param self     Pointer to ols_model_t instance.
 * @param t_ret    Mean return temperature [°C].
 * @param t_ext    Effective outdoor condensing temperature [°C].
 * @param f_carga  Compressor operational load fraction (0.0..1.0).
 * @param q_total  Total cooling capacity [kW].
 */
void ols_model_accumulate(ols_model_t *self, double t_ret, double t_ext, double f_carga, double q_total);

/**
 * @brief Solve the OLS normal equations with Tikhonov regularization.
 *
 * Inverts (X^T * X + lambda * I) in double precision and computes beta = A^-1 * (X^T * Y).
 * Computes matrix condition number and defends against singularity.
 *
 * @param self   Pointer to ols_model_t instance.
 * @param lambda Tikhonov regularization factor (if <= 0.0, defaults to 1.0e-6).
 *
 * @return 0 on success (sets ols_trained = true).
 * @return -EINVAL if self is NULL.
 * @return -EDOM if matrix is singular, degenerate, or sample_count < 4 (ols_trained = false).
 */
int ols_model_solve(ols_model_t *self, double lambda);

/**
 * @brief Predict expected total cooling capacity using solved OLS coefficients.
 *
 * @param self     Pointer to ols_model_t instance.
 * @param t_ret    Current return air temperature [°C].
 * @param t_ext    Current outdoor condensing temperature [°C].
 * @param f_carga  Current compressor load fraction (0.0..1.0).
 *
 * @return Expected cooling capacity in kW, or 0.0f if model is untrained or NULL.
 */
float ols_model_predict_q(const ols_model_t *self, float t_ret, float t_ext, float f_carga);

/**
 * @brief Calculate expected COP from predicted capacity and electrical power.
 *
 * @param q_expected_kw Expected cooling capacity [kW].
 * @param p_elec_kw     Active electrical power [kW] (must be >= 0.5 kW).
 *
 * @return Expected COP, or 0.0f if p_elec_kw < 0.5 kW or q_expected_kw <= 0.
 */
float predictive_calc_expected_cop(float q_expected_kw, float p_elec_kw);

/**
 * @brief Calculate % mechanical degradation from operational COP and Second Law efficiency.
 *
 * Evaluates:
 *   %Deg = clamp[0..100]( max( (COP_esp - COP_real)/COP_esp, (eta_II_base - eta_II)/eta_II_base ) * 100.0 )
 *
 * @param cop_real     Actual operational COP.
 * @param cop_expected Expected COP from OLS model.
 * @param eta_ii       Actual Second Law exergetic efficiency [%].
 * @param eta_ii_base  Baseline Second Law exergetic efficiency [%].
 *
 * @return Degradation index clamped to [0.0..100.0] %.
 */
float predictive_calc_mechanical_degradation(float cop_real, float cop_expected, float eta_ii, float eta_ii_base);

/**
 * @brief Calculate air filter restriction loading percentage.
 *
 * Evaluates:
 *   %Filter = clamp[0..100]( 60.0 * (T_inj_base - T_inj) / 3.0 + 40.0 * (DeltaT - DeltaT_base) / 4.0 )
 *
 * @param t_inj_base    Supply temperature baseline at clean filter [°C].
 * @param t_inj         Current supply temperature [°C].
 * @param delta_t_base  Temperature lift baseline at clean filter [°C].
 * @param delta_t       Current temperature lift (T_ret - T_inj) [°C].
 *
 * @return Filter restriction loading clamped to [0.0..100.0] %.
 */
float predictive_calc_filter_restriction(float t_inj_base, float t_inj, float delta_t_base, float delta_t);

/**
 * @brief Calculate air filter days remaining according to Precedence Hierarchy F-04.
 *
 * Priority rules:
 *   1. training_hours < 168 -> 100 days (initialization code / 0xFFFF in Modbus).
 *   2. pct_filter < 25.0% OR slope_c_per_day >= -0.05 °C/day -> 90 days (clean ceiling).
 *   3. training_hours >= 168 AND pct_filter >= 25.0% AND slope_c_per_day < -0.05 °C/day ->
 *      min(90, max(0.0, T_inj - 7.5) / max(|slope|, 0.05)).
 *
 * @param training_hours    Accumulated steady-state hours (0..168+).
 * @param pct_filter        Filter restriction loading index (0.0..100.0 %).
 * @param t_inj             Current cold supply temperature [°C].
 * @param slope_c_per_day   Linear regression rate of change of T_inj [°C/day].
 *
 * @return Projected days remaining until maintenance (0..100 days).
 */
uint16_t predictive_calc_filter_days_remaining(uint16_t training_hours, float pct_filter, float t_inj, float slope_c_per_day);

/**
 * @brief Calculate refrigerant leak / coil fouling probability decoupled from ambient temperature.
 *
 * Evaluates:
 *   If pct_filter >= 50% or f_carga < 0.20 -> return 0.0f (lockout).
 *   DeltaT_target = DeltaT_base * f_carga
 *   Term 1 = 50.0 * (DeltaT_target - DeltaT) / max(0.5 * DeltaT_target, 0.5)
 *   Term 2 = 50.0 * (eta_II_base - eta_II) / max(0.35 * eta_II_base, 1.0)
 *   return clamp[0..100](Term 1 + Term 2)
 *
 * @param delta_t_base  Baseline thermal lift at 100% load [°C].
 * @param delta_t       Current thermal lift (T_ret - T_inj) [°C].
 * @param f_carga       Compressor load fraction (0.0..1.0).
 * @param eta_ii        Current Second Law exergetic efficiency [%].
 * @param eta_ii_base   Baseline Second Law exergetic efficiency [%].
 * @param pct_filter    Current filter restriction loading (0.0..100.0 %).
 *
 * @return Refrigerant leak probability clamped to [0.0..100.0] %.
 */
float predictive_calc_refrigerant_leak(float delta_t_base, float delta_t, float f_carga, float eta_ii, float eta_ii_base, float pct_filter);

/**
 * @brief Check for room ambient thermal hotspot condition.
 *
 * Evaluates:
 *   DeltaT_spread = T_sala_max - T_sala_min
 *   Alert triggered if DeltaT_spread > 3.5 °C.
 *
 * @param t_sala_max  Maximum room zone temperature [°C].
 * @param t_sala_min  Minimum room zone temperature [°C].
 * @param out_spread  Optional output pointer to receive computed spread [°C].
 *
 * @return true if hotspot condition detected, false otherwise.
 */
bool predictive_check_room_hotspot(float t_sala_max, float t_sala_min, float *out_spread);

/**
 * @brief Check for room airflow bypass condition.
 *
 * Evaluates:
 *   DeltaT_bypass = T_sala_mean - T_ret_mean
 *   Alert triggered if T_sala_max > 27.0 °C OR DeltaT_bypass > 3.0 °C.
 *
 * @param t_sala_mean  Mean room ambient temperature [°C].
 * @param t_ret_mean   Mean return air temperature [°C].
 * @param t_sala_max   Maximum room zone temperature [°C].
 * @param out_bypass   Optional output pointer to receive computed bypass gradient [°C].
 *
 * @return true if bypass condition detected, false otherwise.
 */
bool predictive_check_airflow_bypass(float t_sala_mean, float t_ret_mean, float t_sala_max, float *out_bypass);

/**
 * @brief Check for envelope moisture ingress / infiltration condition.
 *
 * Evaluates:
 *   Alert triggered if %Latent > 30.0% OR SHF < 0.70 OR omega_ret > 0.012 kg/kg.
 *
 * @param pct_latent  Latent cooling load percentage (0.0..100.0 %).
 * @param shf         Sensible Heat Factor (0.0..1.0).
 * @param omega_ret   Return absolute humidity ratio [kg_vapor / kg_dry_air].
 *
 * @return true if moisture ingress detected, false otherwise.
 */
bool predictive_check_moisture_ingress(float pct_latent, float shf, float omega_ret);

/**
 * @brief Calculate weather-corrected nominal capacity derating for an HVAC unit.
 *
 * Evaluates:
 *   Q_nom_corr = Q_nominal * [1.0 - 0.005 * max(0.0, T_outdoor - 35.0)]
 *
 * @param q_nominal_kw Nominal nameplate capacity [kW].
 * @param t_outdoor    Effective outdoor condensing temperature [°C].
 *
 * @return Derated capacity [kW].
 */
float predictive_calc_weather_corrected_capacity(float q_nominal_kw, float t_outdoor);

/**
 * @brief Calculate safe redundant capacity across required operating units.
 *
 * Evaluates:
 *   Q_max_safe = (sum_{u=0}^{u_req-1} Q_nom_corr[u]) * (1.0 - %Degradation / 100.0)
 *
 * @param q_nom_corr       Array of weather-corrected nominal capacities [kW].
 * @param u_req            Number of required active units (N required in N+M plant).
 * @param pct_degradation  Plant mechanical degradation percentage (0.0..100.0 %).
 *
 * @return Safe available redundant capacity [kW].
 */
float predictive_calc_safe_redundant_capacity(const float q_nom_corr[], uint8_t u_req, float pct_degradation);

/**
 * @brief Calculate weather-corrected free cooling margin.
 *
 * Evaluates:
 *   kW_free = max(0.0, Q_max_safe - Q_sensible_total)
 *
 * @param q_max_safe         Safe redundant capacity [kW].
 * @param q_sensible_total   Actual total sensible heat dissipated by IT load [kW].
 *
 * @return Free cooling capacity margin [kW].
 */
float predictive_calc_free_cooling_margin(float q_max_safe, float q_sensible_total);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_THERMAL_ENERGY_PREDICTIVE_ANALYTICS_H_ */
