/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_hvac_group.c
 * @brief Unit tests for HVAC group entity: generic N+M rotation, staggered delay,
 * cooling timeout rescue, bypass hysteresis, fire interlock, and sensor faults.
 */

#include <zephyr/ztest.h>
#include <errno.h>

#include "common/hvac_types.h"
#include "domain/models/hvac_unit_entity.h"
#include "domain/models/hvac_group_entity.h"

static hvac_unit_entity_t s_units[HVAC_MAX_UNITS_TOTAL];
static hvac_group_entity_t s_group;

static void hvac_group_before(void *data)
{
    ARG_UNUSED(data);

    hvac_group_entity_init(&s_group, 0U);
    s_group.temp_min = 21.0f;
    s_group.temp_max = 24.0f;
    s_group.temp_alarm = 27.0f;
    s_group.temp_bypass = 32.0f;
    s_group.delay_on_s = 20U;
    s_group.cooling_timeout_s = 100U;
    s_group.active_turn_count = 1U;
    s_group.enable_fan_delay = true;

    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        hvac_unit_entity_init(&s_units[i], i, 3U, 20U, 60U, 180U, true);
        hvac_group_entity_add_unit(&s_group, &s_units[i]);
    }
}

ZTEST_SUITE(hvac_group_suite, NULL, NULL, hvac_group_before, NULL, NULL);

/*============================================================================*
 * 1. Generic N+M Redundancy Rotation Tests (7+1, 6+2, 2+1, circular shifting)
 *============================================================================*/

ZTEST(hvac_group_suite, test_generic_n_plus_m_rotation_7_1)
{
    /* 8 units total, 7 active in turn (7+1 scheme) */
    s_group.active_turn_count = 7U;

    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f, /* >= temp_max (24.0) -> GROUP_STATE_COOLING */
        .lowest_temp = 23.0f,
        .average_temp = 24.0f,
        .valid_sensors = 2U,
        .total_sensors = 2U,
    };

    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group should be in COOLING");

    /* Initial turn offset 0: Units 0..6 active, Unit 7 standby */
    zassert_equal(s_units[0].state, UNIT_STATE_RUNNING, "Unit 0 (delay 0) must be RUNNING");
    for (int i = 1; i <= 6; i++) {
        zassert_equal(s_units[i].state, UNIT_STATE_STARTING, "Unit %d must be STARTING", i);
    }
    zassert_equal(s_units[7].state, UNIT_STATE_OFF, "Unit 7 must be standby (OFF)");

    /* Circular duty shift rotation */
    hvac_group_entity_rotate_shift(&s_group);
    zassert_equal(s_group.current_turn_offset, 1U, "Offset must advance to 1");

    /* After rotation: Units 1..7 active, Unit 0 standby */
    zassert_equal(s_units[7].state, UNIT_STATE_STARTING, "Unit 7 was standby, now active STARTING");
    zassert_true(s_units[0].state == UNIT_STATE_OFF || s_units[0].state == UNIT_STATE_STOPPING,
                 "Unit 0 must be commanded to stop (standby)");

    /* Rotate again: offset 2 -> Units 2..7 and 0 active, Unit 1 standby */
    hvac_group_entity_rotate_shift(&s_group);
    zassert_equal(s_group.current_turn_offset, 2U, "Offset must advance to 2");
    zassert_true(s_units[1].state == UNIT_STATE_OFF || s_units[1].state == UNIT_STATE_STOPPING,
                 "Unit 1 must now be standby");
}

ZTEST(hvac_group_suite, test_generic_n_plus_m_rotation_6_2)
{
    /* 8 units total, 6 active in turn (6+2 scheme) */
    s_group.active_turn_count = 6U;

    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 2U,
    };

    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group in COOLING");

    /* Units 0..5 active, Units 6..7 standby */
    zassert_equal(s_units[0].state, UNIT_STATE_RUNNING, "Unit 0 active");
    for (int i = 1; i <= 5; i++) {
        zassert_equal(s_units[i].state, UNIT_STATE_STARTING, "Unit %d active", i);
    }
    zassert_equal(s_units[6].state, UNIT_STATE_OFF, "Unit 6 standby");
    zassert_equal(s_units[7].state, UNIT_STATE_OFF, "Unit 7 standby");

    /* Shift rotation */
    hvac_group_entity_rotate_shift(&s_group);
    zassert_equal(s_group.current_turn_offset, 1U, "Offset = 1");

    /* Units 1..6 active, Units 7 and 0 standby */
    zassert_equal(s_units[6].state, UNIT_STATE_STARTING, "Unit 6 now active");
    zassert_equal(s_units[7].state, UNIT_STATE_OFF, "Unit 7 still standby");
    zassert_true(s_units[0].state == UNIT_STATE_OFF || s_units[0].state == UNIT_STATE_STOPPING,
                 "Unit 0 now standby");
}

ZTEST(hvac_group_suite, test_generic_n_plus_m_rotation_2_1)
{
    /* Reconfigure with 3 units total: 2 active, 1 standby (2+1 scheme) */
    hvac_group_entity_init(&s_group, 1U);
    s_group.temp_min = 21.0f;
    s_group.temp_max = 24.0f;
    s_group.delay_on_s = 20U;
    s_group.active_turn_count = 2U;

    for (uint8_t i = 0U; i < 3U; i++) {
        hvac_unit_entity_init(&s_units[i], i, 3U, 20U, 60U, 180U, true);
        hvac_group_entity_add_unit(&s_group, &s_units[i]);
    }

    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 1U,
    };

    hvac_group_entity_evaluate(&s_group, &metrics);

    /* Offset 0: Units 0, 1 active; Unit 2 standby */
    zassert_equal(s_units[0].state, UNIT_STATE_RUNNING, "Unit 0 active");
    zassert_equal(s_units[1].state, UNIT_STATE_STARTING, "Unit 1 active");
    zassert_equal(s_units[2].state, UNIT_STATE_OFF, "Unit 2 standby");

    /* Offset 1: Units 1, 2 active; Unit 0 standby */
    hvac_group_entity_rotate_shift(&s_group);
    zassert_equal(s_group.current_turn_offset, 1U, "Offset = 1");
    zassert_equal(s_units[2].state, UNIT_STATE_STARTING, "Unit 2 active");
    zassert_true(s_units[0].state == UNIT_STATE_OFF || s_units[0].state == UNIT_STATE_STOPPING, "Unit 0 standby");

    /* Offset 2: Units 2, 0 active; Unit 1 standby */
    hvac_group_entity_rotate_shift(&s_group);
    zassert_equal(s_group.current_turn_offset, 2U, "Offset = 2");
    zassert_true(s_units[1].state == UNIT_STATE_OFF || s_units[1].state == UNIT_STATE_STOPPING, "Unit 1 standby");
}

/*============================================================================*
 * 2. Staggered Anti-Inrush Delays (unit i gets i * delay_on_s)
 *============================================================================*/

ZTEST(hvac_group_suite, test_staggered_anti_inrush_delays)
{
    s_group.active_turn_count = 3U;
    s_group.delay_on_s = 20U;

    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 2U,
    };

    hvac_group_entity_evaluate(&s_group, &metrics);

    /* Unit 0: 0 * 20 = 0s -> RUNNING immediately */
    zassert_equal(s_units[0].state, UNIT_STATE_RUNNING, "Unit 0 starts immediately");
    zassert_equal(s_units[0].startup_timer_s, 0U, "Unit 0 timer is 0");

    /* Unit 1: 1 * 20 = 20s -> STARTING */
    zassert_equal(s_units[1].state, UNIT_STATE_STARTING, "Unit 1 starting");
    zassert_equal(s_units[1].startup_timer_s, 20U, "Unit 1 delay is 20s");

    /* Unit 2: 2 * 20 = 40s -> STARTING */
    zassert_equal(s_units[2].state, UNIT_STATE_STARTING, "Unit 2 starting");
    zassert_equal(s_units[2].startup_timer_s, 40U, "Unit 2 delay is 40s");

    /* Tick 19 seconds */
    for (int i = 0; i < 19; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_units[1].state, UNIT_STATE_STARTING, "Unit 1 still starting at 19s");
    zassert_equal(s_units[1].startup_timer_s, 1U, "Unit 1 has 1s remaining");
    zassert_equal(s_units[2].state, UNIT_STATE_STARTING, "Unit 2 still starting at 19s");
    zassert_equal(s_units[2].startup_timer_s, 21U, "Unit 2 has 21s remaining");

    /* 20th second tick: Unit 1 enters RUNNING */
    hvac_group_entity_tick_1s(&s_group);
    zassert_equal(s_units[1].state, UNIT_STATE_RUNNING, "Unit 1 must be RUNNING at 20s");
    zassert_equal(s_units[2].state, UNIT_STATE_STARTING, "Unit 2 still starting at 20s");
    zassert_equal(s_units[2].startup_timer_s, 20U, "Unit 2 has 20s remaining");

    /* 20 more ticks (40s total): Unit 2 enters RUNNING */
    for (int i = 0; i < 20; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_units[2].state, UNIT_STATE_RUNNING, "Unit 2 must be RUNNING at 40s");
}

/*============================================================================*
 * 3. Cooling Timeout & Rescue Boost
 *============================================================================*/

ZTEST(hvac_group_suite, test_cooling_timeout_and_rescue_boost)
{
    s_group.active_turn_count = 2U;
    s_group.cooling_timeout_s = 50U;

    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 2U,
    };

    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group in COOLING");

    /* Tick 49 seconds without reaching temp_min */
    for (int i = 0; i < 49; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Still in COOLING at 49s");
    zassert_equal(s_group.cooling_timeout_count, 0U, "No timeout yet");

    /* 50th second: First timeout attempt */
    hvac_group_entity_tick_1s(&s_group);
    zassert_equal(s_group.cooling_timeout_count, 1U, "First timeout registered");
    zassert_equal(s_group.state, GROUP_STATE_COOL_BOOST, "First timeout triggers COOL_BOOST rescue");

    /* Verify active units 0 and 1 got fault counter incremented */
    zassert_equal(s_units[0].fault_counter, 1U, "Active unit 0 fault incremented");
    zassert_equal(s_units[1].fault_counter, 1U, "Active unit 1 fault incremented");

    /* Verify rescue boost energized ALL units */
    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        zassert_true(s_units[i].state == UNIT_STATE_RUNNING || s_units[i].state == UNIT_STATE_STARTING ||
                     s_units[i].state == UNIT_STATE_BLOCKED_ALARM,
                     "Unit %d was commanded to rescue", i);
    }

    /* Second cooling cycle: tick 49 more seconds */
    for (int i = 0; i < 49; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_group.cooling_timeout_count, 1U, "Still on first timeout count at 49s");

    /* 50th tick of second cycle (second timeout): trips BYPASS_CAUSE_COOLING_TIMEOUT */
    hvac_group_entity_tick_1s(&s_group);
    zassert_equal(s_group.cooling_timeout_count, 2U, "Second timeout registered");
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Second timeout must trigger GROUP_STATE_BYPASS");
    zassert_true((s_group.bypass_causes_mask & (uint32_t)BYPASS_CAUSE_COOLING_TIMEOUT) != 0U,
                 "BYPASS_CAUSE_COOLING_TIMEOUT bit must be set");

    /* Verify all units transferred to bypass (relay_sm = 0) */
    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        bool relay_aa, relay_sm, relay_g;
        hvac_unit_entity_get_relays(&s_units[i], &relay_aa, &relay_sm, &relay_g);
        zassert_false(relay_sm, "Unit %d relay_sm must be 0 in bypass", i);
    }
}

/*============================================================================*
 * 4. High-Temperature Bypass Trigger and Hysteresis
 *============================================================================*/

ZTEST(hvac_group_suite, test_high_temp_bypass_and_hysteresis)
{
    group_temperature_metrics_t metrics = {
        .highest_temp = 32.5f, /* >= temp_bypass (32.0) */
        .valid_sensors = 2U,
    };

    /* High temperature trips bypass */
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Group must be in BYPASS");
    zassert_true((s_group.bypass_causes_mask & (uint32_t)BYPASS_CAUSE_HIGH_TEMP) != 0U,
                 "BYPASS_CAUSE_HIGH_TEMP bit set");

    /* Temperature cools to 28.0 C (< temp_bypass, but > temp_min 21.0 C) */
    metrics.highest_temp = 28.0f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Bypass must remain active due to hysteresis at 28.0 C");
    zassert_true((s_group.bypass_causes_mask & (uint32_t)BYPASS_CAUSE_HIGH_TEMP) != 0U,
                 "Bypass flag must remain set");

    /* Temperature cools to 23.0 C (> temp_min) */
    metrics.highest_temp = 23.0f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Bypass must remain active at 23.0 C");

    /* Temperature reaches temp_min (20.5 C <= 21.0 C): Bypass clears! */
    metrics.highest_temp = 20.5f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "Bypass must clear when T <= temp_min");
    zassert_equal(s_group.bypass_causes_mask, 0U, "All bypass flags must be cleared");

    /* Verify units restored to smart mode (relay_sm = 1) */
    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        bool relay_aa, relay_sm, relay_g;
        hvac_unit_entity_get_relays(&s_units[i], &relay_aa, &relay_sm, &relay_g);
        zassert_true(relay_sm, "Unit %d relay_sm restored to 1", i);
    }
}

/*============================================================================*
 * 5. Fire Emergency Interlock Priority
 *============================================================================*/

ZTEST(hvac_group_suite, test_fire_emergency_priority)
{
    group_temperature_metrics_t metrics = {
        .highest_temp = 26.0f,
        .valid_sensors = 2U,
    };

    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group in COOLING");

    /* Trigger emergency fire interlock */
    hvac_group_entity_set_fire(&s_group, true);
    zassert_equal(s_group.state, GROUP_STATE_FIRE, "Group must transition to GROUP_STATE_FIRE");
    zassert_true(s_group.is_fire_active, "Fire active flag must be set");

    /* Verify all compressors and fan purge relays are immediately shut down */
    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        bool relay_aa, relay_sm, relay_g;
        hvac_unit_entity_get_relays(&s_units[i], &relay_aa, &relay_sm, &relay_g);
        zassert_false(relay_aa, "Unit %d compressor must be OFF during fire", i);
        zassert_false(relay_g, "Unit %d fan relay must be OFF during fire", i);
    }

    /* Fire interlock overrides even extreme high temperature bypass */
    metrics.highest_temp = 45.0f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_FIRE, "Fire must strictly override high temperature bypass");

    /* Releasing fire alarm restores normal sequencing */
    hvac_group_entity_set_fire(&s_group, false);
    zassert_false(s_group.is_fire_active, "Fire cleared");
}

/*============================================================================*
 * 6. Sensor Fault Bypass (valid_sensors == 0)
 *============================================================================*/

ZTEST(hvac_group_suite, test_sensor_fault_bypass)
{
    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 0U, /* Sensor fault! */
        .total_sensors = 2U,
    };

    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Zero valid sensors must trip BYPASS");
    zassert_true((s_group.bypass_causes_mask & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT) != 0U,
                 "BYPASS_CAUSE_SENSOR_FAULT must be set");

    /* Relays in bypass */
    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        bool relay_aa, relay_sm, relay_g;
        hvac_unit_entity_get_relays(&s_units[i], &relay_aa, &relay_sm, &relay_g);
        zassert_false(relay_sm, "relay_sm must be 0 in sensor fault bypass");
    }

    /* Sensor health restored */
    metrics.valid_sensors = 2U;
    metrics.highest_temp = 20.0f; /* <= temp_min */
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "Restoring healthy sensors restores normal state");
    zassert_equal(s_group.bypass_causes_mask, 0U, "Sensor fault bypass cleared");
}
