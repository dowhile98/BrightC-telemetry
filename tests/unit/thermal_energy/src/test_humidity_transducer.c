/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_humidity_transducer.c
 * @brief Unit tests for 4-20 mA NAMUR NE43 Return Relative Humidity Transducer (Suite 2).
 */

#include <zephyr/ztest.h>
#include <math.h>

#include "domain/thermal_energy/humidity_transducer.h"

ZTEST_SUITE(humidity_transducer_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(humidity_transducer_suite, test_humidity_loop_break_fault)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(0.0f, &rh);

    zassert_equal(q, QUALITY_FAILED, "0.0 mA must be QUALITY_FAILED");
    zassert_within(rh, 0.0f, 0.001f, "HR_ret must be clamped to 0.0%%");
}

ZTEST(humidity_transducer_suite, test_humidity_loop_sub_minimum)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(3.2f, &rh);

    zassert_equal(q, QUALITY_FAILED, "3.2 mA must be QUALITY_FAILED (< 3.5 mA NAMUR)");
    zassert_within(rh, 0.0f, 0.001f, "HR_ret must be clamped to 0.0%%");
}

ZTEST(humidity_transducer_suite, test_humidity_lower_margin_clamp)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(3.7f, &rh);

    zassert_equal(q, QUALITY_SUSPECT, "3.7 mA must be QUALITY_SUSPECT (3.5 to 4.0 mA margin)");
    zassert_within(rh, 0.0f, 0.001f, "HR_ret must be clamped to 0.0%%");
}

ZTEST(humidity_transducer_suite, test_humidity_exact_zero_scale)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(4.0f, &rh);

    zassert_equal(q, QUALITY_VALID, "4.0 mA must be QUALITY_VALID");
    zassert_within(rh, 0.0f, 0.001f, "HR_ret must be 0.0%% at 4.0 mA");
}

ZTEST(humidity_transducer_suite, test_humidity_mid_scale_reading)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(12.0f, &rh);

    zassert_equal(q, QUALITY_VALID, "12.0 mA must be QUALITY_VALID");
    zassert_within(rh, 50.0f, 0.05f, "HR_ret must be 50.0%% at 12.0 mA");
}

ZTEST(humidity_transducer_suite, test_humidity_exact_full_scale)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(20.0f, &rh);

    zassert_equal(q, QUALITY_VALID, "20.0 mA must be QUALITY_VALID");
    zassert_within(rh, 100.0f, 0.05f, "HR_ret must be 100.0%% at 20.0 mA");
}

ZTEST(humidity_transducer_suite, test_humidity_upper_margin_clamp)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(20.5f, &rh);

    zassert_equal(q, QUALITY_SUSPECT, "20.5 mA must be QUALITY_SUSPECT (saturation margin)");
    zassert_within(rh, 100.0f, 0.001f, "HR_ret must be clamped to 100.0%%");
}

ZTEST(humidity_transducer_suite, test_humidity_over_current_fault)
{
    float rh = -1.0f;
    quality_status_t q = humidity_transducer_process(22.0f, &rh);

    zassert_equal(q, QUALITY_FAILED, "22.0 mA must be QUALITY_FAILED (> 20.5 mA overcurrent)");
    zassert_within(rh, 100.0f, 0.001f, "HR_ret must be clamped to 100.0%%");
}

ZTEST(humidity_transducer_suite, test_humidity_from_adc)
{
    /* 150 Ohm shunt with 3.3V full scale 16-bit ADC (65536 counts) */
    /* For 12.0 mA (mid scale -> 50% RH):
     * V = 0.012 A * 150 Ohm = 1.8 V
     * counts = 1.8 * (65536 / 3.3) = 35746.9 -> 35747 counts
     */
    uint16_t counts_mid = 35747U;
    float current_ma = 0.0f;
    float rh = 0.0f;

    quality_status_t q = humidity_transducer_from_adc(counts_mid, 150.0f, &current_ma, &rh);

    zassert_equal(q, QUALITY_VALID, "Mid-scale ADC conversion must be valid");
    zassert_within(current_ma, 12.0f, 0.05f, "Current should be approx 12.0 mA");
    zassert_within(rh, 50.0f, 0.3f, "RH should be approx 50.0%%");
}

ZTEST(humidity_transducer_suite, test_humidity_defensive_guards)
{
    /* NULL output pointer */
    zassert_equal(humidity_transducer_process(12.0f, NULL), QUALITY_FAILED,
                  "NULL out_rh_pct must return QUALITY_FAILED");

    /* NaN and Infinity */
    float rh = 0.0f;
    zassert_equal(humidity_transducer_process(NAN, &rh), QUALITY_FAILED,
                  "NaN must return QUALITY_FAILED");
    zassert_equal(humidity_transducer_process(INFINITY, &rh), QUALITY_FAILED,
                  "Inf must return QUALITY_FAILED");

    /* Invalid shunt resistance */
    float current_ma = 0.0f;
    zassert_equal(humidity_transducer_from_adc(1000, 0.0f, &current_ma, &rh), QUALITY_FAILED,
                  "Zero shunt resistance must return QUALITY_FAILED");
    zassert_equal(humidity_transducer_from_adc(1000, -50.0f, &current_ma, &rh), QUALITY_FAILED,
                  "Negative shunt resistance must return QUALITY_FAILED");
}
