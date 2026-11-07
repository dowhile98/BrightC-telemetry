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
#include "common/default_config_profiles.h"
#include "fake_hvac_unit.h"

static ao_hvac_t s_ao;
static struct k_msgq s_queue;
static uint8_t s_queue_buf[AO_HVAC_QUEUE_DEPTH * sizeof(ao_hvac_event_t)];
static K_THREAD_STACK_DEFINE(s_test_stack, AO_HVAC_STACK_SIZE);
static i_hvac_unit_t s_actuator_port;

#define zassert_tri_equal(a, b, msg) zassert_true(hvac_tri_state_is_equal((a), (b)), msg)

static uint32_t s_event_count;
static hvac_event_t s_last_event;

static void test_hvac_event_logger_cb(const struct zbus_channel *chan)
{
    if (chan == &chan_hvac_event) {
        const hvac_event_t *ev = zbus_chan_const_msg(chan);
        if (ev != NULL) {
            s_event_count++;
            s_last_event = *ev;
        }
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_event_logger, test_hvac_event_logger_cb);

static void setup_fixture(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_hvac_unit_reset();
    s_actuator_port = fake_hvac_unit_get_port();
    k_msgq_init(&s_queue, (char *)s_queue_buf, sizeof(ao_hvac_event_t), AO_HVAC_QUEUE_DEPTH);
    s_event_count = 0U;
    memset(&s_last_event, 0, sizeof(s_last_event));
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
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 should be OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 should be OFF");

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
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_ON, "Unit 1 should be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_OFF, "Unit 2 should be standby (OFF)");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 should be standby (OFF)");
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
    cfg.groups[0].sensor_mask = 0x0FU;
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
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");

    /* Trigger fire emergency */
    int ret = ao_hvac_set_fire(&s_ao, true);
    zassert_equal(ret, 0, "set_fire failed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    hvac_status_msg_t status;
    (void)ao_hvac_get_status(&s_ao, &status);
    zassert_equal(status.group_state[0], GROUP_STATE_FIRE, "Group must be in FIRE state");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must shut down immediately");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 must shut down immediately");
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
    cfg.groups[0].sensor_mask = 0x0FU;
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
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 running");

    /* Unit 0 trips High Pressure alarm */
    int ret = ao_hvac_set_unit_alarms(&s_ao, 0, true, false, false, false);
    zassert_equal(ret, 0, "set_unit_alarms failed");

    (void)ao_hvac_step(&s_ao);

    /* Unit 0 must shut down */
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must shut down due to alarm");

    /* Verify Zbus channel publication */
    hvac_status_msg_t zbus_msg;
    ret = zbus_chan_read(&chan_hvac_status, &zbus_msg, K_MSEC(100));
    zassert_equal(ret, 0, "Failed to read from chan_hvac_status");
    zassert_tri_equal(zbus_msg.unit_tri_state[0], HVAC_TRI_STATE_OFF, "Zbus snapshot must show Unit 0 OFF");
    zassert_tri_equal(zbus_msg.unit_tri_state[1], HVAC_TRI_STATE_ON, "Zbus snapshot must show Unit 1 ON");
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
    cfg.groups[0].sensor_mask = 0x0FU;
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
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must be OFF (idle)");

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
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 must now be energized ON");
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
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 should be OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_OFF, "Unit 2 should be OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 should be OFF");

    /* Dynamically increase active_turn_count to 3 */
    config_hvac_group_t updated_group = cfg.groups[0];
    updated_group.active_turn_count = 3U;

    ret = ao_hvac_set_group_config(&s_ao, 0, &updated_group);
    zassert_equal(ret, 0, "ao_hvac_set_group_config failed");

    /* Step again without restarting: units 0, 1, 2 must now be energized */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_ON, "Unit 1 should be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_ON, "Unit 2 should be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 should be OFF (standby)");
}

ZTEST(ao_hvac_suite, test_ao_hvac_feed_temperatures_multi_sensor)
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
    cfg.groups[0].sensor_mask = 0x0FU;
    cfg.groups[0].unit_mask = 0x03U;
    cfg.groups[0].active_turn_count = 1U;
    cfg.groups[0].sensor_mask = 0x0007U; /* Probes 0, 1, 2 */

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "init failed");

    /* Feed temperatures where sensor 0=22.0C (below max), sensor 1=25.5C (above max), sensor 2=23.0C */
    float temps[4] = { 22.0f, 25.5f, 23.0f, 15.0f };
    uint16_t valid_mask = 0x0007U;

    ret = ao_hvac_feed_temperatures(&s_ao, temps, valid_mask, 4U);
    zassert_equal(ret, 0, "ao_hvac_feed_temperatures failed");

    /* Step ao_hvac */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step failed");

    /* Verify group transitions to GROUP_STATE_COOLING driven by ceiling temperature (25.5 C >= temp_max 24.0 C) */
    hvac_status_msg_t status;
    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.group_state[0], GROUP_STATE_COOLING, "Group 0 should transition to COOLING");
    zassert_within(status.highest_temp[0], 25.5f, 0.001f, "Highest temp should be 25.5 C");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 should be energized ON");

    /* Test invalid arguments on ao_hvac_feed_temperatures */
    zassert_equal(ao_hvac_feed_temperatures(NULL, temps, valid_mask, 4U), -EINVAL, "NULL self must return -EINVAL");
    zassert_equal(ao_hvac_feed_temperatures(&s_ao, NULL, valid_mask, 4U), -EINVAL, "NULL temperatures must return -EINVAL");
}

ZTEST(ao_hvac_suite, test_default_hybrid_profile_verification)
{
    /* 1. Verify static HVAC hybrid config validity and parameters */
    zassert_true(config_hvac_is_valid(&k_default_hvac_config_hybrid_4units),
                 "k_default_hvac_config_hybrid_4units must be valid");
    zassert_equal(k_default_hvac_config_hybrid_4units.operation_mode, HVAC_MODE_HYBRID_FULL,
                  "Mode must be HVAC_MODE_HYBRID_FULL");
    zassert_equal(k_default_hvac_config_hybrid_4units.total_groups, 1U, "total_groups must be 1");
    zassert_equal(k_default_hvac_config_hybrid_4units.total_units, 4U, "total_units must be 4");

    const config_hvac_group_t *g0 = &k_default_hvac_config_hybrid_4units.groups[0];
    zassert_equal(g0->unit_mask, 0x0FU, "unit_mask must be 0x0F (units 0..3)");
    zassert_equal(g0->active_turn_count, 2U, "active_turn_count must be 2 (2+2 rotation)");
    zassert_equal(g0->sensor_mask, 0x001FU, "sensor_mask must be 0x001F (5 internal RTD probes)");
    zassert_within(g0->temp_min, 20.0f, 0.001f, "temp_min must be 20.0 C");
    zassert_within(g0->temp_max, 24.0f, 0.001f, "temp_max must be 24.0 C");
    zassert_within(g0->temp_alarm, 28.0f, 0.001f, "temp_alarm must be 28.0 C");
    zassert_within(g0->temp_bypass, 32.0f, 0.001f, "temp_bypass must be 32.0 C");
    zassert_equal(g0->time_to_cool_s, 1800U, "time_to_cool_s must be 1800");
    zassert_equal(g0->delay_on_s, 10U, "delay_on_s must be 10");
    zassert_equal(g0->fan_delay_s, 30U, "fan_delay_s must be 30");
    zassert_equal(g0->pressure_retry_s, 180U, "pressure_retry_s must be 180");
    zassert_equal(g0->rotation_interval_days, 7U, "rotation_interval_days must be 7");

    /* 2. Verify relay actuator endpoints (Internal relays on mcp23s17_2, device_id = 2) */
    for (uint8_t u = 0; u < 4U; u++) {
        const hvac_unit_hw_config_t *u_cfg = &k_default_hvac_config_hybrid_4units.units[u];
        zassert_equal(u_cfg->relay_aa.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "Relay backend");
        zassert_equal(u_cfg->relay_aa.device_id, HARDWARE_MCP23S17_IDX_HVAC_RELAYS, "Relay device_id 2");

        zassert_equal(u_cfg->relay_sm.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "Fan backend");
        zassert_equal(u_cfg->relay_sm.device_id, HARDWARE_MCP23S17_IDX_HVAC_RELAYS, "Fan device_id 2");

        zassert_equal(u_cfg->relay_g.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "Alarm relay backend");
        zassert_equal(u_cfg->relay_g.device_id, HARDWARE_MCP23S17_IDX_HVAC_RELAYS, "Alarm relay device_id 2");

        /* Alarms (Units 0..2 on mcp23s17_0 dev_id 0, Unit 3 on mcp23s17_1 dev_id 1) */
        uint8_t expected_alm_dev = (u < 3U) ? HARDWARE_MCP23S17_IDX_ALM_IN_0_11 : HARDWARE_MCP23S17_IDX_ALM_IN_12_23;

        zassert_equal(u_cfg->alarm_hp.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "HP backend");
        zassert_equal(u_cfg->alarm_hp.device_id, expected_alm_dev, "HP device_id");

        zassert_equal(u_cfg->alarm_lp.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "LP backend");
        zassert_equal(u_cfg->alarm_lp.device_id, expected_alm_dev, "LP device_id");

        zassert_equal(u_cfg->alarm_flow.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "Flow backend");
        zassert_equal(u_cfg->alarm_flow.device_id, expected_alm_dev, "Flow device_id");

        zassert_equal(u_cfg->alarm_volt.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "Volt backend");
        zassert_equal(u_cfg->alarm_volt.device_id, expected_alm_dev, "Volt device_id");

        /* Feedbacks (MBSL32DI, device_id = 4) */
        zassert_equal(u_cfg->fb_fan.backend, HVAC_IO_BACKEND_REMOTE_MODBUS, "FB fan backend");
        zassert_equal(u_cfg->fb_fan.device_id, MODBUS_ADDR_MBSL32DI, "FB fan device_id 4");

        zassert_equal(u_cfg->fb_compressor.backend, HVAC_IO_BACKEND_REMOTE_MODBUS, "FB comp backend");
        zassert_equal(u_cfg->fb_compressor.device_id, MODBUS_ADDR_MBSL32DI, "FB comp device_id 4");
    }

    /* Interlocks */
    zassert_equal(k_default_hvac_config_hybrid_4units.fire_interlock.backend, HVAC_IO_BACKEND_LOCAL_MCP23S17, "Fire backend");
    zassert_equal(k_default_hvac_config_hybrid_4units.fire_interlock.device_id, HARDWARE_MCP23S17_IDX_ALM_IN_12_23, "Fire dev_id");
    zassert_equal(k_default_hvac_config_hybrid_4units.fire_interlock.channel, 4U, "Fire channel");

    zassert_equal(k_default_hvac_config_hybrid_4units.bypass_switch.backend, HVAC_IO_BACKEND_LOCAL_GPIO, "Bypass backend");
    zassert_equal(k_default_hvac_config_hybrid_4units.bypass_switch.device_id, 0U, "Bypass dev_id");
    zassert_equal(k_default_hvac_config_hybrid_4units.bypass_switch.channel, (uint16_t)ONCHIP_DI_SW_BYPASS, "Bypass channel");

    /* 3. Verify DI Declarative Profiles */
    size_t di_count = 0;
    const di_channel_config_t *di_prof = default_config_get_di_profile(HVAC_MODE_HYBRID_FULL, &di_count);
    zassert_not_null(di_prof, "Hybrid DI profile must not be NULL");
    zassert_equal(di_count, HARDWARE_SYSTEM_DI_COUNT, "System DI profile must have 58 channels");

    /* Channels 0..11: on mcp23s17_0 (dev_id 0) */
    for (uint8_t i = 0; i < 12U; i++) {
        zassert_equal(di_prof[i].source_type, DI_SOURCE_INTERNAL_SPI, "DI source SPI dev 0");
        zassert_equal(di_prof[i].source_dev_id, HARDWARE_MCP23S17_IDX_ALM_IN_0_11, "DI dev_id 0");
        zassert_equal(di_prof[i].pin_index, i, "DI pin_index matches");
        zassert_true(di_prof[i].enabled, "DI channel enabled");
    }
    zassert_equal(di_prof[0].role, DI_ROLE_UNIT_0_ALARM_HP, "CH0 is Unit 0 HP");
    zassert_equal(di_prof[11].role, DI_ROLE_UNIT_2_ALARM_VOLT, "CH11 is Unit 2 Volt");

    /* Channels 12..23: on mcp23s17_1 (dev_id 1) */
    for (uint8_t i = 0; i < 12U; i++) {
        zassert_equal(di_prof[12U + i].source_type, DI_SOURCE_INTERNAL_SPI, "DI source SPI dev 1");
        zassert_equal(di_prof[12U + i].source_dev_id, HARDWARE_MCP23S17_IDX_ALM_IN_12_23, "DI dev_id 1");
        zassert_equal(di_prof[12U + i].pin_index, i, "DI pin_index matches");
        if (i < 9U) {
            zassert_true(di_prof[12U + i].enabled, "DI channel enabled");
        } else {
            zassert_false(di_prof[12U + i].enabled, "Spare DI channel disabled");
        }
    }
    zassert_equal(di_prof[12].role, DI_ROLE_UNIT_3_ALARM_HP, "CH12 is Unit 3 HP");
    zassert_equal(di_prof[15].role, DI_ROLE_UNIT_3_ALARM_VOLT, "CH15 is Unit 3 Volt");
    zassert_equal(di_prof[16].role, DI_ROLE_SMOKE_FIRE, "CH16 is Fire Interlock");
    zassert_equal(di_prof[17].role, DI_ROLE_DOOR_OPEN, "CH17 is Door Open");

    /* Verify Remote MBSL32DI channels */
    size_t remote_count = 0;
    const di_channel_config_t *remote_prof = hardware_pin_map_get_remote_di(&remote_count);
    zassert_not_null(remote_prof, "Remote DI profile must not be NULL");
    zassert_equal(remote_count, HARDWARE_REMOTE_DI_COUNT, "Remote DI profile must have 32 channels");

    /* 4. Verify helper function profile dispatch */
    zassert_equal_ptr(default_config_get_hvac_profile(HVAC_MODE_HYBRID_FULL), &k_default_hvac_config_hybrid_4units, "Hybrid profile lookup");
    zassert_equal_ptr(default_config_get_hvac_profile(HVAC_MODE_HVAC_CONTROL_ONLY), &k_default_hvac_config_control_only_8units, "Control-only profile lookup");
    zassert_equal_ptr(default_config_get_hvac_profile(HVAC_MODE_TELEMETRY_ONLY), &k_default_hvac_config_telemetry_only, "Telemetry-only profile lookup");

    /* 5. Initialize ao_hvac with hybrid profile and feed 5 internal RTDs */
    int ret = ao_hvac_init(&s_ao, &k_default_hvac_config_hybrid_4units, &s_actuator_port,
                           &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init with hybrid profile must succeed");

    /* Feed 5 internal RTDs: all at 25.0 C (above max 24.0 C) */
    float rtd_temps[5] = { 25.0f, 25.0f, 25.0f, 25.0f, 25.0f };
    ret = ao_hvac_feed_temperatures(&s_ao, rtd_temps, 0x001FU, 5U);
    zassert_equal(ret, 0, "ao_hvac_feed_temperatures must succeed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step must succeed");

    hvac_status_msg_t status;
    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "ao_hvac_get_status must succeed");
    zassert_equal(status.group_state[0], GROUP_STATE_COOLING, "Group 0 must enter COOLING");
    zassert_within(status.highest_temp[0], 25.0f, 0.001f, "Highest temp should be 25.0 C");
}

ZTEST(ao_hvac_suite, test_safe_state_and_lifecycle_contract)
{
    /* P2-5 & P2-6: Verify safe state forces relays OFF and stop/wait_stopped contract works */
    int ret = ao_hvac_init(&s_ao, &k_default_hvac_config_hybrid_4units, &s_actuator_port,
                           &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init failed");

    /* Pre-load units into active ON */
    fake_hvac_unit_set_unit_state(0, HVAC_TRI_STATE_ON);
    fake_hvac_unit_set_unit_state(1, HVAC_TRI_STATE_BYPASS);
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 state");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_BYPASS, "Unit 1 state");

    /* Explicit safe state invocation */
    ret = ao_hvac_safe_state(&s_ao);
    zassert_equal(ret, 0, "safe_state failed");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must be forced OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 must be forced OFF");

    /* Start and then stop AO - stopping should invoke safe_state and signal stopped_sem */
    ret = ao_hvac_start(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_start failed");
    zassert_true(ao_hvac_is_running(&s_ao), "Should be running");

    fake_hvac_unit_set_unit_state(0, HVAC_TRI_STATE_ON);

    ret = ao_hvac_stop(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_stop failed");

    ret = ao_hvac_wait_stopped(&s_ao, K_MSEC(200));
    zassert_equal(ret, 0, "ao_hvac_wait_stopped should succeed without double wait");
    zassert_false(ao_hvac_is_running(&s_ao), "Should no longer be running");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must be OFF after stop");
}

ZTEST(ao_hvac_suite, test_actuator_error_handling)
{
    /* P2-7 / R-07: Handle actuator errors in ao_hvac_step without faulting healthy mechanical units */
    int ret = ao_hvac_init(&s_ao, &k_default_hvac_config_hybrid_4units, &s_actuator_port,
                           &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init failed");
    zassert_equal(s_ao.actuator_fault_count, 0U, "Initial fault count must be 0");

    /* Inject set_tri_state failure */
    fake_hvac_unit_set_errors(-EIO, 0);

    /* Feed hot temperatures (26 C) to demand cooling actuation */
    float rtd_temps[5] = { 26.0f, 26.0f, 26.0f, 26.0f, 26.0f };
    ret = ao_hvac_feed_temperatures(&s_ao, rtd_temps, 0x001FU, 5U);
    zassert_equal(ret, 0, "ao_hvac_feed_temperatures failed");

    /* Stepping should encounter actuator errors and increment fault count */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step should run");
    zassert_true(s_ao.actuator_fault_count > 0U, "Actuator fault counter must be incremented on failure");

    /* R-07 Invariant: Actuator communication error must NOT cause mechanical fault on unit */
    for (uint8_t u = 0U; u < 4U; u++) {
        zassert_equal(s_ao.units[u].fault_counter, 0U, "Unit %u mechanical fault count must stay 0", u);
        zassert_not_equal(s_ao.units[u].state, UNIT_STATE_FAULT, "Unit %u must not transition to FAULT on bus error", u);
    }

    /* Reset errors */
    fake_hvac_unit_set_errors(0, 0);
}

ZTEST(ao_hvac_suite, test_multi_room_disjoint_sensor_ranges)
{
    /* Phase 3: Multi-room disjoint sensor ranges across two independent groups
     * Group 0 (Room A): Units 0..1, Sensors flat IDs 1..2
     * Group 1 (Room B): Units 2..3, Sensors flat IDs 3..5
     */
    config_hvac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.operation_mode = (uint8_t)HVAC_MODE_HVAC_CONTROL_ONLY;
    cfg.total_groups = 2U;
    cfg.total_units = 4U;

    /* Group 0: Room A */
    cfg.groups[0].temp_min = 20.0f;
    cfg.groups[0].temp_max = 24.0f;
    cfg.groups[0].temp_alarm = 28.0f;
    cfg.groups[0].temp_bypass = 32.0f;
    cfg.groups[0].delay_on_s = 0U;
    cfg.groups[0].sensor_mask = 0x0FU;
    cfg.groups[0].unit_mask = 0x03U;       /* Units 0, 1 */
    cfg.groups[0].active_turn_count = 1U;  /* 1 active + 1 standby */
    cfg.groups[0].max_allowed_faults = 3U;
    cfg.groups[0].sensor_start = 1U;
    cfg.groups[0].sensor_end = 2U;
    cfg.groups[0].metric_policy = (uint8_t)SENSOR_METRIC_MAX;

    /* Group 1: Room B */
    cfg.groups[1].temp_min = 20.0f;
    cfg.groups[1].temp_max = 24.0f;
    cfg.groups[1].temp_alarm = 28.0f;
    cfg.groups[1].temp_bypass = 32.0f;
    cfg.groups[1].delay_on_s = 0U;
    cfg.groups[1].unit_mask = 0x0CU;       /* Units 2, 3 */
    cfg.groups[1].active_turn_count = 1U;  /* 1 active + 1 standby */
    cfg.groups[1].max_allowed_faults = 3U;
    cfg.groups[1].sensor_start = 3U;
    cfg.groups[1].sensor_end = 5U;
    cfg.groups[1].metric_policy = (uint8_t)SENSOR_METRIC_MAX;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init failed");

    /* Step 1: Room A is hot (26 C >= 24 C), Room B is cool (19 C <= 20 C) */
    /* Array indices 0..5 correspond to flat IDs 1..6 */
    float temps[6] = {
        26.0f, /* ID 1 (Room A) */
        25.0f, /* ID 2 (Room A) */
        19.0f, /* ID 3 (Room B) */
        19.5f, /* ID 4 (Room B) */
        18.5f, /* ID 5 (Room B) */
        45.0f  /* ID 6 (External / unrelated probe) */
    };
    uint64_t valid_mask = 0x3FULL; /* IDs 1..6 all valid */

    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, valid_mask, 6U);
    zassert_equal(ret, 0, "ao_hvac_feed_sensor_snapshot failed");

    /* Verify metrics isolation between rooms */
    zassert_equal(s_ao.group_metrics[0].total_sensors, 2U, "Room A total sensors");
    zassert_equal(s_ao.group_metrics[0].valid_sensors, 2U, "Room A valid sensors");
    zassert_within(s_ao.group_metrics[0].highest_temp, 26.0f, 0.001f, "Room A highest temp");

    zassert_equal(s_ao.group_metrics[1].total_sensors, 3U, "Room B total sensors");
    zassert_equal(s_ao.group_metrics[1].valid_sensors, 3U, "Room B valid sensors");
    zassert_within(s_ao.group_metrics[1].highest_temp, 19.5f, 0.001f, "Room B highest temp");

    /* Actuate */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");

    /* Group 0 must be COOLING, Group 1 must be IDLE */
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_COOLING, "Room A must be in COOLING");
    zassert_equal(s_ao.groups[1].state, GROUP_STATE_IDLE, "Room B must be IDLE");

    /* Actuator verification: Unit 0 active ON, Unit 1 standby OFF, Units 2..3 OFF */
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 (Room A) must be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 (Room A standby) must be OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_OFF, "Unit 2 (Room B) must be OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 (Room B) must be OFF");

    /* Step 2: Swap thermal load - Room A cooled down (19 C), Room B overheated (27 C) */
    temps[0] = 19.0f;
    temps[1] = 18.5f;
    temps[2] = 27.0f;
    temps[3] = 26.5f;
    temps[4] = 25.0f;

    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, valid_mask, 6U);
    zassert_equal(ret, 0, "ao_hvac_feed_sensor_snapshot failed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");

    /* Group 0 must be IDLE, Group 1 must be COOLING */
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_IDLE, "Room A must transition to IDLE");
    zassert_equal(s_ao.groups[1].state, GROUP_STATE_COOLING, "Room B must transition to COOLING");

    /* Actuator verification: Units 0..1 OFF, Unit 2 ON, Unit 3 OFF */
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must be OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(1), HVAC_TRI_STATE_OFF, "Unit 1 must be OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(2), HVAC_TRI_STATE_ON, "Unit 2 (Room B) must be ON");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(3), HVAC_TRI_STATE_OFF, "Unit 3 (Room B standby) must be OFF");
}

ZTEST(ao_hvac_suite, test_dynamic_group_sensor_range_and_metric_policy)
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
    cfg.groups[0].sensor_mask = 0x0FU;
    cfg.groups[0].unit_mask = 0x03U;       /* Units 0, 1 */
    cfg.groups[0].active_turn_count = 1U;  /* 1 active + 1 standby */
    cfg.groups[0].max_allowed_faults = 3U;
    cfg.groups[0].sensor_start = 1U;
    cfg.groups[0].sensor_end = 2U;
    cfg.groups[0].metric_policy = (uint8_t)SENSOR_METRIC_MAX;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init failed");

    /* Initial temperatures: Probe 1 is hot (26 C), Probe 2 is mild (22 C), Probes 3 & 4 are cool (18 C) */
    float temps[4] = {26.0f, 22.0f, 18.0f, 18.0f};
    uint64_t valid_mask = 0x0FULL;

    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, valid_mask, 4U);
    zassert_equal(ret, 0, "ao_hvac_feed_sensor_snapshot failed");
    zassert_equal(s_ao.group_metrics[0].valid_sensors, 2U, "Initial valid sensors count");
    zassert_within(s_ao.group_metrics[0].highest_temp, 26.0f, 0.001f, "Initial highest temp");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_COOLING, "Group 0 must enter COOLING");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 must turn ON");

    /* Dynamically reconfigure Group 0 to monitor sensor range 3..4 with policy AVG */
    config_hvac_group_t updated_group = cfg.groups[0];
    updated_group.sensor_start = 3U;
    updated_group.sensor_end = 4U;
    updated_group.metric_policy = (uint8_t)SENSOR_METRIC_AVG;

    ret = ao_hvac_set_group_config(&s_ao, 0, &updated_group);
    zassert_equal(ret, 0, "ao_hvac_set_group_config failed");
    zassert_equal(s_ao.config.groups[0].sensor_start, 3U, "Config sensor_start must be updated");
    zassert_equal(s_ao.config.groups[0].sensor_end, 4U, "Config sensor_end must be updated");
    zassert_equal(s_ao.config.groups[0].metric_policy, (uint8_t)SENSOR_METRIC_AVG, "Config metric_policy must be updated");

    /* Feed sensor snapshot again - now probes 3..4 (18 C) should determine the metrics */
    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, valid_mask, 4U);
    zassert_equal(ret, 0, "ao_hvac_feed_sensor_snapshot failed");
    zassert_equal(s_ao.group_metrics[0].valid_sensors, 2U, "Updated valid sensors count");
    zassert_within(s_ao.group_metrics[0].highest_temp, 18.0f, 0.001f, "Updated highest temp");
    zassert_within(s_ao.group_metrics[0].average_temp, 18.0f, 0.001f, "Updated average temp");

    /* Subsequent evaluation step: temperature 18.0 C <= temp_min 20.0 C -> transitions to IDLE */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_IDLE, "Group 0 must transition to IDLE");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must turn OFF");

    /* Dynamically reconfigure policy to SENSOR_METRIC_MIN on probes 1..3 */
    /* Temps: probe 1 = 26 C, probe 2 = 25 C, probe 3 = 18 C */
    temps[1] = 25.0f;
    updated_group.sensor_start = 1U;
    updated_group.sensor_end = 3U;
    updated_group.metric_policy = (uint8_t)SENSOR_METRIC_MIN;

    ret = ao_hvac_set_group_config(&s_ao, 0, &updated_group);
    zassert_equal(ret, 0, "ao_hvac_set_group_config to MIN failed");

    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, valid_mask, 4U);
    zassert_equal(ret, 0, "ao_hvac_feed_sensor_snapshot failed");
    zassert_within(s_ao.group_metrics[0].lowest_temp, 18.0f, 0.001f, "Lowest temp must be 18.0 C");
    zassert_within(s_ao.group_metrics[0].highest_temp, 26.0f, 0.001f, "Highest temp must be 26.0 C");

    /* Under MIN policy, effective temp is 18.0 C (< 20.0 C), so group remains IDLE */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_IDLE, "Group 0 remains IDLE under MIN policy");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must stay OFF");

    /* Now switch policy back to SENSOR_METRIC_MAX on probes 1..3 */
    updated_group.metric_policy = (uint8_t)SENSOR_METRIC_MAX;
    ret = ao_hvac_set_group_config(&s_ao, 0, &updated_group);
    zassert_equal(ret, 0, "ao_hvac_set_group_config to MAX failed");

    /* Under MAX policy, effective temp is 26.0 C (>= 24.0 C), so group transitions to COOLING */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_COOLING, "Group 0 transitions to COOLING under MAX policy");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_ON, "Unit 0 must turn ON");
}

ZTEST(ao_hvac_suite, test_ao_hvac_rotation_interval_and_stepping)
{
    /* Initialize with default hybrid config */
    const config_hvac_t *cfg = &k_default_hvac_config_hybrid_4units;
    int ret = ao_hvac_init(&s_ao, cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init failed");

    /* Default rotation interval is 7 days */
    zassert_equal(s_ao.groups[0].rotation_interval_days, 7U, "Default rotation interval is 7 days");
    zassert_equal(s_ao.groups[0].rotation_timer_s, 0U, "Initial rotation timer is 0");

    /* Test ao_hvac_set_group_rotation */
    ret = ao_hvac_set_group_rotation(&s_ao, 0, 15U);
    zassert_equal(ret, 0, "ao_hvac_set_group_rotation failed");
    zassert_equal(s_ao.groups[0].rotation_interval_days, 15U, "Group 0 rotation interval updated to 15");
    zassert_equal(s_ao.config.groups[0].rotation_interval_days, 15U, "Group 0 config updated to 15");

    /* Step ao_hvac: should increment rotation_timer_s */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step failed");
    zassert_equal(s_ao.groups[0].rotation_timer_s, 1U, "Rotation timer advanced by 1s");

    /* Test ao_hvac_force_rotation resets rotation timer */
    ret = ao_hvac_force_rotation(&s_ao, 0);
    zassert_equal(ret, 0, "ao_hvac_force_rotation failed");
    zassert_equal(s_ao.groups[0].rotation_timer_s, 0U, "Rotation timer reset to 0 by forced rotation");
    zassert_equal(s_ao.groups[0].current_turn_offset, 1U, "Current turn offset advanced to 1");

    /* Error handling */
    zassert_equal(ao_hvac_set_group_rotation(NULL, 0, 7U), -EINVAL, "NULL ao should return -EINVAL");
    zassert_equal(ao_hvac_set_group_rotation(&s_ao, HVAC_MAX_GROUPS, 7U), -EINVAL, "Invalid group should return -EINVAL");
    zassert_equal(ao_hvac_set_group_rotation(&s_ao, 0, 0U), -EINVAL, "0 days should return -EINVAL");
}

ZTEST(ao_hvac_suite, test_ao_hvac_set_bypass_cause)
{
    const config_hvac_t *cfg = &k_default_hvac_config_hybrid_4units;
    int ret = ao_hvac_init(&s_ao, cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init failed");

    /* Initial state: no bypass */
    zassert_equal(s_ao.groups[0].bypass_causes_mask, 0U, "Initial bypass causes must be 0");
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_IDLE, "Initial state IDLE");

    /* Set physical bypass switch cause */
    ret = ao_hvac_set_bypass_cause(&s_ao, 0U, BYPASS_CAUSE_PHYSICAL_SWITCH, true);
    zassert_equal(ret, 0, "ao_hvac_set_bypass_cause must return 0");
    zassert_equal(s_ao.groups[0].bypass_causes_mask, (uint32_t)BYPASS_CAUSE_PHYSICAL_SWITCH,
                  "Bypass causes must include PHYSICAL_SWITCH");
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_BYPASS, "Group must transition to GROUP_STATE_BYPASS");

    /* Clear physical bypass switch cause */
    ret = ao_hvac_set_bypass_cause(&s_ao, 0U, BYPASS_CAUSE_PHYSICAL_SWITCH, false);
    zassert_equal(ret, 0, "ao_hvac_set_bypass_cause clear must return 0");
    zassert_equal(s_ao.groups[0].bypass_causes_mask, 0U, "Bypass causes must be 0 after clear");
    zassert_equal(s_ao.groups[0].state, GROUP_STATE_IDLE, "Group must return to GROUP_STATE_IDLE");

    /* Error handling */
    zassert_equal(ao_hvac_set_bypass_cause(NULL, 0U, BYPASS_CAUSE_PHYSICAL_SWITCH, true), -EINVAL,
                  "NULL ao must return -EINVAL");
    zassert_equal(ao_hvac_set_bypass_cause(&s_ao, HVAC_MAX_GROUPS, BYPASS_CAUSE_PHYSICAL_SWITCH, true), -EINVAL,
                  "Invalid group_id must return -EINVAL");
}

ZTEST(ao_hvac_suite, test_overlapping_unit_masks_rejected)
{
    /* R-10: Overlapping unit masks between groups must be rejected by config_hvac_is_valid */
    config_hvac_t cfg = k_default_hvac_config_hybrid_4units;
    cfg.total_groups = 2U;
    cfg.groups[0].unit_mask = 0x03U; /* Units 0, 1 */
    cfg.groups[1].unit_mask = 0x02U; /* Unit 1 shared! Overlap! */
    cfg.groups[1].active_turn_count = 1U;
    cfg.groups[1].temp_min = 20.0f;
    cfg.groups[1].temp_max = 24.0f;
    cfg.groups[1].temp_alarm = 28.0f;
    cfg.groups[1].temp_bypass = 32.0f;

    zassert_false(config_hvac_is_valid(&cfg), "Overlapping unit masks must be rejected");

    int ret = ao_hvac_init(&s_ao, &k_default_hvac_config_hybrid_4units, &s_actuator_port,
                           &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init must succeed");

    ret = ao_hvac_apply_config(&s_ao, &cfg);
    zassert_equal(ret, -EINVAL, "ao_hvac_apply_config must reject overlapping unit masks");
}

ZTEST(ao_hvac_suite, test_hot_unit_mask_rebuilds_topology)
{
    /* R-10: Hot topology rebuild on ao_hvac_apply_config */
    config_hvac_t cfg = k_default_hvac_config_hybrid_4units;
    cfg.total_units = 8U;
    cfg.total_groups = 1U;
    cfg.groups[0].unit_mask = 0x0FU; /* Initially units 0..3 */
    cfg.groups[0].active_turn_count = 1U;
    cfg.groups[0].delay_on_s = 0U;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init must succeed");

    zassert_true(s_ao.unit_active[0], "Unit 0 active initially");
    zassert_true(s_ao.unit_active[3], "Unit 3 active initially");
    zassert_false(s_ao.unit_active[4], "Unit 4 inactive initially");
    zassert_false(s_ao.unit_active[5], "Unit 5 inactive initially");

    /* Apply new config switching group 0 to units 4 and 5 (mask 0x30) */
    cfg.groups[0].unit_mask = 0x30U;
    cfg.groups[0].active_turn_count = 1U;

    ret = ao_hvac_apply_config(&s_ao, &cfg);
    zassert_equal(ret, 0, "ao_hvac_apply_config must succeed");

    /* Verify topology was rebuilt */
    zassert_false(s_ao.unit_active[0], "Unit 0 must now be inactive");
    zassert_false(s_ao.unit_active[1], "Unit 1 must now be inactive");
    zassert_false(s_ao.unit_active[2], "Unit 2 must now be inactive");
    zassert_false(s_ao.unit_active[3], "Unit 3 must now be inactive");
    zassert_true(s_ao.unit_active[4], "Unit 4 must now be active");
    zassert_true(s_ao.unit_active[5], "Unit 5 must now be active");

    /* Feed hot temperature and step */
    group_temperature_metrics_t metrics = {
        .highest_temp = 26.0f,
        .valid_sensors = 1U,
        .total_sensors = 1U,
    };
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step must succeed");

    /* Units 0..3 must not be actuated (remain OFF), Unit 4 must be ON */
    zassert_tri_equal(fake_hvac_unit_get_unit_state(0), HVAC_TRI_STATE_OFF, "Unit 0 must remain OFF");
    zassert_tri_equal(fake_hvac_unit_get_unit_state(4), HVAC_TRI_STATE_ON, "Unit 4 must be energized ON");
}

ZTEST(ao_hvac_suite, test_transition_events_published_to_chan_hvac_event)
{
    /* R-04: Transition events published to chan_hvac_event */
    config_hvac_t cfg = k_default_hvac_config_hybrid_4units;
    cfg.groups[0].delay_on_s = 0U;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "ao_hvac_init must succeed");

    s_event_count = 0U;

    /* 1. Transition IDLE -> COOLING */
    group_temperature_metrics_t metrics = {
        .highest_temp = 26.0f,
        .valid_sensors = 1U,
        .total_sensors = 1U,
    };
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step must succeed");

    zassert_true(s_event_count >= 1U, "At least one transition event must have been published");
    zassert_equal(s_last_event.type, HVAC_EVENT_COOLING_STARTED, "Event type must be COOLING_STARTED");
    zassert_equal(s_last_event.group_id, 0U, "Group ID must be 0");

    /* 2. Transition COOLING -> FIRE */
    (void)ao_hvac_set_fire(&s_ao, true);
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step must succeed");

    zassert_equal(s_last_event.type, HVAC_EVENT_FIRE_ACTIVATED, "Event type must be FIRE_ACTIVATED");

    /* 3. Transition FIRE -> IDLE (when fire clears and temp <= temp_min) */
    uint32_t prev_events = s_event_count;
    (void)ao_hvac_set_fire(&s_ao, false);
    metrics.highest_temp = 19.0f;
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "ao_hvac_step must succeed");

    /* Both IDLE_ENTERED and FIRE_CLEARED are published */
    zassert_true(s_event_count >= prev_events + 2U, "Both IDLE_ENTERED and FIRE_CLEARED should be published");
    zassert_equal(s_last_event.type, HVAC_EVENT_FIRE_CLEARED, "Final event type must be FIRE_CLEARED");

    /* 4. Direct transition COOLING -> IDLE */
    metrics.highest_temp = 26.0f;
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    (void)ao_hvac_step(&s_ao);
    zassert_equal(s_last_event.type, HVAC_EVENT_COOLING_STARTED, "Event must be COOLING_STARTED");

    metrics.highest_temp = 19.0f;
    (void)ao_hvac_set_group_metrics(&s_ao, 0, &metrics);
    (void)ao_hvac_step(&s_ao);
    zassert_equal(s_last_event.type, HVAC_EVENT_IDLE_ENTERED, "Direct cooldown event must be IDLE_ENTERED");
}

ZTEST(ao_hvac_suite, test_ao_hvac_boot_stabilization_guard)
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
    cfg.groups[0].sensor_mask = 0x0FU;
    cfg.groups[0].unit_mask = 0x03U;
    cfg.groups[0].active_turn_count = 1U;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "init failed");
    zassert_equal(s_ao.boot_stabilization_s, 5U, "Initial boot stabilization must be 5s");

    /* Steps 1 to 5: no valid sensors arrived yet (grace window active) */
    for (uint8_t step = 1U; step <= 5U; step++) {
        ret = ao_hvac_step(&s_ao);
        zassert_equal(ret, 0, "step %u failed", step);

        hvac_status_msg_t status;
        ret = ao_hvac_get_status(&s_ao, &status);
        zassert_equal(ret, 0, "get_status failed");
        zassert_equal(status.group_state[0], GROUP_STATE_IDLE,
                      "Group must remain IDLE during boot stabilization (step %u)", step);
        zassert_equal(status.bypass_causes[0] & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT, 0U,
                      "SENSOR_FAULT must NOT trip during boot stabilization (step %u)", step);
    }

    zassert_equal(s_ao.boot_stabilization_s, 0U, "Grace period counter should reach 0 after 5 steps");

    /* Step 6: grace period has expired, and still no valid sensors -> BYPASS_CAUSE_SENSOR_FAULT trips */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step 6 failed");

    hvac_status_msg_t status;
    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.group_state[0], GROUP_STATE_BYPASS,
                  "Group must transition to BYPASS after grace window expires without sensors");
    zassert_not_equal(status.bypass_causes[0] & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT, 0U,
                      "BYPASS_CAUSE_SENSOR_FAULT must be active after grace window expires");

    /* Now feed valid sensor snapshot -> BYPASS_CAUSE_SENSOR_FAULT clears */
    float temps[4] = { 22.0f, 22.0f, 22.0f, 22.0f };
    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, 0x0FULL, 4U);
    zassert_equal(ret, 0, "feed snapshot failed");

    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step after sensor arrival failed");

    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.bypass_causes[0] & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT, 0U,
                  "SENSOR_FAULT must clear once valid sensor data arrives");
}

ZTEST(ao_hvac_suite, test_sensor_snapshot_staleness_watchdog)
{
    config_hvac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.operation_mode = HVAC_MODE_HYBRID_FULL;
    cfg.total_groups = 1U;
    cfg.total_units = 2U;
    cfg.groups[0].temp_min = 21.0f;
    cfg.groups[0].temp_max = 24.0f;
    cfg.groups[0].temp_alarm = 28.0f;
    cfg.groups[0].temp_bypass = 32.0f;
    cfg.groups[0].delay_on_s = 0U;
    cfg.groups[0].sensor_mask = 0x0FU;
    cfg.groups[0].unit_mask = 0x03U;
    cfg.groups[0].active_turn_count = 1U;

    int ret = ao_hvac_init(&s_ao, &cfg, &s_actuator_port, &s_queue, s_test_stack, K_THREAD_STACK_SIZEOF(s_test_stack));
    zassert_equal(ret, 0, "init failed");

    /* 1. Feed valid sensor snapshot (25.0°C >= temp_max 24.0°C) */
    float temps[4] = { 25.0f, 25.0f, 25.0f, 25.0f };
    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, 0x0FULL, 4U);
    zassert_equal(ret, 0, "feed snapshot failed");

    /* Step 1: Group evaluates and cools normally */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step 1 failed");

    hvac_status_msg_t status;
    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.group_state[0], GROUP_STATE_COOLING, "Group must be in COOLING");
    zassert_equal(status.bypass_causes[0] & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT, 0U,
                  "No SENSOR_FAULT initially");

    /* Step 2, 3, 4: No new snapshots fed; snapshot age increases (2, 3, 4s < 5s) */
    for (uint8_t s = 2U; s <= 4U; s++) {
        ret = ao_hvac_step(&s_ao);
        zassert_equal(ret, 0, "step %u failed", s);

        ret = ao_hvac_get_status(&s_ao, &status);
        zassert_equal(ret, 0, "get_status failed");
        zassert_equal(status.group_state[0], GROUP_STATE_COOLING, "Group still in COOLING at step %u", s);
        zassert_equal(status.bypass_causes[0] & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT, 0U,
                      "No SENSOR_FAULT at step %u", s);
    }

    /* Step 5: 5th second without fresh snapshot -> staleness watchdog triggers */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step 5 failed");

    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.group_state[0], GROUP_STATE_BYPASS,
                  "On 5th second without fresh data, group must enter BYPASS");
    zassert_not_equal(status.bypass_causes[0] & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT, 0U,
                      "BYPASS_CAUSE_SENSOR_FAULT must be set on staleness timeout");

    /* 2. Fresh sensor data resumes */
    ret = ao_hvac_feed_sensor_snapshot(&s_ao, temps, 0x0FULL, 4U);
    zassert_equal(ret, 0, "feed fresh snapshot failed");

    /* Step ao_hvac again: clears SENSOR_FAULT and resumes normal cooling */
    ret = ao_hvac_step(&s_ao);
    zassert_equal(ret, 0, "step after fresh sensor data failed");

    ret = ao_hvac_get_status(&s_ao, &status);
    zassert_equal(ret, 0, "get_status failed");
    zassert_equal(status.bypass_causes[0] & (uint32_t)BYPASS_CAUSE_SENSOR_FAULT, 0U,
                  "SENSOR_FAULT must clear once fresh data arrives");
    zassert_equal(status.group_state[0], GROUP_STATE_COOLING,
                  "Group must resume normal COOLING");
}





