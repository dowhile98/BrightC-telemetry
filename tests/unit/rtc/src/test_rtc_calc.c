/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "common/rtc_types.h"
#include "domain/time/rtc_time_calc.h"

ZTEST_SUITE(rtc_calc_suite, NULL, NULL, NULL, NULL, NULL);

/* =========================================================================
 * 1. Leap Year Verification
 * ========================================================================= */
ZTEST(rtc_calc_suite, test_leap_year_rules)
{
    /* Centurial years not divisible by 400 are NOT leap years */
    zassert_false(rtc_calc_is_leap_year(1900), "1900 must not be a leap year");
    zassert_false(rtc_calc_is_leap_year(2100), "2100 must not be a leap year");
    zassert_false(rtc_calc_is_leap_year(2200), "2200 must not be a leap year");

    /* Centurial years divisible by 400 ARE leap years */
    zassert_true(rtc_calc_is_leap_year(2000), "2000 must be a leap year");
    zassert_true(rtc_calc_is_leap_year(2400), "2400 must be a leap year");

    /* Standard leap years */
    zassert_true(rtc_calc_is_leap_year(2024), "2024 must be a leap year");
    zassert_true(rtc_calc_is_leap_year(2028), "2028 must be a leap year");

    /* Non-leap years */
    zassert_false(rtc_calc_is_leap_year(2025), "2025 must not be a leap year");
    zassert_false(rtc_calc_is_leap_year(2026), "2026 must not be a leap year");
    zassert_false(rtc_calc_is_leap_year(2027), "2027 must not be a leap year");
}

/* =========================================================================
 * 2. DateTime Validation Checks
 * ========================================================================= */
ZTEST(rtc_calc_suite, test_datetime_validation)
{
    rtc_date_time_t dt;

    /* NULL check */
    zassert_false(rtc_calc_is_valid_datetime(NULL), "NULL dt must be invalid");

    /* Base valid date */
    dt.year = 2026;
    dt.month = 11;
    dt.day = 11;
    dt.hours = 18;
    dt.minutes = 45;
    dt.seconds = 12;
    dt.milliseconds = 382;
    dt.day_of_week = 3;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Valid dt must pass");

    /* Year boundaries */
    dt.year = 1969;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Year < 1970 must fail");
    dt.year = 1970;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Year 1970 must pass");
    dt.year = 2099;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Year 2099 must pass");
    dt.year = 2100;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Year 2100 must fail");
    dt.year = 2026;

    /* Month boundaries */
    dt.month = 0;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Month 0 must fail");
    dt.month = 13;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Month 13 must fail");
    dt.month = 2;

    /* February leap vs non-leap */
    dt.year = 2026; /* Non-leap */
    dt.day = 28;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Feb 28 in non-leap must pass");
    dt.day = 29;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Feb 29 in non-leap must fail");

    dt.year = 2024; /* Leap */
    dt.day = 29;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Feb 29 in leap must pass");
    dt.day = 30;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Feb 30 in leap must fail");
    dt.year = 2026;

    /* 30-day month (April) */
    dt.month = 4;
    dt.day = 30;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Apr 30 must pass");
    dt.day = 31;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Apr 31 must fail");

    /* 31-day month (January) */
    dt.month = 1;
    dt.day = 31;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Jan 31 must pass");
    dt.day = 32;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Jan 32 must fail");
    dt.day = 0;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Day 0 must fail");
    dt.day = 15;

    /* Hours (0..23) */
    dt.hours = 23;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Hour 23 must pass");
    dt.hours = 24;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Hour 24 must fail");
    dt.hours = 12;

    /* Minutes (0..59) */
    dt.minutes = 59;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Minute 59 must pass");
    dt.minutes = 60;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Minute 60 must fail");
    dt.minutes = 30;

    /* Seconds (0..59) */
    dt.seconds = 59;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Second 59 must pass");
    dt.seconds = 60;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Second 60 must fail");
    dt.seconds = 0;

    /* Milliseconds (0..999) */
    dt.milliseconds = 999;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "Millisecond 999 must pass");
    dt.milliseconds = 1000;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "Millisecond 1000 must fail");
    dt.milliseconds = 0;

    /* Day of week (0=unspecified allowed, 1..7 valid, 8 invalid) */
    dt.day_of_week = 0;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "DOW 0 must pass (unspecified)");
    dt.day_of_week = 7;
    zassert_true(rtc_calc_is_valid_datetime(&dt), "DOW 7 must pass");
    dt.day_of_week = 8;
    zassert_false(rtc_calc_is_valid_datetime(&dt), "DOW 8 must fail");
}

/* =========================================================================
 * 3. Bidirectional Epoch & DateTime Conversion (including Y2038 / 64-bit)
 * ========================================================================= */
ZTEST(rtc_calc_suite, test_datetime_epoch_bidirectional)
{
    rtc_date_time_t dt_in;
    rtc_date_time_t dt_out;
    int64_t epoch_s = -1;

    /* Test 1: Epoch 0 -> 1970-01-01 00:00:00 UTC, Thursday (DOW=4) */
    dt_in.year = 1970;
    dt_in.month = 1;
    dt_in.day = 1;
    dt_in.hours = 0;
    dt_in.minutes = 0;
    dt_in.seconds = 0;
    dt_in.milliseconds = 0;
    dt_in.day_of_week = 4;

    zassert_equal(rtc_calc_datetime_to_epoch(&dt_in, &epoch_s), 0, "Epoch conversion failed");
    zassert_equal(epoch_s, 0LL, "Epoch for 1970-01-01 must be 0");

    zassert_equal(rtc_calc_epoch_to_datetime(0LL, 0, &dt_out), 0, "Inverse conversion failed");
    zassert_equal(dt_out.year, 1970, "Year mismatch");
    zassert_equal(dt_out.month, 1, "Month mismatch");
    zassert_equal(dt_out.day, 1, "Day mismatch");
    zassert_equal(dt_out.hours, 0, "Hour mismatch");
    zassert_equal(dt_out.minutes, 0, "Minute mismatch");
    zassert_equal(dt_out.seconds, 0, "Second mismatch");
    zassert_equal(dt_out.milliseconds, 0, "Millisecond mismatch");
    zassert_equal(dt_out.day_of_week, 4, "Day of week mismatch for 1970-01-01 (Thursday)");

    /* Test 2: Known reference point 2026-11-11 18:45:12.382 UTC */
    dt_in.year = 2026;
    dt_in.month = 11;
    dt_in.day = 11;
    dt_in.hours = 18;
    dt_in.minutes = 45;
    dt_in.seconds = 12;
    dt_in.milliseconds = 382;
    dt_in.day_of_week = 3; /* Wednesday */

    zassert_equal(rtc_calc_datetime_to_epoch(&dt_in, &epoch_s), 0, "Epoch conversion failed");
    zassert_equal(epoch_s, 1794422712LL, "Epoch for 2026-11-11 18:45:12 must match");

    zassert_equal(rtc_calc_epoch_to_datetime(epoch_s, 382, &dt_out), 0, "Inverse conversion failed");
    zassert_equal(dt_out.year, 2026, "Year mismatch");
    zassert_equal(dt_out.month, 11, "Month mismatch");
    zassert_equal(dt_out.day, 11, "Day mismatch");
    zassert_equal(dt_out.hours, 18, "Hours mismatch");
    zassert_equal(dt_out.minutes, 45, "Minutes mismatch");
    zassert_equal(dt_out.seconds, 12, "Seconds mismatch");
    zassert_equal(dt_out.milliseconds, 382, "Milliseconds mismatch");
    zassert_equal(dt_out.day_of_week, 3, "Day of week mismatch (Wednesday)");

    /* Test 3: Y2038 Overflow boundary test (2038-01-19 03:14:08 UTC -> 2147483648 s) */
    int64_t y2038_epoch = 2147483648LL; /* 2^31, exceeds signed 32-bit int */
    zassert_equal(rtc_calc_epoch_to_datetime(y2038_epoch, 0, &dt_out), 0, "Y2038 conversion failed");
    zassert_equal(dt_out.year, 2038, "Year mismatch");
    zassert_equal(dt_out.month, 1, "Month mismatch");
    zassert_equal(dt_out.day, 19, "Day mismatch");
    zassert_equal(dt_out.hours, 3, "Hours mismatch");
    zassert_equal(dt_out.minutes, 14, "Minutes mismatch");
    zassert_equal(dt_out.seconds, 8, "Seconds mismatch");

    int64_t roundtrip_epoch = 0;
    zassert_equal(rtc_calc_datetime_to_epoch(&dt_out, &roundtrip_epoch), 0, "Y2038 reverse failed");
    zassert_equal(roundtrip_epoch, y2038_epoch, "Y2038 roundtrip mismatch");

    /* Test 4: Year 2050 (2050-06-15 12:30:00 UTC) */
    dt_in.year = 2050;
    dt_in.month = 6;
    dt_in.day = 15;
    dt_in.hours = 12;
    dt_in.minutes = 30;
    dt_in.seconds = 0;
    dt_in.milliseconds = 500;
    dt_in.day_of_week = 3; /* Wednesday */

    zassert_equal(rtc_calc_datetime_to_epoch(&dt_in, &epoch_s), 0, "2050 epoch failed");
    zassert_true(epoch_s > 2147483648LL, "2050 epoch must exceed 32-bit int");
    zassert_equal(rtc_calc_epoch_to_datetime(epoch_s, 500, &dt_out), 0, "2050 reverse failed");
    zassert_equal(dt_out.year, 2050, "Year mismatch");
    zassert_equal(dt_out.month, 6, "Month mismatch");
    zassert_equal(dt_out.day, 15, "Day mismatch");
    zassert_equal(dt_out.hours, 12, "Hours mismatch");
    zassert_equal(dt_out.minutes, 30, "Minutes mismatch");
    zassert_equal(dt_out.milliseconds, 500, "Subsecond ms mismatch");
}

/* =========================================================================
 * 4. ISO-8601 Formatting and Parsing Tests
 * ========================================================================= */
ZTEST(rtc_calc_suite, test_iso8601_format_and_parse)
{
    char buf[RTC_ISO8601_STR_LEN];
    rtc_date_time_t dt_in;
    rtc_date_time_t dt_out;

    dt_in.year = 2026;
    dt_in.month = 11;
    dt_in.day = 11;
    dt_in.hours = 18;
    dt_in.minutes = 45;
    dt_in.seconds = 12;
    dt_in.milliseconds = 382;
    dt_in.day_of_week = 3;

    /* Buffer capacity bounds */
    zassert_equal(rtc_calc_format_iso8601(&dt_in, buf, 24), -ENOBUFS, "Buffer < 25 must return -ENOBUFS");
    zassert_equal(rtc_calc_format_iso8601(&dt_in, NULL, 32), -EINVAL, "NULL buf must return -EINVAL");

    /* Successful formatting */
    zassert_equal(rtc_calc_format_iso8601(&dt_in, buf, sizeof(buf)), 0, "Format failed");
    zassert_str_equal(buf, "2026-11-11T18:45:12.382Z", "Formatted string mismatch");

    /* Parse standard ISO-8601 with T and Z */
    zassert_equal(rtc_calc_parse_iso8601("2026-11-11T18:45:12.382Z", &dt_out), 0, "Parse failed");
    zassert_equal(dt_out.year, 2026, "Year mismatch");
    zassert_equal(dt_out.month, 11, "Month mismatch");
    zassert_equal(dt_out.day, 11, "Day mismatch");
    zassert_equal(dt_out.hours, 18, "Hours mismatch");
    zassert_equal(dt_out.minutes, 45, "Minutes mismatch");
    zassert_equal(dt_out.seconds, 12, "Seconds mismatch");
    zassert_equal(dt_out.milliseconds, 382, "Milliseconds mismatch");
    zassert_equal(dt_out.day_of_week, 3, "Day of week mismatch");

    /* Parse space-separated ISO string */
    zassert_equal(rtc_calc_parse_iso8601("2026-11-11 18:45:12", &dt_out), 0, "Parse space failed");
    zassert_equal(dt_out.year, 2026, "Year mismatch");
    zassert_equal(dt_out.milliseconds, 0, "Milliseconds should be 0");

    /* Parse ISO without milliseconds */
    zassert_equal(rtc_calc_parse_iso8601("2024-02-29T12:00:00Z", &dt_out), 0, "Parse leap day failed");
    zassert_equal(dt_out.year, 2024, "Year mismatch");
    zassert_equal(dt_out.month, 2, "Month mismatch");
    zassert_equal(dt_out.day, 29, "Day mismatch");
    zassert_equal(dt_out.day_of_week, 4, "2024-02-29 was Thursday");

    /* Invalid string formats */
    zassert_equal(rtc_calc_parse_iso8601(NULL, &dt_out), -EINVAL, "NULL string must fail");
    zassert_equal(rtc_calc_parse_iso8601("invalid-string", &dt_out), -EINVAL, "Garbage string must fail");
    zassert_equal(rtc_calc_parse_iso8601("2026/11/11 18:45:12", &dt_out), -EINVAL, "Wrong delimiter must fail");
    zassert_equal(rtc_calc_parse_iso8601("2026-02-29T12:00:00Z", &dt_out), -EINVAL, "Invalid date in ISO must fail");
}

/* =========================================================================
 * 5. FATFS 32-bit DOS Timestamp Encoding Tests
 * ========================================================================= */
ZTEST(rtc_calc_suite, test_fattime_encoding)
{
    rtc_date_time_t dt;
    dt.year = 2026;
    dt.month = 11;
    dt.day = 11;
    dt.hours = 18;
    dt.minutes = 45;
    dt.seconds = 12;
    dt.milliseconds = 0;
    dt.day_of_week = 3;

    uint32_t fattime = rtc_calc_datetime_to_fattime(&dt);

    /* Bit extraction */
    uint32_t year = ((fattime >> 25) & 0x7F) + 1980;
    uint32_t month = (fattime >> 21) & 0x0F;
    uint32_t day = (fattime >> 16) & 0x1F;
    uint32_t hours = (fattime >> 11) & 0x1F;
    uint32_t minutes = (fattime >> 5) & 0x3F;
    uint32_t seconds = (fattime & 0x1F) * 2;

    zassert_equal(year, 2026, "FAT year mismatch");
    zassert_equal(month, 11, "FAT month mismatch");
    zassert_equal(day, 11, "FAT day mismatch");
    zassert_equal(hours, 18, "FAT hours mismatch");
    zassert_equal(minutes, 45, "FAT minutes mismatch");
    zassert_equal(seconds, 12, "FAT seconds mismatch");

    /* Fallback on NULL */
    uint32_t fallback = rtc_calc_datetime_to_fattime(NULL);
    uint32_t fb_year = ((fallback >> 25) & 0x7F) + 1980;
    zassert_equal(fb_year, 2026, "Fallback year should be 2026");
}
