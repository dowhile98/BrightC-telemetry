/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_compressor_fsm.c
 * @brief Unit tests for compressor stabilization 5-phase FSM and load fraction (Suite 5).
 */

#include <zephyr/ztest.h>

#include "domain/thermal_energy/compressor_fsm.h"

ZTEST_SUITE(compressor_fsm_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(compressor_fsm_suite, test_fsm_power_on_initialization)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    zassert_equal(fsm.state, COMPRESSOR_STATE_OFF, "Boot state must be COMPRESSOR_STATE_OFF");
    zassert_equal(fsm.t_off_s, 0U, "Initial t_off must be 0");
    zassert_equal(fsm.t_on_s, 0U, "Initial t_on must be 0");
    zassert_equal(fsm.starts_count, 0U, "Initial starts_count must be 0");
    zassert_equal(fsm.short_cycle_count, 0U, "Initial short_cycle_count must be 0");
    zassert_false(fsm.steady_state, "steady_state must be false initially");

    /* Accumulate stopped time */
    for (int i = 0; i < 50; i++) {
        compressor_fsm_update(&fsm, false, 5.0f, false);
    }

    zassert_equal(fsm.state, COMPRESSOR_STATE_OFF, "State must remain OFF while contact open");
    zassert_equal(fsm.t_off_s, 50U, "t_off must accumulate to 50s");
}

ZTEST(compressor_fsm_suite, test_fsm_short_cycle_increment)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    /* Simulate stopped for 90 seconds (< 180 s) */
    for (int i = 0; i < 90; i++) {
        compressor_fsm_update(&fsm, false, 5.0f, false);
    }
    zassert_equal(fsm.t_off_s, 90U, "Stopped for 90s");

    /* Contact asserts on run edge */
    compressor_fsm_update(&fsm, true, 5.0f, false);

    zassert_equal(fsm.short_cycle_count, 1U, "Short cycle count must increment (90s < 180s)");
    zassert_equal(fsm.starts_count, 1U, "Starts count must increment");
    zassert_equal(fsm.state, COMPRESSOR_STATE_INRUSH_WAIT, "State must transition to INRUSH_WAIT");
    zassert_within(fsm.p_pre_start_kw, 5.0f, 0.001f, "Pre-start power must be latched");
}

ZTEST(compressor_fsm_suite, test_fsm_no_short_cycle_when_long_stop)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    /* Simulate stopped for 200 seconds (>= 180 s) */
    for (int i = 0; i < 200; i++) {
        compressor_fsm_update(&fsm, false, 5.0f, false);
    }

    /* Contact asserts */
    compressor_fsm_update(&fsm, true, 5.0f, false);

    zassert_equal(fsm.short_cycle_count, 0U, "Short cycle count must NOT increment (200s >= 180s)");
    zassert_equal(fsm.starts_count, 1U, "Starts count must increment");
    zassert_equal(fsm.state, COMPRESSOR_STATE_INRUSH_WAIT, "State must transition to INRUSH_WAIT");
}

ZTEST(compressor_fsm_suite, test_fsm_inrush_suppression)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    /* Start compressor */
    compressor_fsm_update(&fsm, true, 4.0f, false);
    zassert_equal(fsm.state, COMPRESSOR_STATE_INRUSH_WAIT, "Must enter INRUSH_WAIT");

    /* Advance 14 seconds (t_on = 1 .. 14) */
    for (int i = 1; i <= 14; i++) {
        compressor_fsm_update(&fsm, true, 25.0f /* inrush spike */, false);
        zassert_equal(fsm.state, COMPRESSOR_STATE_INRUSH_WAIT,
                      "At t_on=%d, state must still be INRUSH_WAIT", i);
        zassert_equal(fsm.delta_p_quality, QUALITY_UNAVAILABLE,
                      "Power samples must be discarded during inrush");
        zassert_false(fsm.steady_state, "steady_state must be false during inrush");
    }
}

ZTEST(compressor_fsm_suite, test_fsm_step_load_capture_success)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    /* Pre-start baseline power = 4.0 kW */
    compressor_fsm_update(&fsm, true, 4.0f, false);

    /* Run through 14 seconds */
    for (int i = 1; i <= 14; i++) {
        compressor_fsm_update(&fsm, true, 18.0f, false);
    }

    /* 15th second: isolated step load capture with total_power = 16.5 kW */
    compressor_fsm_update(&fsm, true, 16.5f, false);

    zassert_equal(fsm.state, COMPRESSOR_STATE_STEP_LOAD_CAPTURE,
                  "At t_on=15s, state must be STEP_LOAD_CAPTURE");
    zassert_equal(fsm.delta_p_quality, QUALITY_VALID, "Delta P must be QUALITY_VALID");
    /* Delta P = 16.5 - 4.0 = 12.5 kW */
    zassert_within(fsm.delta_p_comp_kw, 12.5f, 0.001f, "Delta P must be 12.5 kW");

    /* Next second (t_on=16s) -> transitions to THERMAL_WARMUP */
    compressor_fsm_update(&fsm, true, 16.5f, false);
    zassert_equal(fsm.state, COMPRESSOR_STATE_THERMAL_WARMUP,
                  "State must transition to THERMAL_WARMUP at t_on=16s");
    zassert_false(fsm.steady_state, "steady_state must still be false during warmup");
}

ZTEST(compressor_fsm_suite, test_fsm_step_load_concurrency_lockout)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    compressor_fsm_update(&fsm, true, 4.0f, false);

    /* Advance to 9 seconds */
    for (int i = 1; i <= 9; i++) {
        compressor_fsm_update(&fsm, true, 18.0f, false);
    }

    /* At t_on = 10s, concurrent transition detected (another motor started) */
    compressor_fsm_update(&fsm, true, 28.0f, true);

    /* Advance to 14 seconds */
    for (int i = 11; i <= 14; i++) {
        compressor_fsm_update(&fsm, true, 28.0f, false);
    }

    /* 15th second: step load capture evaluates concurrency lockout */
    compressor_fsm_update(&fsm, true, 28.0f, false);

    zassert_equal(fsm.state, COMPRESSOR_STATE_STEP_LOAD_CAPTURE,
                  "State must be STEP_LOAD_CAPTURE at 15s");
    zassert_equal(fsm.delta_p_quality, QUALITY_UNAVAILABLE,
                  "Delta P must be marked QUALITY_UNAVAILABLE due to concurrent start");
    zassert_equal(fsm.delta_p_comp_kw, 0.0f, "Delta P must be 0.0 when unavailable");
}

ZTEST(compressor_fsm_suite, test_fsm_steady_state_entry)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    /* Start and advance past inrush (15s) */
    compressor_fsm_update(&fsm, true, 4.0f, false);
    for (int i = 1; i <= 15; i++) {
        compressor_fsm_update(&fsm, true, 14.0f, false);
    }
    zassert_equal(fsm.state, COMPRESSOR_STATE_STEP_LOAD_CAPTURE, "Step load capture at 15s");

    /* Advance through warmup (16 to 179s) */
    for (int i = 16; i < 180; i++) {
        compressor_fsm_update(&fsm, true, 14.0f, false);
        zassert_equal(fsm.state, COMPRESSOR_STATE_THERMAL_WARMUP,
                      "Must remain in THERMAL_WARMUP at t_on=%d", i);
        zassert_false(fsm.steady_state, "steady_state must be false");
    }

    /* 180th second: reaches steady state */
    compressor_fsm_update(&fsm, true, 14.0f, false);
    zassert_equal(fsm.state, COMPRESSOR_STATE_STEADY_STATE,
                  "State must transition to STEADY_STATE at t_on=180s");
    zassert_true(fsm.steady_state, "steady_state must be true");

    /* Stays in steady state */
    compressor_fsm_update(&fsm, true, 14.0f, false);
    zassert_equal(fsm.state, COMPRESSOR_STATE_STEADY_STATE, "Must stay in STEADY_STATE");
    zassert_true(fsm.steady_state, "steady_state must remain true");
}

ZTEST(compressor_fsm_suite, test_fsm_stop_edge_reset)
{
    compressor_fsm_t fsm;
    compressor_fsm_init(&fsm);

    /* Run to steady state */
    compressor_fsm_update(&fsm, true, 4.0f, false);
    for (int i = 1; i <= 180; i++) {
        compressor_fsm_update(&fsm, true, 14.0f, false);
    }
    zassert_equal(fsm.state, COMPRESSOR_STATE_STEADY_STATE, "In steady state");

    /* Stop edge */
    compressor_fsm_update(&fsm, false, 4.0f, false);
    zassert_equal(fsm.state, COMPRESSOR_STATE_OFF, "Must immediately transition to OFF");
    zassert_equal(fsm.t_off_s, 0U, "t_off_s must be reset to 0");
    zassert_equal(fsm.t_on_s, 0U, "t_on_s must be reset to 0");
    zassert_false(fsm.steady_state, "steady_state must be cleared");

    /* Stays off and accumulates t_off */
    compressor_fsm_update(&fsm, false, 4.0f, false);
    zassert_equal(fsm.state, COMPRESSOR_STATE_OFF, "Remains OFF");
    zassert_equal(fsm.t_off_s, 1U, "t_off accumulates");
}

ZTEST(compressor_fsm_suite, test_compressor_load_fraction)
{
    compressor_fsm_t fsms[4];
    for (int i = 0; i < 4; i++) {
        compressor_fsm_init(&fsms[i]);
    }

    float nominal_caps[4] = {10.0f, 10.0f, 20.0f, 20.0f}; /* Total 60 kW */

    /* All OFF */
    float f_load = compressor_fsm_calc_load_fraction(fsms, 4, nominal_caps);
    zassert_equal(f_load, 0.0f, "Load fraction must be 0 when all OFF");

    /* Turn ON compressor 0 (10 kW) and 2 (20 kW) -> 30 kW active / 60 kW total = 0.50 */
    compressor_fsm_update(&fsms[0], true, 10.0f, false);
    compressor_fsm_update(&fsms[2], true, 20.0f, false);

    f_load = compressor_fsm_calc_load_fraction(fsms, 4, nominal_caps);
    zassert_within(f_load, 0.50f, 0.001f, "Load fraction must be 30/60 = 0.50");

    /* Turn ON all 4 compressors -> 60 / 60 = 1.00 */
    compressor_fsm_update(&fsms[1], true, 10.0f, false);
    compressor_fsm_update(&fsms[3], true, 20.0f, false);

    f_load = compressor_fsm_calc_load_fraction(fsms, 4, nominal_caps);
    zassert_within(f_load, 1.00f, 0.001f, "Load fraction must be 60/60 = 1.00");

    /* Unweighted calculation (NULL nominal_capacities) */
    /* 4 active out of 4 -> 1.0 */
    f_load = compressor_fsm_calc_load_fraction(fsms, 4, NULL);
    zassert_within(f_load, 1.00f, 0.001f, "Unweighted 4/4 must be 1.00");

    /* Turn off 1 unit -> 3 active out of 4 -> 0.75 */
    compressor_fsm_update(&fsms[0], false, 0.0f, false);
    f_load = compressor_fsm_calc_load_fraction(fsms, 4, NULL);
    zassert_within(f_load, 0.75f, 0.001f, "Unweighted 3/4 must be 0.75");

    /* Defensive: NULL fsms or count = 0 */
    zassert_equal(compressor_fsm_calc_load_fraction(NULL, 4, nominal_caps), 0.0f,
                  "NULL fsms must return 0.0");
    zassert_equal(compressor_fsm_calc_load_fraction(fsms, 0, nominal_caps), 0.0f,
                  "Zero count must return 0.0");
}
