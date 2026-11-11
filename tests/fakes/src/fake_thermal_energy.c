/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_thermal_energy.c
 * @brief In-memory Fake implementation of i_thermal_energy port.
 */

#include "fake_thermal_energy.h"
#include <errno.h>
#include <string.h>

static fake_thermal_energy_state_t s_fake_state;

static int fake_get_latest_result(void *impl, thermal_energy_result_t *out_result)
{
    fake_thermal_energy_state_t *s = (fake_thermal_energy_state_t *)impl;
    if (s == NULL || out_result == NULL) {
        return -EINVAL;
    }
    s->get_latest_result_calls++;
    if (s->get_latest_result_ret != 0) {
        return s->get_latest_result_ret;
    }
    memcpy(out_result, &s->result, sizeof(thermal_energy_result_t));
    return 0;
}

static int fake_get_latest_capture(void *impl, thermal_energy_capture_t *out_capture)
{
    fake_thermal_energy_state_t *s = (fake_thermal_energy_state_t *)impl;
    if (s == NULL || out_capture == NULL) {
        return -EINVAL;
    }
    s->get_latest_capture_calls++;
    if (s->get_latest_capture_ret != 0) {
        return s->get_latest_capture_ret;
    }
    memcpy(out_capture, &s->capture, sizeof(thermal_energy_capture_t));
    return 0;
}

static int fake_get_hourly_history(void *impl, uint16_t hours_ago, thermal_hourly_record_t *out_record)
{
    fake_thermal_energy_state_t *s = (fake_thermal_energy_state_t *)impl;
    if (s == NULL || out_record == NULL) {
        return -EINVAL;
    }
    s->get_hourly_history_calls++;
    if (s->get_hourly_history_ret != 0) {
        return s->get_hourly_history_ret;
    }
    if (hours_ago >= FAKE_THERMAL_ENERGY_HISTORY_DEPTH || !s->hourly_record_present[hours_ago]) {
        return -ENOENT;
    }
    memcpy(out_record, &s->hourly_records[hours_ago], sizeof(thermal_hourly_record_t));
    return 0;
}

static int fake_get_baseline(void *impl, config_thermal_baseline_t *out_baseline)
{
    fake_thermal_energy_state_t *s = (fake_thermal_energy_state_t *)impl;
    if (s == NULL || out_baseline == NULL) {
        return -EINVAL;
    }
    s->get_baseline_calls++;
    if (s->get_baseline_ret != 0) {
        return s->get_baseline_ret;
    }
    memcpy(out_baseline, &s->baseline, sizeof(config_thermal_baseline_t));
    return 0;
}

static int fake_trigger_baseline_recalibration(void *impl)
{
    fake_thermal_energy_state_t *s = (fake_thermal_energy_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->trigger_recalibration_calls++;
    return s->trigger_recalibration_ret;
}

static const i_thermal_energy_api_t s_fake_api = {
    .get_latest_result = fake_get_latest_result,
    .get_latest_capture = fake_get_latest_capture,
    .get_hourly_history = fake_get_hourly_history,
    .get_baseline = fake_get_baseline,
    .trigger_baseline_recalibration = fake_trigger_baseline_recalibration,
};

void fake_thermal_energy_reset(void)
{
    memset(&s_fake_state, 0, sizeof(s_fake_state));
}

i_thermal_energy_t fake_thermal_energy_get_port(void)
{
    i_thermal_energy_t port = {
        .api = &s_fake_api,
        .impl = &s_fake_state,
    };
    return port;
}

void fake_thermal_energy_set_latest_result(const thermal_energy_result_t *result)
{
    if (result != NULL) {
        memcpy(&s_fake_state.result, result, sizeof(thermal_energy_result_t));
    }
}

void fake_thermal_energy_set_latest_capture(const thermal_energy_capture_t *capture)
{
    if (capture != NULL) {
        memcpy(&s_fake_state.capture, capture, sizeof(thermal_energy_capture_t));
    }
}

void fake_thermal_energy_set_hourly_record(uint16_t hours_ago, const thermal_hourly_record_t *record)
{
    if (hours_ago < FAKE_THERMAL_ENERGY_HISTORY_DEPTH && record != NULL) {
        memcpy(&s_fake_state.hourly_records[hours_ago], record, sizeof(thermal_hourly_record_t));
        s_fake_state.hourly_record_present[hours_ago] = true;
    }
}

void fake_thermal_energy_set_baseline(const config_thermal_baseline_t *baseline)
{
    if (baseline != NULL) {
        memcpy(&s_fake_state.baseline, baseline, sizeof(config_thermal_baseline_t));
    }
}

void fake_thermal_energy_set_latest_result_ret(int ret)
{
    s_fake_state.get_latest_result_ret = ret;
}

void fake_thermal_energy_set_latest_capture_ret(int ret)
{
    s_fake_state.get_latest_capture_ret = ret;
}

void fake_thermal_energy_set_hourly_history_ret(int ret)
{
    s_fake_state.get_hourly_history_ret = ret;
}

void fake_thermal_energy_set_baseline_ret(int ret)
{
    s_fake_state.get_baseline_ret = ret;
}

void fake_thermal_energy_set_recalibration_ret(int ret)
{
    s_fake_state.trigger_recalibration_ret = ret;
}

const fake_thermal_energy_state_t *fake_thermal_energy_get_state(void)
{
    return &s_fake_state;
}
