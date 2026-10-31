/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * 3-Phase Electrical Energy Meter Data Contract.
 * Pure C99/C11 - Zero Hardware / Zero RTOS Dependencies.
 */

#ifndef APP_COMMON_ENERGY_TYPES_H_
#define APP_COMMON_ENERGY_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENERGY_PHASE_A = 0,
    ENERGY_PHASE_B,
    ENERGY_PHASE_C,
    ENERGY_PHASE_MAX
} energy_phase_t;

/**
 * @brief Structured snapshot of 3-phase electrical energy meter measurements.
 */
typedef struct {
    uint8_t device_id;                          /**< Logical device identifier */
    uint64_t timestamp_us;                      /**< Acquisition monotonic timestamp (microseconds) */
    bool is_valid;                              /**< True if communication and data are valid */

    /* Voltages (V) */
    float voltage_v[ENERGY_PHASE_MAX];          /**< Phase voltages Va, Vb, Vc */

    /* Currents (A) */
    float current_a[ENERGY_PHASE_MAX];          /**< Phase currents Ia, Ib, Ic */
    float leakage_current_a;                    /**< Residual/leakage current */

    /* Active Powers (kW) */
    float active_power_kw[ENERGY_PHASE_MAX];    /**< Phase active powers Pa, Pb, Pc */
    float total_active_power_kw;                /**< Total active power P_total (crucial for P_elec_total) */

    /* Reactive Powers (kVar) */
    float reactive_power_kvar[ENERGY_PHASE_MAX];/**< Phase reactive powers Qa, Qb, Qc */
    float total_reactive_power_kvar;            /**< Total reactive power Q_total */

    /* Apparent Powers (kVA) */
    float apparent_power_kva[ENERGY_PHASE_MAX]; /**< Phase apparent powers Sa, Sb, Sc */
    float total_apparent_power_kva;             /**< Total apparent power S_total */

    /* Power Factors (0.0 to 1.0) */
    float power_factor[ENERGY_PHASE_MAX];       /**< Phase power factors PFa, PFb, PFc */
    float total_power_factor;                   /**< Total power factor PF_total */

    /* Active Energies (kWh) */
    double active_energy_kwh[ENERGY_PHASE_MAX]; /**< Accumulated active energy per phase */
    double total_active_energy_kwh;             /**< Total accumulated active energy */

    /* Reactive Energies (kvarh) */
    double reactive_energy_kvarh[ENERGY_PHASE_MAX]; /**< Accumulated reactive energy per phase */
    double total_reactive_energy_kvarh;             /**< Total accumulated reactive energy */

    /* Apparent Energies (kVAh) */
    double apparent_energy_kvah[ENERGY_PHASE_MAX];  /**< Cumulative apparent energy per phase */
    double total_apparent_energy_kvah;              /**< Total cumulative apparent energy */

    /* Line Frequency (Hz) */
    float frequency_hz;                         /**< AC Line Frequency */
} energy_reading_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_ENERGY_TYPES_H_ */
