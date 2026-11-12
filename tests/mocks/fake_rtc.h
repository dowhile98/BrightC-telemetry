/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-Memory Fake RTC Port Implementation for Host & Unit Testing.
 * Zero Zephyr headers, zero hardware registers. Pure C99/C11.
 */

#ifndef TESTS_MOCKS_FAKE_RTC_H_
#define TESTS_MOCKS_FAKE_RTC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/rtc_types.h"
#include "interfaces/i_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Call invocation counters */
    uint32_t init_calls;
    uint32_t get_time_calls;
    uint32_t set_time_calls;
    uint32_t get_epoch_calls;
    uint32_t set_epoch_calls;
    uint32_t get_sync_status_calls;
    uint32_t set_sync_status_calls;
    uint32_t set_calibration_calls;

    /* Error injection */
    int simulated_init_err;
    int simulated_get_time_err;
    int simulated_set_time_err;
    int simulated_get_epoch_err;
    int simulated_set_epoch_err;
    int simulated_get_sync_err;
    int simulated_set_sync_err;
    int simulated_calib_err;

    /* Current simulated time and status */
    rtc_date_time_t   current_dt;
    int64_t           current_epoch_s;
    uint32_t          current_ms;
    rtc_sync_status_t sync_status;
    int32_t           calibration_ppb;
    bool              is_initialized;
} fake_rtc_state_t;

/**
 * @brief Reset the fake RTC state to default baseline (2026-01-01 00:00:00 UTC).
 */
void fake_rtc_reset(void);

/**
 * @brief Obtain an i_rtc_t port handle bound to the fake RTC implementation.
 */
i_rtc_t fake_rtc_get_port(void);

/**
 * @brief Direct inspection of internal fake RTC state.
 */
const fake_rtc_state_t *fake_rtc_get_state(void);

/**
 * @brief Advance fake RTC simulated time forward by specified milliseconds.
 */
void fake_rtc_advance_time_ms(uint32_t delta_ms);

/**
 * @brief Direct test fixture setup: set current calendar time without port API.
 */
void fake_rtc_set_time_direct(const rtc_date_time_t *dt);

/**
 * @brief Direct test fixture setup: set current epoch time without port API.
 */
void fake_rtc_set_epoch_direct(int64_t epoch_s, uint32_t ms);

/**
 * @brief Configure oscillator health flag in sync status.
 */
void fake_rtc_set_oscillator_fault(bool fault);

/**
 * @brief Configure battery health flag in sync status.
 */
void fake_rtc_set_battery_ok(bool ok);

/**
 * @brief Inject simulated error return codes into fake RTC operations.
 */
void fake_rtc_set_errors(int init_err, int get_time_err, int set_time_err,
                         int get_epoch_err, int set_epoch_err);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_MOCKS_FAKE_RTC_H_ */
