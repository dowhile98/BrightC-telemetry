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

    /* R-03: Recovery on T <= temp_min (e.g. 19.5 C <= temp_min of 20.0 C) */
    metrics.highest_temp = 19.5f;
    hvac_group_entity_evaluate(&s_group, &metrics);

    zassert_equal(s_group.bypass_causes_mask & (uint32_t)BYPASS_CAUSE_COOLING_TIMEOUT, 0U,
                  "BYPASS_CAUSE_COOLING_TIMEOUT must be cleared on T <= temp_min");
    zassert_equal(s_group.cooling_timeout_count, 0U, "cooling_timeout_count must be reset to 0");
    zassert_equal(s_group.cooling_timer_s, 0U, "cooling_timer_s must be reset to 0");
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "Group must return to GROUP_STATE_IDLE");

    /* Verify units are stopped and restored to smart mode (relay_sm = 1, relay_aa = 0) */
    for (uint8_t i = 0U; i < HVAC_MAX_UNITS_TOTAL; i++) {
        bool relay_aa, relay_sm, relay_g;
        hvac_unit_entity_get_relays(&s_units[i], &relay_aa, &relay_sm, &relay_g);
        zassert_true(relay_sm, "Unit %d relay_sm restored to 1", i);
        zassert_false(relay_aa, "Unit %d relay_aa must be 0", i);
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

    /* Temperature cools to 28.0 C (< temp_bypass, but > temp_max 24.0 C) */
    metrics.highest_temp = 28.0f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Bypass must remain active due to hysteresis at 28.0 C");
    zassert_true((s_group.bypass_causes_mask & (uint32_t)BYPASS_CAUSE_HIGH_TEMP) != 0U,
                 "Bypass flag must remain set");

    /* Temperature cools to 25.0 C (> temp_max 24.0 C) */
    metrics.highest_temp = 25.0f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Bypass must remain active at 25.0 C");

    /* Temperature drops to or below temp_max (23.5 C <= 24.0 C): Bypass clears! */
    metrics.highest_temp = 23.5f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "Bypass must clear when T <= temp_max");
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

/*============================================================================*
 * 7. Multi-Sensor Group Temperature Aggregation
 *============================================================================*/

ZTEST(hvac_group_suite, test_hvac_compute_group_metrics_multi_sensor)
{
    float temperatures[4] = { 23.0f, 26.5f, 24.5f, 99.0f };
    uint16_t sensor_mask = 0x0007U; /* Sensors 0, 1, 2 assigned to group */
    group_temperature_metrics_t metrics;

    /* 1. All assigned sensors healthy and valid (valid_mask = 0x0007) */
    uint16_t valid_mask = 0x0007U;
    int ret = hvac_compute_group_metrics(temperatures, valid_mask, 4U, sensor_mask, &metrics);
    zassert_equal(ret, 0, "hvac_compute_group_metrics failed");
    zassert_equal(metrics.total_sensors, 3U, "Total sensors must be 3");
    zassert_equal(metrics.valid_sensors, 3U, "Valid sensors must be 3");
    zassert_within(metrics.highest_temp, 26.5f, 0.001f, "Highest temp should be 26.5 C");
    zassert_within(metrics.lowest_temp, 23.0f, 0.001f, "Lowest temp should be 23.0 C");
    zassert_within(metrics.average_temp, 24.667f, 0.01f, "Average temp should be ~24.667 C");

    /* 2. Invalidate Sensor 1 (valid_mask = 0x0005) */
    valid_mask = 0x0005U;
    ret = hvac_compute_group_metrics(temperatures, valid_mask, 4U, sensor_mask, &metrics);
    zassert_equal(ret, 0, "hvac_compute_group_metrics failed");
    zassert_equal(metrics.total_sensors, 3U, "Total sensors must remain 3");
    zassert_equal(metrics.valid_sensors, 2U, "Valid sensors must be 2");
    zassert_within(metrics.highest_temp, 24.5f, 0.001f, "Highest temp should be 24.5 C");
    zassert_within(metrics.lowest_temp, 23.0f, 0.001f, "Lowest temp should be 23.0 C");
    zassert_within(metrics.average_temp, 23.75f, 0.001f, "Average temp should be 23.75 C");

    /* 3. Invalidate all assigned sensors (valid_mask = 0x0000) */
    valid_mask = 0x0000U;
    ret = hvac_compute_group_metrics(temperatures, valid_mask, 4U, sensor_mask, &metrics);
    zassert_equal(ret, 0, "hvac_compute_group_metrics failed");
    zassert_equal(metrics.total_sensors, 3U, "Total sensors must remain 3");
    zassert_equal(metrics.valid_sensors, 0U, "Valid sensors must be 0");
    zassert_within(metrics.highest_temp, 0.0f, 0.001f, "Highest temp should reset to 0.0 C");
    zassert_within(metrics.lowest_temp, 0.0f, 0.001f, "Lowest temp should reset to 0.0 C");
    zassert_within(metrics.average_temp, 0.0f, 0.001f, "Average temp should reset to 0.0 C");

    /* 4. Invalid arguments check */
    zassert_equal(hvac_compute_group_metrics(NULL, valid_mask, 4U, sensor_mask, &metrics), -EINVAL,
                  "NULL temperatures must return -EINVAL");
    zassert_equal(hvac_compute_group_metrics(temperatures, valid_mask, 4U, sensor_mask, NULL), -EINVAL,
                  "NULL metrics must return -EINVAL");
}

/*============================================================================*
 * 8. Multi-Room Range Engine & Metric Policy Tests (Phase 3)
 *============================================================================*/

ZTEST(hvac_group_suite, test_config_hvac_group_size_and_validation)
{
    /* Invariant: config_hvac_group_t must remain exactly 40 bytes aligned */
    zassert_equal(sizeof(config_hvac_group_t), 40U,
                  "sizeof(config_hvac_group_t) must be exactly 40 bytes");

    /* Policy validation */
    zassert_true(sensor_metric_policy_is_valid((uint8_t)SENSOR_METRIC_MAX), "MAX must be valid");
    zassert_true(sensor_metric_policy_is_valid((uint8_t)SENSOR_METRIC_AVG), "AVG must be valid");
    zassert_true(sensor_metric_policy_is_valid((uint8_t)SENSOR_METRIC_MIN), "MIN must be valid");
    zassert_false(sensor_metric_policy_is_valid(3U), "Policy 3 must be invalid");
    zassert_false(sensor_metric_policy_is_valid(255U), "Policy 255 must be invalid");

    /* Group config validation */
    config_hvac_group_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.temp_min = 20.0f;
    cfg.temp_max = 24.0f;
    cfg.temp_alarm = 28.0f;
    cfg.temp_bypass = 32.0f;
    cfg.hum_min = 30U;
    cfg.hum_max = 70U;
    cfg.active_turn_count = 2U;
    cfg.metric_policy = (uint8_t)SENSOR_METRIC_MAX;
    cfg.sensor_start = 1U;
    cfg.sensor_end = 5U;

    zassert_true(config_hvac_group_is_valid(&cfg), "Valid config should pass");

    /* Invalid metric policy */
    cfg.metric_policy = 3U;
    zassert_false(config_hvac_group_is_valid(&cfg), "Invalid metric policy must fail validation");
    cfg.metric_policy = (uint8_t)SENSOR_METRIC_MAX;

    /* Inverted range: sensor_start > sensor_end */
    cfg.sensor_start = 6U;
    cfg.sensor_end = 2U;
    zassert_false(config_hvac_group_is_valid(&cfg), "sensor_start > sensor_end must fail validation");

    /* Unassigned range (start = 0) */
    cfg.sensor_start = 0U;
    cfg.sensor_end = 0U;
    zassert_true(config_hvac_group_is_valid(&cfg), "sensor_start == 0 must be allowed (legacy mask mode)");
}

ZTEST(hvac_group_suite, test_hvac_compute_group_metrics_range)
{
    /* 10 temperature readings for flat IDs 1..10 (indices 0..9) */
    float temps[10] = { 20.0f, 22.0f, 24.0f, 26.0f, 28.0f, 30.0f, 32.0f, 34.0f, 36.0f, 38.0f };
    group_temperature_metrics_t metrics;

    /* Range IDs 2..4 -> indices 1..3: temps[1]=22.0, temps[2]=24.0, temps[3]=26.0 */
    /* Bitmask: bit 1, 2, 3 -> (1ULL << 1) | (1ULL << 2) | (1ULL << 3) */
    uint64_t valid_mask = (1ULL << 1) | (1ULL << 2) | (1ULL << 3);

    int ret = hvac_compute_group_metrics_range(temps, valid_mask, 10U, 2U, 4U, &metrics);
    zassert_equal(ret, 0, "hvac_compute_group_metrics_range failed");
    zassert_equal(metrics.total_sensors, 3U, "Total sensors must be 3");
    zassert_equal(metrics.valid_sensors, 3U, "Valid sensors must be 3");
    zassert_within(metrics.highest_temp, 26.0f, 0.001f, "Highest should be 26.0 C");
    zassert_within(metrics.lowest_temp, 22.0f, 0.001f, "Lowest should be 22.0 C");
    zassert_within(metrics.average_temp, 24.0f, 0.001f, "Average should be 24.0 C");

    /* Partial validity: invalidate ID 3 (index 2) -> only IDs 2 and 4 valid */
    valid_mask = (1ULL << 1) | (1ULL << 3);
    ret = hvac_compute_group_metrics_range(temps, valid_mask, 10U, 2U, 4U, &metrics);
    zassert_equal(ret, 0, "hvac_compute_group_metrics_range failed");
    zassert_equal(metrics.total_sensors, 3U, "Total sensors must remain 3");
    zassert_equal(metrics.valid_sensors, 2U, "Valid sensors must be 2");
    zassert_within(metrics.highest_temp, 26.0f, 0.001f, "Highest should be 26.0 C");
    zassert_within(metrics.lowest_temp, 22.0f, 0.001f, "Lowest should be 22.0 C");
    zassert_within(metrics.average_temp, 24.0f, 0.001f, "Average should be (22+26)/2 = 24.0 C");

    /* Zero valid sensors in range */
    valid_mask = 0ULL;
    ret = hvac_compute_group_metrics_range(temps, valid_mask, 10U, 2U, 4U, &metrics);
    zassert_equal(ret, 0, "hvac_compute_group_metrics_range failed");
    zassert_equal(metrics.total_sensors, 3U, "Total sensors must remain 3");
    zassert_equal(metrics.valid_sensors, 0U, "Valid sensors must be 0");
    zassert_within(metrics.highest_temp, 0.0f, 0.001f, "Highest should reset to 0.0 C");
    zassert_within(metrics.lowest_temp, 0.0f, 0.001f, "Lowest should reset to 0.0 C");
    zassert_within(metrics.average_temp, 0.0f, 0.001f, "Average should reset to 0.0 C");

    /* Defensive error checks */
    zassert_equal(hvac_compute_group_metrics_range(NULL, valid_mask, 10U, 2U, 4U, &metrics), -EINVAL,
                  "NULL temps must return -EINVAL");
    zassert_equal(hvac_compute_group_metrics_range(temps, valid_mask, 10U, 2U, 4U, NULL), -EINVAL,
                  "NULL metrics must return -EINVAL");
    zassert_equal(hvac_compute_group_metrics_range(temps, valid_mask, 10U, 0U, 4U, &metrics), -EINVAL,
                  "sensor_start == 0 must return -EINVAL");
    zassert_equal(hvac_compute_group_metrics_range(temps, valid_mask, 10U, 5U, 3U, &metrics), -EINVAL,
                  "sensor_end < sensor_start must return -EINVAL");
    zassert_equal(hvac_compute_group_metrics_range(temps, valid_mask, 10U, 1U, 65U, &metrics), -EINVAL,
                  "sensor_end > 64 must return -EINVAL");
}

ZTEST(hvac_group_suite, test_metric_policy_evaluation)
{
    /* Hot-spot scenario:
     * highest_temp = 29.0 C (>= temp_alarm 27.0 C)
     * average_temp = 23.0 C (<= temp_max 24.0 C, > temp_min 21.0 C)
     * lowest_temp  = 20.0 C (<= temp_min 21.0 C)
     */
    group_temperature_metrics_t metrics = {
        .highest_temp = 29.0f,
        .lowest_temp  = 20.0f,
        .average_temp = 23.0f,
        .valid_sensors = 3U,
        .total_sensors = 3U,
    };

    /* Policy 1: SENSOR_METRIC_MAX (Default) -> Hot-spot triggers COOL_BOOST */
    s_group.state = GROUP_STATE_IDLE;
    s_group.metric_policy = (uint8_t)SENSOR_METRIC_MAX;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOL_BOOST,
                  "MAX policy should trigger COOL_BOOST due to hot-spot (29.0 >= 27.0)");

    /* Policy 2: SENSOR_METRIC_AVG -> Average 23.0 is below temp_max (24.0) -> IDLE */
    s_group.state = GROUP_STATE_IDLE;
    s_group.metric_policy = (uint8_t)SENSOR_METRIC_AVG;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_IDLE,
                  "AVG policy: average 23.0 is below temp_max 24.0, should remain IDLE");

    /* Policy 2: SENSOR_METRIC_AVG with elevated average (25.5 C >= temp_max 24.0 C) -> COOLING */
    metrics.average_temp = 25.5f;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING,
                  "AVG policy: elevated average 25.5 should trigger COOLING");

    /* Policy 3: SENSOR_METRIC_MIN -> Lowest 20.0 is <= temp_min (21.0) -> IDLE */
    s_group.metric_policy = (uint8_t)SENSOR_METRIC_MIN;
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_IDLE,
                  "MIN policy: lowest 20.0 <= temp_min 21.0 should transition to IDLE");
}

/*============================================================================*
 * 9. Automatic Duty Rotation Interval & Timer Tests
 *============================================================================*/

ZTEST(hvac_group_suite, test_duty_rotation_timer_and_automatic_shift)
{
    /* Initialize group with 3 units total: 2 active, 1 standby */
    hvac_group_entity_init(&s_group, 0U);
    s_group.temp_min = 21.0f;
    s_group.temp_max = 24.0f;
    s_group.delay_on_s = 20U;
    s_group.active_turn_count = 2U;
    s_group.cooling_timeout_s = 0U; /* Disable cooling timeout for rotation timing test */

    for (uint8_t i = 0U; i < 3U; i++) {
        hvac_unit_entity_init(&s_units[i], i, 3U, 20U, 60U, 180U, true);
        hvac_group_entity_add_unit(&s_group, &s_units[i]);
    }

    /* Verify default rotation configuration */
    zassert_equal(hvac_group_entity_get_rotation_interval_days(&s_group), 7U,
                  "Default rotation interval must be 7 days");
    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), 0U,
                  "Initial rotation timer must be 0s");
    zassert_equal(s_group.current_turn_offset, 0U, "Initial offset must be 0");

    /* Put group into COOLING */
    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 1U,
    };
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group in COOLING");

    /* 7 days = 7 * 86400 = 604,800 seconds */
    const uint32_t rotation_target_s = 7U * 86400U;

    /* Advance 604,799 seconds */
    for (uint32_t t = 0U; t < (rotation_target_s - 1U); t++) {
        hvac_group_entity_tick_1s(&s_group);
    }

    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), rotation_target_s - 1U,
                  "Timer must have accumulated 604,799 seconds");
    zassert_equal(s_group.current_turn_offset, 0U,
                  "Duty shift must not have occurred yet at 604,799s");

    /* 604,800th tick: triggers automatic rotation of duty shift */
    hvac_group_entity_tick_1s(&s_group);

    zassert_equal(s_group.current_turn_offset, 1U,
                  "Automatic duty shift must advance current_turn_offset to 1");
    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), 0U,
                  "Rotation timer must reset to 0 after automatic shift");

    /* Verify units 1, 2 active, unit 0 standby */
    zassert_equal(s_units[2].state, UNIT_STATE_STARTING, "Unit 2 now active");
    zassert_true(s_units[0].state == UNIT_STATE_OFF || s_units[0].state == UNIT_STATE_STOPPING,
                 "Unit 0 now standby");

    /* Test manual rotation resets timer */
    /* Advance timer by 500 seconds */
    for (int t = 0; t < 500; t++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), 500U,
                  "Rotation timer must be 500s");

    /* Trigger manual shift */
    hvac_group_entity_rotate_shift(&s_group);
    zassert_equal(s_group.current_turn_offset, 2U, "Manual shift advances offset to 2");
    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), 0U,
                  "Manual rotation must reset rotation timer to 0");

    /* Test reconfiguring rotation interval (e.g. 15, 20, 30 days) */
    hvac_group_entity_set_rotation_interval_days(&s_group, 15U);
    zassert_equal(hvac_group_entity_get_rotation_interval_days(&s_group), 15U,
                  "Rotation interval must be 15 days");
    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), 0U,
                  "Setting rotation interval must reset timer to 0");

    /* Advance 15 days = 15 * 86400 = 1,296,000 seconds */
    const uint32_t rotation_15d_s = 15U * 86400U;
    for (uint32_t t = 0U; t < rotation_15d_s; t++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_group.current_turn_offset, 0U,
                  "15-day rotation must wrap offset back to 0");
    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), 0U,
                  "Timer must reset to 0 after 15-day rotation");

    /* Test disabled rotation (0 days) */
    hvac_group_entity_set_rotation_interval_days(&s_group, 0U);
    for (int t = 0; t < 1000; t++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(hvac_group_entity_get_rotation_timer(&s_group), 0U,
                  "Rotation timer must remain 0 when interval is 0");
    zassert_equal(s_group.current_turn_offset, 0U,
                  "Offset must not rotate when interval is 0");
}

/*============================================================================*
 * 7. Manual Override FSM and Safety Tests (R-13 & R-08)
 *============================================================================*/

ZTEST(hvac_group_suite, test_manual_off_does_not_timeout_or_energize)
{
    /* 1. Put group in active cooling */
    s_group.cooling_timeout_s = 50U;
    group_temperature_metrics_t metrics = {
        .highest_temp = 26.0f, /* >= temp_max (24.0) */
        .lowest_temp = 24.0f,
        .average_temp = 25.0f,
        .valid_sensors = 2U,
        .total_sensors = 2U,
    };
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group must be in COOLING");

    /* 2. Switch to manual mode ("mode off") */
    hvac_group_entity_set_manual(&s_group, true);
    zassert_equal(s_group.state, GROUP_STATE_MANUAL, "Group state must transition to GROUP_STATE_MANUAL");
    zassert_true(s_group.is_manual_active, "is_manual_active must be true");
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must be reset on entering manual");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Cooling timeout count must be reset");

    /* 3. Advance tick past cooling_timeout_s (e.g. 60 seconds) */
    for (int i = 0; i < 60; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }

    /* Verify cooling timeout did NOT advance and rescue boost was NOT triggered */
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must remain 0 in manual mode");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must remain 0");
    zassert_equal(s_group.state, GROUP_STATE_MANUAL, "Group must remain in GROUP_STATE_MANUAL");

    /* Units must NOT have been energized by rescue boost */
    for (uint8_t i = 1U; i < s_group.unit_count; i++) {
        zassert_not_equal(s_units[i].state, UNIT_STATE_RUNNING, "Standby unit must not be RUNNING");
    }
}

ZTEST(hvac_group_suite, test_manual_set_does_not_energize_all_units)
{
    /* Initial state: idle, all units off */
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "Initial state IDLE");
    for (uint8_t i = 0U; i < s_group.unit_count; i++) {
        zassert_equal(s_units[i].state, UNIT_STATE_OFF, "Unit must be OFF initially");
    }

    /* Calling set_manual(true) must NOT turn on all units (preventing simultaneous inrush) */
    hvac_group_entity_set_manual(&s_group, true);
    zassert_equal(s_group.state, GROUP_STATE_MANUAL, "Group state must be GROUP_STATE_MANUAL");

    for (uint8_t i = 0U; i < s_group.unit_count; i++) {
        zassert_not_equal(s_units[i].state, UNIT_STATE_RUNNING,
                          "Unit %u must NOT be forced to RUNNING by group set_manual", i);
        zassert_not_equal(s_units[i].state, UNIT_STATE_STARTING,
                          "Unit %u must NOT be STARTING", i);
    }
}

ZTEST(hvac_group_suite, test_manual_expiry_releases_units_and_returns_to_idle)
{
    s_group.manual_timeout_s = 5U;
    hvac_group_entity_set_manual(&s_group, true);
    zassert_equal(s_group.state, GROUP_STATE_MANUAL, "Group state must be GROUP_STATE_MANUAL");
    zassert_true(s_group.is_manual_active, "is_manual_active must be true");

    /* Tick for 5 seconds to expire manual timer */
    for (int i = 0; i < 5; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }

    /* After expiry: is_manual_active is false and state transitions to IDLE */
    zassert_false(s_group.is_manual_active, "Manual mode must expire");
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "State must revert to GROUP_STATE_IDLE");
}

ZTEST(hvac_group_suite, test_cooling_timers_reset_on_manual_fire_and_bypass_transitions)
{
    /* 1. Enter COOLING */
    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .lowest_temp = 23.0f,
        .average_temp = 24.0f,
        .valid_sensors = 2U,
        .total_sensors = 2U,
    };
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group must be in COOLING");

    /* Advance cooling timer by 30 seconds */
    for (int i = 0; i < 30; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_group.cooling_timer_s, 30U, "Cooling timer should be 30s");

    /* 2. Transition to MANUAL: cooling timers must be reset to 0 */
    hvac_group_entity_set_manual(&s_group, true);
    zassert_equal(s_group.state, GROUP_STATE_MANUAL, "Must be in MANUAL");
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must be reset on manual");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must be reset on manual");

    /* Clear manual, re-enter cooling and advance timer */
    hvac_group_entity_set_manual(&s_group, false);
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Must be back in COOLING");
    for (int i = 0; i < 40; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_group.cooling_timer_s, 40U, "Cooling timer should be 40s");

    /* 3. Transition to FIRE: cooling timers must be reset to 0 */
    hvac_group_entity_set_fire(&s_group, true);
    zassert_equal(s_group.state, GROUP_STATE_FIRE, "Must be in FIRE");
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must be reset on fire");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must be reset on fire");

    /* Clear fire alarm: timers must remain reset */
    hvac_group_entity_set_fire(&s_group, false);
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must remain 0 after fire clear");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must remain 0 after fire clear");

    /* Re-enter cooling and advance timer */
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Must be back in COOLING");
    for (int i = 0; i < 40; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_group.cooling_timer_s, 40U, "Cooling timer should be 40s");

    /* 4. Transition to BYPASS: cooling timers must be reset to 0 */
    hvac_group_entity_set_bypass_cause(&s_group, BYPASS_CAUSE_HIGH_TEMP, true);
    zassert_equal(s_group.state, GROUP_STATE_BYPASS, "Must be in BYPASS");
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must be reset on bypass");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must be reset on bypass");

    /* Clear bypass */
    hvac_group_entity_set_bypass_cause(&s_group, BYPASS_CAUSE_HIGH_TEMP, false);
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must be 0 after bypass clear");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must be 0 after bypass clear");

    /* Re-enter cooling and advance timer */
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Must be back in COOLING");
    for (int i = 0; i < 40; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }
    zassert_equal(s_group.cooling_timer_s, 40U, "Cooling timer should be 40s");

    /* 5. Drop temp to temp_min -> IDLE: cooling timers must be reset to 0 */
    group_temperature_metrics_t cold_metrics = {
        .highest_temp = 20.0f, /* <= temp_min (21.0f) */
        .lowest_temp = 19.0f,
        .average_temp = 19.5f,
        .valid_sensors = 2U,
        .total_sensors = 2U,
    };
    hvac_group_entity_evaluate(&s_group, &cold_metrics);
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "Must transition to IDLE");
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must be reset on IDLE transition");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must be reset on IDLE transition");
}

/*============================================================================*
 * Fault-Aware Standby Substitution in N+M Redundancy (Observation 1)
 *============================================================================*/

ZTEST(hvac_group_suite, test_standby_substitution_on_active_unit_fault)
{
    /* Configure a 2+2 redundancy group (4 units total, 2 active turn, 2 standby) */
    hvac_group_entity_clear_units(&s_group);
    for (uint8_t i = 0U; i < 4U; i++) {
        hvac_unit_entity_init(&s_units[i], i, 3U, 20U, 60U, 180U, true);
        hvac_group_entity_add_unit(&s_group, &s_units[i]);
    }
    s_group.active_turn_count = 2U;
    s_group.current_turn_offset = 0U;
    s_group.delay_on_s = 20U;

    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f, /* >= temp_max (24.0) -> GROUP_STATE_COOLING */
        .lowest_temp = 23.0f,
        .average_temp = 24.0f,
        .valid_sensors = 2U,
        .total_sensors = 2U,
    };

    /* 1. Normal cooling evaluation: Units 0 and 1 start; Units 2 and 3 remain standby (OFF) */
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group must be in COOLING");
    zassert_equal(s_units[0].state, UNIT_STATE_RUNNING, "Unit 0 (delay 0) must be RUNNING");
    zassert_equal(s_units[1].state, UNIT_STATE_STARTING, "Unit 1 (delay 20) must be STARTING");
    zassert_equal(s_units[2].state, UNIT_STATE_OFF, "Unit 2 must be standby (OFF)");
    zassert_equal(s_units[3].state, UNIT_STATE_OFF, "Unit 3 must be standby (OFF)");

    /* 2. Unit 0 trips on high-pressure alarm -> transitions to BLOCKED_ALARM */
    hvac_unit_entity_update_alarms(&s_units[0], true, false, false, false);
    zassert_equal(s_units[0].state, UNIT_STATE_BLOCKED_ALARM, "Unit 0 must be in BLOCKED_ALARM");

    /* 3. Re-evaluate group: Unit 0 is unavailable, so Unit 1 and Unit 2 (standby) must be commanded */
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group must remain in COOLING");
    zassert_equal(s_units[0].state, UNIT_STATE_BLOCKED_ALARM, "Unit 0 stays in BLOCKED_ALARM");
    zassert_true(s_units[1].state == UNIT_STATE_RUNNING || s_units[1].state == UNIT_STATE_STARTING,
                 "Unit 1 must be active");
    zassert_true(s_units[2].state == UNIT_STATE_RUNNING || s_units[2].state == UNIT_STATE_STARTING,
                 "Unit 2 (standby substitute) must be commanded to START");
    zassert_equal(s_units[3].state, UNIT_STATE_OFF, "Unit 3 must remain standby (OFF)");
}

/*============================================================================*
 * Permanent Mode OFF Behavior (Observation 2)
 *============================================================================*/

ZTEST(hvac_group_suite, test_permanent_mode_off_does_not_timeout_or_revert)
{
    hvac_group_entity_clear_units(&s_group);
    for (uint8_t i = 0U; i < 4U; i++) {
        hvac_unit_entity_init(&s_units[i], i, 3U, 20U, 60U, 180U, false);
        hvac_group_entity_add_unit(&s_group, &s_units[i]);
    }
    s_group.active_turn_count = 2U;
    s_group.cooling_timeout_s = 50U;
    s_group.manual_timeout_s = 60U;

    group_temperature_metrics_t metrics = {
        .highest_temp = 26.0f,
        .lowest_temp = 24.0f,
        .average_temp = 25.0f,
        .valid_sensors = 2U,
        .total_sensors = 2U,
    };

    /* 1. Enter active cooling */
    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group must be in COOLING");
    zassert_true(s_units[0].state == UNIT_STATE_RUNNING || s_units[0].state == UNIT_STATE_STARTING, "Unit 0 active");
    zassert_true(s_units[1].state == UNIT_STATE_RUNNING || s_units[1].state == UNIT_STATE_STARTING, "Unit 1 active");

    /* 2. Set permanent Mode OFF */
    hvac_group_entity_set_mode_off(&s_group, true);
    zassert_equal(s_group.state, GROUP_STATE_OFF, "Group must be in GROUP_STATE_OFF");
    zassert_true(s_group.is_mode_off, "is_mode_off must be true");
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must be 0");
    zassert_equal(s_group.cooling_timeout_count, 0U, "Timeout count must be 0");
    zassert_equal(s_group.manual_timer_s, 0U, "Manual timer must be 0");

    for (uint8_t u = 0U; u < 4U; u++) {
        zassert_true(s_units[u].state == UNIT_STATE_OFF || s_units[u].state == UNIT_STATE_STOPPING,
                     "Unit %u must be stopped", u);
    }

    /* 3. Advance tick by 5000 seconds (far beyond manual_timeout_s and cooling_timeout_s) */
    for (int i = 0; i < 5000; i++) {
        hvac_group_entity_tick_1s(&s_group);
    }

    /* Re-evaluate with hot metrics */
    hvac_group_entity_evaluate(&s_group, &metrics);

    /* 4. Verify group strictly remains in GROUP_STATE_OFF and does NOT turn on units */
    zassert_equal(s_group.state, GROUP_STATE_OFF, "Group must strictly remain in GROUP_STATE_OFF after 5000s");
    zassert_true(s_group.is_mode_off, "is_mode_off must remain true");
    zassert_equal(s_group.cooling_timer_s, 0U, "Cooling timer must remain 0");
    zassert_equal(s_group.manual_timer_s, 0U, "Manual timer must remain 0");

    for (uint8_t u = 0U; u < 4U; u++) {
        zassert_equal(s_units[u].state, UNIT_STATE_OFF, "Unit %u must remain OFF", u);
    }

    /* 5. Clear mode OFF -> transitions to GROUP_STATE_IDLE and can resume cooling */
    hvac_group_entity_set_mode_off(&s_group, false);
    zassert_false(s_group.is_mode_off, "is_mode_off must be false");
    zassert_equal(s_group.state, GROUP_STATE_IDLE, "Group must transition to IDLE");

    hvac_group_entity_evaluate(&s_group, &metrics);
    zassert_equal(s_group.state, GROUP_STATE_COOLING, "Group must resume COOLING on hot metrics");
}



