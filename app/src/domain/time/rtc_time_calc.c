/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain Date-Time Calculation Utilities for RTC and System Time Subsystem.
 * Pure C99/C11 - Zero Zephyr headers, zero hardware registers.
 */

#include "domain/time/rtc_time_calc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const uint8_t s_days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

bool rtc_calc_is_leap_year(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

uint8_t rtc_calc_day_of_week(uint16_t year, uint8_t month, uint8_t day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }

    int y = (int)year - (month < 3);
    int dow = (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;

    /* Sakamoto returns 0 for Sunday, 1 for Monday, ..., 6 for Saturday.
     * ISO-8601 mandates 1 = Monday .. 7 = Sunday.
     */
    return (dow == 0) ? 7 : (uint8_t)dow;
}

bool rtc_calc_is_valid_datetime(const rtc_date_time_t *dt)
{
    if (dt == NULL) {
        return false;
    }

    /* Valid year range: 1970 to 2099 */
    if (dt->year < 1970 || dt->year > 2099) {
        return false;
    }

    /* Valid month: 1 to 12 */
    if (dt->month < 1 || dt->month > 12) {
        return false;
    }

    /* Days in month */
    uint8_t max_days = s_days_in_month[dt->month - 1];
    if (dt->month == 2 && rtc_calc_is_leap_year(dt->year)) {
        max_days = 29;
    }

    if (dt->day < 1 || dt->day > max_days) {
        return false;
    }

    /* Time fields */
    if (dt->hours > 23 || dt->minutes > 59 || dt->seconds > 59 || dt->milliseconds > 999) {
        return false;
    }

    /* Day of week if populated: 1..7 */
    if (dt->day_of_week != 0 && (dt->day_of_week < 1 || dt->day_of_week > 7)) {
        return false;
    }

    return true;
}

int rtc_calc_datetime_to_epoch(const rtc_date_time_t *dt, int64_t *out_epoch_s)
{
    if (!rtc_calc_is_valid_datetime(dt) || out_epoch_s == NULL) {
        return -EINVAL;
    }

    int y = (int)dt->year;
    int m = (int)dt->month;
    int d = (int)dt->day;

    /* Howard Hinnant / Fliegel-Van Flandern civil date algorithm */
    if (m <= 2) {
        y -= 1;
        m += 12;
    }

    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m - 3) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;

    *out_epoch_s = days * 86400LL +
                   (int64_t)dt->hours * 3600LL +
                   (int64_t)dt->minutes * 60LL +
                   (int64_t)dt->seconds;

    return 0;
}

int rtc_calc_epoch_to_datetime(int64_t epoch_s, uint16_t subsecond_ms, rtc_date_time_t *out_dt)
{
    if (out_dt == NULL || epoch_s < 0 || subsecond_ms > 999) {
        return -EINVAL;
    }

    int64_t days = epoch_s / 86400LL;
    int64_t rem_s = epoch_s % 86400LL;
    if (rem_s < 0) {
        rem_s += 86400LL;
        days -= 1;
    }

    out_dt->hours = (uint8_t)(rem_s / 3600);
    rem_s %= 3600;
    out_dt->minutes = (uint8_t)(rem_s / 60);
    out_dt->seconds = (uint8_t)(rem_s % 60);
    out_dt->milliseconds = subsecond_ms;

    /* Day of week: 1970-01-01 was Thursday (ISO day 4).
     * (days + 3) % 7 yields: 0 = Monday, ..., 6 = Sunday.
     */
    int dow = (int)((days + 3) % 7);
    if (dow < 0) {
        dow += 7;
    }
    out_dt->day_of_week = (uint8_t)(dow + 1);

    /* Civil date calculation from days since 1970-01-01 */
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    uint64_t doe = (uint64_t)(days - era * 146097);
    uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint64_t mp = (5 * doy + 2) / 153;
    uint8_t d = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    uint8_t m = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2) {
        y += 1;
    }

    if (y < 1970 || y > 2099) {
        return -ERANGE;
    }

    out_dt->year = (uint16_t)y;
    out_dt->month = m;
    out_dt->day = d;

    return 0;
}

int rtc_calc_format_iso8601(const rtc_date_time_t *dt, char *buf, size_t buf_sz)
{
    if (buf == NULL) {
        return -EINVAL;
    }

    if (!rtc_calc_is_valid_datetime(dt)) {
        return -EINVAL;
    }

    /* "YYYY-MM-DDTHH:MM:SS.mmmZ" is 24 characters + NUL = 25 bytes */
    if (buf_sz < 25) {
        return -ENOBUFS;
    }

    int ret = snprintf(buf, buf_sz, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                       (unsigned int)dt->year,
                       (unsigned int)dt->month,
                       (unsigned int)dt->day,
                       (unsigned int)dt->hours,
                       (unsigned int)dt->minutes,
                       (unsigned int)dt->seconds,
                       (unsigned int)dt->milliseconds);

    if (ret < 0 || (size_t)ret >= buf_sz) {
        return -ENOBUFS;
    }

    return 0;
}

int rtc_calc_parse_iso8601(const char *str, rtc_date_time_t *out_dt)
{
    if (str == NULL || out_dt == NULL) {
        return -EINVAL;
    }

    unsigned int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0, ms = 0;
    char sep = 0;

    /* Try format with milliseconds and optional 'Z': YYYY-MM-DDTHH:MM:SS.mmm */
    int n = sscanf(str, "%u-%u-%u%c%u:%u:%u.%u", &yr, &mo, &dy, &sep, &hr, &mn, &sc, &ms);
    if (n >= 7 && (sep == 'T' || sep == 't' || sep == ' ')) {
        /* Handled */
    } else {
        /* Try without milliseconds: YYYY-MM-DDTHH:MM:SS */
        n = sscanf(str, "%u-%u-%u%c%u:%u:%u", &yr, &mo, &dy, &sep, &hr, &mn, &sc);
        if (n < 7 || (sep != 'T' && sep != 't' && sep != ' ')) {
            return -EINVAL;
        }
        ms = 0;
    }

    /* If milliseconds has fewer or more than 3 digits, normalize if needed */
    if (ms > 999) {
        /* If 4+ digits were parsed, scale down to 3 digits */
        while (ms > 999) {
            ms /= 10;
        }
    }

    rtc_date_time_t dt;
    dt.year = (uint16_t)yr;
    dt.month = (uint8_t)mo;
    dt.day = (uint8_t)dy;
    dt.hours = (uint8_t)hr;
    dt.minutes = (uint8_t)mn;
    dt.seconds = (uint8_t)sc;
    dt.milliseconds = (uint16_t)ms;
    dt.day_of_week = rtc_calc_day_of_week(dt.year, dt.month, dt.day);

    if (!rtc_calc_is_valid_datetime(&dt)) {
        return -EINVAL;
    }

    *out_dt = dt;
    return 0;
}

uint32_t rtc_calc_datetime_to_fattime(const rtc_date_time_t *dt)
{
    if (!rtc_calc_is_valid_datetime(dt) || dt->year < 1980) {
        /* Fallback: 2026-01-01 00:00:00 */
        return (((uint32_t)(2026 - 1980) << 25) | (1U << 21) | (1U << 16));
    }

    return (((uint32_t)(dt->year - 1980) << 25) |
            ((uint32_t)dt->month << 21) |
            ((uint32_t)dt->day << 16) |
            ((uint32_t)dt->hours << 11) |
            ((uint32_t)dt->minutes << 5) |
            ((uint32_t)(dt->seconds >> 1)));
}
