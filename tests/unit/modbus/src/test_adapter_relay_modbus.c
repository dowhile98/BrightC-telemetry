/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for adapter_relay_modbus (Waveshare Modbus RTU 32CH relay adapter).
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "application/active_objects/ao_modbus.h"
#include "domain/services/waveshare_relay_codec.h"
#include "infrastructure/adapters/adapter_relay_modbus.h"
#include "interfaces/i_relay.h"

#define TEST_SLAVE_ADDR 4U
#define TEST_TIMEOUT_MS 300U

static struct k_msgq s_test_queue;
static modbus_event_t s_queue_buf[4];
static ao_modbus_t s_test_ao;
static adapter_relay_modbus_t s_adapter;
static i_relay_t s_relay;

static void setup_fixture(void *fixture)
{
    ARG_UNUSED(fixture);
    k_msgq_init(&s_test_queue, (char *) s_queue_buf, sizeof(modbus_event_t), 4);
    (void) memset(&s_test_ao, 0, sizeof(s_test_ao));
    s_test_ao.queue = &s_test_queue;

    (void) memset(&s_adapter, 0, sizeof(s_adapter));
    (void) memset(&s_relay, 0, sizeof(s_relay));
}

ZTEST_SUITE(adapter_relay_modbus_suite, NULL, NULL, setup_fixture, NULL, NULL);

ZTEST(adapter_relay_modbus_suite, test_init_and_port_binding)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "adapter_relay_modbus_init failed (%d)", err);

    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "adapter_relay_modbus_port failed (%d)", err);
    zassert_true(i_relay_is_bound(&s_relay), "Port must be bound");

    /* Default states must be OPEN and status initialised */
    for (relay_channel_t ch = 0; ch < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; ch++) {
        relay_state_t state = RELAY_STATE_CLOSED;
        err = i_relay_get_state(&s_relay, ch, &state);
        zassert_equal(err, 0, "get_state failed for channel %d", ch);
        zassert_equal(state, RELAY_STATE_OPEN, "Default state for channel %d must be OPEN", ch);

        relay_channel_status_t status;
        err = i_relay_get_status(&s_relay, ch, &status);
        zassert_equal(err, 0, "get_status failed for channel %d", ch);
        zassert_equal(status.state, RELAY_STATE_OPEN, "Default status state for channel %d must be OPEN", ch);
        zassert_equal(status.cycle_count, 0U, "Default cycle_count for channel %d must be 0", ch);
        zassert_equal(status.last_toggle_ms, 0U, "Default last_toggle_ms for channel %d must be 0", ch);
        zassert_false(status.is_fault, "Default is_fault for channel %d must be false", ch);
    }
}

ZTEST(adapter_relay_modbus_suite, test_init_rejects_invalid_arguments)
{
    zassert_equal(adapter_relay_modbus_init(NULL, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS),
                  -EINVAL, "NULL self must be rejected");
    zassert_equal(adapter_relay_modbus_init(&s_adapter, NULL, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS),
                  -EINVAL, "NULL ao must be rejected");
    zassert_equal(adapter_relay_modbus_init(&s_adapter, &s_test_ao, 0U, TEST_TIMEOUT_MS),
                  -EINVAL, "Zero slave_addr must be rejected");
    zassert_equal(adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, 0),
                  -EINVAL, "Zero timeout must be rejected");
    zassert_equal(adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, -1),
                  -EINVAL, "Negative timeout must be rejected");

    zassert_equal(adapter_relay_modbus_port(NULL, &s_relay), -EINVAL, "NULL self must be rejected");
    zassert_equal(adapter_relay_modbus_port(&s_adapter, NULL), -EINVAL, "NULL out must be rejected");
}

ZTEST(adapter_relay_modbus_suite, test_set_state_posts_event_and_updates_cache)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "Init failed");
    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "Port failed");

    /* Set channel 5 to CLOSED */
    err = i_relay_set_state(&s_relay, (relay_channel_t) 5, RELAY_STATE_CLOSED);
    zassert_equal(err, 0, "set_state channel 5 CLOSED failed");

    /* Verify event in queue */
    modbus_event_t event;
    err = k_msgq_get(&s_test_queue, &event, K_NO_WAIT);
    zassert_equal(err, 0, "Event not found in queue");
    zassert_equal(event.type, AO_MODBUS_EVENT_WRITE_COIL, "Event type mismatch");
    zassert_equal(event.slave_addr, TEST_SLAVE_ADDR, "Slave address mismatch");
    zassert_equal(event.coil_addr, 5U, "Coil address mismatch");
    zassert_equal(event.coil_value, WAVESHARE_RELAY_COIL_ON, "Coil value mismatch (expected 0xFF00)");
    zassert_equal(event.timeout_ms, TEST_TIMEOUT_MS, "Timeout mismatch");

    /* Verify cache updated optimistically */
    relay_state_t state = RELAY_STATE_OPEN;
    err = i_relay_get_state(&s_relay, (relay_channel_t) 5, &state);
    zassert_equal(err, 0, "get_state failed");
    zassert_equal(state, RELAY_STATE_CLOSED, "Cache state must be CLOSED");

    /* Set channel 5 back to OPEN */
    err = i_relay_set_state(&s_relay, (relay_channel_t) 5, RELAY_STATE_OPEN);
    zassert_equal(err, 0, "set_state channel 5 OPEN failed");

    err = k_msgq_get(&s_test_queue, &event, K_NO_WAIT);
    zassert_equal(err, 0, "Event not found in queue");
    zassert_equal(event.coil_addr, 5U, "Coil address mismatch");
    zassert_equal(event.coil_value, WAVESHARE_RELAY_COIL_OFF, "Coil value mismatch (expected 0x0000)");

    err = i_relay_get_state(&s_relay, (relay_channel_t) 5, &state);
    zassert_equal(err, 0, "get_state failed");
    zassert_equal(state, RELAY_STATE_OPEN, "Cache state must be OPEN");
}

ZTEST(adapter_relay_modbus_suite, test_set_and_get_state_rejects_out_of_range_channels)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "Init failed");
    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "Port failed");

    relay_state_t state;
    /* Out-of-range channel >= WAVESHARE_RELAY_32CH_CHANNEL_COUNT */
    zassert_equal(i_relay_set_state(&s_relay, (relay_channel_t) 32, RELAY_STATE_CLOSED),
                  -EINVAL, "Channel 32 must be rejected");
    zassert_equal(i_relay_get_state(&s_relay, (relay_channel_t) 32, &state),
                  -EINVAL, "Channel 32 get must be rejected");

    zassert_equal(i_relay_get_state(&s_relay, (relay_channel_t) 0, NULL),
                  -EINVAL, "NULL out_state must be rejected");

    /* Queue should still be empty */
    zassert_equal(k_msgq_num_used_get(&s_test_queue), 0, "Queue must remain empty on rejected calls");
}

ZTEST(adapter_relay_modbus_suite, test_queue_full_returns_enomsg)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "Init failed");
    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "Port failed");

    /* Fill the queue (depth = 4) */
    for (int i = 0; i < 4; i++) {
        err = i_relay_set_state(&s_relay, (relay_channel_t) i, RELAY_STATE_CLOSED);
        zassert_equal(err, 0, "Queue fill %d failed", i);
    }

    /* 5th call must return -ENOMSG */
    err = i_relay_set_state(&s_relay, (relay_channel_t) 4, RELAY_STATE_CLOSED);
    zassert_equal(err, -ENOMSG, "Full queue must yield -ENOMSG");
}

ZTEST(adapter_relay_modbus_suite, test_update_coils_synchronizes_cache)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "Init failed");
    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "Port failed");

    relay_state_t new_states[WAVESHARE_RELAY_32CH_CHANNEL_COUNT];
    for (size_t i = 0; i < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; i++) {
        new_states[i] = (i % 2 == 0) ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
    }

    err = adapter_relay_modbus_update_coils(&s_adapter, new_states, WAVESHARE_RELAY_32CH_CHANNEL_COUNT);
    zassert_equal(err, 0, "update_coils failed");

    for (relay_channel_t ch = 0; ch < WAVESHARE_RELAY_32CH_CHANNEL_COUNT; ch++) {
        relay_state_t state;
        err = i_relay_get_state(&s_relay, ch, &state);
        zassert_equal(err, 0, "get_state failed");
        relay_state_t expected = (ch % 2 == 0) ? RELAY_STATE_CLOSED : RELAY_STATE_OPEN;
        zassert_equal(state, expected, "Channel %d state mismatch after update", ch);
    }

    /* Rejection of invalid arguments */
    zassert_equal(adapter_relay_modbus_update_coils(NULL, new_states, 32), -EINVAL, "NULL self");
    zassert_equal(adapter_relay_modbus_update_coils(&s_adapter, NULL, 32), -EINVAL, "NULL states");
    zassert_equal(adapter_relay_modbus_update_coils(&s_adapter, new_states, 0), -EINVAL, "Zero count");
    zassert_equal(adapter_relay_modbus_update_coils(&s_adapter, new_states, 33), -EINVAL, "Count > 32");
}

ZTEST(adapter_relay_modbus_suite, test_update_from_raw_parses_and_synchronizes)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "Init failed");
    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "Port failed");

    /* Byte 0 = 0x05 (ch0 and ch2 CLOSED) */
    /* Byte 1 = 0x80 (ch15 CLOSED) */
    /* Byte 2 = 0x00 */
    /* Byte 3 = 0x01 (ch24 CLOSED) */
    uint8_t raw_bytes[WAVESHARE_RELAY_COIL_BYTES] = {0x05, 0x80, 0x00, 0x01};

    err = adapter_relay_modbus_update_from_raw(&s_adapter, raw_bytes, sizeof(raw_bytes));
    zassert_equal(err, 0, "update_from_raw failed");

    relay_state_t state;
    zassert_equal(i_relay_get_state(&s_relay, (relay_channel_t) 0, &state), 0, "get ch0");
    zassert_equal(state, RELAY_STATE_CLOSED, "ch0 must be CLOSED");

    zassert_equal(i_relay_get_state(&s_relay, (relay_channel_t) 1, &state), 0, "get ch1");
    zassert_equal(state, RELAY_STATE_OPEN, "ch1 must be OPEN");

    zassert_equal(i_relay_get_state(&s_relay, (relay_channel_t) 2, &state), 0, "get ch2");
    zassert_equal(state, RELAY_STATE_CLOSED, "ch2 must be CLOSED");

    zassert_equal(i_relay_get_state(&s_relay, (relay_channel_t) 15, &state), 0, "get ch15");
    zassert_equal(state, RELAY_STATE_CLOSED, "ch15 must be CLOSED");

    zassert_equal(i_relay_get_state(&s_relay, (relay_channel_t) 24, &state), 0, "get ch24");
    zassert_equal(state, RELAY_STATE_CLOSED, "ch24 must be CLOSED");

    /* Short buffer rejected */
    zassert_equal(adapter_relay_modbus_update_from_raw(&s_adapter, raw_bytes, 3U), -EINVAL, "Short buffer");
}

ZTEST(adapter_relay_modbus_suite, test_get_status_tracks_cycle_count_and_timing)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "Init failed");
    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "Port failed");

    relay_channel_status_t status;

    /* First toggle */
    err = i_relay_set_state(&s_relay, (relay_channel_t) 7, RELAY_STATE_CLOSED);
    zassert_equal(err, 0, "set_state channel 7 CLOSED failed");

    err = i_relay_get_status(&s_relay, (relay_channel_t) 7, &status);
    zassert_equal(err, 0, "get_status failed");
    zassert_equal(status.state, RELAY_STATE_CLOSED, "State mismatch");
    zassert_equal(status.cycle_count, 1U, "Cycle count must be 1");
    zassert_false(status.is_fault, "is_fault must be false");

    /* Second toggle */
    err = i_relay_set_state(&s_relay, (relay_channel_t) 7, RELAY_STATE_OPEN);
    zassert_equal(err, 0, "set_state channel 7 OPEN failed");

    err = i_relay_get_status(&s_relay, (relay_channel_t) 7, &status);
    zassert_equal(err, 0, "get_status failed");
    zassert_equal(status.state, RELAY_STATE_OPEN, "State mismatch");
    zassert_equal(status.cycle_count, 2U, "Cycle count must be 2");
    zassert_false(status.is_fault, "is_fault must be false");

    /* Other channel remains unaffected */
    err = i_relay_get_status(&s_relay, (relay_channel_t) 8, &status);
    zassert_equal(err, 0, "get_status failed");
    zassert_equal(status.state, RELAY_STATE_OPEN, "Channel 8 must remain OPEN");
    zassert_equal(status.cycle_count, 0U, "Channel 8 cycle count must be 0");
}

ZTEST(adapter_relay_modbus_suite, test_get_status_rejects_invalid_arguments)
{
    int err = adapter_relay_modbus_init(&s_adapter, &s_test_ao, TEST_SLAVE_ADDR, TEST_TIMEOUT_MS);
    zassert_equal(err, 0, "Init failed");
    err = adapter_relay_modbus_port(&s_adapter, &s_relay);
    zassert_equal(err, 0, "Port failed");

    relay_channel_status_t status;

    /* Out of range channel */
    zassert_equal(i_relay_get_status(&s_relay, (relay_channel_t) 32, &status),
                  -EINVAL, "Channel 32 must be rejected");

    /* NULL output */
    zassert_equal(i_relay_get_status(&s_relay, (relay_channel_t) 0, NULL),
                  -EINVAL, "NULL out_status must be rejected");

    /* Unbound port */
    i_relay_t unbound = {0};
    zassert_equal(i_relay_get_status(&unbound, (relay_channel_t) 0, &status),
                  -EINVAL, "Unbound port must be rejected");

    /* NULL handle */
    zassert_equal(i_relay_get_status(NULL, (relay_channel_t) 0, &status),
                  -EINVAL, "NULL handle must be rejected");
}
