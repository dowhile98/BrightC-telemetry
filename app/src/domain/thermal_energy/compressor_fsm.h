/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file compressor_fsm.h
 * @brief Compressor 5-phase temporal stabilization state machine, inrush suppression,
 *        step load capture, and plant load fraction calculations (Revision F Baseline).
 *
 * Pure C99/C11 Domain Layer:
 * - Zero Zephyr RTOS dependencies
 * - Zero hardware register dependencies
 * - Zero dynamic memory allocation
 */

#ifndef APP_DOMAIN_THERMAL_ENERGY_COMPRESSOR_FSM_H_
#define APP_DOMAIN_THERMAL_ENERGY_COMPRESSOR_FSM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Timing Bounds & Invariants                                                */
/* ========================================================================= */

#define COMPRESSOR_SHORT_CYCLE_LIMIT_S   (180U)
#define COMPRESSOR_INRUSH_DURATION_S     (15U)
#define COMPRESSOR_WARMUP_DURATION_S     (180U)

/* ========================================================================= */
/* Context Structure                                                         */
/* ========================================================================= */

/**
 * @brief Compressor stabilization and load disaggregation context.
 */
typedef struct {
    compressor_fsm_state_t state;
    uint32_t               t_off_s;
    uint32_t               t_on_s;
    float                  p_pre_start_kw;
    float                  delta_p_comp_kw;
    quality_status_t       delta_p_quality;
    uint32_t               short_cycle_count;
    uint32_t               starts_count;
    bool                   steady_state;
    bool                   concurrent_detected;
    bool                   prev_contact_run;
} compressor_fsm_t;

/* ========================================================================= */
/* Public Domain Functions                                                   */
/* ========================================================================= */

/**
 * @brief Initialize compressor FSM context to default powered-off state.
 *
 * @param[in,out] self Pointer to FSM instance.
 */
void compressor_fsm_init(compressor_fsm_t *self);

/**
 * @brief Update compressor FSM state machine with periodic 1 Hz sample.
 *
 * @param[in,out] self                            Pointer to FSM instance.
 * @param[in]     contact_run                     Debounced run contact state (true = closed/running).
 * @param[in]     total_power_kw                  Measured total site electrical power in kW.
 * @param[in]     concurrent_transition_detected  True if any other equipment transitioned during window.
 */
void compressor_fsm_update(compressor_fsm_t *self,
                           bool contact_run,
                           float total_power_kw,
                           bool concurrent_transition_detected);

/**
 * @brief Calculate active plant load fraction F_carga from array of compressor FSMs.
 *
 * F_carga = sum(Q_nom,active) / sum(Q_nom,total)
 *
 * @param[in] fsms                  Array of compressor FSM contexts.
 * @param[in] count                 Number of configured compressors.
 * @param[in] nominal_capacities_kw Array of nominal unit capacities in kW (may be NULL for unweighted).
 * @return Active load fraction in range 0.0 to 1.0.
 */
float compressor_fsm_calc_load_fraction(const compressor_fsm_t *fsms,
                                       uint8_t count,
                                       const float *nominal_capacities_kw);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_THERMAL_ENERGY_COMPRESSOR_FSM_H_ */
