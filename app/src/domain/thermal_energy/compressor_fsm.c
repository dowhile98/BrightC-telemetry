/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file compressor_fsm.c
 * @brief Compressor 5-phase temporal stabilization state machine, inrush suppression,
 *        step load capture, and plant load fraction calculations (Revision F Baseline).
 */

#include "domain/thermal_energy/compressor_fsm.h"

#include <math.h>
#include <string.h>

void compressor_fsm_init(compressor_fsm_t *self)
{
    if (self == NULL) {
        return;
    }

    memset(self, 0, sizeof(*self));
    self->state = COMPRESSOR_STATE_OFF;
    self->delta_p_quality = QUALITY_UNAVAILABLE;
}

void compressor_fsm_update(compressor_fsm_t *self,
                           bool contact_run,
                           float total_power_kw,
                           bool concurrent_transition_detected)
{
    if (self == NULL) {
        return;
    }

    bool stop_edge = !contact_run && self->prev_contact_run;
    self->prev_contact_run = contact_run;

    /* Stop edge or contact open while in any active running state -> transition to OFF */
    if (stop_edge || (!contact_run && self->state != COMPRESSOR_STATE_OFF)) {
        self->state = COMPRESSOR_STATE_OFF;
        self->t_off_s = 0U;
        self->t_on_s = 0U;
        self->steady_state = false;
        self->concurrent_detected = false;
        return;
    }

    if (self->state == COMPRESSOR_STATE_OFF) {
        if (!contact_run) {
            self->t_off_s++;
            self->steady_state = false;
            return;
        }

        /* Run edge from OFF state */
        if (self->t_off_s < COMPRESSOR_SHORT_CYCLE_LIMIT_S) {
            self->short_cycle_count++;
        }
        self->starts_count++;
        self->p_pre_start_kw = total_power_kw;
        self->t_on_s = 0U;
        self->delta_p_comp_kw = 0.0f;
        self->delta_p_quality = QUALITY_UNAVAILABLE;
        self->concurrent_detected = false;
        self->steady_state = false;
        self->state = COMPRESSOR_STATE_INRUSH_WAIT;
        return;
    }

    /* While running, latch concurrent transitions if detected */
    if (concurrent_transition_detected) {
        self->concurrent_detected = true;
    }

    self->t_on_s++;

    switch (self->state) {
    case COMPRESSOR_STATE_INRUSH_WAIT:
        if (self->t_on_s >= COMPRESSOR_INRUSH_DURATION_S) {
            self->state = COMPRESSOR_STATE_STEP_LOAD_CAPTURE;
            if (self->concurrent_detected) {
                self->delta_p_comp_kw = 0.0f;
                self->delta_p_quality = QUALITY_UNAVAILABLE;
            } else {
                float p_diff = total_power_kw - self->p_pre_start_kw;
                self->delta_p_comp_kw = (p_diff > 0.0f) ? p_diff : 0.0f;
                self->delta_p_quality = QUALITY_VALID;
            }
        }
        break;

    case COMPRESSOR_STATE_STEP_LOAD_CAPTURE:
        self->state = COMPRESSOR_STATE_THERMAL_WARMUP;
        if (self->t_on_s >= COMPRESSOR_WARMUP_DURATION_S) {
            self->state = COMPRESSOR_STATE_STEADY_STATE;
            self->steady_state = true;
        }
        break;

    case COMPRESSOR_STATE_THERMAL_WARMUP:
        if (self->t_on_s >= COMPRESSOR_WARMUP_DURATION_S) {
            self->state = COMPRESSOR_STATE_STEADY_STATE;
            self->steady_state = true;
        }
        break;

    case COMPRESSOR_STATE_STEADY_STATE:
        self->steady_state = true;
        break;

    default:
        self->state = COMPRESSOR_STATE_OFF;
        break;
    }
}

float compressor_fsm_calc_load_fraction(const compressor_fsm_t *fsms,
                                       uint8_t count,
                                       const float *nominal_capacities_kw)
{
    if (fsms == NULL || count == 0U) {
        return 0.0f;
    }

    float total_capacity = 0.0f;
    float active_capacity = 0.0f;

    for (uint8_t i = 0U; i < count; i++) {
        float cap = (nominal_capacities_kw != NULL) ? nominal_capacities_kw[i] : 1.0f;
        if (cap < 0.0f) {
            cap = 0.0f;
        }
        total_capacity += cap;
        if (fsms[i].state != COMPRESSOR_STATE_OFF) {
            active_capacity += cap;
        }
    }

    if (total_capacity <= 0.001f) {
        return 0.0f;
    }

    float frac = active_capacity / total_capacity;
    if (frac < 0.0f) {
        frac = 0.0f;
    } else if (frac > 1.0f) {
        frac = 1.0f;
    }
    return frac;
}
