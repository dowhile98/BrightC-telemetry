/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-Memory Fake RTC Port Implementation.
 * Pure C99/C11 - Zero Zephyr headers, zero hardware registers.
 */

#include "fake_rtc.h"
#include "domain/time/rtc_time_calc.h"

#include <errno.h>
#include <string.h>

static fake_rtc_state_t s_fake_rtc;

static int fake_rtc_impl_init(void *impl)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->init_calls++;
    if (s->simulated_init_err != 0) {
        return s->simulated_init_err;
    }
    s->is_initialized = true;
    return 0;
}

static int fake_rtc_impl_get_time(void *impl, rtc_date_time_t *out_dt)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL || out_dt == NULL) {
        return -EINVAL;
    }
    s->get_time_calls++;
    if (s->simulated_get_time_err != 0) {
        return s->simulated_get_time_err;
    }
    *out_dt = s->current_dt;
    return 0;
}

static int fake_rtc_impl_set_time(void *impl, const rtc_date_time_t *dt)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL || dt == NULL) {
        return -EINVAL;
    }
    s->set_time_calls++;
    if (s->simulated_set_time_err != 0) {
        return s->simulated_set_time_err;
    }
    if (!rtc_calc_is_valid_datetime(dt)) {
        return -EINVAL;
    }

    s->current_dt = *dt;
    if (s->current_dt.day_of_week == 0) {
        s->current_dt.day_of_week = rtc_calc_day_of_week(dt->year, dt->month, dt->day);
    }
    (void)rtc_calc_datetime_to_epoch(&s->current_dt, &s->current_epoch_s);
    s->current_ms = s->current_dt.milliseconds;
    return 0;
}

static int fake_rtc_impl_get_epoch(void *impl, int64_t *out_epoch_s, uint32_t *out_ms)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL || out_epoch_s == NULL) {
        return -EINVAL;
    }
    s->get_epoch_calls++;
    if (s->simulated_get_epoch_err != 0) {
        return s->simulated_get_epoch_err;
    }
    *out_epoch_s = s->current_epoch_s;
    if (out_ms != NULL) {
        *out_ms = s->current_ms;
    }
    return 0;
}

static int fake_rtc_impl_set_epoch(void *impl, int64_t epoch_s, uint32_t ms)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL || epoch_s < 0 || ms > 999) {
        return -EINVAL;
    }
    s->set_epoch_calls++;
    if (s->simulated_set_epoch_err != 0) {
        return s->simulated_set_epoch_err;
    }

    s->current_epoch_s = epoch_s;
    s->current_ms = ms;
    int err = rtc_calc_epoch_to_datetime(epoch_s, (uint16_t)ms, &s->current_dt);
    if (err != 0) {
        return err;
    }
    return 0;
}

static int fake_rtc_impl_get_sync_status(void *impl, rtc_sync_status_t *out_status)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL || out_status == NULL) {
        return -EINVAL;
    }
    s->get_sync_status_calls++;
    if (s->simulated_get_sync_err != 0) {
        return s->simulated_get_sync_err;
    }
    *out_status = s->sync_status;
    return 0;
}

static int fake_rtc_impl_set_sync_status(void *impl, const rtc_sync_status_t *status)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL || status == NULL) {
        return -EINVAL;
    }
    s->set_sync_status_calls++;
    if (s->simulated_set_sync_err != 0) {
        return s->simulated_set_sync_err;
    }
    s->sync_status = *status;
    return 0;
}

static int fake_rtc_impl_set_calibration(void *impl, int32_t ppb_offset)
{
    fake_rtc_state_t *s = (fake_rtc_state_t *)impl;
    if (s == NULL) {
        return -EINVAL;
    }
    s->set_calibration_calls++;
    if (s->simulated_calib_err != 0) {
        return s->simulated_calib_err;
    }
    s->calibration_ppb = ppb_offset;
    return 0;
}

static const i_rtc_api_t s_fake_rtc_api = {
    .init            = fake_rtc_impl_init,
    .get_time        = fake_rtc_impl_get_time,
    .set_time        = fake_rtc_impl_set_time,
    .get_epoch       = fake_rtc_impl_get_epoch,
    .set_epoch       = fake_rtc_impl_set_epoch,
    .get_sync_status = fake_rtc_impl_get_sync_status,
    .set_sync_status = fake_rtc_impl_set_sync_status,
    .set_calibration = fake_rtc_impl_set_calibration,
};

void fake_rtc_reset(void)
{
    memset(&s_fake_rtc, 0, sizeof(s_fake_rtc));

    /* Default time: 2026-01-01 00:00:00.000 UTC */
    s_fake_rtc.current_dt.year = 2026;
    s_fake_rtc.current_dt.month = 1;
    s_fake_rtc.current_dt.day = 1;
    s_fake_rtc.current_dt.hours = 0;
    s_fake_rtc.current_dt.minutes = 0;
    s_fake_rtc.current_dt.seconds = 0;
    s_fake_rtc.current_dt.milliseconds = 0;
    s_fake_rtc.current_dt.day_of_week = 4; /* Thursday */

    (void)rtc_calc_datetime_to_epoch(&s_fake_rtc.current_dt, &s_fake_rtc.current_epoch_s);
    s_fake_rtc.current_ms = 0;

    s_fake_rtc.sync_status.sync_state = RTC_SYNC_STATE_BATTERY_RETAINED;
    s_fake_rtc.sync_status.last_sync_source = RTC_TIME_SOURCE_VBAT_BACKUP;
    s_fake_rtc.sync_status.last_sync_epoch_s = s_fake_rtc.current_epoch_s;
    s_fake_rtc.sync_status.last_sync_uptime_ms = 0;
    s_fake_rtc.sync_status.estimated_drift_ppb = 0;
    s_fake_rtc.sync_status.sync_count = 1;
    s_fake_rtc.sync_status.battery_ok = true;
    s_fake_rtc.sync_status.oscillator_running = true;

    s_fake_rtc.is_initialized = true;
}

i_rtc_t fake_rtc_get_port(void)
{
    i_rtc_t port;
    port.api = &s_fake_rtc_api;
    port.impl = &s_fake_rtc;
    return port;
}

const fake_rtc_state_t *fake_rtc_get_state(void)
{
    return &s_fake_rtc;
}

void fake_rtc_advance_time_ms(uint32_t delta_ms)
{
    uint64_t total_ms = (uint64_t)s_fake_rtc.current_ms + delta_ms;
    s_fake_rtc.current_epoch_s += (int64_t)(total_ms / 1000ULL);
    s_fake_rtc.current_ms = (uint32_t)(total_ms % 1000ULL);
    (void)rtc_calc_epoch_to_datetime(s_fake_rtc.current_epoch_s,
                                    (uint16_t)s_fake_rtc.current_ms,
                                    &s_fake_rtc.current_dt);
}

void fake_rtc_set_time_direct(const rtc_date_time_t *dt)
{
    if (dt != NULL) {
        s_fake_rtc.current_dt = *dt;
        (void)rtc_calc_datetime_to_epoch(dt, &s_fake_rtc.current_epoch_s);
        s_fake_rtc.current_ms = dt->milliseconds;
    }
}

void fake_rtc_set_epoch_direct(int64_t epoch_s, uint32_t ms)
{
    s_fake_rtc.current_epoch_s = epoch_s;
    s_fake_rtc.current_ms = ms;
    (void)rtc_calc_epoch_to_datetime(epoch_s, (uint16_t)ms, &s_fake_rtc.current_dt);
}

void fake_rtc_set_oscillator_fault(bool fault)
{
    s_fake_rtc.sync_status.oscillator_running = !fault;
    if (fault) {
        s_fake_rtc.sync_status.sync_state = RTC_SYNC_STATE_HARDWARE_FAULT;
    }
}

void fake_rtc_set_battery_ok(bool ok)
{
    s_fake_rtc.sync_status.battery_ok = ok;
}

void fake_rtc_set_errors(int init_err, int get_time_err, int set_time_err,
                         int get_epoch_err, int set_epoch_err)
{
    s_fake_rtc.simulated_init_err = init_err;
    s_fake_rtc.simulated_get_time_err = get_time_err;
    s_fake_rtc.simulated_set_time_err = set_time_err;
    s_fake_rtc.simulated_get_epoch_err = get_epoch_err;
    s_fake_rtc.simulated_set_epoch_err = set_epoch_err;
}
