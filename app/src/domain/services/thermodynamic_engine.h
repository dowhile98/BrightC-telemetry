/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thermodynamic and Psychrometric Engine (Revision F Engineering Baseline).
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies / Zero Dynamic Memory.
 */

#ifndef APP_DOMAIN_SERVICES_THERMODYNAMIC_ENGINE_H_
#define APP_DOMAIN_SERVICES_THERMODYNAMIC_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/sensor_types.h"
#include "common/hvac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Physical Constants & Formulation Parameters                               */
/* ========================================================================= */

/** Ideal gas constant for dry air in kJ/(kg·K) */
#define THERMO_R_DRY_AIR_KJ_KG_K        (0.287055f)

/** Specific heat capacity of dry air at constant pressure in kJ/(kg·°C) */
#define THERMO_CP_DRY_AIR_KJ_KG_C       (1.006f)

/** Specific heat capacity of water vapor in kJ/(kg·°C) */
#define THERMO_CP_VAPOR_KJ_KG_C         (1.86f)

/** Water latent heat of vaporization at coil temperature in kJ/kg */
#define THERMO_H_FG_COIL_KJ_KG          (2465.0f)

/** Zero degrees Celsius water vapor reference enthalpy in kJ/kg */
#define THERMO_H_V0_KJ_KG               (2501.0f)

/** Standard absolute zero Kelvin offset in degrees Celsius */
#define THERMO_KELVIN_OFFSET_C          (273.15f)

/** Effective discharge relative humidity factor on coil fins (92% saturation) */
#define THERMO_COIL_DISCHARGE_RH_FACTOR (0.92f)

/** Default evaporator heat exchanger approach / pinch differential in Kelvin */
#define THERMO_DEFAULT_PINCH_EVAP_K     (8.0f)

/** Default condenser heat exchanger approach / pinch differential in Kelvin */
#define THERMO_DEFAULT_PINCH_COND_K     (10.0f)

/** Minimum temperature lift denominator in Carnot COP to avoid division by zero */
#define THERMO_CARNOT_MIN_LIFT_K        (5.0f)

/** Default design outdoor condensing air temperature in degrees Celsius (AHRI 210/240) */
#define THERMO_DEFAULT_T_EXT_DESIGN_C   (35.0f)

/** Default thermal derating coefficient per degree above design temperature (0.5%/°C) */
#define THERMO_DEFAULT_K_EXT_DERATING   (0.005f)

/** Default Tikhonov diagonal regularization parameter for OLS matrix inversion */
#define THERMO_OLS_DEFAULT_LAMBDA       (1.0e-6)

/** Precedence hierarchy: fixed initialization code for Days Remaining (< 168 hours) */
#define THERMO_DAYS_REMAINING_INIT_DAYS (100.0f)
#define THERMO_DAYS_REMAINING_INIT_RAW  (0xFFFFU)

/** Precedence hierarchy: healthy operating ceiling for Days Remaining */
#define THERMO_DAYS_REMAINING_CEILING   (90.0f)

/** Minimum RAM history hours before evaluating differential drift rate equations */
#define THERMO_DAYS_REMAINING_RAM_GATE  (168U)

/** Coil freeze threshold in degrees Celsius for Days Remaining extrapolation */
#define THERMO_COIL_FREEZE_THRESHOLD_C  (7.5f)

/** Minimum daily temperature drift slope threshold in °C/day */
#define THERMO_MIN_DRIFT_SLOPE_C_DAY    (0.05f)

/** Number of points in Flash R410A Saturation Temperature Lookup Table (0..45 bar) */
#define THERMO_R410A_LUT_POINTS         46U

/* ========================================================================= */
/* Domain Data Structures                                                    */
/* ========================================================================= */

/**
 * @brief Static site thermodynamic configuration parameters.
 */
typedef struct {
    float   site_altitude_m;                         /**< Site altitude Z in meters above sea level */
    uint8_t configured_units;                        /**< Number of HVAC handling units (1..8) */
    float   v_nom_m3h[HVAC_MAX_UNITS_TOTAL];         /**< Nominal volumetric airflow per unit (m3/h) */
    float   delta_t_pinch_evap_k;                    /**< Evaporator approach differential (default 8.0 K) */
    float   delta_t_pinch_cond_k;                    /**< Condenser approach differential (default 10.0 K) */
    float   t_ext_design_c;                          /**< Design outdoor temperature (default 35.0 C) */
    float   k_ext_derating;                          /**< High-ambient derating slope (default 0.005 / C) */
    float   q_nominal_unit_kw[HVAC_MAX_UNITS_TOTAL]; /**< Nameplate cooling capacity per unit (kW) */
    uint8_t u_req;                                   /**< Required operational units for N+1 redundancy */
} thermo_config_t;

/**
 * @brief Learned thermal baseline signature and historical context.
 */
typedef struct {
    float    t_inj_base_c[SENSOR_MATRIX_MAX_PAIRS];   /**< Initial supply temperature per pair (C) */
    float    delta_t_base_c[SENSOR_MATRIX_MAX_PAIRS]; /**< Initial thermal differential per pair (C) */
    float    eta_ii_base_pct;                         /**< Baseline Second Law exergetic efficiency (%) */
    uint32_t hours_ram_history;                       /**< Accumulated steady-state hours in RAM */
    float    m_t_day_c[SENSOR_MATRIX_MAX_PAIRS];      /**< Daily slope of T_inj drift (C/day) */
    double   ols_beta[4];                             /**< Learned OLS coefficients [b0, b1, b2, b3] */
    bool     ols_valid;                               /**< True if OLS model is trained and valid */
} thermo_baseline_cfg_t;

/**
 * @brief Operational runtime inputs fed to the thermodynamic engine.
 */
typedef struct {
    const thermal_matrix_frame_t *matrix;          /**< Ingested semantic sensor matrix frame */
    float                        p_elec_total_kw;  /**< Total active electrical power P_elec (kW) */
    uint8_t                      fan_active_mask;  /**< Bit u-1 = 1 if unit u supply fan is ON (1..8) */
    float                        f_carga;          /**< Active compressor capacity fraction (0.0..1.0) */
} thermo_input_t;

/**
 * @brief Thermodynamic metrics computed for an individual evaporator pair k.
 */
typedef struct {
    float t_inj_c;       /**< Cold air supply temperature (C) */
    float t_ret_c;       /**< Warm air return temperature (C) */
    float delta_t_c;     /**< Thermal differential T_ret - T_inj (C) */
    float m_da_kg_s;     /**< Dry air mass flow mapped to this pair (kg/s) */
    float p_w_inj_kpa;   /**< Supply air partial vapor pressure (kPa) */
    float omega_inj;     /**< Supply air absolute humidity ratio (kg_v / kg_da) */
    float q_sensible_kw; /**< Sensible cooling capacity (kW) */
    float q_latent_kw;   /**< Latent cooling capacity (kW) */
    float q_total_kw;    /**< Total cooling capacity (kW) */
    float h_ret_kj_kg;   /**< Return specific enthalpy (kJ/kg_da) */
    float h_inj_kj_kg;   /**< Supply specific enthalpy (kJ/kg_da) */
    bool  is_condensing; /**< True if condensing regime (T_inj < T_rocio,ret) */
    bool  is_active;     /**< True if pair is valid and unit fan is ON */
} thermo_pair_metrics_t;

/**
 * @brief Predictive diagnostics, probability scores, and alarm flags.
 */
typedef struct {
    bool     room_hotspot;                                  /**< Room spatial spread Delta T_sala,max > 3.5 C */
    bool     airflow_bypass;                                /**< T_sala,max > 27.0 C or Delta T_bypass > 3.0 C */
    bool     moisture_ingress;                              /**< SHF < 0.70 or omega_ret > 0.012 kg/kg */
    float    filter_fouling_pct[SENSOR_MATRIX_MAX_PAIRS];   /**< Filter restriction probability (0..100 %) */
    float    days_remaining[SENSOR_MATRIX_MAX_PAIRS];       /**< Projected days remaining per pair */
    uint16_t days_remaining_raw[SENSOR_MATRIX_MAX_PAIRS];   /**< Raw code (0xFFFF init, or days) */
    float    leak_probability_pct[SENSOR_MATRIX_MAX_PAIRS]; /**< Leak / fatigue probability (0..100 %) */
    float    mech_degradation_pct;                          /**< Overall mechanical degradation (0..100 %) */
} thermo_diagnostics_t;

/**
 * @brief Complete thermodynamic and psychrometric engine output frame.
 */
typedef struct {
    /* Atmospheric & Return Psychrometrics */
    float p_atm_kpa;       /**< Local atmospheric pressure P_atm (kPa) */
    float p_ws_ret_kpa;    /**< Return saturation vapor pressure P_ws(T_ret_bar) (kPa) */
    float p_w_ret_kpa;     /**< Return partial vapor pressure P_w,ret (kPa) */
    float omega_ret;       /**< Return humidity ratio omega_ret (kg_v / kg_da) */
    float rho_da_ret;      /**< Return dry air density rho_da,ret (kg_da / m3) */
    float t_rocio_ret_c;   /**< Return air dew point temperature (C) */
    float m_da_total_kg_s; /**< Total fan-governed dry air mass flow (kg_da / s) */

    /* Spatial & Boundary Temperatures */
    float   t_inj_avg_c;                /**< Average supply temperature over active pairs (C) */
    float   t_ret_avg_c;                /**< Average return temperature over active pairs (C) */
    float   delta_t_clima_avg_c;        /**< Global climate differential (C) */
    uint8_t active_pairs_count;         /**< Number of active valid pairs participating */

    float   t_room_avg_c;               /**< Room mean temperature T_sala (C) */
    float   t_room_max_c;               /**< Maximum room zone temperature (C) */
    float   t_room_min_c;               /**< Minimum room zone temperature (C) */
    float   delta_t_room_max_c;         /**< Spatial thermal spread Delta T_sala,max (C) */
    float   delta_t_bypass_c;           /**< Bypass gradient Delta T_bypass (C) */
    uint8_t valid_room_probes_count;    /**< Number of healthy room probes */

    float   t_outdoor_eff_c;            /**< Effective outdoor temperature T_ext (C) */
    uint8_t valid_outdoor_probes_count; /**< Number of healthy outdoor probes */

    /* Energy & Capacity Balances */
    thermo_pair_metrics_t pairs[SENSOR_MATRIX_MAX_PAIRS];
    float                 q_sensible_total_kw; /**< Total sensible cooling capacity (kW) */
    float                 q_latent_total_kw;   /**< Total latent cooling capacity (kW) */
    float                 q_total_kw;          /**< Total cooling capacity (kW) */
    float                 m_cond_l_h;          /**< Condensate drainage rate (Liters/hour) */
    float                 shf;                 /**< Sensible Heat Factor (0.0 to 1.0) */
    float                 latent_heat_pct;     /**< Latent heat percentage ((1 - SHF) * 100) */

    /* First & Second Law Thermodynamic Performance */
    float cop_real;     /**< Total machine enthalpic COP (Q_total / P_elec) */
    float cop_sensible; /**< IT-useful COP (Q_sensible / P_elec) */
    float t_evap_sat_k; /**< Estimated evaporator saturation temperature (K) */
    float t_cond_sat_k; /**< Estimated condenser saturation temperature (K) */
    float cop_carnot;   /**< Theoretical Carnot COP ceiling */
    float eta_ii_pct;   /**< Second Law exergetic efficiency (%) */

    /* Capacity Auditing & Free Margin */
    float q_expected_kw;  /**< OLS expected cooling capacity Q_total,esp (kW) */
    float cop_expected;   /**< OLS expected COP */
    float q_max_safe_kw;  /**< Weather-corrected safe maximum capacity (kW) */
    float kw_free;        /**< Weather-corrected free cooling margin reserve (kW) */

    /* Predictive Diagnostics */
    thermo_diagnostics_t diagnostics;
} thermo_output_t;

/**
 * @brief Double-precision OLS matrix accumulator structure (Change F-03).
 */
typedef struct {
    double   xtx[4][4];    /**< 4x4 moment matrix X^T * X accumulator */
    double   xty[4];       /**< 4x1 moment vector X^T * Y accumulator */
    uint32_t sample_count; /**< Total valid observation samples accumulated */
} thermo_ols_t;

/* ========================================================================= */
/* Public API Functions                                                      */
/* ========================================================================= */

/**
 * @brief Initialize default site thermodynamic configuration parameters.
 *
 * Configures default altitude (0 m), 3 HVAC units with 1200 m3/h each,
 * 8.0 K evap pinch, 10.0 K cond pinch, 35.0 C design outdoor temp,
 * and 0.005 / C derating slope.
 *
 * @param[out] cfg Pointer to configuration structure.
 * @return 0 on success, -EINVAL if pointer is NULL.
 */
int thermo_engine_init_default_config(thermo_config_t *cfg);

/**
 * @brief Initialize default baseline configuration parameters.
 *
 * Sets clean initial states, zeroes OLS coefficients, sets 40.0% baseline
 * exergetic efficiency, and clears sample history.
 *
 * @param[out] baseline Pointer to baseline structure.
 * @return 0 on success, -EINVAL if pointer is NULL.
 */
int thermo_engine_init_default_baseline(thermo_baseline_cfg_t *baseline);

/**
 * @brief Execute one cycle of the Thermodynamic & Psychrometric Engine.
 *
 * Pure domain service: zero dynamic memory allocation, zero static mutable state.
 * Fully reentrant and thread-safe.
 *
 * @param[in]  cfg      Site physical and dimensional configuration.
 * @param[in]  baseline Learned baseline signature and historical context.
 * @param[in]  input    Live sensor frame and operational readings.
 * @param[out] output   Thermodynamic balances, cycle metrics, and diagnostics.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int thermo_engine_compute(const thermo_config_t *cfg,
                          const thermo_baseline_cfg_t *baseline,
                          const thermo_input_t *input,
                          thermo_output_t *output);

/* ========================================================================= */
/* Standalone Psychrometric & Thermodynamic Math Helpers                     */
/* ========================================================================= */

/**
 * @brief Compute local atmospheric pressure from altitude (barometric formula).
 *
 * @param altitude_m Altitude in meters above sea level.
 * @return Atmospheric pressure in kPa.
 */
float thermo_calc_p_atm_kpa(float altitude_m);

/**
 * @brief Compute saturation vapor pressure via Magnus-Tetens / ASHRAE formula.
 *
 * @param temp_c Temperature in degrees Celsius.
 * @return Saturation vapor pressure in kPa.
 */
float thermo_calc_p_ws_kpa(float temp_c);

/**
 * @brief Compute partial vapor pressure from saturation pressure and relative humidity.
 *
 * @param p_ws_kpa Saturation vapor pressure in kPa.
 * @param rh_pct   Relative humidity in percent (0.0 to 100.0).
 * @return Partial vapor pressure in kPa.
 */
float thermo_calc_p_w_kpa(float p_ws_kpa, float rh_pct);

/**
 * @brief Compute absolute humidity ratio omega from partial and atmospheric pressures.
 *
 * @param p_w_kpa   Partial vapor pressure in kPa.
 * @param p_atm_kpa Atmospheric pressure in kPa.
 * @return Absolute humidity ratio in kg_vapor / kg_dry_air.
 */
float thermo_calc_humidity_ratio(float p_w_kpa, float p_atm_kpa);

/**
 * @brief Compute moist air dry-air density via Dalton's ideal gas mixture law.
 *
 * @param p_atm_kpa Atmospheric pressure in kPa.
 * @param p_w_kpa   Partial vapor pressure in kPa.
 * @param temp_c    Air dry-bulb temperature in degrees Celsius.
 * @return Dry air density in kg_dry_air / m3.
 */
float thermo_calc_dry_air_density(float p_atm_kpa, float p_w_kpa, float temp_c);

/**
 * @brief Compute return air dew point temperature from partial vapor pressure.
 *
 * @param p_w_kpa Partial vapor pressure in kPa.
 * @return Dew point temperature in degrees Celsius.
 */
float thermo_calc_dew_point_c(float p_w_kpa);

/**
 * @brief Compute ideal Carnot COP ceiling between evaporator and condenser temperatures.
 *
 * @param t_evap_sat_k Absolute evaporator saturation temperature in Kelvin.
 * @param t_cond_sat_k Absolute condenser saturation temperature in Kelvin.
 * @return Ideal Carnot COP.
 */
float thermo_calc_carnot_cop(float t_evap_sat_k, float t_cond_sat_k);

/**
 * @brief Compute Second Law exergetic efficiency (eta_II).
 *
 * @param cop_real   Actual measured machine COP.
 * @param cop_carnot Ideal Carnot COP ceiling.
 * @return Exergetic efficiency in percent (0.0 to 100.0 %).
 */
float thermo_calc_exergetic_efficiency(float cop_real, float cop_carnot);

/* ========================================================================= */
/* Phase 2 Static R410A Flash Lookup Table API (Change F-02)                 */
/* ========================================================================= */

/**
 * @brief Static Flash 46-point R410A saturation temperature lookup table.
 * Stored in Flash (ROM) spanning 0.0 to 45.0 bar gauge in 1.0 bar steps.
 */
extern const float R410A_TSAT_LUT[THERMO_R410A_LUT_POINTS];

/**
 * @brief Compute R410A dew-point saturation temperature via O(1) linear interpolation.
 *
 * @param[in]  p_gauge_bar Gauge pressure in bar (0.0 to 45.0 bar).
 * @param[out] out_t_sat_c Pointer to receive saturation temperature in °C.
 * @return 0 on success, -EINVAL on NULL pointer.
 */
int thermo_r410a_sat_temp_c(float p_gauge_bar, float *out_t_sat_c);

/**
 * @brief Compute R410A suction superheat (Delta T_SH).
 *
 * @param[in]  p_gauge_bar   Suction gauge pressure in bar (0.0 to 45.0 bar).
 * @param[in]  t_suction_c   Measured suction pipe temperature in °C.
 * @param[out] out_sh_k      Pointer to receive superheat in Kelvin (°C differential).
 * @return 0 on success, -EINVAL on NULL pointer.
 */
int thermo_r410a_superheat_k(float p_gauge_bar, float t_suction_c, float *out_sh_k);

/* ========================================================================= */
/* Double-Precision Ordinary Least Squares (OLS) Solver API (Change F-03)     */
/* ========================================================================= */

/**
 * @brief Reset and initialize OLS accumulator.
 *
 * @param[out] ols Pointer to OLS accumulator structure.
 * @return 0 on success, -EINVAL on NULL pointer.
 */
int thermo_ols_init(thermo_ols_t *ols);

/**
 * @brief Add one steady-state observation sample to the OLS moment accumulators.
 *
 * Uses 64-bit IEEE 754 double precision on ARM Cortex-M7 DP-FPU.
 *
 * @param[in,out] ols     Pointer to accumulator.
 * @param[in]     t_ret   Return air temperature in °C.
 * @param[in]     t_ext   Effective outdoor temperature in °C.
 * @param[in]     f_carga Fraction of active compressor capacity (0.0 to 1.0).
 * @param[in]     q_total Measured total cooling capacity in kW.
 * @return 0 on success, -EINVAL on NULL pointer.
 */
int thermo_ols_add_sample(thermo_ols_t *ols,
                          double t_ret,
                          double t_ext,
                          double f_carga,
                          double q_total);

/**
 * @brief Solve for regression coefficients beta using Tikhonov regularization.
 *
 * Solves: beta = (X^T * X + lambda * I_4)^(-1) * (X^T * Y)
 * strictly in 64-bit double precision using Gauss-Jordan elimination with partial pivoting.
 *
 * @param[in]  ols         Pointer to populated OLS accumulator.
 * @param[in]  lambda_reg  Tikhonov diagonal regularization parameter (e.g. 1.0e-6).
 * @param[out] out_beta    Array of 4 doubles to receive [b0, b1, b2, b3].
 * @return 0 on success, -EINVAL on NULL pointer or insufficient samples, -EDOM if singular.
 */
int thermo_ols_solve(const thermo_ols_t *ols, double lambda_reg, double out_beta[4]);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_SERVICES_THERMODYNAMIC_ENGINE_H_ */
