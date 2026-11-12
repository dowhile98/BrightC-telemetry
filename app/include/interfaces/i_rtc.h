/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract Port Interface for Hardware Real-Time Clock & System Time.
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-Table (i_rtc_api_t)
 * 2) Handle (i_rtc_t)
 * 3) Validated Wrappers (static inline i_rtc_*())
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_RTC_H_
#define APP_INTERFACES_I_RTC_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/rtc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE CONTRACT (Implementation details, never called directly)
 *============================================================================*/

typedef struct {
    /** Initialize hardware peripheral, verify oscillator and backup domain */
    int (*init)(void *impl);

    /** Read current calendar date, time, and hardware subsecond milliseconds */
    int (*get_time)(void *impl, rtc_date_time_t *out_dt);

    /** Write calendar date and time to hardware registers */
    int (*set_time)(void *impl, const rtc_date_time_t *dt);

    /** Fast query: returns UTC epoch seconds and fractional milliseconds */
    int (*get_epoch)(void *impl, int64_t *out_epoch_s, uint32_t *out_ms);

    /** Fast set: commits UTC epoch seconds and fractional milliseconds */
    int (*set_epoch)(void *impl, int64_t epoch_s, uint32_t ms);

    /** Query hardware oscillator health and synchronization metadata */
    int (*get_sync_status)(void *impl, rtc_sync_status_t *out_status);

    /** Update persistent sync status in backup registers */
    int (*set_sync_status)(void *impl, const rtc_sync_status_t *status);

    /** Apply digital smooth calibration offset (-511..+512 ppm trimming) */
    int (*set_calibration)(void *impl, int32_t ppb_offset);
} i_rtc_api_t;

/*============================================================================*
 * 2. HANDLE (Fat pointer passed to consumers)
 *============================================================================*/

typedef struct {
    const i_rtc_api_t *api;
    void *impl;
} i_rtc_t;

/*============================================================================*
 * 3. VALIDATED WRAPPERS (Mandatory per doc/STRUCTURE.md §4.1.D)
 *============================================================================*/

/**
 * @brief Check whether an i_rtc_t handle is bound to a valid implementation.
 *
 * @param rtc RTC instance handle.
 * @return true if valid, false otherwise.
 */
static inline bool i_rtc_is_bound(const i_rtc_t *rtc)
{
    return (rtc != NULL && rtc->api != NULL && rtc->impl != NULL);
}

/**
 * @brief Initialize the hardware RTC peripheral and verify oscillator health.
 *
 * @param rtc RTC instance handle.
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_init(const i_rtc_t *rtc)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->init == NULL) {
        return -EINVAL;
    }
    return rtc->api->init(rtc->impl);
}

/**
 * @brief Read current calendar date and time from the RTC.
 *
 * @param rtc     RTC instance handle.
 * @param out_dt  Pointer to date/time structure to populate.
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_get_time(const i_rtc_t *rtc, rtc_date_time_t *out_dt)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->get_time == NULL || out_dt == NULL) {
        return -EINVAL;
    }
    return rtc->api->get_time(rtc->impl, out_dt);
}

/**
 * @brief Set calendar date and time in the RTC registers.
 *
 * @param rtc RTC instance handle.
 * @param dt  Pointer to date/time structure containing new time.
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_set_time(const i_rtc_t *rtc, const rtc_date_time_t *dt)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->set_time == NULL || dt == NULL) {
        return -EINVAL;
    }
    return rtc->api->set_time(rtc->impl, dt);
}

/**
 * @brief Obtain current UTC Unix epoch seconds and subsecond milliseconds.
 *
 * @param rtc          RTC instance handle.
 * @param out_epoch_s  Pointer to receive UTC epoch seconds.
 * @param out_ms       Optional pointer to receive subsecond milliseconds (0..999, may be NULL).
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_get_epoch(const i_rtc_t *rtc, int64_t *out_epoch_s, uint32_t *out_ms)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->get_epoch == NULL || out_epoch_s == NULL) {
        return -EINVAL;
    }
    return rtc->api->get_epoch(rtc->impl, out_epoch_s, out_ms);
}

/**
 * @brief Set current RTC time from UTC Unix epoch seconds and subsecond milliseconds.
 *
 * @param rtc      RTC instance handle.
 * @param epoch_s  UTC epoch seconds.
 * @param ms       Subsecond milliseconds (0..999).
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_set_epoch(const i_rtc_t *rtc, int64_t epoch_s, uint32_t ms)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->set_epoch == NULL) {
        return -EINVAL;
    }
    return rtc->api->set_epoch(rtc->impl, epoch_s, ms);
}

/**
 * @brief Query current synchronization state and drift metadata.
 *
 * @param rtc         RTC instance handle.
 * @param out_status  Pointer to status structure to populate.
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_get_sync_status(const i_rtc_t *rtc, rtc_sync_status_t *out_status)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->get_sync_status == NULL || out_status == NULL) {
        return -EINVAL;
    }
    return rtc->api->get_sync_status(rtc->impl, out_status);
}

/**
 * @brief Record persistent synchronization metadata in hardware backup registers.
 *
 * @param rtc     RTC instance handle.
 * @param status  Pointer to updated status metadata.
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_set_sync_status(const i_rtc_t *rtc, const rtc_sync_status_t *status)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->set_sync_status == NULL || status == NULL) {
        return -EINVAL;
    }
    return rtc->api->set_sync_status(rtc->impl, status);
}

/**
 * @brief Configure hardware smooth digital calibration offset.
 *
 * @param rtc         RTC instance handle.
 * @param ppb_offset  Calibration adjustment in parts-per-billion (-487000..+488000 ppb).
 * @return 0 on success, negative POSIX error code on failure.
 */
static inline int i_rtc_set_calibration(const i_rtc_t *rtc, int32_t ppb_offset)
{
    if (!i_rtc_is_bound(rtc) || rtc->api->set_calibration == NULL) {
        return -EINVAL;
    }
    return rtc->api->set_calibration(rtc->impl, ppb_offset);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_RTC_H_ */
