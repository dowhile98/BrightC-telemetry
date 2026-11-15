/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for Telemetry Sampler Aggregator Application Service.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include "application/services/telemetry_sampler.h"
#include "application/active_objects/ao_datalogger.h"
#include "interfaces/i_hvac_status.h"
#include "fake_storage_fs.h"
#include "fake_bkpsram.h"
#include "fake_rtc.h"

static ao_datalogger_t s_sampler_ao;
static fake_bkpsram_t  s_mock_bkp;
static struct k_msgq   s_sampler_msgq;
static uint8_t         s_sampler_queue_buffer[16 * sizeof(datalogger_event_envelope_t)];
K_THREAD_STACK_DEFINE(s_sampler_datalogger_stack, 2048);

static thermal_energy_result_t  s_mock_thermal_res;
static thermal_energy_capture_t s_mock_thermal_cap;
static alarm_bitmask_snapshot_t s_mock_alarm_snap;

static int mock_get_latest_result(void *impl, thermal_energy_result_t *out)
{
    ARG_UNUSED(impl);
    *out = s_mock_thermal_res;
    return 0;
}

static int mock_get_latest_capture(void *impl, thermal_energy_capture_t *out)
{
    ARG_UNUSED(impl);
    *out = s_mock_thermal_cap;
    return 0;
}

static int mock_get_hourly_history(void *impl, uint16_t h, thermal_hourly_record_t *out) { ARG_UNUSED(impl); ARG_UNUSED(h); ARG_UNUSED(out); return 0; }
static int mock_get_baseline(void *impl, config_thermal_baseline_t *out) { ARG_UNUSED(impl); ARG_UNUSED(out); return 0; }
static int mock_trigger_baseline(void *impl) { ARG_UNUSED(impl); return 0; }

static const i_thermal_energy_api_t s_mock_thermal_api = {
    .get_latest_result = mock_get_latest_result,
    .get_latest_capture = mock_get_latest_capture,
    .get_hourly_history = mock_get_hourly_history,
    .get_baseline = mock_get_baseline,
    .trigger_baseline_recalibration = mock_trigger_baseline,
};

static size_t mock_get_active_count(void *impl) { ARG_UNUSED(impl); return 1; }
static alarm_severity_t mock_get_highest_severity(void *impl) { ARG_UNUSED(impl); return ALARM_SEVERITY_WARNING; }
static bool mock_is_active(void *impl, uint16_t id) { ARG_UNUSED(impl); ARG_UNUSED(id); return false; }
static int mock_get_alarm_state(void *impl, uint16_t id, alarm_state_t *s) { ARG_UNUSED(impl); ARG_UNUSED(id); ARG_UNUSED(s); return 0; }
static int mock_get_alarm_snapshot(void *impl, uint16_t id, alarm_snapshot_entry_t *s) { ARG_UNUSED(impl); ARG_UNUSED(id); ARG_UNUSED(s); return 0; }
static size_t mock_get_active_alarm_ids(void *impl, uint16_t *ids, size_t m) { ARG_UNUSED(impl); ARG_UNUSED(ids); ARG_UNUSED(m); return 0; }
static int mock_get_descriptor(void *impl, uint16_t id, alarm_descriptor_t *d) { ARG_UNUSED(impl); ARG_UNUSED(id); ARG_UNUSED(d); return 0; }
static uint32_t mock_get_active_duration(void *impl, uint16_t id) { ARG_UNUSED(impl); ARG_UNUSED(id); return 0; }

static int mock_get_bitmask_snapshot(void *impl, alarm_bitmask_snapshot_t *out)
{
    ARG_UNUSED(impl);
    *out = s_mock_alarm_snap;
    return 0;
}

static const struct i_alarm_query_api s_mock_alarm_api = {
    .get_active_count = mock_get_active_count,
    .get_highest_active_severity = mock_get_highest_severity,
    .is_active = mock_is_active,
    .get_alarm_state = mock_get_alarm_state,
    .get_alarm_snapshot = mock_get_alarm_snapshot,
    .get_active_alarm_ids = mock_get_active_alarm_ids,
    .get_descriptor = mock_get_descriptor,
    .get_active_duration = mock_get_active_duration,
    .get_bitmask_snapshot = mock_get_bitmask_snapshot,
};

static hvac_status_msg_t s_mock_hvac_status;

static int mock_get_hvac_status(void *impl, hvac_status_msg_t *out)
{
    ARG_UNUSED(impl);
    *out = s_mock_hvac_status;
    return 0;
}

static const i_hvac_status_api_t s_mock_hvac_api = {
    .get_status = mock_get_hvac_status,
};

static void *telemetry_sampler_suite_setup(void)
{
    return NULL;
}

static void telemetry_sampler_suite_before(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_storage_fs_reset();
    fake_bkpsram_init(&s_mock_bkp);
    fake_bkpsram_reset(&s_mock_bkp);
    fake_rtc_reset();
    fake_rtc_set_epoch_direct(1794474862, 0);

    memset(&s_mock_thermal_res, 0, sizeof(s_mock_thermal_res));
    memset(&s_mock_thermal_cap, 0, sizeof(s_mock_thermal_cap));
    memset(&s_mock_alarm_snap, 0, sizeof(s_mock_alarm_snap));
    memset(&s_mock_hvac_status, 0, sizeof(s_mock_hvac_status));

    s_mock_hvac_status.highest_temp[0] = 24.2f;
    s_mock_hvac_status.highest_temp[1] = 23.9f;
    s_mock_hvac_status.group_state[0] = GROUP_STATE_COOLING;
    s_mock_hvac_status.unit_tri_state[0].relay_aa = true;
    s_mock_hvac_status.unit_tri_state[1].relay_aa = true;

    s_mock_thermal_res.t_sala_mean = 23.4f;
    s_mock_thermal_res.t_sala_max = 24.5f;
    s_mock_thermal_res.t_sala_min = 22.1f;
    s_mock_thermal_res.t_inj_mean = 14.5f;
    s_mock_thermal_res.t_ret_mean = 24.2f;
    s_mock_thermal_res.delta_t_clima_mean = 9.7f;
    s_mock_thermal_res.q_sensible_total = 12.5f;
    s_mock_thermal_res.q_latente_total = 2.1f;
    s_mock_thermal_res.cop_real = 3.65f;
    s_mock_thermal_res.m_dot_da_total = 0.850f;
    s_mock_thermal_res.t_rocio_ret = 12.8f;

    s_mock_thermal_cap.voltage_v[0] = 220.5f;
    s_mock_thermal_cap.voltage_v[1] = 221.0f;
    s_mock_thermal_cap.voltage_v[2] = 219.8f;
    s_mock_thermal_cap.current_a[0] = 14.2f;
    s_mock_thermal_cap.current_a[1] = 13.9f;
    s_mock_thermal_cap.current_a[2] = 14.0f;
    s_mock_thermal_cap.total_active_power_kw = 8.250f;
    s_mock_thermal_cap.power_factor = 0.95f;
    s_mock_thermal_cap.total_active_energy_kwh = 12543.5;

    s_mock_alarm_snap.active_mask[0] = 0x00000004;

    k_msgq_init(&s_sampler_msgq, (char *)s_sampler_queue_buffer,
                sizeof(datalogger_event_envelope_t), 16);

    i_storage_fs_t fs = fake_storage_fs_get_port();
    i_bkpsram_t bkp = fake_bkpsram_port(&s_mock_bkp);
    zassert_equal(ao_datalogger_init(&s_sampler_ao, &fs, &bkp, &s_sampler_msgq,
                                     s_sampler_datalogger_stack,
                                     K_THREAD_STACK_SIZEOF(s_sampler_datalogger_stack), 8), 0);
}

static void telemetry_sampler_suite_after(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)ao_datalogger_stop(&s_sampler_ao);
    (void)ao_datalogger_wait_stopped(&s_sampler_ao, K_MSEC(500));
    (void)ao_datalogger_deinit(&s_sampler_ao);
}

ZTEST_SUITE(telemetry_sampler_suite, NULL, telemetry_sampler_suite_setup,
            telemetry_sampler_suite_before, telemetry_sampler_suite_after, NULL);

ZTEST(telemetry_sampler_suite, test_sampler_init_validation)
{
    telemetry_sampler_t sampler;

    /* NULL checks */
    zassert_equal(telemetry_sampler_init(NULL, &s_sampler_ao, NULL, NULL, NULL, NULL, 600), -EINVAL);
    zassert_equal(telemetry_sampler_init(&sampler, NULL, NULL, NULL, NULL, NULL, 600), -EINVAL);

    /* Valid initialization with optional ports */
    zassert_equal(telemetry_sampler_init(&sampler, &s_sampler_ao, NULL, NULL, NULL, NULL, 0), 0);
    zassert_equal(sampler.sample_interval_s, TELEMETRY_SAMPLER_DEFAULT_INTERVAL_S);
}

ZTEST(telemetry_sampler_suite, test_sampler_sample_now_aggregation)
{
    telemetry_sampler_t sampler;
    i_thermal_energy_t thermal = { .api = &s_mock_thermal_api, .impl = (void *)1 };
    i_alarm_query_t alarms = { .api = &s_mock_alarm_api, .impl = (void *)1 };
    i_rtc_t rtc = fake_rtc_get_port();
    i_hvac_status_t hvac = { .api = &s_mock_hvac_api, .impl = (void *)1 };

    zassert_equal(telemetry_sampler_init(&sampler, &s_sampler_ao, &thermal, &alarms, &rtc, &hvac, 300), 0);

    datalogger_telemetry_record_t rec;
    zassert_equal(telemetry_sampler_sample_now(&sampler, &rec), 0);

    /* Verify aggregated fields */
    zassert_equal(rec.epoch_s, 1794474862);
    zassert_within(rec.t_ctrl_avg, 23.4f, 0.01f);
    zassert_within(rec.t_ctrl_max, 24.5f, 0.01f);
    zassert_within(rec.t_ctrl_min, 22.1f, 0.01f);
    zassert_within(rec.t_ctrl_g0, 24.2f, 0.01f);
    zassert_within(rec.t_ctrl_g1, 23.9f, 0.01f);
    zassert_within(rec.t_inj_mean, 14.5f, 0.01f);
    zassert_within(rec.t_ret_mean, 24.2f, 0.01f);
    zassert_within(rec.delta_t_clima, 9.7f, 0.01f);
    zassert_equal(rec.hvac_mode, GROUP_STATE_COOLING);
    zassert_equal(rec.active_unit_mask, 0x03);
    zassert_within(rec.v_a, 220.5f, 0.01f);
    zassert_within(rec.p_tot_kw, 8.250f, 0.01f);
    zassert_within(rec.cop_real, 3.65f, 0.01f);
    zassert_equal(rec.alarm_bits, 0x00000004);

    /* Verify event was posted to ao_datalogger queue */
    datalogger_event_envelope_t queued_evt;
    zassert_equal(k_msgq_get(&s_sampler_msgq, &queued_evt, K_NO_WAIT), 0);
    zassert_equal(queued_evt.type, DATALOGGER_EVT_TELEMETRY_SAMPLE);
    zassert_equal(queued_evt.payload.telemetry.epoch_s, 1794474862);
}

ZTEST(telemetry_sampler_suite, test_sampler_periodic_tick_cadence)
{
    telemetry_sampler_t sampler;
    i_rtc_t rtc = fake_rtc_get_port();
    zassert_equal(telemetry_sampler_init(&sampler, &s_sampler_ao, NULL, NULL, &rtc, NULL, 60), 0);

    /* First tick triggers immediate sample */
    zassert_equal(telemetry_sampler_tick(&sampler, 100), 1);

    /* Subsequent ticks before 60 seconds do not sample */
    zassert_equal(telemetry_sampler_tick(&sampler, 120), 0);
    zassert_equal(telemetry_sampler_tick(&sampler, 159), 0);

    /* Tick at 60s elapsed triggers sample */
    zassert_equal(telemetry_sampler_tick(&sampler, 160), 1);
}
