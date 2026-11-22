/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file telemetry_sampler.c
 * @brief Telemetry Sampler Aggregator Application Service Implementation.
 */

#include "application/services/telemetry_sampler.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(telemetry_sampler, CONFIG_LOG_DEFAULT_LEVEL);

int telemetry_sampler_init(telemetry_sampler_t *self,
                           ao_datalogger_t *ao,
                           const i_thermal_energy_t *thermal,
                           const i_alarm_query_t *alarms,
                           const i_rtc_t *rtc,
                           const i_hvac_status_t *hvac,
                           uint32_t interval_s)
{
    if (self == NULL || ao == NULL) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));
    self->datalogger_ao = ao;

    if (thermal != NULL) {
        self->thermal_port = *thermal;
    }
    if (alarms != NULL) {
        self->alarm_query = *alarms;
    }
    if (rtc != NULL) {
        self->rtc_port = *rtc;
    }
    if (hvac != NULL) {
        self->hvac_port = *hvac;
    }

    self->sample_interval_s = (interval_s > 0U) ? interval_s : TELEMETRY_SAMPLER_DEFAULT_INTERVAL_S;
    self->last_sample_uptime_s  = -((int64_t)self->sample_interval_s); /* Force sample on first tick */
    self->last_sample_temp_s    = -((int64_t)TELEMETRY_SAMPLER_DEFAULT_INTERVAL_TEMP_S);
    self->last_sample_energy_s  = -((int64_t)TELEMETRY_SAMPLER_DEFAULT_INTERVAL_ENERGY_S);
    self->last_sample_thermal_s = -((int64_t)TELEMETRY_SAMPLER_DEFAULT_INTERVAL_THERMAL_S);
    self->is_initialized = true;

    LOG_INF("Telemetry sampler initialized: interval=%u s", self->sample_interval_s);
    return 0;
}

int telemetry_sampler_sample_now(telemetry_sampler_t *self,
                                 datalogger_telemetry_record_t *out_rec)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    datalogger_telemetry_record_t rec;
    memset(&rec, 0, sizeof(rec));

    int64_t uptime_ms = k_uptime_get();
    rec.uptime_ms = (uint64_t)uptime_ms;

    /* 1. Timestamp from RTC */
    if (i_rtc_is_bound(&self->rtc_port)) {
        int64_t epoch_s = 0;
        uint32_t frac_ms = 0;
        if (i_rtc_get_epoch(&self->rtc_port, &epoch_s, &frac_ms) == 0) {
            rec.epoch_s = epoch_s;
        }
    }

    /* 2. Thermal and Psychrometric Metrics */
    if (i_thermal_energy_is_bound(&self->thermal_port)) {
        thermal_energy_result_t res;
        if (i_thermal_energy_get_latest_result(&self->thermal_port, &res) == 0) {
            rec.t_ctrl_avg = res.t_sala_mean;
            rec.t_ctrl_max = res.t_sala_max;
            rec.t_ctrl_min = res.t_sala_min;
            rec.t_inj_mean = res.t_inj_mean;
            rec.t_ret_mean = res.t_ret_mean;
            rec.delta_t_clima = res.delta_t_clima_mean;
            rec.q_sens_kw = res.q_sensible_total;
            rec.q_lat_kw = res.q_latente_total;
            rec.cop_real = res.cop_real;
            rec.airflow_kg_s = res.m_dot_da_total;
            rec.t_dew_c = res.t_rocio_ret;
        }

        thermal_energy_capture_t cap;
        if (i_thermal_energy_get_latest_capture(&self->thermal_port, &cap) == 0) {
            rec.v_a = cap.voltage_v[0];
            rec.v_b = cap.voltage_v[1];
            rec.v_c = cap.voltage_v[2];
            rec.i_a = cap.current_a[0];
            rec.i_b = cap.current_a[1];
            rec.i_c = cap.current_a[2];
            rec.p_tot_kw = cap.total_active_power_kw;
            rec.power_factor = cap.power_factor;
            rec.energy_kwh = (float)cap.total_active_energy_kwh;
        }
    }

    /* 3. HVAC Runtime States & Control Temperatures */
    if (i_hvac_status_is_bound(&self->hvac_port)) {
        hvac_status_msg_t hvac_msg;
        if (i_hvac_status_get(&self->hvac_port, &hvac_msg) == 0) {
            rec.t_ctrl_g0 = hvac_msg.highest_temp[0];
            rec.t_ctrl_g1 = hvac_msg.highest_temp[1];
            rec.hvac_mode = (uint8_t)hvac_msg.group_state[0];

            uint8_t unit_mask = 0U;
            for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
                if (hvac_msg.unit_tri_state[u].relay_aa) {
                    unit_mask |= (uint8_t)(1U << u);
                }
            }
            rec.active_unit_mask = unit_mask;
        }
    }

    /* 4. Alarm bitfield snapshot */
    if (i_alarm_query_is_bound(&self->alarm_query)) {
        alarm_bitmask_snapshot_t snap;
        if (i_alarm_query_get_bitmask_snapshot(&self->alarm_query, &snap) == 0) {
            rec.alarm_bits = snap.active_mask[0];
        }
    }

    /* 5. Post event envelope to Datalogger Active Object */
    datalogger_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.type = DATALOGGER_EVT_TELEMETRY_SAMPLE;
    env.seq_id = datalogger_next_seq_id();
    env.uptime_ms = rec.uptime_ms;
    env.payload.telemetry = rec;

    int rc = ao_datalogger_post_event(self->datalogger_ao, &env);
    if (rc != 0) {
        LOG_WRN("Failed to post telemetry sample event: %d", rc);
    }

    self->last_sample_uptime_s = (int64_t)(rec.uptime_ms / 1000ULL);

    if (out_rec != NULL) {
        *out_rec = rec;
    }

    return rc;
}

int telemetry_sampler_sample_temperature(telemetry_sampler_t *self,
                                         datalogger_temperature_record_t *out_rec)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    datalogger_temperature_record_t rec;
    memset(&rec, 0, sizeof(rec));

    int64_t uptime_ms = k_uptime_get();
    rec.uptime_ms = (uint64_t)uptime_ms;

    if (i_rtc_is_bound(&self->rtc_port)) {
        int64_t epoch_s = 0;
        uint32_t frac_ms = 0;
        if (i_rtc_get_epoch(&self->rtc_port, &epoch_s, &frac_ms) == 0) {
            rec.epoch_s = epoch_s;
        }
    }

    if (i_thermal_energy_is_bound(&self->thermal_port)) {
        thermal_energy_result_t res;
        if (i_thermal_energy_get_latest_result(&self->thermal_port, &res) == 0) {
            rec.t_ctrl_avg = res.t_sala_mean;
            rec.t_ctrl_max = res.t_sala_max;
            rec.t_ctrl_min = res.t_sala_min;
            rec.t_inj_mean = res.t_inj_mean;
            rec.t_ret_mean = res.t_ret_mean;
            rec.delta_t_clima = res.delta_t_clima_mean;
        }
    }

    if (i_hvac_status_is_bound(&self->hvac_port)) {
        hvac_status_msg_t hvac_msg;
        if (i_hvac_status_get(&self->hvac_port, &hvac_msg) == 0) {
            rec.t_ctrl_g0 = hvac_msg.highest_temp[0];
            rec.t_ctrl_g1 = hvac_msg.highest_temp[1];
            rec.hvac_mode = (uint8_t)hvac_msg.group_state[0];

            uint8_t unit_mask = 0U;
            for (uint8_t u = 0U; u < HVAC_MAX_UNITS_TOTAL; u++) {
                if (hvac_msg.unit_tri_state[u].relay_aa) {
                    unit_mask |= (uint8_t)(1U << u);
                }
            }
            rec.active_unit_mask = unit_mask;
        }
    }

    datalogger_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.type = DATALOGGER_EVT_TEMPERATURE_SAMPLE;
    env.seq_id = datalogger_next_seq_id();
    env.uptime_ms = rec.uptime_ms;
    env.payload.temperature = rec;

    int rc = ao_datalogger_post_event(self->datalogger_ao, &env);
    if (rc != 0) {
        LOG_WRN("Failed to post temperature sample event: %d", rc);
    }

    self->last_sample_temp_s = (int64_t)(rec.uptime_ms / 1000ULL);

    if (out_rec != NULL) {
        *out_rec = rec;
    }
    return rc;
}

int telemetry_sampler_sample_energy(telemetry_sampler_t *self,
                                    datalogger_energy_record_t *out_rec)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    datalogger_energy_record_t rec;
    memset(&rec, 0, sizeof(rec));

    int64_t uptime_ms = k_uptime_get();
    rec.uptime_ms = (uint64_t)uptime_ms;

    if (i_rtc_is_bound(&self->rtc_port)) {
        int64_t epoch_s = 0;
        uint32_t frac_ms = 0;
        if (i_rtc_get_epoch(&self->rtc_port, &epoch_s, &frac_ms) == 0) {
            rec.epoch_s = epoch_s;
        }
    }

    if (i_thermal_energy_is_bound(&self->thermal_port)) {
        thermal_energy_capture_t cap;
        if (i_thermal_energy_get_latest_capture(&self->thermal_port, &cap) == 0) {
            rec.v_a = cap.voltage_v[0];
            rec.v_b = cap.voltage_v[1];
            rec.v_c = cap.voltage_v[2];
            rec.i_a = cap.current_a[0];
            rec.i_b = cap.current_a[1];
            rec.i_c = cap.current_a[2];
            rec.p_tot_kw = cap.total_active_power_kw;
            rec.power_factor = cap.power_factor;
            rec.energy_kwh = (float)cap.total_active_energy_kwh;
            rec.frequency_hz = 60.0f;
            rec.reactive_energy_kvarh = 0.0f;
        }
    }

    datalogger_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.type = DATALOGGER_EVT_ENERGY_SAMPLE;
    env.seq_id = datalogger_next_seq_id();
    env.uptime_ms = rec.uptime_ms;
    env.payload.energy = rec;

    int rc = ao_datalogger_post_event(self->datalogger_ao, &env);
    if (rc != 0) {
        LOG_WRN("Failed to post energy sample event: %d", rc);
    }

    self->last_sample_energy_s = (int64_t)(rec.uptime_ms / 1000ULL);

    if (out_rec != NULL) {
        *out_rec = rec;
    }
    return rc;
}

int telemetry_sampler_sample_thermal(telemetry_sampler_t *self,
                                     datalogger_thermal_record_t *out_rec)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    datalogger_thermal_record_t rec;
    memset(&rec, 0, sizeof(rec));

    int64_t uptime_ms = k_uptime_get();
    rec.uptime_ms = (uint64_t)uptime_ms;

    if (i_rtc_is_bound(&self->rtc_port)) {
        int64_t epoch_s = 0;
        uint32_t frac_ms = 0;
        if (i_rtc_get_epoch(&self->rtc_port, &epoch_s, &frac_ms) == 0) {
            rec.epoch_s = epoch_s;
        }
    }

    if (i_thermal_energy_is_bound(&self->thermal_port)) {
        thermal_energy_result_t res;
        if (i_thermal_energy_get_latest_result(&self->thermal_port, &res) == 0) {
            rec.q_sens_kw = res.q_sensible_total;
            rec.q_lat_kw = res.q_latente_total;
            rec.q_tot_kw = res.q_total;
            rec.cop_real = res.cop_real;
            rec.cop_carnot = res.cop_carnot;
            rec.eta_ii_pct = res.eta_ii_pct;
            rec.airflow_kg_s = res.m_dot_da_total;
            rec.t_dew_c = res.t_rocio_ret;
            rec.shf = res.shf;
        }
    }

    if (i_alarm_query_is_bound(&self->alarm_query)) {
        alarm_bitmask_snapshot_t snap;
        if (i_alarm_query_get_bitmask_snapshot(&self->alarm_query, &snap) == 0) {
            rec.alarm_bits = snap.active_mask[0];
        }
    }

    datalogger_event_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.type = DATALOGGER_EVT_THERMAL_SAMPLE;
    env.seq_id = datalogger_next_seq_id();
    env.uptime_ms = rec.uptime_ms;
    env.payload.thermal = rec;

    int rc = ao_datalogger_post_event(self->datalogger_ao, &env);
    if (rc != 0) {
        LOG_WRN("Failed to post thermal sample event: %d", rc);
    }

    self->last_sample_thermal_s = (int64_t)(rec.uptime_ms / 1000ULL);

    if (out_rec != NULL) {
        *out_rec = rec;
    }
    return rc;
}

int telemetry_sampler_tick(telemetry_sampler_t *self, int64_t now_uptime_s)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if ((now_uptime_s - self->last_sample_uptime_s) >= (int64_t)self->sample_interval_s) {
        self->last_sample_uptime_s = now_uptime_s;
        self->last_sample_temp_s = now_uptime_s;
        self->last_sample_energy_s = now_uptime_s;
        self->last_sample_thermal_s = now_uptime_s;

        (void)telemetry_sampler_sample_temperature(self, NULL);
        (void)telemetry_sampler_sample_energy(self, NULL);
        (void)telemetry_sampler_sample_thermal(self, NULL);
        (void)telemetry_sampler_sample_now(self, NULL);

        self->last_sample_uptime_s = now_uptime_s;
        self->last_sample_temp_s = now_uptime_s;
        self->last_sample_energy_s = now_uptime_s;
        self->last_sample_thermal_s = now_uptime_s;
        return 1;
    }

    return 0;
}
