/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <errno.h>

#include "interfaces/i_rtc.h"
#include "fake_rtc.h"

static void fake_rtc_test_before(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_rtc_reset();
}

ZTEST_SUITE(fake_rtc_suite, NULL, NULL, fake_rtc_test_before, NULL, NULL);

/* =========================================================================
 * 1. Interface Binding and Port Contract
 * ========================================================================= */
ZTEST(fake_rtc_suite, test_port_binding_and_wrappers)
{
    i_rtc_t port = fake_rtc_get_port();
    zassert_true(i_rtc_is_bound(&port), "Port must be bound");

    /* Null checks on wrappers */
    rtc_date_time_t dt;
    int64_t epoch_s;
    uint32_t ms;
    rtc_sync_status_t status;

    zassert_equal(i_rtc_init(NULL), -EINVAL);
    zassert_equal(i_rtc_get_time(NULL, &dt), -EINVAL);
    zassert_equal(i_rtc_get_time(&port, NULL), -EINVAL);
    zassert_equal(i_rtc_set_time(NULL, &dt), -EINVAL);
    zassert_equal(i_rtc_set_time(&port, NULL), -EINVAL);
    zassert_equal(i_rtc_get_epoch(NULL, &epoch_s, &ms), -EINVAL);
    zassert_equal(i_rtc_get_epoch(&port, NULL, &ms), -EINVAL);
    zassert_equal(i_rtc_set_epoch(NULL, 100, 0), -EINVAL);
    zassert_equal(i_rtc_get_sync_status(NULL, &status), -EINVAL);
    zassert_equal(i_rtc_get_sync_status(&port, NULL), -EINVAL);
    zassert_equal(i_rtc_set_sync_status(NULL, &status), -EINVAL);
    zassert_equal(i_rtc_set_sync_status(&port, NULL), -EINVAL);
    zassert_equal(i_rtc_set_calibration(NULL, 100), -EINVAL);
}

/* =========================================================================
 * 2. Set and Get Time Operations
 * ========================================================================= */
ZTEST(fake_rtc_suite, test_set_and_get_time)
{
    i_rtc_t port = fake_rtc_get_port();
    rtc_date_time_t dt_set;
    rtc_date_time_t dt_get;

    dt_set.year = 2026;
    dt_set.month = 11;
    dt_set.day = 11;
    dt_set.hours = 18;
    dt_set.minutes = 45;
    dt_set.seconds = 12;
    dt_set.milliseconds = 500;
    dt_set.day_of_week = 3;

    zassert_equal(i_rtc_set_time(&port, &dt_set), 0, "Set time failed");
    zassert_equal(i_rtc_get_time(&port, &dt_get), 0, "Get time failed");

    zassert_equal(dt_get.year, 2026);
    zassert_equal(dt_get.month, 11);
    zassert_equal(dt_get.day, 11);
    zassert_equal(dt_get.hours, 18);
    zassert_equal(dt_get.minutes, 45);
    zassert_equal(dt_get.seconds, 12);
    zassert_equal(dt_get.milliseconds, 500);
    zassert_equal(dt_get.day_of_week, 3);

    /* Setting invalid time must be rejected */
    dt_set.month = 13;
    zassert_equal(i_rtc_set_time(&port, &dt_set), -EINVAL, "Invalid date must be rejected");
}

/* =========================================================================
 * 3. Set and Get Epoch Operations
 * ========================================================================= */
ZTEST(fake_rtc_suite, test_set_and_get_epoch)
{
    i_rtc_t port = fake_rtc_get_port();
    int64_t epoch_s = 1794422712LL; /* 2026-11-11 18:45:12 UTC */
    uint32_t ms = 382;

    zassert_equal(i_rtc_set_epoch(&port, epoch_s, ms), 0, "Set epoch failed");

    int64_t read_epoch = 0;
    uint32_t read_ms = 0;
    zassert_equal(i_rtc_get_epoch(&port, &read_epoch, &read_ms), 0, "Get epoch failed");

    zassert_equal(read_epoch, epoch_s);
    zassert_equal(read_ms, ms);

    /* Verify broken-down calendar time was updated correctly */
    rtc_date_time_t dt_get;
    zassert_equal(i_rtc_get_time(&port, &dt_get), 0);
    zassert_equal(dt_get.year, 2026);
    zassert_equal(dt_get.month, 11);
    zassert_equal(dt_get.day, 11);
    zassert_equal(dt_get.hours, 18);
    zassert_equal(dt_get.minutes, 45);
    zassert_equal(dt_get.seconds, 12);
    zassert_equal(dt_get.milliseconds, 382);
}

/* =========================================================================
 * 4. Advancing Time & Drift/Status Simulation
 * ========================================================================= */
ZTEST(fake_rtc_suite, test_advance_time_and_diagnostics)
{
    i_rtc_t port = fake_rtc_get_port();

    /* Set baseline */
    zassert_equal(i_rtc_set_epoch(&port, 1000LL, 500), 0);

    /* Advance by 1500 ms -> should be epoch 1002, 0 ms */
    fake_rtc_advance_time_ms(1500);

    int64_t epoch_s = 0;
    uint32_t ms = 0;
    zassert_equal(i_rtc_get_epoch(&port, &epoch_s, &ms), 0);
    zassert_equal(epoch_s, 1002LL);
    zassert_equal(ms, 0);

    /* Query sync status */
    rtc_sync_status_t status;
    zassert_equal(i_rtc_get_sync_status(&port, &status), 0);
    zassert_true(status.oscillator_running);
    zassert_true(status.battery_ok);

    /* Simulate oscillator fault */
    fake_rtc_set_oscillator_fault(true);
    zassert_equal(i_rtc_get_sync_status(&port, &status), 0);
    zassert_false(status.oscillator_running);
    zassert_equal(status.sync_state, RTC_SYNC_STATE_HARDWARE_FAULT);

    /* Calibration trimming */
    zassert_equal(i_rtc_set_calibration(&port, -1250), 0);
    const fake_rtc_state_t *st = fake_rtc_get_state();
    zassert_equal(st->calibration_ppb, -1250);
}

/* =========================================================================
 * 5. Error Injection
 * ========================================================================= */
ZTEST(fake_rtc_suite, test_error_injection)
{
    i_rtc_t port = fake_rtc_get_port();

    fake_rtc_set_errors(-EIO, -ENODEV, -EACCES, -EBUSY, -EPERM);

    zassert_equal(i_rtc_init(&port), -EIO);

    rtc_date_time_t dt;
    zassert_equal(i_rtc_get_time(&port, &dt), -ENODEV);
    zassert_equal(i_rtc_set_time(&port, &dt), -EACCES);

    int64_t epoch_s;
    uint32_t ms;
    zassert_equal(i_rtc_get_epoch(&port, &epoch_s, &ms), -EBUSY);
    zassert_equal(i_rtc_set_epoch(&port, 0, 0), -EPERM);
}
