/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file thermodynamics.c
 * @brief Sensible and latent heat balances, COP, Carnot ceiling, and exergetic efficiency
 *        calculations (Revision F Baseline).
 */

#include "domain/thermal_energy/thermodynamics.h"
#include "domain/thermal_energy/psychrometrics.h"

#include <errno.h>
#include <math.h>
#include <string.h>

float thermodynamics_calc_cop(float q_kw, float p_elec_kw, quality_status_t *out_quality)
{
    if (p_elec_kw < THERMO_MIN_ELEC_POWER_FOR_COP_KW || isnan(p_elec_kw) || isinf(p_elec_kw) ||
        isnan(q_kw) || isinf(q_kw)) {
        if (out_quality != NULL) {
            *out_quality = QUALITY_UNAVAILABLE;
        }
        return 0.0f;
    }

    if (out_quality != NULL) {
        *out_quality = QUALITY_VALID;
    }
    return q_kw / p_elec_kw;
}

float thermodynamics_calc_carnot_cop(float t_evap_sat_k, float t_cond_sat_k)
{
    float lift = t_cond_sat_k - t_evap_sat_k;
    float denom = (lift > THERMO_CARNOT_MIN_LIFT_K) ? lift : THERMO_CARNOT_MIN_LIFT_K;

    if (t_evap_sat_k <= 0.0f) {
        return 0.0f;
    }

    return t_evap_sat_k / denom;
}

float thermodynamics_calc_eta_ii(float cop_real, float cop_carnot)
{
    if (cop_carnot <= 0.01f || cop_real <= 0.0f || isnan(cop_real) || isnan(cop_carnot)) {
        return 0.0f;
    }

    float eta = (cop_real / cop_carnot) * 100.0f;
    if (eta < 0.0f) {
        eta = 0.0f;
    } else if (eta > 100.0f) {
        eta = 100.0f;
    }
    return eta;
}

int thermodynamics_compute(const thermodynamics_input_t *input,
                           thermodynamics_output_t *output)
{
    if (input == NULL || output == NULL) {
        return -EINVAL;
    }

    memset(output, 0, sizeof(*output));

    uint8_t pairs = input->configured_pairs;
    if (pairs > THERMAL_MAX_PAIRS) {
        pairs = THERMAL_MAX_PAIRS;
    }

    for (uint8_t k = 0U; k < pairs; k++) {
        if ((input->pair_valid_mask & (1U << k)) == 0U) {
            continue;
        }

        /* 1. Temperature lift */
        output->delta_t[k] = input->t_ret[k] - input->t_inj[k];

        /* 2. Sensible capacity per pair: Q_sen,k = m_da,k * (1.006 + 1.86 * omega_ret) * Delta_T_k */
        float cp_moist = THERMO_CP_DRY_AIR_KJ_KG_C + THERMO_CP_VAPOR_KJ_KG_C * input->omega_ret;
        output->q_sensible_pair[k] = input->m_dot_da[k] * cp_moist * output->delta_t[k];

        /* 3. Condensation regime & latent capacity per pair */
        if (input->t_inj[k] >= input->t_rocio_ret) {
            /* Dry regime */
            output->q_latent_pair[k] = 0.0f;
        } else {
            /* Wet regime */
            float p_ws_inj = psychrometrics_p_ws(input->t_inj[k]);
            float p_w_inj = THERMO_COIL_DISCHARGE_RH_FACTOR * p_ws_inj;
            float omega_inj = psychrometrics_humidity_ratio(input->p_atm_kpa, p_w_inj);
            if (omega_inj > input->omega_ret) {
                omega_inj = input->omega_ret;
            }

            float d_omega = input->omega_ret - omega_inj;
            if (d_omega < 0.0f) {
                d_omega = 0.0f;
            }

            output->q_latent_pair[k] = input->m_dot_da[k] * THERMO_H_FG_COIL_KJ_KG * d_omega;
        }

        output->q_total_pair[k] = output->q_sensible_pair[k] + output->q_latent_pair[k];

        /* Accumulate totals */
        output->q_sensible_total += output->q_sensible_pair[k];
        output->q_latent_total += output->q_latent_pair[k];
    }

    output->q_total = output->q_sensible_total + output->q_latent_total;

    /* 4. Condensate rate in Liters/hour */
    output->m_dot_condensate_lph = (output->q_latent_total / THERMO_H_FG_COIL_KJ_KG) * 3600.0f;

    /* 5. Sensible Heat Factor */
    float q_denom = (output->q_total > 0.001f) ? output->q_total : 0.001f;
    float shf = output->q_sensible_total / q_denom;
    if (shf < 0.0f) {
        shf = 0.0f;
    } else if (shf > 1.0f) {
        shf = 1.0f;
    }
    output->shf = shf;

    /* 6. Operational COP */
    if (input->elec_power_valid) {
        output->cop_real = thermodynamics_calc_cop(output->q_total,
                                                  input->total_active_power_kw,
                                                  &output->cop_quality);
        output->cop_sensible = thermodynamics_calc_cop(output->q_sensible_total,
                                                      input->total_active_power_kw,
                                                      NULL);
    } else {
        output->cop_real = 0.0f;
        output->cop_sensible = 0.0f;
        output->cop_quality = QUALITY_UNAVAILABLE;
    }

    /* 7. Carnot COP & Exergetic Efficiency */
    output->t_evap_sat_k = (input->t_inj_mean - THERMO_DEFAULT_PINCH_EVAP_K) + PSYCHRO_KELVIN_OFFSET_C;
    output->t_cond_sat_k = (input->t_outdoor_eff + THERMO_DEFAULT_PINCH_COND_K) + PSYCHRO_KELVIN_OFFSET_C;
    output->cop_carnot = thermodynamics_calc_carnot_cop(output->t_evap_sat_k, output->t_cond_sat_k);

    if (output->cop_quality == QUALITY_VALID) {
        output->eta_ii_pct = thermodynamics_calc_eta_ii(output->cop_real, output->cop_carnot);
    } else {
        output->eta_ii_pct = 0.0f;
    }

    return 0;
}
