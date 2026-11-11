/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_thermal_energy.c
 * @brief Active Object coordinator for Thermal and Energy Monitoring (Revision F Baseline).
 */

#include "application/active_objects/ao_thermal_energy.h"
#include "application/zbus/sensor_channels.h"
#include "application/zbus/digital_input_channels.h"
#include "application/zbus/thermal_channels.h"
#include "common/app_defaults.h"
#include "common/config_envelope.h"
#include "domain/thermal_energy/humidity_transducer.h"
#include "domain/thermal_energy/psychrometrics.h"
#include "domain/thermal_energy/thermodynamics.h"
#include "domain/thermal_energy/compressor_fsm.h"
#include "domain/thermal_energy/predictive_analytics.h"
#include "infrastructure/adapters/sensor_matrix_mapper.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ao_thermal_energy, LOG_LEVEL_INF);

/* ========================================================================= */
/* Forward Declarations of Port V-Table Methods                              */
/* ========================================================================= */

static int ao_thermal_energy_api_get_latest_result(void *impl, thermal_energy_result_t *out_result);
static int ao_thermal_energy_api_get_latest_capture(void *impl, thermal_energy_capture_t *out_capture);
static int ao_thermal_energy_api_get_hourly_history(void *impl, uint16_t hours_ago, thermal_hourly_record_t *out_record);
static int ao_thermal_energy_api_get_baseline(void *impl, config_thermal_baseline_t *out_baseline);
static int ao_thermal_energy_api_trigger_recalibration(void *impl);

static const i_thermal_energy_api_t s_ao_thermal_energy_api = {
    .get_latest_result               = ao_thermal_energy_api_get_latest_result,
    .get_latest_capture              = ao_thermal_energy_api_get_latest_capture,
    .get_hourly_history              = ao_thermal_energy_api_get_hourly_history,
    .get_baseline                    = ao_thermal_energy_api_get_baseline,
    .trigger_baseline_recalibration  = ao_thermal_energy_api_trigger_recalibration,
};

/* ========================================================================= */
/* Worker Thread Entry Loop                                                  */
/* ========================================================================= */

static void ao_thermal_energy_thread_entry(void *p1, void *p2, void *p3)
{
    ao_thermal_energy_t *self = (ao_thermal_energy_t *)p1;
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (!self->terminate) {
        /* 1. Block on start_sem until started */
        (void)k_sem_take(&self->start_sem, K_FOREVER);
        if (self->terminate) {
            break;
        }

        LOG_INF("ao_thermal_energy thread entered active loop");

        /* 2. Inner work loop */
        while (self->is_running && !self->stop_requested) {
            (void)ao_thermal_energy_step(self);

            /* Cadence: sleep for 1000 ms, waking promptly if stop requested */
            for (uint32_t elapsed = 0U; elapsed < AO_THERMAL_ENERGY_PERIOD_MS; elapsed += 50U) {
                if (!self->is_running || self->stop_requested || self->terminate) {
                    break;
                }
                k_msleep(50);
            }
        }

        LOG_INF("ao_thermal_energy thread exited inner loop");
        self->is_running = false;
        (void)k_sem_give(&self->stopped_sem);
    }

    LOG_INF("ao_thermal_energy thread terminated");
}

/* ========================================================================= */
/* Canonical 5-Phase Lifecycle Implementations                               */
/* ========================================================================= */

int ao_thermal_energy_init(ao_thermal_energy_t *self,
                           const config_thermal_site_t *site_cfg,
                           config_thermal_baseline_t *baseline_cfg,
                           k_thread_stack_t *stack,
                           size_t stack_size,
                           int priority)
{
    if (self == NULL || stack == NULL || stack_size < AO_THERMAL_ENERGY_STACK_SIZE) {
        return -EINVAL;
    }

    memset(self, 0, sizeof(*self));

    self->site_cfg = (site_cfg != NULL) ? site_cfg : app_defaults_get_thermal_site_cfg();
    self->baseline_cfg = baseline_cfg;
    self->stack = stack;
    self->stack_size = stack_size;
    self->priority = priority;

    k_sem_init(&self->start_sem, 0, 1);
    k_sem_init(&self->stopped_sem, 0, 1);
    k_mutex_init(&self->result_mutex);

    /* Initialize compressor FSMs */
    for (uint8_t c = 0U; c < THERMAL_MAX_HVAC_UNITS; c++) {
        compressor_fsm_init(&self->comp_fsm[c]);
    }

    /* Initialize OLS predictive model */
    ols_model_init(&self->ols_model);
    if (self->baseline_cfg != NULL && self->baseline_cfg->is_valid != 0U) {
        for (int i = 0; i < 4; i++) {
            self->ols_model.beta[i] = self->baseline_cfg->ols_beta_coefficients[i];
        }
        self->ols_model.condition_number = self->baseline_cfg->ols_condition_number;
        self->ols_model.training_hours = self->baseline_cfg->training_hours_accumulated;
        self->ols_model.ols_trained = true;
    }

    /* Bind fat-pointer port handle */
    self->port.api = &s_ao_thermal_energy_api;
    self->port.impl = self;

    self->is_initialized = true;
    self->is_running = false;
    self->stop_requested = false;
    self->terminate = false;

    /* Spawn worker thread in gated state */
    (void)k_thread_create(&self->thread,
                          self->stack,
                          self->stack_size,
                          ao_thermal_energy_thread_entry,
                          self, NULL, NULL,
                          self->priority, 0, K_NO_WAIT);
    (void)k_thread_name_set(&self->thread, "ao_thermal_energy");

    return 0;
}

int ao_thermal_energy_start(ao_thermal_energy_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (self->is_running) {
        return -EALREADY;
    }

    self->stop_requested = false;
    self->is_running = true;
    k_sem_give(&self->start_sem);

    return 0;
}

int ao_thermal_energy_stop(ao_thermal_energy_t *self)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return -EALREADY;
    }

    self->stop_requested = true;
    return 0;
}

int ao_thermal_energy_wait_stopped(ao_thermal_energy_t *self, k_timeout_t timeout)
{
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }

    if (!self->is_running) {
        return 0;
    }

    int ret = k_sem_take(&self->stopped_sem, timeout);
    if (ret == 0) {
        self->is_running = false;
        self->stop_requested = false;
    }

    return ret;
}

int ao_thermal_energy_deinit(ao_thermal_energy_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }

    if (!self->is_initialized) {
        return 0;
    }

    if (self->is_running) {
        (void)ao_thermal_energy_stop(self);
        (void)ao_thermal_energy_wait_stopped(self, K_MSEC(500));
    }

    self->terminate = true;
    k_sem_give(&self->start_sem);
    k_thread_abort(&self->thread);

    self->is_initialized = false;
    return 0;
}

i_thermal_energy_t ao_thermal_energy_get_port(ao_thermal_energy_t *self)
{
    if (self == NULL || !self->is_initialized) {
        i_thermal_energy_t empty = {0};
        return empty;
    }

    return self->port;
}

/* ========================================================================= */
/* Port V-Table Interface Methods                                            */
/* ========================================================================= */

static int ao_thermal_energy_api_get_latest_result(void *impl, thermal_energy_result_t *out_result)
{
    ao_thermal_energy_t *self = (ao_thermal_energy_t *)impl;
    if (self == NULL || out_result == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->result_mutex, K_FOREVER);
    *out_result = self->latest_result;
    k_mutex_unlock(&self->result_mutex);

    return 0;
}

static int ao_thermal_energy_api_get_latest_capture(void *impl, thermal_energy_capture_t *out_capture)
{
    ao_thermal_energy_t *self = (ao_thermal_energy_t *)impl;
    if (self == NULL || out_capture == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->result_mutex, K_FOREVER);
    *out_capture = self->latest_capture;
    k_mutex_unlock(&self->result_mutex);

    return 0;
}

static int ao_thermal_energy_api_get_hourly_history(void *impl, uint16_t hours_ago, thermal_hourly_record_t *out_record)
{
    ao_thermal_energy_t *self = (ao_thermal_energy_t *)impl;
    if (self == NULL || out_record == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->result_mutex, K_FOREVER);

    if (self->history_count == 0U || hours_ago >= self->history_count) {
        k_mutex_unlock(&self->result_mutex);
        return -ENOENT;
    }

    uint16_t idx = (self->history_head + THERMAL_HOURLY_HISTORY_CAPACITY - 1U - hours_ago) % THERMAL_HOURLY_HISTORY_CAPACITY;
    *out_record = self->history[idx];

    k_mutex_unlock(&self->result_mutex);
    return 0;
}

static int ao_thermal_energy_api_get_baseline(void *impl, config_thermal_baseline_t *out_baseline)
{
    ao_thermal_energy_t *self = (ao_thermal_energy_t *)impl;
    if (self == NULL || out_baseline == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->result_mutex, K_FOREVER);
    if (self->baseline_cfg != NULL) {
        *out_baseline = *self->baseline_cfg;
    } else {
        memset(out_baseline, 0, sizeof(*out_baseline));
    }
    k_mutex_unlock(&self->result_mutex);

    return 0;
}

static int ao_thermal_energy_api_trigger_recalibration(void *impl)
{
    ao_thermal_energy_t *self = (ao_thermal_energy_t *)impl;
    if (self == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->result_mutex, K_FOREVER);
    ols_model_init(&self->ols_model);
    if (self->baseline_cfg != NULL) {
        self->baseline_cfg->training_hours_accumulated = 0U;
        self->baseline_cfg->is_valid = 0U;
    }
    k_mutex_unlock(&self->result_mutex);

    return 0;
}

/* ========================================================================= */
/* Telemetry Feed & Injection Helpers                                        */
/* ========================================================================= */

int ao_thermal_energy_feed_sensor_snapshot(ao_thermal_energy_t *self,
                                           const sensor_snapshot_msg_t *snapshot)
{
    if (self == NULL || snapshot == NULL) {
        return -EINVAL;
    }

    self->last_sensor_snapshot = *snapshot;
    self->has_sensor_snapshot = true;
    self->sensor_snapshot_fed = true;
    return 0;
}

int ao_thermal_energy_feed_di_snapshot(ao_thermal_energy_t *self,
                                       const di_snapshot_msg_t *snapshot)
{
    if (self == NULL || snapshot == NULL) {
        return -EINVAL;
    }

    self->last_di_snapshot = *snapshot;
    self->has_di_snapshot = true;
    self->di_snapshot_fed = true;
    return 0;
}

int ao_thermal_energy_feed_energy_reading(ao_thermal_energy_t *self,
                                          const energy_reading_t *reading)
{
    if (self == NULL || reading == NULL) {
        return -EINVAL;
    }

    self->latest_elec_reading = *reading;
    self->has_elec_reading = true;
    return 0;
}

int ao_thermal_energy_feed_capture(ao_thermal_energy_t *self,
                                   const thermal_energy_capture_t *capture)
{
    if (self == NULL || capture == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&self->result_mutex, K_FOREVER);
    self->latest_capture = *capture;
    self->capture_injected = true;
    k_mutex_unlock(&self->result_mutex);

    return 0;
}

void ao_thermal_energy_set_baseline_persist_cb(ao_thermal_energy_t *self,
                                               ao_thermal_baseline_persist_cb_t cb,
                                               void *user_data)
{
    if (self != NULL) {
        self->baseline_persist_cb = cb;
        self->baseline_persist_user_data = user_data;
    }
}

/* ========================================================================= */
/* Hourly History Compression                                                */
/* ========================================================================= */

int ao_thermal_energy_record_hour(ao_thermal_energy_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }

    thermal_hourly_record_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.timestamp_unix = (uint32_t)(k_uptime_get() / 1000U);

    /* Temperatures */
    for (uint8_t k = 0U; k < CONFIG_THERMAL_HOURLY_PAIRS_COUNT; k++) {
        if (k < THERMAL_MAX_PAIRS) {
            rec.t_inj_x10[k] = (int16_t)roundf(self->latest_capture.t_inj[k] * 10.0f);
            rec.t_ret_x10[k] = (int16_t)roundf(self->latest_capture.t_ret[k] * 10.0f);
        }
    }

    for (uint8_t m = 0U; m < CONFIG_THERMAL_HOURLY_ROOM_PROBES_COUNT; m++) {
        if (m < THERMAL_MAX_ROOM_PROBES) {
            rec.t_room_x10[m] = (int16_t)roundf(self->latest_capture.t_room[m] * 10.0f);
        }
    }

    for (uint8_t l = 0U; l < CONFIG_THERMAL_HOURLY_OUTDOOR_PROBES_COUNT; l++) {
        if (l < THERMAL_MAX_OUTDOOR_PROBES) {
            rec.t_outdoor_x10[l] = (int16_t)roundf(self->latest_capture.t_outdoor[l] * 10.0f);
        }
    }

    rec.humidity_return_x10 = (uint16_t)roundf(self->latest_capture.humidity_return_pct * 10.0f);
    rec.active_power_kw = self->latest_result.active_electrical_power_kw;
    rec.sensible_capacity_kw = self->latest_result.q_sensible_total;
    rec.latent_capacity_kw = self->latest_result.q_latente_total;
    rec.cop_real_x100 = (uint16_t)roundf(self->latest_result.cop_real * 100.0f);
    rec.eta_ii_x100 = (uint16_t)roundf(self->latest_result.eta_ii_pct * 100.0f);
    rec.fan_state_mask = self->latest_capture.fan_state_mask;
    rec.comp_state_mask = self->latest_capture.comp_state_mask;

    uint32_t cumulative_starts = 0U;
    for (uint8_t c = 0U; c < THERMAL_MAX_HVAC_UNITS; c++) {
        cumulative_starts += self->comp_fsm[c].starts_count;
    }
    rec.comp_starts_count = cumulative_starts;

    /* Compute CRC-16-CCITT across preceding fields */
    rec.crc16 = config_envelope_crc16((const uint8_t *)&rec, offsetof(thermal_hourly_record_t, crc16));

    /* Insert into RAM circular buffer */
    k_mutex_lock(&self->result_mutex, K_FOREVER);

    self->history[self->history_head] = rec;
    self->history_head = (self->history_head + 1U) % THERMAL_HOURLY_HISTORY_CAPACITY;
    if (self->history_count < THERMAL_HOURLY_HISTORY_CAPACITY) {
        self->history_count++;
    }

    k_mutex_unlock(&self->result_mutex);

    /* Check steady state and accumulate in OLS */
    uint8_t n_units = (self->site_cfg != NULL && self->site_cfg->configured_pairs_count > 0U)
                          ? self->site_cfg->configured_pairs_count
                          : 6U;
    if (n_units > THERMAL_MAX_HVAC_UNITS) {
        n_units = THERMAL_MAX_HVAC_UNITS;
    }

    float f_carga = compressor_fsm_calc_load_fraction(self->comp_fsm, n_units,
                                                     (self->site_cfg != NULL) ? self->site_cfg->nominal_capacity_kw : NULL);

    bool steady_state = (f_carga > 0.05f);
    for (uint8_t c = 0U; c < n_units; c++) {
        if (self->comp_fsm[c].state == COMPRESSOR_STATE_INRUSH_WAIT ||
            self->comp_fsm[c].state == COMPRESSOR_STATE_THERMAL_WARMUP) {
            steady_state = false;
            break;
        }
    }

    if (steady_state && !self->ols_model.ols_trained) {
        ols_model_accumulate(&self->ols_model,
                             self->latest_result.t_ret_mean,
                             self->latest_result.t_outdoor_effective,
                             (double)f_carga,
                             (double)self->latest_result.q_total);
        self->ols_model.training_hours++;

        if (self->ols_model.training_hours >= OLS_MIN_TRAINING_HOURS) {
            int solve_rc = ols_model_solve(&self->ols_model, OLS_DEFAULT_TIKHONOV_LAMBDA);
            if (solve_rc == 0) {
                if (self->baseline_cfg != NULL) {
                    for (int i = 0; i < 4; i++) {
                        self->baseline_cfg->ols_beta_coefficients[i] = self->ols_model.beta[i];
                    }
                    self->baseline_cfg->ols_condition_number = self->ols_model.condition_number;
                    self->baseline_cfg->training_hours_accumulated = self->ols_model.training_hours;
                    self->baseline_cfg->is_valid = 1U;
                    self->baseline_cfg->training_timestamp_unix = rec.timestamp_unix;
                }

                if (self->baseline_persist_cb != NULL) {
                    self->baseline_persist_cb(self->baseline_persist_user_data, self->baseline_cfg);
                }
            }
        }
    }

    return 0;
}

/* ========================================================================= */
/* Periodic 1 Hz Calculation Engine                                          */
/* ========================================================================= */

int ao_thermal_energy_step(ao_thermal_energy_t *self)
{
    if (self == NULL) {
        return -EINVAL;
    }

    uint64_t start_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());

    /* 1. Ingestion: build capture if not injected directly */
    if (!self->capture_injected) {
        /* Read latest telemetry from Zbus if available and not fed directly */
        if (!self->sensor_snapshot_fed) {
            sensor_snapshot_msg_t s_snap;
            if (zbus_chan_read(&chan_sensor_snapshot, &s_snap, K_NO_WAIT) == 0 && s_snap.timestamp_ms > 0ULL) {
                self->last_sensor_snapshot = s_snap;
                self->has_sensor_snapshot = true;
            }
        }

        if (!self->di_snapshot_fed) {
            di_snapshot_msg_t d_snap;
            if (zbus_chan_read(&chan_digital_input_snapshot, &d_snap, K_NO_WAIT) == 0 && d_snap.timestamp_ms > 0ULL) {
                self->last_di_snapshot = d_snap;
                self->has_di_snapshot = true;
            }
        }

        thermal_capture_source_t src;
        memset(&src, 0, sizeof(src));
        src.generation_id = ++self->result_generation_id;
        src.timestamp_ms = (uint64_t)k_uptime_get();
        src.site_cfg = self->site_cfg;

        if (self->has_sensor_snapshot) {
            src.flat_temps = self->last_sensor_snapshot.temperatures;
            src.flat_valid_mask = self->last_sensor_snapshot.valid_mask;
            src.temp_timestamp_ms = self->last_sensor_snapshot.timestamp_ms;
        }

        if (self->has_di_snapshot) {
            src.di_snapshot = &self->last_di_snapshot;
        }

        if (self->has_elec_reading) {
            src.elec_reading = &self->latest_elec_reading;
        }

        (void)sensor_matrix_mapper_build_capture(&src, &self->latest_capture);
    } else {
        self->capture_injected = false;
        self->latest_capture.generation_id = ++self->result_generation_id;
        self->latest_capture.capture_timestamp_ms = (uint64_t)k_uptime_get();
    }

    uint8_t n_pairs = self->latest_capture.configured_pairs;
    if (n_pairs == 0U || n_pairs > THERMAL_MAX_PAIRS) {
        n_pairs = 6U;
    }

    uint8_t n_units = n_pairs;
    if (n_units > THERMAL_MAX_HVAC_UNITS) {
        n_units = THERMAL_MAX_HVAC_UNITS;
    }

    /* 2. Run compressor FSM updates */
    for (uint8_t c = 0U; c < n_units; c++) {
        bool comp_run = (self->latest_capture.comp_state_mask & (1U << c)) != 0U;
        compressor_fsm_update(&self->comp_fsm[c],
                              comp_run,
                              self->latest_capture.total_active_power_kw,
                              false);
    }

    float f_carga = compressor_fsm_calc_load_fraction(self->comp_fsm, n_units,
                                                     (self->site_cfg != NULL) ? self->site_cfg->nominal_capacity_kw : NULL);

    /* 3. Evaluate psychrometrics and boundary temperatures */
    int16_t alt = (self->site_cfg != NULL) ? self->site_cfg->site_altitude_m : 0;
    float p_atm_kpa = psychrometrics_p_atm(alt);

    float sum_ret = 0.0f, sum_inj = 0.0f;
    uint8_t valid_p_cnt = 0U;
    for (uint8_t k = 0U; k < n_pairs; k++) {
        if ((self->latest_capture.pair_valid_mask & (1U << k)) != 0U) {
            sum_ret += self->latest_capture.t_ret[k];
            sum_inj += self->latest_capture.t_inj[k];
            valid_p_cnt++;
        }
    }
    float t_ret_mean = (valid_p_cnt > 0U) ? (sum_ret / (float)valid_p_cnt) : 24.0f;
    float t_inj_mean = (valid_p_cnt > 0U) ? (sum_inj / (float)valid_p_cnt) : 14.0f;

    /* Room ambient summary */
    float sum_room = 0.0f;
    float t_room_max = -999.0f;
    float t_room_min = 999.0f;
    uint8_t valid_r_cnt = 0U;
    for (uint8_t m = 0U; m < self->latest_capture.configured_room_probes; m++) {
        if ((self->latest_capture.room_valid_mask & (1U << m)) != 0U) {
            float tr = self->latest_capture.t_room[m];
            sum_room += tr;
            if (tr > t_room_max) t_room_max = tr;
            if (tr < t_room_min) t_room_min = tr;
            valid_r_cnt++;
        }
    }
    float t_room_mean = (valid_r_cnt > 0U) ? (sum_room / (float)valid_r_cnt) : t_ret_mean;
    if (valid_r_cnt == 0U) {
        t_room_max = t_ret_mean;
        t_room_min = t_ret_mean;
    }

    /* Outdoor condenser reference */
    float sum_out = 0.0f;
    uint8_t valid_o_cnt = 0U;
    for (uint8_t l = 0U; l < self->latest_capture.configured_outdoor_probes; l++) {
        if ((self->latest_capture.outdoor_valid_mask & (1U << l)) != 0U) {
            sum_out += self->latest_capture.t_outdoor[l];
            valid_o_cnt++;
        }
    }
    float t_outdoor_eff = (valid_o_cnt > 0U) ? (sum_out / (float)valid_o_cnt) : 35.0f;

    float p_ws_ret = psychrometrics_p_ws(t_ret_mean);
    float p_w_ret = psychrometrics_p_w(p_ws_ret, self->latest_capture.humidity_return_pct);
    float omega_ret = psychrometrics_humidity_ratio(p_atm_kpa, p_w_ret);
    float t_rocio_ret = psychrometrics_dew_point(p_w_ret);
    float rho_da_ret = psychrometrics_dry_air_density(p_atm_kpa, p_w_ret, t_ret_mean);

    /* 4. Evaluate thermodynamics */
    thermodynamics_input_t th_in;
    memset(&th_in, 0, sizeof(th_in));
    th_in.configured_pairs = n_pairs;
    th_in.pair_valid_mask = self->latest_capture.pair_valid_mask;

    float total_m_da = 0.0f;
    for (uint8_t k = 0U; k < n_pairs; k++) {
        th_in.t_inj[k] = self->latest_capture.t_inj[k];
        th_in.t_ret[k] = self->latest_capture.t_ret[k];
        float v_nom = (self->site_cfg != NULL && self->site_cfg->nominal_airflow_m3h[k] > 0.0f)
                          ? self->site_cfg->nominal_airflow_m3h[k]
                          : 4500.0f;
        bool fan_on = (self->latest_capture.fan_state_mask & (1U << k)) != 0U;
        bool fan_effective = fan_on || (self->latest_capture.fan_state_mask == 0U);
        th_in.m_dot_da[k] = psychrometrics_air_mass_flow(v_nom, rho_da_ret, fan_effective);
        if ((th_in.pair_valid_mask & (1U << k)) != 0U) {
            total_m_da += th_in.m_dot_da[k];
        }
    }

    th_in.p_atm_kpa = p_atm_kpa;
    th_in.omega_ret = omega_ret;
    th_in.t_rocio_ret = t_rocio_ret;
    th_in.t_inj_mean = t_inj_mean;
    th_in.t_outdoor_eff = t_outdoor_eff;
    th_in.total_active_power_kw = self->latest_capture.total_active_power_kw;
    th_in.elec_power_valid = (self->latest_capture.elec_quality == QUALITY_VALID);

    thermodynamics_output_t th_out;
    memset(&th_out, 0, sizeof(th_out));
    (void)thermodynamics_compute(&th_in, &th_out);

    /* 5. Predictive Diagnostics */
    float q_exp = 0.0f;
    float cop_exp = 0.0f;
    if (self->ols_model.ols_trained) {
        q_exp = ols_model_predict_q(&self->ols_model, t_ret_mean, t_outdoor_eff, f_carga);
        cop_exp = predictive_calc_expected_cop(q_exp, th_in.total_active_power_kw);
    }

    float eta_ii_base = (self->baseline_cfg != NULL && self->baseline_cfg->baseline_exergetic_efficiency > 0.0f)
                            ? self->baseline_cfg->baseline_exergetic_efficiency
                            : 40.0f;
    float pct_mech_deg = predictive_calc_mechanical_degradation(th_out.cop_real, cop_exp, th_out.eta_ii_pct, eta_ii_base);

    thermal_energy_result_t res;
    memset(&res, 0, sizeof(res));

    res.generation_id = self->latest_capture.generation_id;
    res.result_timestamp_ms = (uint64_t)k_uptime_get();

    for (uint8_t k = 0U; k < n_pairs; k++) {
        res.delta_t_pair[k] = th_out.delta_t[k];
        res.q_sensible_pair[k] = th_out.q_sensible_pair[k];
        res.q_latente_pair[k] = th_out.q_latent_pair[k];

        float t_inj_base = (self->baseline_cfg != NULL && self->baseline_cfg->baseline_pair_t_inj[k] > 0.0f)
                               ? self->baseline_cfg->baseline_pair_t_inj[k]
                               : 14.0f;
        float delta_t_base = (self->baseline_cfg != NULL && self->baseline_cfg->baseline_pair_delta_t[k] > 0.0f)
                                 ? self->baseline_cfg->baseline_pair_delta_t[k]
                                 : 10.0f;

        res.pct_filter_restriction[k] = predictive_calc_filter_restriction(t_inj_base, self->latest_capture.t_inj[k], delta_t_base, th_out.delta_t[k]);
        res.filter_days_remaining[k] = predictive_calc_filter_days_remaining(self->ols_model.training_hours, res.pct_filter_restriction[k], self->latest_capture.t_inj[k], 0.0f);
        res.pct_refrigerant_leak[k] = predictive_calc_refrigerant_leak(delta_t_base, th_out.delta_t[k], f_carga, th_out.eta_ii_pct, eta_ii_base, res.pct_filter_restriction[k]);
    }

    res.t_inj_mean = t_inj_mean;
    res.t_ret_mean = t_ret_mean;
    res.delta_t_clima_mean = t_ret_mean - t_inj_mean;
    res.t_sala_mean = t_room_mean;
    res.t_sala_max = t_room_max;
    res.t_sala_min = t_room_min;
    res.t_outdoor_effective = t_outdoor_eff;

    float hotspot_spread = 0.0f;
    bool is_hotspot = predictive_check_room_hotspot(t_room_max, t_room_min, &hotspot_spread);
    res.delta_t_sala_spread = hotspot_spread;

    float bypass_grad = 0.0f;
    bool is_bypass = predictive_check_airflow_bypass(t_room_mean, t_ret_mean, t_room_max, &bypass_grad);
    res.delta_t_bypass = bypass_grad;

    float pct_latent = (1.0f - th_out.shf) * 100.0f;
    bool is_moisture = predictive_check_moisture_ingress(pct_latent, th_out.shf, omega_ret);

    res.p_atm_kpa = p_atm_kpa;
    res.p_ws_ret_kpa = p_ws_ret;
    res.p_w_ret_kpa = p_w_ret;
    res.humidity_ratio_ret = omega_ret;
    res.t_rocio_ret = t_rocio_ret;
    res.rho_da_ret = rho_da_ret;
    res.m_dot_da_total = total_m_da;

    res.q_sensible_total = th_out.q_sensible_total;
    res.q_latente_total = th_out.q_latent_total;
    res.q_total = th_out.q_total;
    res.m_dot_condensate_lph = th_out.m_dot_condensate_lph;
    res.shf = th_out.shf;
    res.pct_latent_heat = pct_latent;

    res.cop_real = th_out.cop_real;
    res.cop_sensible = th_out.cop_sensible;
    res.t_evap_sat_k = th_out.t_evap_sat_k;
    res.t_cond_sat_k = th_out.t_cond_sat_k;
    res.cop_carnot = th_out.cop_carnot;
    res.eta_ii_pct = th_out.eta_ii_pct;
    res.active_electrical_power_kw = self->latest_capture.total_active_power_kw;

    for (uint8_t c = 0U; c < n_units; c++) {
        res.comp_fsm_state[c] = self->comp_fsm[c].state;
    }

    res.ols_trained = self->ols_model.ols_trained;
    res.training_hours_accumulated = self->ols_model.training_hours;
    res.q_total_expected_kw = q_exp;
    res.cop_expected = cop_exp;
    res.pct_mechanical_degradation = pct_mech_deg;

    if (is_hotspot) res.diagnostic_alarm_mask |= (1U << 0);
    if (is_bypass)  res.diagnostic_alarm_mask |= (1U << 1);
    if (is_moisture) res.diagnostic_alarm_mask |= (1U << 2);

    /* Weather derating and redundant capacity */
    float q_nom_corr[THERMAL_MAX_HVAC_UNITS] = {0};
    float q_nom_total_corr = 0.0f;
    for (uint8_t u = 0U; u < n_units; u++) {
        float q_n = (self->site_cfg != NULL && self->site_cfg->nominal_capacity_kw[u] > 0.0f)
                        ? self->site_cfg->nominal_capacity_kw[u]
                        : 20.0f;
        q_nom_corr[u] = predictive_calc_weather_corrected_capacity(q_n, t_outdoor_eff);
        q_nom_total_corr += q_nom_corr[u];
    }
    uint8_t u_req = (self->site_cfg != NULL && self->site_cfg->required_redundancy_units > 0U)
                        ? self->site_cfg->required_redundancy_units
                        : 2U;
    res.q_nominal_weather_corrected = q_nom_total_corr;
    res.q_max_safe = predictive_calc_safe_redundant_capacity(q_nom_corr, u_req, pct_mech_deg);
    res.kw_free_cooling_margin = predictive_calc_free_cooling_margin(res.q_max_safe, th_out.q_sensible_total);

    /* Quality statuses */
    res.cop_quality = th_out.cop_quality;
    res.thermal_quality = (valid_p_cnt > 0U) ? QUALITY_VALID : QUALITY_FAILED;
    res.psychrometric_quality = self->latest_capture.humidity_quality;
    res.predictive_quality = self->ols_model.ols_trained ? QUALITY_VALID : QUALITY_SUSPECT;
    res.overall_quality = (res.thermal_quality == QUALITY_VALID && res.cop_quality == QUALITY_VALID)
                              ? QUALITY_VALID
                              : QUALITY_SUSPECT;

    res.calculation_duration_us = (uint32_t)(k_ticks_to_us_near64(k_uptime_ticks()) - start_us);

    /* 6. Commit atomic result update */
    k_mutex_lock(&self->result_mutex, K_FOREVER);
    self->latest_result = res;
    k_mutex_unlock(&self->result_mutex);

    /* 7. Publish to Zbus */
    thermal_energy_event_t evt;
    evt.generation_id = res.generation_id;
    evt.timestamp_ms = res.result_timestamp_ms;
    evt.quality = res.overall_quality;
    (void)zbus_chan_pub(&chan_thermal_energy_update, &evt, K_MSEC(50));

    /* 8. Periodic hourly accumulator */
    self->seconds_in_current_hour++;
    if (self->seconds_in_current_hour >= 3600U) {
        self->seconds_in_current_hour = 0U;
        (void)ao_thermal_energy_record_hour(self);
    }

    return 0;
}
