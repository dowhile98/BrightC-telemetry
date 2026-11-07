/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_ao_sensor_matrix.c
 * @brief Unit tests for the Sensor Matrix Active Object (canonical Pattern A) with Zbus decoupling.
 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include "application/active_objects/ao_sensor_matrix.h"
#include "application/zbus/sensor_channels.h"
#include "domain/services/sensor_matrix_mapper.h"
#include "fake_sensor_matrix.h"

/*============================================================================*
 * FIXTURE & ZBUS OBSERVER
 *============================================================================*/

K_THREAD_STACK_DEFINE(s_test_ao_sensor_stack, AO_SENSOR_MATRIX_STACK_SIZE);
static struct k_msgq s_test_ao_sensor_msgq;
static uint8_t s_test_ao_sensor_queue_buf[AO_SENSOR_MATRIX_QUEUE_DEPTH *
                                           sizeof(ao_sensor_matrix_event_t)];
static ao_sensor_matrix_t s_test_ao_sensor;

static uint32_t s_hvac_feed_calls;
static sensor_snapshot_msg_t s_last_snapshot;

static void test_hvac_sensor_feed_cb(const struct zbus_channel *chan)
{
    if (chan == &chan_sensor_snapshot) {
        const sensor_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
        if (msg != NULL) {
            s_hvac_feed_calls++;
            s_last_snapshot = *msg;
        }
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_sensor_feed, test_hvac_sensor_feed_cb);

static void *sensor_ao_suite_setup(void)
{
    k_msgq_init(&s_test_ao_sensor_msgq, s_test_ao_sensor_queue_buf,
                sizeof(ao_sensor_matrix_event_t), AO_SENSOR_MATRIX_QUEUE_DEPTH);
    return NULL;
}

static void sensor_ao_before(void *fixture)
{
    ARG_UNUSED(fixture);

    fake_sensor_matrix_reset();
    k_msgq_purge(&s_test_ao_sensor_msgq);

    s_hvac_feed_calls = 0U;
    memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));

    memset(&s_test_ao_sensor, 0, sizeof(s_test_ao_sensor));
}

static void sensor_ao_after(void *fixture)
{
    ARG_UNUSED(fixture);

    if (s_test_ao_sensor.is_initialized) {
        (void)ao_sensor_matrix_stop(&s_test_ao_sensor);
        (void)ao_sensor_matrix_wait_stopped(&s_test_ao_sensor, K_MSEC(500));
        (void)ao_sensor_matrix_deinit(&s_test_ao_sensor);
    }
}

static int sensor_ao_init(void)
{
    /* The port handle must outlive the AO, so it lives in static storage. */
    static i_sensor_matrix_t s_bound_port;
    s_bound_port = fake_sensor_matrix_get_port();

    const ao_sensor_matrix_config_t cfg = {
        .matrix = &s_bound_port,
        .poll_period_ms = 50U,
        .stack = s_test_ao_sensor_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(s_test_ao_sensor_stack),
    };

    return ao_sensor_matrix_init(&s_test_ao_sensor, &cfg, &s_test_ao_sensor_msgq);
}

/*============================================================================*
 * INITIALISATION CONTRACT
 *============================================================================*/

ZTEST(sensor_ao, test_init_rejects_invalid_arguments)
{
    i_sensor_matrix_t port = fake_sensor_matrix_get_port();
    const ao_sensor_matrix_config_t cfg = {
        .matrix = &port,
        .stack = s_test_ao_sensor_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(s_test_ao_sensor_stack),
    };

    zassert_equal(ao_sensor_matrix_init(NULL, &cfg, &s_test_ao_sensor_msgq), -EINVAL);
    zassert_equal(ao_sensor_matrix_init(&s_test_ao_sensor, NULL, &s_test_ao_sensor_msgq), -EINVAL);
    zassert_equal(ao_sensor_matrix_init(&s_test_ao_sensor, &cfg, NULL), -EINVAL);

    ao_sensor_matrix_config_t bad = cfg;
    bad.stack = NULL;
    zassert_equal(ao_sensor_matrix_init(&s_test_ao_sensor, &bad, &s_test_ao_sensor_msgq), -EINVAL);

    bad = cfg;
    bad.stack_size = 0U;
    zassert_equal(ao_sensor_matrix_init(&s_test_ao_sensor, &bad, &s_test_ao_sensor_msgq), -EINVAL);
}

ZTEST(sensor_ao, test_init_rejects_unbound_port)
{
    const i_sensor_matrix_t unbound = {0};
    const ao_sensor_matrix_config_t cfg = {
        .matrix = &unbound,
        .stack = s_test_ao_sensor_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(s_test_ao_sensor_stack),
    };

    zassert_equal(ao_sensor_matrix_init(&s_test_ao_sensor, &cfg, &s_test_ao_sensor_msgq), -EINVAL);
    zassert_false(s_test_ao_sensor.is_initialized);
}

/*============================================================================*
 * DETERMINISTIC STEP & ZBUS PUBLISHING
 *============================================================================*/

ZTEST(sensor_ao, test_step_drives_update_and_publishes_zbus)
{
    thermal_matrix_frame_t frame = {
        .configured_pairs = 1U,
        .t_inj = {21.5f},
        .t_ret = {26.5f},
        .pair_valid_mask = 0x1U,
    };

    raw_sensor_readings_t raw = {0};
    raw.cwt_comm_ok[0] = true;
    raw.cwt_valid_mask[0] = 0x0001U; /* CWT1 CH1 (flat ID 10, index 9) */
    raw.cwt_temperatures_c[0][0] = 23.5f;

    zassert_ok(sensor_ao_init());
    fake_sensor_matrix_set_frame(&frame);
    fake_sensor_matrix_set_raw_readings(&raw);

    zassert_ok(ao_sensor_matrix_step(&s_test_ao_sensor, 50U));

    const fake_sensor_matrix_state_t *fake = fake_sensor_matrix_get_state();
    zassert_equal(fake->update_calls, 1U, "update must be triggered exactly once");
    zassert_equal(fake->get_frame_calls, 1U, "the mapped frame must be read once");
    zassert_equal(fake->get_raw_calls, 1U, "raw readings must be read once");

    /* Verify listener was notified synchronously via chan_sensor_snapshot */
    zassert_equal(s_hvac_feed_calls, 1U, "lis_hvac_sensor_feed listener must be notified");
    uint64_t expected_bit = 1ULL << 9; /* Flat ID 10 */
    zassert_equal(s_last_snapshot.valid_mask & expected_bit, expected_bit, "Bit 9 must be valid");
    zassert_within(s_last_snapshot.temperatures[9], 23.5f, 0.001f, "Channel reading match");
    zassert_equal(s_last_snapshot.sequence, 0U, "Sequence number should start at 0");

    /* Verify chan_thermal_matrix publication via zbus_chan_read */
    thermal_matrix_frame_t rx_frame;
    zassert_ok(zbus_chan_read(&chan_thermal_matrix, &rx_frame, K_NO_WAIT), "Read chan_thermal_matrix");
    zassert_within(rx_frame.t_inj[0], 21.5f, 0.001f, "Supply temp match");
    zassert_within(rx_frame.t_ret[0], 26.5f, 0.001f, "Return temp match");
}

ZTEST(sensor_ao, test_step_increments_sequence_number)
{
    raw_sensor_readings_t raw = {0};
    raw.cwt_comm_ok[0] = true;
    raw.cwt_valid_mask[0] = 0x0001U;
    raw.cwt_temperatures_c[0][0] = 25.0f;

    zassert_ok(sensor_ao_init());
    fake_sensor_matrix_set_raw_readings(&raw);

    zassert_ok(ao_sensor_matrix_step(&s_test_ao_sensor, 50U));
    zassert_equal(s_hvac_feed_calls, 1U);
    zassert_equal(s_last_snapshot.sequence, 0U);

    zassert_ok(ao_sensor_matrix_step(&s_test_ao_sensor, 50U));
    zassert_equal(s_hvac_feed_calls, 2U);
    zassert_equal(s_last_snapshot.sequence, 1U);
}

ZTEST(sensor_ao, test_step_propagates_update_failure)
{
    zassert_ok(sensor_ao_init());

    fake_sensor_matrix_set_update_ret(-EIO);

    zassert_equal(ao_sensor_matrix_step(&s_test_ao_sensor, 50U), -EIO);
    zassert_equal(s_hvac_feed_calls, 0U, "no snapshot must be published on failure");
    zassert_equal(s_test_ao_sensor.error_count, 1U);

    /* A subsequent success clears the consecutive error counter. */
    fake_sensor_matrix_set_update_ret(0);
    zassert_ok(ao_sensor_matrix_step(&s_test_ao_sensor, 50U));
    zassert_equal(s_test_ao_sensor.error_count, 0U);
    zassert_equal(s_hvac_feed_calls, 1U);
}

ZTEST(sensor_ao, test_step_after_deinit_is_rejected)
{
    zassert_ok(sensor_ao_init());
    zassert_ok(ao_sensor_matrix_deinit(&s_test_ao_sensor));

    zassert_equal(ao_sensor_matrix_step(&s_test_ao_sensor, 50U), -EINVAL);
}

/*============================================================================*
 * LIFECYCLE CONTRACT
 *============================================================================*/

ZTEST(sensor_ao, test_start_stop_cycle)
{
    zassert_ok(sensor_ao_init());

    zassert_false(ao_sensor_matrix_is_running(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_start(&s_test_ao_sensor));
    zassert_true(ao_sensor_matrix_is_running(&s_test_ao_sensor));
    zassert_equal(ao_sensor_matrix_start(&s_test_ao_sensor), -EALREADY);

    zassert_ok(ao_sensor_matrix_stop(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_wait_stopped(&s_test_ao_sensor, K_MSEC(1000)));
    zassert_false(ao_sensor_matrix_is_running(&s_test_ao_sensor));

    /* The thread is never destroyed: a second cycle must succeed. */
    zassert_ok(ao_sensor_matrix_start(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_stop(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_wait_stopped(&s_test_ao_sensor, K_MSEC(1000)));
}

ZTEST(sensor_ao, test_stop_when_not_running_is_noop)
{
    zassert_ok(sensor_ao_init());
    zassert_ok(ao_sensor_matrix_stop(&s_test_ao_sensor));
}

ZTEST(sensor_ao, test_deinit_rejects_running_instance)
{
    zassert_ok(sensor_ao_init());
    zassert_ok(ao_sensor_matrix_start(&s_test_ao_sensor));

    zassert_equal(ao_sensor_matrix_deinit(&s_test_ao_sensor), -EBUSY);

    zassert_ok(ao_sensor_matrix_stop(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_wait_stopped(&s_test_ao_sensor, K_MSEC(1000)));
    zassert_ok(ao_sensor_matrix_deinit(&s_test_ao_sensor));
    zassert_false(s_test_ao_sensor.is_initialized);
}

ZTEST(sensor_ao, test_trigger_requires_running_instance)
{
    zassert_ok(sensor_ao_init());

    zassert_equal(ao_sensor_matrix_trigger(&s_test_ao_sensor), -EAGAIN);

    zassert_ok(ao_sensor_matrix_start(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_trigger(&s_test_ao_sensor));
}

/*============================================================================*
 * SUITE REGISTRATION
 *============================================================================*/

ZTEST_SUITE(sensor_ao, NULL, sensor_ao_suite_setup, sensor_ao_before, sensor_ao_after, NULL);
