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
                                     s_test_ao_stack,
                                     K_THREAD_STACK_SIZEOF(s_test_ao_stack),
                                     AO_THERMAL_ENERGY_PRIORITY);
    zassert_equal(ret, 0);

    i_thermal_energy_t port = ao_thermal_energy_get_port(&s_test_ao);

    /* Ingest test electrical reading & feed sensor snapshot */
    energy_reading_t elec = {0};
    elec.total_active_power_kw = 12.0f; /* 12 kW */
    elec.is_valid = true;
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
