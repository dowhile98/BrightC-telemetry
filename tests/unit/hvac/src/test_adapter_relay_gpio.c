/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for adapter_relay_gpio (Internal MCP23S17 relays on BrightC V3).
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "infrastructure/adapters/adapter_relay_gpio.h"
#include "interfaces/i_relay.h"

static adapter_relay_gpio_t s_gpio_relay;

static void setup_fixture(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&s_gpio_relay, 0, sizeof(s_gpio_relay));
}

ZTEST_SUITE(adapter_relay_gpio_suite, NULL, NULL, setup_fixture, NULL, NULL);

ZTEST(adapter_relay_gpio_suite, test_init_and_binding)
{
    int ret = adapter_relay_gpio_init(NULL, NULL, NULL);
    zassert_equal(ret, -EINVAL, "NULL self must fail");

    ret = adapter_relay_gpio_init(&s_gpio_relay, NULL, NULL);
    zassert_equal(ret, 0, "Init must succeed");
    zassert_true(s_gpio_relay.is_ready, "Must be ready");

    i_relay_t port;
    ret = adapter_relay_gpio_port(&s_gpio_relay, &port);
    zassert_equal(ret, 0, "adapter_relay_gpio_port must succeed");
    zassert_true(i_relay_is_bound(&port), "Port handle must be bound");

    i_relay_t direct_port = adapter_relay_gpio_get_port(&s_gpio_relay);
    zassert_true(i_relay_is_bound(&direct_port), "Direct port handle must be bound");
}

ZTEST(adapter_relay_gpio_suite, test_initial_state_open)
{
    (void)adapter_relay_gpio_init(&s_gpio_relay, NULL, NULL);
    i_relay_t port = adapter_relay_gpio_get_port(&s_gpio_relay);

    for (uint32_t ch = 0; ch < ADAPTER_RELAY_GPIO_CHANNEL_COUNT; ch++) {
        relay_state_t state = RELAY_STATE_CLOSED;
        int ret = i_relay_get_state(&port, (relay_channel_t)ch, &state);
        zassert_equal(ret, 0, "get_state must succeed for channel %u", ch);
        zassert_equal(state, RELAY_STATE_OPEN, "Channel %u should initially be OPEN", ch);

        relay_channel_status_t status;
        ret = i_relay_get_status(&port, (relay_channel_t)ch, &status);
        zassert_equal(ret, 0, "get_status must succeed for channel %u", ch);
        zassert_equal(status.state, RELAY_STATE_OPEN, "Status state must be OPEN");
        zassert_equal(status.cycle_count, 0U, "Cycle count must be 0");
        zassert_false(status.is_fault, "Channel must not be in fault");
    }
}

ZTEST(adapter_relay_gpio_suite, test_set_and_get_state_all_channels)
{
    (void)adapter_relay_gpio_init(&s_gpio_relay, NULL, NULL);
    i_relay_t port = adapter_relay_gpio_get_port(&s_gpio_relay);

    for (uint32_t ch = 0; ch < ADAPTER_RELAY_GPIO_CHANNEL_COUNT; ch++) {
        /* 1. Close relay */
        int ret = i_relay_set_state(&port, (relay_channel_t)ch, RELAY_STATE_CLOSED);
        zassert_equal(ret, 0, "set_state CLOSED failed for channel %u", ch);

        relay_state_t state = RELAY_STATE_OPEN;
        ret = i_relay_get_state(&port, (relay_channel_t)ch, &state);
        zassert_equal(ret, 0, "get_state failed");
        zassert_equal(state, RELAY_STATE_CLOSED, "Channel %u must be CLOSED", ch);

        relay_channel_status_t status;
        ret = i_relay_get_status(&port, (relay_channel_t)ch, &status);
        zassert_equal(ret, 0, "get_status failed");
        zassert_equal(status.state, RELAY_STATE_CLOSED, "Status must be CLOSED");
        zassert_equal(status.cycle_count, 1U, "Cycle count must be 1");

        /* 2. Open relay */
        ret = i_relay_set_state(&port, (relay_channel_t)ch, RELAY_STATE_OPEN);
        zassert_equal(ret, 0, "set_state OPEN failed for channel %u", ch);

        ret = i_relay_get_state(&port, (relay_channel_t)ch, &state);
        zassert_equal(ret, 0, "get_state failed");
        zassert_equal(state, RELAY_STATE_OPEN, "Channel %u must be OPEN", ch);

        ret = i_relay_get_status(&port, (relay_channel_t)ch, &status);
        zassert_equal(ret, 0, "get_status failed");
        zassert_equal(status.cycle_count, 2U, "Cycle count must be 2 after toggle back");
    }
}

ZTEST(adapter_relay_gpio_suite, test_boundary_and_error_handling)
{
    (void)adapter_relay_gpio_init(&s_gpio_relay, NULL, NULL);
    i_relay_t port = adapter_relay_gpio_get_port(&s_gpio_relay);

    /* Out of bounds channels */
    int ret = i_relay_set_state(&port, (relay_channel_t)ADAPTER_RELAY_GPIO_CHANNEL_COUNT, RELAY_STATE_CLOSED);
    zassert_not_equal(ret, 0, "Setting channel 18 must fail");

    relay_state_t state;
    ret = i_relay_get_state(&port, (relay_channel_t)ADAPTER_RELAY_GPIO_CHANNEL_COUNT, &state);
    zassert_not_equal(ret, 0, "Getting state for channel 18 must fail");

    relay_channel_status_t status;
    ret = i_relay_get_status(&port, (relay_channel_t)ADAPTER_RELAY_GPIO_CHANNEL_COUNT, &status);
    zassert_not_equal(ret, 0, "Getting status for channel 18 must fail");

    /* Channel 31 */
    ret = i_relay_set_state(&port, (relay_channel_t)31, RELAY_STATE_CLOSED);
    zassert_not_equal(ret, 0, "Setting channel 31 must fail");

    /* Invalid state */
    ret = i_relay_set_state(&port, (relay_channel_t)0, (relay_state_t)42);
    zassert_not_equal(ret, 0, "Invalid state must fail");

    /* NULL state output */
    ret = i_relay_get_state(&port, (relay_channel_t)0, NULL);
    zassert_equal(ret, -EINVAL, "NULL out_state must return -EINVAL");

    /* NULL status output */
    ret = i_relay_get_status(&port, (relay_channel_t)0, NULL);
    zassert_equal(ret, -EINVAL, "NULL out_status must return -EINVAL");
}
