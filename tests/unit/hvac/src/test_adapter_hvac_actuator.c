/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for adapter_hvac_actuator.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "common/hvac_types.h"
#include "fake_relay.h"
#include "infrastructure/adapters/adapter_hvac_actuator.h"
#include "interfaces/i_hvac_unit.h"

static adapter_hvac_actuator_t s_adapter;
static i_relay_t s_remote_relay;
static i_relay_t s_local_relay;
static hvac_unit_hw_config_t s_hw_configs[HVAC_MAX_UNITS_TOTAL];

static void setup_fixture(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_relay_reset();
    s_remote_relay = fake_relay_get_port();
    s_local_relay = fake_relay_get_port();

    memset(s_hw_configs, 0, sizeof(s_hw_configs));

    /* Unit 0: Remote Modbus relay endpoints (aa=0, sm=1, g=2) */
    s_hw_configs[0].relay_aa.backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS;
    s_hw_configs[0].relay_aa.channel = 0U;

    s_hw_configs[0].relay_sm.backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS;
    s_hw_configs[0].relay_sm.channel = 1U;

    s_hw_configs[0].relay_g.backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS;
    s_hw_configs[0].relay_g.channel = 2U;

    /* Unit 1: Local GPIO relay endpoints (aa=3, sm=4, g=5) */
    s_hw_configs[1].relay_aa.backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO;
    s_hw_configs[1].relay_aa.channel = 3U;

    s_hw_configs[1].relay_sm.backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO;
    s_hw_configs[1].relay_sm.channel = 4U;

    s_hw_configs[1].relay_g.backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO;
    s_hw_configs[1].relay_g.channel = 5U;
}

ZTEST_SUITE(adapter_hvac_actuator_suite, NULL, NULL, setup_fixture, NULL, NULL);

ZTEST(adapter_hvac_actuator_suite, test_init_and_port_binding)
{
    int ret = adapter_hvac_actuator_init(&s_adapter, s_hw_configs, 2U, &s_remote_relay, &s_local_relay);
    zassert_equal(ret, 0, "Init failed");

    i_hvac_unit_t port = adapter_hvac_actuator_port(&s_adapter);
    zassert_true(i_hvac_unit_is_bound(&port), "Port must be bound");

    hvac_tri_state_t tri = HVAC_TRI_STATE_ON;
    ret = i_hvac_unit_get_tri_state(&port, 0, &tri);
    zassert_equal(ret, 0, "get_tri_state failed");
    zassert_equal(tri, HVAC_TRI_STATE_OFF, "Initial cached state should be OFF");
}

ZTEST(adapter_hvac_actuator_suite, test_tri_state_remote_modbus_mapping)
{
    (void)adapter_hvac_actuator_init(&s_adapter, s_hw_configs, 2U, &s_remote_relay, &s_local_relay);
    i_hvac_unit_t port = adapter_hvac_actuator_port(&s_adapter);

    /* 1. Set to ON: aa = closed (1), sm = closed (1), g = open (0) */
    int ret = i_hvac_unit_set_tri_state(&port, 0, HVAC_TRI_STATE_ON);
    zassert_equal(ret, 0, "set_tri_state ON failed");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_0), RELAY_STATE_CLOSED, "relay_aa must be CLOSED");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_1), RELAY_STATE_CLOSED, "relay_sm must be CLOSED");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_2), RELAY_STATE_OPEN, "relay_g must be OPEN");

    hvac_tri_state_t read_tri = HVAC_TRI_STATE_OFF;
    ret = i_hvac_unit_get_tri_state(&port, 0, &read_tri);
    zassert_equal(ret, 0, "get_tri_state failed");
    zassert_equal(read_tri, HVAC_TRI_STATE_ON, "Cached tri-state must be ON");

    /* 2. Set to OFF: aa = open (0), sm = closed (1), g = open (0) */
    ret = i_hvac_unit_set_tri_state(&port, 0, HVAC_TRI_STATE_OFF);
    zassert_equal(ret, 0, "set_tri_state OFF failed");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_0), RELAY_STATE_OPEN, "relay_aa must be OPEN");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_1), RELAY_STATE_CLOSED, "relay_sm must be CLOSED");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_2), RELAY_STATE_OPEN, "relay_g must be OPEN");

    ret = i_hvac_unit_get_tri_state(&port, 0, &read_tri);
    zassert_equal(ret, 0, "get_tri_state failed");
    zassert_equal(read_tri, HVAC_TRI_STATE_OFF, "Cached tri-state must be OFF");

    /* 3. Set to BYPASS: aa = open (0), sm = open (0), g = open (0) */
    ret = i_hvac_unit_set_tri_state(&port, 0, HVAC_TRI_STATE_BYPASS);
    zassert_equal(ret, 0, "set_tri_state BYPASS failed");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_0), RELAY_STATE_OPEN, "relay_aa must be OPEN");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_1), RELAY_STATE_OPEN, "relay_sm must be OPEN");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_2), RELAY_STATE_OPEN, "relay_g must be OPEN");

    ret = i_hvac_unit_get_tri_state(&port, 0, &read_tri);
    zassert_equal(ret, 0, "get_tri_state failed");
    zassert_equal(read_tri, HVAC_TRI_STATE_BYPASS, "Cached tri-state must be BYPASS");
}

ZTEST(adapter_hvac_actuator_suite, test_tri_state_local_gpio_mapping)
{
    (void)adapter_hvac_actuator_init(&s_adapter, s_hw_configs, 2U, &s_remote_relay, &s_local_relay);
    i_hvac_unit_t port = adapter_hvac_actuator_port(&s_adapter);

    /* Unit 1 uses local GPIO relay channels 3, 4, 5 */
    int ret = i_hvac_unit_set_tri_state(&port, 1, HVAC_TRI_STATE_ON);
    zassert_equal(ret, 0, "set_tri_state ON failed");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_3), RELAY_STATE_CLOSED, "local aa must be CLOSED");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_4), RELAY_STATE_CLOSED, "local sm must be CLOSED");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_5), RELAY_STATE_OPEN, "local g must be OPEN");

    ret = i_hvac_unit_set_tri_state(&port, 1, HVAC_TRI_STATE_BYPASS);
    zassert_equal(ret, 0, "set_tri_state BYPASS failed");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_3), RELAY_STATE_OPEN, "local aa must be OPEN");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_4), RELAY_STATE_OPEN, "local sm must be OPEN");
    zassert_equal(fake_relay_get_channel_state(RELAY_CHANNEL_5), RELAY_STATE_OPEN, "local g must be OPEN");
}

ZTEST(adapter_hvac_actuator_suite, test_error_handling_and_validation)
{
    (void)adapter_hvac_actuator_init(&s_adapter, s_hw_configs, 2U, &s_remote_relay, &s_local_relay);
    i_hvac_unit_t port = adapter_hvac_actuator_port(&s_adapter);

    /* Out of bounds unit ID */
    int ret = i_hvac_unit_set_tri_state(&port, HVAC_MAX_UNITS_TOTAL, HVAC_TRI_STATE_ON);
    zassert_equal(ret, -EINVAL, "Should reject invalid unit_id");

    /* Invalid tri state */
    ret = i_hvac_unit_set_tri_state(&port, 0, (hvac_tri_state_t)99);
    zassert_equal(ret, -EINVAL, "Should reject invalid tri-state");

    /* Simulated relay hardware error */
    fake_relay_set_simulated_error(-EIO);
    ret = i_hvac_unit_set_tri_state(&port, 0, HVAC_TRI_STATE_ON);
    zassert_equal(ret, -EIO, "Should propagate relay error");
    fake_relay_set_simulated_error(0);
}
