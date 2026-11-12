/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain Date-Time Calculation Utilities for RTC and System Time Subsystem.
 * Pure C99/C11 - Zero Zephyr headers, zero hardware registers.
 */

#ifndef APP_DOMAIN_TIME_RTC_TIME_CALC_H_
#define APP_DOMAIN_TIME_RTC_TIME_CALC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/rtc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Determine whether a given year is a Gregorian leap year.
 *
 * Follows the standard leap year rule: divisible by 4, not divisible by 100,
 * unless divisible by 400.
 *
 * @param[in] year 4-digit calendar year (e.g. 2026).
 * @return true if leap year, false otherwise.
 */
bool rtc_calc_is_leap_year(uint16_t year);

/**
 * @brief Validate that fields in an rtc_date_time_t structure fall within valid ranges.
 *
 * Verifies month (1..12), day matching the month and leap year rules,
 * hours (0..23), minutes (0..59), seconds (0..59), and milliseconds (0..999).
 * Also verifies year is within realistic range (1970..2099).
 *
 * @param[in] dt Pointer to date/time structure.
 * @return true if valid, false if NULL or invalid.
 */
bool rtc_calc_is_valid_datetime(const rtc_date_time_t *dt);

/**
 * @brief Compute the day of the week (1=Monday .. 7=Sunday per ISO-8601).
 *
 * @param[in] year  Calendar year (e.g. 2026).
 * @param[in] month Calendar month (1..12).
 * @param[in] day   Calendar day (1..31).
 * @return Day of the week (1 = Monday, 7 = Sunday), or 0 on invalid input.
 */
uint8_t rtc_calc_day_of_week(uint16_t year, uint8_t month, uint8_t day);

/**
 * @brief Convert broken-down calendar date/time to UTC Unix epoch seconds.
 *
 * Uses 64-bit signed integer for epoch to guarantee safety past Y2038.
 *
 * @param[in]  dt          Pointer to valid calendar date/time.
 * @param[out] out_epoch_s Pointer to store computed UTC epoch seconds.
 * @return 0 on success, -EINVAL on invalid date or NULL pointer.
 */
int rtc_calc_datetime_to_epoch(const rtc_date_time_t *dt, int64_t *out_epoch_s);

/**
 * @brief Convert UTC Unix epoch seconds and subsecond milliseconds into broken-down calendar date/time.
 *
 * Computes calendar year, month, day, hours, minutes, seconds, milliseconds,
 * and ISO-8601 day of the week (1=Monday .. 7=Sunday).
 *
 * @param[in]  epoch_s       UTC epoch seconds (non-negative, >= 0).
 * @param[in]  subsecond_ms  Fractional milliseconds within second (0..999).
 * @param[out] out_dt        Pointer to destination rtc_date_time_t structure.
 * @return 0 on success, -EINVAL on invalid inputs or NULL pointer.
 */
int rtc_calc_epoch_to_datetime(int64_t epoch_s, uint16_t subsecond_ms, rtc_date_time_t *out_dt);

/**
 * @brief Format a calendar date/time into a standard ISO-8601 UTC string.
 *
 * Produces format: "YYYY-MM-DDTHH:MM:SS.mmmZ" (24 characters + NUL terminator = 25 bytes).
 *
 * @param[in]  dt      Pointer to valid calendar date/time.
 * @param[out] buf     Destination character buffer.
 * @param[in]  buf_sz  Buffer capacity in bytes (must be >= 25).
 * @return 0 on success, -EINVAL if date or buffer is invalid, -ENOBUFS if buffer is too small.
 */
int rtc_calc_format_iso8601(const rtc_date_time_t *dt, char *buf, size_t buf_sz);

/**
 * @brief Parse an ISO-8601 UTC string into a broken-down calendar date/time structure.
 *
 * Accepts "YYYY-MM-DDTHH:MM:SS", "YYYY-MM-DDTHH:MM:SS.mmm", "YYYY-MM-DDTHH:MM:SS.mmmZ",
 * or space-separated "YYYY-MM-DD HH:MM:SS".
 *
 * Automatically populates day_of_week.
 *
 * @param[in]  str     Input ISO-8601 formatted string.
 * @param[out] out_dt  Pointer to destination rtc_date_time_t structure.
 * @return 0 on success, -EINVAL if string format is invalid or values out of range.
 */
int rtc_calc_parse_iso8601(const char *str, rtc_date_time_t *out_dt);

/**
 * @brief Encode a calendar date/time into a 32-bit DOS FATFS timestamp.
 *
 * Bit layout:
 * - Bits 31:25: Year origin from 1980 (0..127 -> 1980..2107)
 * - Bits 24:21: Month (1..12)
 * - Bits 20:16: Day of month (1..31)
 * - Bits 15:11: Hours (0..23)
 * - Bits 10:5:  Minutes (0..59)
 * - Bits 4:0:   Seconds / 2 (0..29)
 *
 * @param[in] dt Pointer to calendar date/time.
 * @return 32-bit packed DOS timestamp (falls back to 2026-01-01 00:00:00 on NULL/invalid).
 */
uint32_t rtc_calc_datetime_to_fattime(const rtc_date_time_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_TIME_RTC_TIME_CALC_H_ */
