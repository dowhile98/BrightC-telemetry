/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_ao_thermal_energy.c
 * @brief Unit tests for Active Object ao_thermal_energy, 720h circular buffer,
 *        and Zbus publication (Phase 5).
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>

#include "application/active_objects/ao_thermal_energy.h"
#include "application/zbus/thermal_channels.h"
#include "common/app_defaults.h"

static K_THREAD_STACK_DEFINE(s_test_ao_stack, AO_THERMAL_ENERGY_STACK_SIZE);
static ao_thermal_energy_t s_test_ao;
static config_thermal_site_t s_test_site_cfg;
static config_thermal_baseline_t s_test_baseline_cfg;

/* Dummy listener callbacks satisfying Zbus observer dependencies for unit tests */
static void dummy_zbus_cb(const struct zbus_channel *chan) { ARG_UNUSED(chan); }
ZBUS_LISTENER_DEFINE(lis_hvac_sensor_feed, dummy_zbus_cb);
ZBUS_LISTENER_DEFINE(lis_alarm_sensor_feed, dummy_zbus_cb);
ZBUS_LISTENER_DEFINE(lis_hvac_di_feed, dummy_zbus_cb);
ZBUS_LISTENER_DEFINE(lis_alarm_di_feed, dummy_zbus_cb);

static void ao_thermal_energy_suite_before(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&s_test_ao, 0, sizeof(s_test_ao));
    memcpy(&s_test_site_cfg, app_defaults_get_thermal_site_cfg(), sizeof(s_test_site_cfg));
    memcpy(&s_test_baseline_cfg, app_defaults_get_thermal_baseline_cfg(), sizeof(s_test_baseline_cfg));
}

static void ao_thermal_energy_suite_after(void *fixture)
{
    ARG_UNUSED(fixture);
    if (s_test_ao.is_initialized) {
        (void)ao_thermal_energy_stop(&s_test_ao);
        (void)ao_thermal_energy_wait_stopped(&s_test_ao, K_MSEC(500));
        (void)ao_thermal_energy_deinit(&s_test_ao);
    }
}

ZTEST_SUITE(ao_thermal_energy_suite, NULL, NULL,
            ao_thermal_energy_suite_before, ao_thermal_energy_suite_after, NULL);

/* ========================================================================= */
/* Test Case 1: Canonical 5-Phase Lifecycle                                  */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_canonical_5phase_lifecycle)
{
    /* Phase 1: Init */
    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0, "ao_thermal_energy_init must succeed");
    zassert_true(s_test_ao.is_initialized, "Must be marked initialized");
    zassert_false(s_test_ao.is_running, "Must not be running after init");

    /* Port must be bound */
    i_thermal_energy_t port = ao_thermal_energy_get_port(&s_test_ao);
    zassert_true(i_thermal_energy_is_bound(&port), "Port must be bound upon init");

    /* Duplicate start check without running */
    ret = ao_thermal_energy_stop(&s_test_ao);
    zassert_equal(ret, -EALREADY, "Stop when not running must return -EALREADY");

    /* Phase 2: Start */
    ret = ao_thermal_energy_start(&s_test_ao);
    zassert_equal(ret, 0, "ao_thermal_energy_start must succeed");
    zassert_true(s_test_ao.is_running, "Must be running after start");

    /* Duplicate start */
    ret = ao_thermal_energy_start(&s_test_ao);
    zassert_equal(ret, -EALREADY, "Duplicate start must return -EALREADY");

    /* Allow the worker thread to execute at least 1-2 cycles */
    k_sleep(K_MSEC(1200));

    /* Verify generations advance */
    thermal_energy_result_t res;
    ret = i_thermal_energy_get_latest_result(&port, &res);
    zassert_equal(ret, 0, "get_latest_result must succeed");
    zassert_true(res.generation_id > 0U, "Generation ID must advance (> 0)");

    /* Phase 3: Stop */
    ret = ao_thermal_energy_stop(&s_test_ao);
    zassert_equal(ret, 0, "ao_thermal_energy_stop must succeed");

    /* Phase 4: Wait Stopped */
    ret = ao_thermal_energy_wait_stopped(&s_test_ao, K_MSEC(AO_THERMAL_ENERGY_STOP_WAIT_MS));
    zassert_equal(ret, 0, "wait_stopped must return 0");
    zassert_false(s_test_ao.is_running, "Must not be running after wait_stopped");

    /* Phase 5: Deinit */
    ret = ao_thermal_energy_deinit(&s_test_ao);
    zassert_equal(ret, 0, "ao_thermal_energy_deinit must succeed");
    zassert_false(s_test_ao.is_initialized, "Must be uninitialized after deinit");
}

/* ========================================================================= */
/* Test Case 2: Atomic Queries via i_thermal_energy Port                     */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_port_atomic_queries)
{
    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    i_thermal_energy_t port = ao_thermal_energy_get_port(&s_test_ao);

    /* Ingest test electrical reading & feed sensor snapshot */
    energy_reading_t elec = {0};
    elec.total_active_power_kw = 12.0f; /* 12 kW */
    elec.is_valid = true;
    elec.timestamp_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());
    ao_thermal_energy_feed_energy_reading(&s_test_ao, &elec);

    sensor_snapshot_msg_t s_snap = {0};
    /* Pilot config uses pair 0: supply 10, ret 11 */
    s_snap.temperatures[9] = 14.0f;  /* supply flat ID 10 */
    s_snap.temperatures[10] = 24.0f; /* return flat ID 11 */
    s_snap.temperatures[49] = 12.0f; /* 4-20mA humidity ID 50: 12mA -> 50% RH */
    s_snap.valid_mask = (1ULL << 9) | (1ULL << 10) | (1ULL << 49);
    s_snap.timestamp_ms = (uint64_t)k_uptime_get();
    ao_thermal_energy_feed_sensor_snapshot(&s_test_ao, &s_snap);

    /* Step calculation manually */
    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0, "ao_thermal_energy_step must succeed");

    /* Query latest result */
    thermal_energy_result_t res;
    ret = i_thermal_energy_get_latest_result(&port, &res);
    zassert_equal(ret, 0);
    zassert_true(res.generation_id > 0U);
    zassert_within(res.delta_t_pair[0], 10.0f, 0.1f, "Delta T must be ~10 C");

    /* Query latest capture */
    thermal_energy_capture_t cap;
    ret = i_thermal_energy_get_latest_capture(&port, &cap);
    zassert_equal(ret, 0);
    zassert_within(cap.t_inj[0], 14.0f, 0.01f);
    zassert_within(cap.t_ret[0], 24.0f, 0.01f);
    zassert_within(cap.total_active_power_kw, 12.0f, 0.01f);

    /* Query baseline */
    config_thermal_baseline_t base;
    ret = i_thermal_energy_get_baseline(&port, &base);
    zassert_equal(ret, 0);

    /* Recalibration trigger */
    ret = i_thermal_energy_trigger_baseline_recalibration(&port);
    zassert_equal(ret, 0);
    ret = i_thermal_energy_get_baseline(&port, &base);
    zassert_equal(ret, 0);
    zassert_equal(base.is_valid, 0U, "Baseline is_valid must be 0 after recalibration");
    zassert_equal(base.training_hours_accumulated, 0U, "Training hours must be 0 after recalibration");
}

/* ========================================================================= */
/* Test Case 3: Circular Buffer 720h RAM History                             */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_circular_buffer_720h)
{
    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    i_thermal_energy_t port = ao_thermal_energy_get_port(&s_test_ao);

    /* When empty, get_hourly_history must return -ENOENT */
    thermal_hourly_record_t rec;
    ret = i_thermal_energy_get_hourly_history(&port, 0, &rec);
    zassert_equal(ret, -ENOENT, "Empty history must return -ENOENT");

    /* Perform a step and force hourly compression for 5 consecutive hours */
    for (uint32_t h = 0; h < 5; h++) {
        (void)ao_thermal_energy_step(&s_test_ao);
        ret = ao_thermal_energy_record_hour(&s_test_ao);
        zassert_equal(ret, 0, "Hourly record must succeed");
    }

    zassert_equal(s_test_ao.history_count, 5U, "History count must be 5");

    /* hours_ago = 0 (most recent closed hour) */
    ret = i_thermal_energy_get_hourly_history(&port, 0, &rec);
    zassert_equal(ret, 0);
    zassert_true(rec.crc16 != 0U, "CRC16 must be populated");

    /* hours_ago = 4 (oldest of the 5) */
    ret = i_thermal_energy_get_hourly_history(&port, 4, &rec);
    zassert_equal(ret, 0);

    /* hours_ago = 5 (beyond count) -> -ENOENT */
    ret = i_thermal_energy_get_hourly_history(&port, 5, &rec);
    zassert_equal(ret, -ENOENT, "Offset beyond count must return -ENOENT");

    /* Test 720 wrap-around */
    for (uint32_t h = 5; h < 730; h++) {
        (void)ao_thermal_energy_record_hour(&s_test_ao);
    }
    zassert_equal(s_test_ao.history_count, THERMAL_HOURLY_HISTORY_CAPACITY,
                  "History count must be clamped at 720");

    /* Can query up to 719 hours ago */
    ret = i_thermal_energy_get_hourly_history(&port, 719, &rec);
    zassert_equal(ret, 0, "Offset 719 must succeed");

    /* Offset 720 -> -ENOENT */
    ret = i_thermal_energy_get_hourly_history(&port, 720, &rec);
    zassert_equal(ret, -ENOENT, "Offset 720 must return -ENOENT");
}

/* ========================================================================= */
/* Test Case 4: Zbus Event Notification                                      */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_zbus_event_publication)
{
    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    /* Perform a step */
    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    /* Read Zbus channel directly */
    thermal_energy_event_t evt = {0};
    ret = zbus_chan_read(&chan_thermal_energy_update, &evt, K_MSEC(100));
    zassert_equal(ret, 0, "zbus_chan_read must succeed");
    zassert_true(evt.generation_id > 0U, "Generation ID in event must be > 0");
    zassert_true(evt.timestamp_ms > 0ULL, "Timestamp in event must be > 0");
}

/* ========================================================================= */
/* Test Case 5: Mode Switching Propagates to Capture Mapping                 */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_mode_switching_propagates_to_capture)
{
    /* Initialize in TELEMETRY_ONLY mode */
    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    /* In TELEMETRY_ONLY mode, pin 0 of DI snapshot maps to Compressor 0 */
    di_snapshot_msg_t di_snap = {0};
    di_snap.active_mask = (1ULL << 0); /* Channel 0 active */
    di_snap.timestamp_ms = (uint64_t)k_uptime_get();
    ret = ao_thermal_energy_feed_di_snapshot(&s_test_ao, &di_snap);
    zassert_equal(ret, 0);

    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    /* Compressor 0 state mask must be set (bit 0 = 1) */
    zassert_true((s_test_ao.latest_capture.comp_state_mask & (1U << 0)) != 0U,
                 "In TELEMETRY_ONLY, pin 0 must map to compressor 0");

    /* Switch mode to HYBRID_FULL at runtime */
    ao_thermal_energy_set_hvac_mode(&s_test_ao, HVAC_MODE_HYBRID_FULL);

    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    /* In HYBRID_FULL mode, local pin 0 is an alarm cutout, NOT compressor feedback.
     * Therefore comp_state_mask bit 0 must be 0! */
    zassert_false((s_test_ao.latest_capture.comp_state_mask & (1U << 0)) != 0U,
                  "In HYBRID_FULL, local pin 0 must not map to compressor 0");
}

/* ========================================================================= */
/* Test Case 6: Filter Slope Calculation from 24h History                    */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_filter_slope_from_24h_history)
{
    /* Initialize with baseline trained (training_hours >= 168) */
    s_test_baseline_cfg.training_hours_accumulated = 168U;
    s_test_baseline_cfg.is_valid = 1U;
    s_test_baseline_cfg.baseline_pair_t_inj[0] = 14.0f;
    s_test_baseline_cfg.baseline_pair_delta_t[0] = 10.0f;

    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    /* Populate 25 hours of history where 24h ago t_inj was 14.0 C */
    for (uint32_t h = 0; h < 25; h++) {
        thermal_hourly_record_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.t_inj_x10[0] = 140; /* 14.0 C */
        rec.t_ret_x10[0] = 240; /* 24.0 C */
        s_test_ao.history[s_test_ao.history_head] = rec;
        s_test_ao.history_head = (s_test_ao.history_head + 1U) % THERMAL_HOURLY_HISTORY_CAPACITY;
        if (s_test_ao.history_count < THERMAL_HOURLY_HISTORY_CAPACITY) {
            s_test_ao.history_count++;
        }
    }

    /* Provide a sensor snapshot where current t_inj = 12.0 C.
     * Slope = 12.0 - 14.0 = -2.0 C / day (active degradation).
     * Supply temp = 12.0 C, delta T = 24.0 - 12.0 = 12.0 C.
     * Filter restriction pct > 15%, so active degradation applies:
     * temp_diff = 12.0 - 7.5 = 4.5.
     * days = 4.5 / 2.0 = 2.25 -> ~2 days. */
    sensor_snapshot_msg_t snap = {0};
    snap.temperatures[9] = 12.0f;  /* pair 0 supply */
    snap.temperatures[10] = 24.0f; /* pair 0 return */
    snap.temperatures[49] = 12.0f; /* humidity */
    snap.valid_mask = (1ULL << 9) | (1ULL << 10) | (1ULL << 49);
    snap.timestamp_ms = (uint64_t)k_uptime_get();
    ret = ao_thermal_energy_feed_sensor_snapshot(&s_test_ao, &snap);
    zassert_equal(ret, 0);

    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    /* Verify days remaining reflects active slope degradation, not default 90 days */
    zassert_true(s_test_ao.latest_result.filter_days_remaining[0] < 90U,
                 "Days remaining (%u) must be < 90 when negative degradation slope is detected",
                 (unsigned int)s_test_ao.latest_result.filter_days_remaining[0]);
    zassert_within(s_test_ao.latest_result.filter_days_remaining[0], 2, 1,
                   "Days remaining should be ~2 days");
}

/* ========================================================================= */
/* Test Case 7: Stale Telemetry Degradation                                  */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_stale_telemetry_degradation)
{
    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    /* 1. Feed initial fresh readings */
    uint64_t now = (uint64_t)k_uptime_get();
    sensor_snapshot_msg_t snap = {0};
    snap.temperatures[9] = 14.0f;
    snap.temperatures[10] = 24.0f;
    snap.temperatures[49] = 12.0f;
    snap.valid_mask = (1ULL << 9) | (1ULL << 10) | (1ULL << 49);
    snap.timestamp_ms = now;
    ao_thermal_energy_feed_sensor_snapshot(&s_test_ao, &snap);

    energy_reading_t elec = {0};
    elec.total_active_power_kw = 10.0f;
    elec.is_valid = true;
    elec.timestamp_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());
    ao_thermal_energy_feed_energy_reading(&s_test_ao, &elec);

    /* Step: verify fresh telemetry is VALID */
    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);
    zassert_equal(s_test_ao.latest_result.thermal_quality, QUALITY_VALID);
    zassert_equal(s_test_ao.latest_result.cop_quality, QUALITY_VALID);
    zassert_equal(s_test_ao.latest_result.overall_quality, QUALITY_VALID);

    /* 2. Advance simulated time by 2100 ms without updating sensor snapshot */
    k_sleep(K_MSEC(2100));

    /* Refresh electrical reading so only temperature is stale */
    elec.timestamp_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());
    ao_thermal_energy_feed_energy_reading(&s_test_ao, &elec);

    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    /* Must degrade thermal_quality and overall_quality to QUALITY_SUSPECT */
    zassert_equal(s_test_ao.latest_result.thermal_quality, QUALITY_SUSPECT,
                  "Thermal quality must degrade to SUSPECT when sensor snapshot is > 2000 ms old");
    zassert_equal(s_test_ao.latest_result.overall_quality, QUALITY_SUSPECT,
                  "Overall quality must be SUSPECT when thermal telemetry is stale");

    /* 3. Refresh sensor snapshot, but advance time by 3100 ms without updating electrical */
    snap.timestamp_ms = (uint64_t)k_uptime_get();
    ao_thermal_energy_feed_sensor_snapshot(&s_test_ao, &snap);

    k_sleep(K_MSEC(3100));

    /* Keep sensor snapshot fresh */
    snap.timestamp_ms = (uint64_t)k_uptime_get();
    ao_thermal_energy_feed_sensor_snapshot(&s_test_ao, &snap);

    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    zassert_equal(s_test_ao.latest_result.thermal_quality, QUALITY_VALID,
                  "Thermal quality must be VALID when sensor snapshot is fresh");
    zassert_equal(s_test_ao.latest_result.cop_quality, QUALITY_SUSPECT,
                  "COP quality must degrade to SUSPECT when electrical reading is > 3000 ms old");
    zassert_equal(s_test_ao.latest_result.overall_quality, QUALITY_SUSPECT,
                  "Overall quality must be SUSPECT when COP is suspect");
}

/* ========================================================================= */
/* Test Case 8: Integrated Hourly Accumulators in OLS                        */
/* ========================================================================= */

ZTEST(ao_thermal_energy_suite, test_integrated_hourly_averages_in_ols)
{
    int ret = ao_thermal_energy_init(&s_test_ao,
                                     &s_test_site_cfg,
                                     &s_test_baseline_cfg,
                                     HVAC_MODE_TELEMETRY_ONLY,
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    /* Compressor 0 running */
    di_snapshot_msg_t di_snap = {0};
    di_snap.active_mask = (1ULL << 0);
    di_snap.timestamp_ms = (uint64_t)k_uptime_get();
    ao_thermal_energy_feed_di_snapshot(&s_test_ao, &di_snap);

    energy_reading_t elec = {0};
    elec.total_active_power_kw = 10.0f;
    elec.is_valid = true;
    elec.timestamp_us = (uint64_t)k_ticks_to_us_near64(k_uptime_ticks());
    ao_thermal_energy_feed_energy_reading(&s_test_ao, &elec);

    /* Step 1: Return temp 24 C, outdoor 30 C (outdoor probe table: 22, 23 -> flat IDs 22, 23 -> indices 21, 22) */
    sensor_snapshot_msg_t snap1 = {0};
    snap1.temperatures[9] = 14.0f;
    snap1.temperatures[10] = 24.0f;
    snap1.temperatures[21] = 30.0f;
    snap1.temperatures[22] = 30.0f;
    snap1.temperatures[49] = 12.0f;
    snap1.valid_mask = (1ULL << 9) | (1ULL << 10) | (1ULL << 21) | (1ULL << 22) | (1ULL << 49);
    snap1.timestamp_ms = (uint64_t)k_uptime_get();
    ao_thermal_energy_feed_sensor_snapshot(&s_test_ao, &snap1);
    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    /* Step 2: Return temp 26 C, outdoor 34 C */
    sensor_snapshot_msg_t snap2 = {0};
    snap2.temperatures[9] = 14.0f;
    snap2.temperatures[10] = 26.0f;
    snap2.temperatures[21] = 34.0f;
    snap2.temperatures[22] = 34.0f;
    snap2.temperatures[49] = 12.0f;
    snap2.valid_mask = (1ULL << 9) | (1ULL << 10) | (1ULL << 21) | (1ULL << 22) | (1ULL << 49);
    snap2.timestamp_ms = (uint64_t)k_uptime_get();
    ao_thermal_energy_feed_sensor_snapshot(&s_test_ao, &snap2);
    ret = ao_thermal_energy_step(&s_test_ao);
    zassert_equal(ret, 0);

    /* Accumulated 2 samples */
    zassert_equal(s_test_ao.hourly_samples_count, 2U, "Must have accumulated 2 samples");
    zassert_within((float)(s_test_ao.hourly_accum_t_ret / 2.0), 25.0f, 0.1f, "Integrated t_ret must be 25 C");
    zassert_within((float)(s_test_ao.hourly_accum_t_ext / 2.0), 32.0f, 0.1f, "Integrated t_ext must be 32 C");

    /* Force steady-state on compressor 0 to allow OLS accumulation */
    s_test_ao.comp_fsm[0].state = COMPRESSOR_STATE_STEADY_STATE;

    /* Call record hour */
    ret = ao_thermal_energy_record_hour(&s_test_ao);
    zassert_equal(ret, 0);

    /* Hourly accumulators must have been reset to zero for next hour */
    zassert_equal(s_test_ao.hourly_samples_count, 0U, "Hourly sample count must reset to 0");
    zassert_equal((uint32_t)s_test_ao.hourly_accum_t_ret, 0U, "Hourly accumulator must reset to 0");
    zassert_equal((uint32_t)s_test_ao.hourly_accum_t_ext, 0U, "Hourly accumulator must reset to 0");

    /* OLS must have accumulated 1 training hour */
    zassert_equal(s_test_ao.ols_model.training_hours, 1U, "OLS model must have accumulated 1 training hour");
}
