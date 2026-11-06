/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for ao_hvac (HVAC Active Object coordinator).
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include <zephyr/zbus/zbus.h>

#include "application/active_objects/ao_hvac.h"
#include "application/zbus/hvac_channels.h"
#include "common/hvac_types.h"
#include "fake_hvac_unit.h"

static ao_hvac_t s_ao;
static struct k_msgq s_queue;
static uint8_t s_queue_buf[AO_HVAC_QUEUE_DEPTH * sizeof(ao_hvac_event_t)];
static K_THREAD_STACK_DEFINE(s_test_stack, AO_HVAC_STACK_SIZE);
static i_hvac_unit_t s_actuator_port;

static void setup_fixture(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_hvac_unit_reset();
    s_actuator_port = fake_hvac_unit_get_port();
    k_msgq_init(&s_queue, (char *)s_queue_buf, sizeof(ao_hvac_event_t), AO_HVAC_QUEUE_DEPTH);
}

static void teardown_fixture(void *fixture)
{
    ARG_UNUSED(fixture);
    if (ao_hvac_is_running(&s_ao)) {
        (void)ao_hvac_stop(&s_ao);
    }
    if (s_ao.is_initialized) {
        (void)ao_hvac_deinit(&s_ao);
    }
}

ZTEST_SUITE(ao_hvac_suite, NULL, NULL, setup_fixture, teardown_fixture, NULL);

ZTEST(ao_hvac_suite, test_5_phase_lifecycle)
{
    /* 1. Init */
    int ret = ao_hvac_init(&s_ao, NULL, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init failed");
    zassert_true(s_ao.is_initialized, "Must be marked initialized");
    zassert_false(ao_hvac_is_running(&s_ao), "Should not be running yet");

    /* 2. Start */
    ret = ao_hvac_start(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_start failed");
    zassert_true(ao_hvac_is_running(&s_ao), "Should be running");

    /* Calling start again returns -EALREADY */
    ret = ao_hvac_start(&s_ao);
    zassert_equal(ret, -EALREADY, "Duplicate start should return -EALREADY");

    /* 3. Step */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");

    /* 4. Stop */
    ret = ao_hvac_stop(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_stop failed");
    zassert_false(ao_hvac_is_running(&s_ao), "Should be stopped");

    /* 5. Deinit */
    ret = ao_hvac_deinit(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_deinit failed");
    zassert_false(s_ao.is_initialized, "Must be deinitialized");
}

ZTEST(ao_hvac_suite, test_thermal_evaluation_and_actuation)
{
    config_hvac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.total_groups = 1U;
    cfg.total_units = 4U;

    cfg.groups[0].temp_min = 20.0f;
    cfg.groups[0].temp_max = 24.0f;
    cfg.groups[0].temp_alarm = 28.0f;
    cfg.groups[0].temp_bypass = 32.0f;
    cfg.groups[0].delay_on_s = 0U; /* Immediate start for deterministic unit test */
    cfg.groups[0].unit_mask = 0x0FU; /* Units 0, 1, 2, 3 */
    cfg.groups[0].active_turn_count = 2U; /* 2 units active (N=2, M=2) */
    cfg.groups[0].max_allowed_faults = 3U;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "init failed");

    /* Set room temperature low: T = 19.0 C <= temp_min */
    group_temperature_metrics_t metrics = {
        .highest_temp = 19.0f,
        .lowest_temp = 18.5f,
        .average_temp = 18.8f,
        .valid_sensors = 2U,
        .total_sensors = 2U,
    };
    ret = ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    zassert_equal(ret, 0, "set_group_metrics failed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    /* Group should be IDLE, units OFF */
    hvac_status_msg_t status;
    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.group_state[0], GROUP_STATE_IDLE, "Group should be IDLE");
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 should be OFF");
    zassert_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 should be OFF");

    /* Now raise temperature: T = 25.5 C >= temp_max (24.0 C) */
    metrics.highest_temp = 25.5f;
    ret = ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    zassert_equal(ret, 0, "set_group_metrics failed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    /* With delay_on_s = 0, units 0 and 1 start immediately */
    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.group_state[0], GROUP_STATE_COOLING, "Group should be COOLING");
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");
    zassert_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_ON, "Unit 1 should be ON");
    zassert_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_OFF, "Unit 2 should be standby (OFF)");
    zassert_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 should be standby (OFF)");
}

ZTEST(ao_hvac_suite, test_fire_emergency_priority)
{
    config_hvac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.total_groups = 1U;
    cfg.total_units = 2U;
    cfg.groups[0].temp_min = 20.0f;
    cfg.groups[0].temp_max = 24.0f;
    cfg.groups[0].temp_alarm = 28.0f;
    cfg.groups[0].temp_bypass = 32.0f;
    cfg.groups[0].delay_on_s = 0U;
    cfg.groups[0].unit_mask = 0x03U;
    cfg.groups[0].active_turn_count = 2U;

    (void)ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));

    /* Start cooling */
    group_temperature_metrics_t metrics = {
        .highest_temp = 26.0f,
        .valid_sensors = 1U,
        .total_sensors = 1U,
    };
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    (void)ao_hvac_step(&s_ao);
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");

    /* Trigger fire emergency */
    int ret = ao_hvac_set_fire(&s_ao, true);
    zassert_equal(ret, 0, "set_fire failed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    hvac_status_msg_t status;
    (void)ao_hvac_get_status(&s_ao, &status);
    zassert_equal(status.group_state[0], GROUP_STATE_FIRE, "Group must be in FIRE state");
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must shut down immediately");
    zassert_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 must shut down immediately");
}

ZTEST(ao_hvac_suite, test_unit_alarm_and_zbus_publishing)
{
    config_hvac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.total_groups = 1U;
    cfg.total_units = 2U;
    cfg.groups[0].temp_min = 20.0f;
    cfg.groups[0].temp_max = 24.0f;
    cfg.groups[0].temp_alarm = 28.0f;
    cfg.groups[0].temp_bypass = 32.0f;
    cfg.groups[0].delay_on_s = 0U;
    cfg.groups[0].unit_mask = 0x03U;
    cfg.groups[0].active_turn_count = 2U;

    (void)ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));

    /* Start cooling */
    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 1U,
        .total_sensors = 1U,
    };
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    (void)ao_hvac_step(&s_ao);
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 running");

    /* Unit 0 trips High Pressure alarm */
    int ret = ao_hvac_set_unit_alarms(&s_ao, 0, true, false, false, false);
    zassert_equal(ret, 0, "set_unit_alarms failed");

    (void)ao_hvac_step(&s_ao);

    /* Unit 0 must shut down */
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must shut down due to alarm");

    /* Verify Zbus channel publication */
    hvac_status_msg_t zbus_msg;
    ret = zbus_chan_read(&chan_hvac_status, &zbus_msg, K_MSEC(100));
    zassert_equal(ret, 0, "Failed to read from chan_hvac_status");
    zassert_equal(zbus_msg.unit_tri_state[0], HVAC_TRI_STATE_OFF, "Zbus snapshot must show Unit 0 OFF");
    zassert_equal(zbus_msg.unit_tri_state[1], HVAC_TRI_STATE_ON, "Zbus snapshot must show Unit 1 ON");
}

ZTEST(ao_hvac_suite, test_ao_hvac_runtime_group_config_update)
{
    config_hvac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.total_groups = 1U;
    cfg.total_units = 2U;
    cfg.groups[0].temp_min = 20.0f;
    cfg.groups[0].temp_max = 26.0f;
    cfg.groups[0].temp_alarm = 28.0f;
    cfg.groups[0].temp_bypass = 32.0f;
    cfg.groups[0].delay_on_s = 0U;
    cfg.groups[0].unit_mask = 0x03U;
    cfg.groups[0].active_turn_count = 1U;
    cfg.groups[0].max_allowed_faults = 3U;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "init failed");

    /* Temperature 25.0 C is below current temp_max (26.0 C) -> remains IDLE */
    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 1U,
        .total_sensors = 1U,
    };
    ret = ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    zassert_equal(ret, 0, "set_group_metrics failed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must be OFF (idle)");

    /* Dynamically lower temp_max to 24.0 C via ao_hvac_set_group_config */
    config_hvac_group_t updated_group = cfg.groups[0];
    updated_group.temp_max = 24.0f;

    ret = ao_hvac_set_group_config(&s_ao, 0, &updated_group);
    zassert_equal(ret, 0, "ao_hvac_set_group_config failed");

    /* Step again without restarting: 25.0 C >= new temp_max (24.0 C) -> starts cooling immediately */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    hvac_status_msg_t status;
    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.group_state[0], GROUP_STATE_COOLING, "Group must be in COOLING state");
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 must now be energized ON");
}

ZTEST(ao_hvac_suite, test_ao_hvac_runtime_turn_count_update)
{
    config_hvac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.total_groups = 1U;
    cfg.total_units = 4U;
    cfg.groups[0].temp_min = 20.0f;
    cfg.groups[0].temp_max = 24.0f;
    cfg.groups[0].temp_alarm = 28.0f;
    cfg.groups[0].temp_bypass = 32.0f;
    cfg.groups[0].delay_on_s = 0U;
    cfg.groups[0].unit_mask = 0x0FU; /* Units 0, 1, 2, 3 */
    cfg.groups[0].active_turn_count = 1U; /* Initially N=1 */
    cfg.groups[0].max_allowed_faults = 3U;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "init failed");

    /* Temperature 25.0 C triggers cooling */
    group_temperature_metrics_t metrics = {
        .highest_temp = 25.0f,
        .valid_sensors = 1U,
        .total_sensors = 1U,
    };
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    (void)ao_hvac_step(&s_ao);

    /* Only unit 0 should be energized (active_turn_count = 1) */
    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");
    zassert_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 should be OFF");
    zassert_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_OFF, "Unit 2 should be OFF");
    zassert_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 should be OFF");

    /* Dynamically increase active_turn_count to 3 */
    config_hvac_group_t updated_group = cfg.groups[0];
    updated_group.active_turn_count = 3U;

    ret = ao_hvac_set_group_config(&s_ao, 0, &updated_group);
    zassert_equal(ret, 0, "ao_hvac_set_group_config failed");

    /* Step again without restarting: units 0, 1, 2 must now be energized */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    zassert_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");
    zassert_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_ON, "Unit 1 should be ON");
    zassert_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_ON, "Unit 2 should be ON");
    zassert_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 should be OFF (standby)");
}

