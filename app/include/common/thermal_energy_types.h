/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file thermal_energy_types.h
 * @brief Common types, constant bounds, DTO contracts and persistent structs
 *        for the Thermal and Energy Subsystem (Revision F Baseline).
 *
 * Pure ISO C99/C11 domain declarations:
 * - Zero Zephyr RTOS dependencies
 * - Zero vendor HAL or hardware register dependencies
 * - Zero dynamic memory allocation
 */

#ifndef APP_COMMON_THERMAL_ENERGY_TYPES_H_
#define APP_COMMON_THERMAL_ENERGY_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Subsystem Capacity & Array Bounds Constants                               */
/* ========================================================================= */

#define THERMAL_MAX_PAIRS                16U /**< Maximum supply/return thermal pairs */
#define THERMAL_MAX_ROOM_PROBES          16U /**< Maximum room ambient monitoring probes */
#define THERMAL_MAX_OUTDOOR_PROBES       8U  /**< Maximum outdoor condenser reference probes */
#define THERMAL_MAX_HVAC_UNITS           8U  /**< Maximum monitored/controlled HVAC units */
#define THERMAL_HOURLY_HISTORY_CAPACITY  720U /**< 30-day (720 h) hourly ring buffer capacity */

/* ========================================================================= */
/* Data Quality & Diagnostic Enumerations                                    */
/* ========================================================================= */

/**
 * @brief Universal telemetry and thermodynamic calculation quality status.
 */
typedef enum {
    QUALITY_VALID       = 0, /**< Measurement / computation verified healthy and within bounds */
    QUALITY_SUSPECT     = 1, /**< Degraded accuracy, out-of-nominal condition, or clamped reading */
    QUALITY_FAILED      = 2, /**< Sensor fault, open loop, communication failure or arithmetic domain error */
    QUALITY_UNAVAILABLE = 3, /**< Signal unmapped, disabled, or locked out by operational rules */
} quality_status_t;

/** Compatibility alias */
typedef quality_status_t data_quality_t;

/**
 * @brief Compressor 5-phase temporal stabilization state machine states.
 */
typedef enum {
    COMPRESSOR_STATE_OFF               = 0, /**< Auxiliary contact open; tracking t_off */
    COMPRESSOR_STATE_INRUSH_WAIT       = 1, /**< Contact closed, t_on < 15s: motor start inrush filtering */
    COMPRESSOR_STATE_STEP_LOAD_CAPTURE = 2, /**< Exactly t_on = 15s: isolated electrical step load capture */
    COMPRESSOR_STATE_THERMAL_WARMUP    = 3, /**< 15s < t_on < 180s: thermal stabilization transient */
    COMPRESSOR_STATE_STEADY_STATE      = 4, /**< t_on >= 180s: steady-state thermodynamic evaluation active */
} compressor_fsm_state_t;

/* ========================================================================= */
/* Topology & Pairing DTOs                                                   */
/* ========================================================================= */

/**
 * @brief Logical pairing descriptor linking flat sensor IDs to an HVAC unit.
 */
typedef struct {
    uint8_t supply_flat_id; /**< Flat sensor ID (1..59) for cold air supply probe */
    uint8_t return_flat_id; /**< Flat sensor ID (1..59) for warm air return probe */
    uint8_t hvac_unit_id;   /**< Associated HVAC unit index (1..8) */
    uint8_t reserved;       /**< 4-byte struct alignment padding */
} thermal_pair_mapping_t;

_Static_assert(sizeof(thermal_pair_mapping_t) == 4U,
               "thermal_pair_mapping_t must be exactly 4 bytes");

/**
 * @brief Per-pair processed metric snapshot.
 */
typedef struct {
    uint8_t  pair_index;                 /**< Pair index k (0..THERMAL_MAX_PAIRS - 1) */
    uint8_t  hvac_unit_id;               /**< Associated HVAC unit (1..8) */
    bool     is_valid;                   /**< True if pair is healthy and active */
    float    t_inj_c;                    /**< Cold supply temperature [deg C] */
    float    t_ret_c;                    /**< Warm return temperature [deg C] */
    float    delta_t_c;                  /**< Thermal differential (T_ret - T_inj) [deg C] */
    float    q_sensible_kw;              /**< Sensible cooling capacity [kW] */
    float    q_latente_kw;               /**< Latent cooling capacity [kW] */
    float    q_total_kw;                 /**< Total cooling capacity [kW] */
    float    pct_filter_restriction;     /**< Filter loading index (0.0..100.0 %) */
    float    pct_refrigerant_leak;       /**< Refrigerant leak index (0.0..100.0 %) */
    uint16_t filter_days_remaining;      /**< Projected days until maintenance */
} thermal_pair_metric_t;

/**
 * @brief Compressor operational state and step electrical load tracking snapshot.
 */
typedef struct {
    uint8_t                compressor_index;   /**< Unit index c (0..THERMAL_MAX_HVAC_UNITS - 1) */
    compressor_fsm_state_t state;              /**< 5-phase temporal state */
    uint32_t               t_on_s;             /**< Continuous run duration [s] */
    uint32_t               t_off_s;            /**< Continuous stopped duration [s] */
    float                  delta_p_comp_kw;    /**< Step electrical load delta [kW] */
    bool                   delta_p_valid;      /**< True if step load captured without concurrent transitions */
    uint32_t               starts_count;       /**< Monotonic start cycle counter */
    uint32_t               short_cycles_count; /**< Starts with t_off < 180s */
} compressor_status_t;

/**
 * @brief High-level predictive diagnostics and machine-learned status summary.
 */
typedef struct {
    bool     ols_trained;                /**< True if 168h baseline training complete */
    uint16_t training_hours_accumulated; /**< Accumulated steady-state training hours (0..168) */
    float    ols_condition_number;       /**< Condition number of (X^T * X) */
    float    q_total_expected_kw;        /**< Expected total capacity via OLS [kW] */
    float    cop_expected;               /**< Expected COP via OLS */
    float    pct_mechanical_degradation; /**< Mechanical degradation index (0.0..100.0 %) */
    float    pct_filter_restriction_max; /**< Highest filter loading across all pairs */
    float    pct_refrigerant_leak_max;   /**< Highest refrigerant leak probability */
    uint32_t active_diagnostic_alarms;   /**< Bitmask of active predictive diagnostic alarms */
} predictive_summary_t;

/* ========================================================================= */
/* Real-Time Telemetry Contracts                                             */
/* ========================================================================= */

/**
 * @brief Coherent multi-source telemetry snapshot assembled at 1 Hz.
 */
typedef struct {
    uint32_t generation_id;               /**< Monotonic sequence ID */
    uint64_t capture_timestamp_ms;        /**< Monotonic timestamp of snapshot assembly */

    /* Temperature Matrix */
    uint64_t temp_timestamp_ms;           /**< Timestamp of sensor matrix acquisition */
    uint8_t  configured_pairs;            /**< N (1..16, default 6) */
    float    t_inj[THERMAL_MAX_PAIRS];    /**< Cold supply temperatures [deg C] */
    float    t_ret[THERMAL_MAX_PAIRS];    /**< Warm return temperatures [deg C] */
    uint16_t pair_valid_mask;             /**< Bit k = 1 if pair k is healthy and valid */
    uint16_t inj_valid_mask;              /**< Bit k = 1 if supply probe k is valid */
    uint16_t ret_valid_mask;              /**< Bit k = 1 if return probe k is valid */

    uint8_t  configured_room_probes;      /**< M (1..16, default 2) */
    float    t_room[THERMAL_MAX_ROOM_PROBES]; /**< Room zone temperatures [deg C] */
    uint16_t room_valid_mask;             /**< Bit m = 1 if probe m is valid */

    uint8_t  configured_outdoor_probes;   /**< L (1..8, default 2) */
    float    t_outdoor[THERMAL_MAX_OUTDOOR_PROBES]; /**< Outdoor condenser references [deg C] */
    uint8_t  outdoor_valid_mask;          /**< Bit l = 1 if outdoor probe l is valid */

    /* Psychrometric Loop (J9 4-20 mA) */
    float            humidity_loop_ma;    /**< Measured loop current in mA (3.5 .. 21.0 mA) */
    float            humidity_return_pct; /**< Return relative humidity (0.0..100.0 %) */
    quality_status_t humidity_quality;    /**< Loop signal quality */

    /* Electrical Power (From JSY-MK-339) */
    uint64_t         elec_timestamp_ms;
    float            total_active_power_kw;   /**< P_elec_total [kW] */
    float            voltage_v[3];            /**< Line voltages [V] */
    float            current_a[3];            /**< Phase currents [A] */
    float            power_factor;            /**< Total PF */
    double           total_active_energy_kwh; /**< Accumulated active energy [kWh] */
    quality_status_t elec_quality;            /**< Data quality */

    /* Contact Feedback States */
    uint64_t contact_timestamp_ms;
    uint8_t  fan_state_mask;              /**< Bit u = 1 if fan u is CONFIRMED_ON (U_vent_ON) */
    uint8_t  fan_quality_mask;            /**< Bit u = 1 if fan feedback is healthy/known */
    uint8_t  comp_state_mask;             /**< Bit c = 1 if compressor c is CONFIRMED_ON */
    uint8_t  comp_quality_mask;           /**< Bit c = 1 if compressor feedback is healthy */

    /* Site Topology Reference */
    uint32_t config_version;              /**< Active site configuration version */
} thermal_energy_capture_t;

/**
 * @brief Processed thermodynamic snapshot calculated at 1 Hz.
 */
typedef struct {
    uint32_t schema_version;              /**< Result contract schema version */
    uint32_t generation_id;               /**< Monotonic result generation sequence */
    uint64_t result_timestamp_ms;         /**< Timestamp of computation completion */
    uint32_t calculation_duration_us;     /**< CPU execution time for this cycle */

    /* Data Quality Flags */
    quality_status_t overall_quality;       /**< Subsystem top-level quality */
    quality_status_t thermal_quality;       /**< Temperature matrix quality */
    quality_status_t psychrometric_quality; /**< Psychrometrics and capacity quality */
    quality_status_t cop_quality;           /**< Efficiency and COP quality */
    quality_status_t predictive_quality;    /**< OLS and diagnostic quality */

    /* 1. Thermal Metrics */
    float delta_t_pair[THERMAL_MAX_PAIRS];/**< Delta T per pair (T_ret - T_inj) [deg C] */
    float t_inj_mean;                     /**< Airflow-weighted mean supply temp [deg C] */
    float t_ret_mean;                     /**< Airflow-weighted mean return temp [deg C] */
    float delta_t_clima_mean;             /**< Global thermal lift (T_ret_mean - T_inj_mean) */
    float t_sala_mean;                    /**< Average room ambient temp [deg C] */
    float t_sala_max;                     /**< Maximum room hotspot temp [deg C] */
    float t_sala_min;                     /**< Minimum room cold spot temp [deg C] */
    float delta_t_sala_spread;            /**< Spatial spread (T_max - T_min) [deg C] */
    float delta_t_bypass;                 /**< Bypass gradient (T_sala_mean - T_ret_mean) */
    float t_outdoor_effective;            /**< Effective outdoor condensing reference [deg C] */

    /* 2. Psychrometric & Airflow Metrics */
    float p_atm_kpa;                      /**< Site barometric pressure [kPa] */
    float p_ws_ret_kpa;                   /**< Saturation vapor pressure at return [kPa] */
    float p_w_ret_kpa;                    /**< Partial vapor pressure at return [kPa] */
    float humidity_ratio_ret;             /**< Return absolute humidity [kg_vap/kg_da] */
    float t_rocio_ret;                    /**< Return dew point temperature [deg C] */
    float rho_da_ret;                     /**< Dry air density [kg/m^3] */
    float m_dot_da_total;                 /**< Total dry air mass flow [kg/s] */

    /* 3. Evaporator Capacity & Balances */
    float q_sensible_pair[THERMAL_MAX_PAIRS]; /**< Sensible cooling capacity per pair [kW] */
    float q_sensible_total;               /**< Room total sensible capacity [kW] */
    float q_latente_pair[THERMAL_MAX_PAIRS];  /**< Latent cooling capacity per pair [kW] */
    float q_latente_total;                /**< Room total latent capacity [kW] */
    float q_total;                        /**< Total thermal cooling capacity [kW] */
    float m_dot_condensate_lph;           /**< Condensate drainage rate [Liters/hour] */
    float shf;                            /**< Sensible Heat Factor (0.0 to 1.0) */
    float pct_latent_heat;                /**< Latent load percentage (0.0 to 100.0 %) */

    /* 4. Efficiency & Exergy Metrics */
    float cop_real;                       /**< Actual operational COP (Total kW / Elec kW) */
    float cop_sensible;                   /**< IT-Useful sensible COP (Sensible kW / Elec kW) */
    float t_evap_sat_k;                   /**< Evaporator saturation temp [K] */
    float t_cond_sat_k;                   /**< Condenser saturation temp [K] */
    float cop_carnot;                     /**< Theoretical Carnot ceiling */
    float eta_ii_pct;                     /**< Second Law exergetic efficiency [%] */
    float active_electrical_power_kw;     /**< P_elec_total consumed [kW] */

    /* 5. Compressor States */
    compressor_fsm_state_t comp_fsm_state[THERMAL_MAX_HVAC_UNITS]; /**< Compressor FSM state per unit */

    /* 6. Predictive Analytics & Diagnostic Matrix */
    bool     ols_trained;                   /**< True if 168 steady-state hours accumulated */
    uint16_t training_hours_accumulated;   /**< Steady-state hours accumulated (0..168) */
    float    q_total_expected_kw;           /**< Expected cooling capacity via OLS [kW] */
    float    cop_expected;                  /**< Expected COP via OLS */
    float    pct_mechanical_degradation;    /**< Degradation index (0.0 to 100.0 %) */
    float    pct_filter_restriction[THERMAL_MAX_PAIRS];   /**< Filter loading index per pair (0.0 to 100.0 %) */
    uint16_t filter_days_remaining[THERMAL_MAX_PAIRS];    /**< Projected days remaining */
    float    pct_refrigerant_leak[THERMAL_MAX_PAIRS];      /**< Decoupled leak probability (0.0 to 100.0 %) */
    uint32_t diagnostic_alarm_mask;        /**< Bitmask of active predictive alarm conditions */

    /* 7. Weather-Corrected Capacity & Free Reserve */
    float q_nominal_weather_corrected;     /**< Derated nominal plant capacity [kW] */
    float q_max_safe;                      /**< Safe redundant capacity minus degradation [kW] */
    float kw_free_cooling_margin;          /**< Weather-corrected free cooling margin [kW] */
} thermal_energy_result_t;

/* ========================================================================= */
/* Exact Compressed Binary Datalogger Record                                 */
/* ========================================================================= */

#ifndef CONFIG_THERMAL_HOURLY_PAIRS_COUNT
#define CONFIG_THERMAL_HOURLY_PAIRS_COUNT 6U
#endif

#ifndef CONFIG_THERMAL_HOURLY_ROOM_PROBES_COUNT
#define CONFIG_THERMAL_HOURLY_ROOM_PROBES_COUNT 2U
#endif

#ifndef CONFIG_THERMAL_HOURLY_OUTDOOR_PROBES_COUNT
#define CONFIG_THERMAL_HOURLY_OUTDOOR_PROBES_COUNT 2U
#endif

/**
 * @brief Dynamic compressed hourly record for 720h RAM ring buffer
 *        and MicroSD FATFS binary datalogger.
 *
 * Sized and packed to match Kconfig configuration (default 64 bytes for pilot 6/2/2)
 * to optimize SD card cluster write operations.
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_unix;               /**< 4 B: UNIX Epoch timestamp (seconds) */
    int16_t  t_inj_x10[CONFIG_THERMAL_HOURLY_PAIRS_COUNT];                 /**< Supply temperatures in 0.1 deg C */
    int16_t  t_ret_x10[CONFIG_THERMAL_HOURLY_PAIRS_COUNT];                 /**< Return temperatures in 0.1 deg C */
    int16_t  t_room_x10[CONFIG_THERMAL_HOURLY_ROOM_PROBES_COUNT];          /**< Room ambient temperatures in 0.1 deg C */
    int16_t  t_outdoor_x10[CONFIG_THERMAL_HOURLY_OUTDOOR_PROBES_COUNT];    /**< Outdoor ambient temperatures in 0.1 deg C */
    uint16_t humidity_return_x10;          /**< 2 B: Return relative humidity in 0.1 %RH */
    float    active_power_kw;              /**< 4 B: Total active electrical power [kW] */
    float    sensible_capacity_kw;         /**< 4 B: Sensible cooling capacity [kW] */
    float    latent_capacity_kw;           /**< 4 B: Latent cooling capacity [kW] */
    uint16_t cop_real_x100;                /**< 2 B: Operational COP * 100 */
    uint16_t eta_ii_x100;                  /**< 2 B: Exergetic efficiency * 100 */
    uint16_t fan_state_mask;               /**< 2 B: Bit u = 1 if fan u was running */
    uint16_t comp_state_mask;              /**< 2 B: Bit c = 1 if compressor c was running */
    uint32_t comp_starts_count;            /**< 4 B: Cumulative compressor start events */
    uint16_t crc16;                        /**< 2 B: CRC-16-CCITT over preceding payload bytes */
} thermal_hourly_record_t;

#define THERMAL_HOURLY_RECORD_BASE_SIZE (32U)
#define THERMAL_HOURLY_RECORD_TEMPS_SIZE \
    ((size_t)(sizeof(int16_t) * ( \
        (2U * (CONFIG_THERMAL_HOURLY_PAIRS_COUNT)) + \
        (CONFIG_THERMAL_HOURLY_ROOM_PROBES_COUNT) + \
        (CONFIG_THERMAL_HOURLY_OUTDOOR_PROBES_COUNT))))

#define THERMAL_HOURLY_RECORD_EXPECTED_SIZE \
    (THERMAL_HOURLY_RECORD_BASE_SIZE + THERMAL_HOURLY_RECORD_TEMPS_SIZE)

_Static_assert(sizeof(thermal_hourly_record_t) == THERMAL_HOURLY_RECORD_EXPECTED_SIZE,
               "thermal_hourly_record_t size MUST match Kconfig dynamic formula");

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_THERMAL_ENERGY_TYPES_H_ */
