/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_ao_sensor_matrix.c
 * @brief Unit tests for the Sensor Matrix Active Object (canonical Pattern A).
 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include "application/active_objects/ao_sensor_matrix.h"
#include "domain/services/sensor_matrix_mapper.h"
#include "fake_sensor_matrix.h"

/*============================================================================*
 * FIXTURE
 *============================================================================*/

K_THREAD_STACK_DEFINE(s_test_ao_sensor_stack, AO_SENSOR_MATRIX_STACK_SIZE);
static struct k_msgq s_test_ao_sensor_msgq;
static uint8_t s_test_ao_sensor_queue_buf[AO_SENSOR_MATRIX_QUEUE_DEPTH *
                                           sizeof(ao_sensor_matrix_event_t)];
static ao_sensor_matrix_t s_test_ao_sensor;

static uint32_t s_sink_calls;
static thermal_matrix_frame_t s_last_frame;

static void test_frame_sink(void *ctx, const thermal_matrix_frame_t *frame)
{
    ARG_UNUSED(ctx);
    s_sink_calls++;
    if (frame != NULL) {
        s_last_frame = *frame;
    }
}

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

    s_sink_calls = 0U;
    memset(&s_last_frame, 0, sizeof(s_last_frame));

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

static int sensor_ao_init(ao_sensor_matrix_frame_sink_t sink)
{
    /* The port handle must outlive the AO, so it lives in static storage. */
    static i_sensor_matrix_t s_bound_port;
    s_bound_port = fake_sensor_matrix_get_port();

    const ao_sensor_matrix_config_t cfg = {
        .matrix = &s_bound_port,
        .frame_sink = sink,
        .frame_sink_ctx = NULL,
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
 * DETERMINISTIC STEP
 *============================================================================*/

ZTEST(sensor_ao, test_step_drives_update_and_forwards_frame)
{
    thermal_matrix_frame_t frame = {
        .configured_pairs = 1U,
        .t_inj = {21.5f},
        .t_ret = {26.5f},
        .pair_valid_mask = 0x1U,
    };

    zassert_ok(sensor_ao_init(test_frame_sink));
    fake_sensor_matrix_set_frame(&frame);

    zassert_ok(ao_sensor_matrix_step(&s_test_ao_sensor, 50U));

    const fake_sensor_matrix_state_t *fake = fake_sensor_matrix_get_state();
    zassert_equal(fake->update_calls, 1U, "update must be triggered exactly once");
    zassert_equal(fake->get_frame_calls, 1U, "the mapped frame must be read once");
    zassert_equal(s_sink_calls, 1U, "the sink must receive the frame once");
    zassert_within(s_last_frame.t_inj[0], 21.5f, 0.001f);
}

ZTEST(sensor_ao, test_step_without_sink_skips_frame_read)
{
    zassert_ok(sensor_ao_init(NULL));
    zassert_ok(ao_sensor_matrix_step(&s_test_ao_sensor, 50U));

    const fake_sensor_matrix_state_t *fake = fake_sensor_matrix_get_state();
    zassert_equal(fake->update_calls, 1U);
    zassert_equal(fake->get_frame_calls, 0U);
    zassert_equal(s_sink_calls, 0U);
}

ZTEST(sensor_ao, test_step_propagates_update_failure)
{
    zassert_ok(sensor_ao_init(test_frame_sink));

    fake_sensor_matrix_set_update_ret(-EIO);

    zassert_equal(ao_sensor_matrix_step(&s_test_ao_sensor, 50U), -EIO);
    zassert_equal(s_sink_calls, 0U, "no frame must be published on failure");
    zassert_equal(s_test_ao_sensor.error_count, 1U);

    /* A subsequent success clears the consecutive error counter. */
    fake_sensor_matrix_set_update_ret(0);
    zassert_ok(ao_sensor_matrix_step(&s_test_ao_sensor, 50U));
    zassert_equal(s_test_ao_sensor.error_count, 0U);
}

ZTEST(sensor_ao, test_step_after_deinit_is_rejected)
{
    zassert_ok(sensor_ao_init(test_frame_sink));
    zassert_ok(ao_sensor_matrix_deinit(&s_test_ao_sensor));

    zassert_equal(ao_sensor_matrix_step(&s_test_ao_sensor, 50U), -EINVAL);
}

/*============================================================================*
 * LIFECYCLE CONTRACT
 *============================================================================*/

ZTEST(sensor_ao, test_start_stop_cycle)
{
    zassert_ok(sensor_ao_init(test_frame_sink));

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
    zassert_ok(sensor_ao_init(test_frame_sink));
    zassert_ok(ao_sensor_matrix_stop(&s_test_ao_sensor));
}

ZTEST(sensor_ao, test_deinit_rejects_running_instance)
{
    zassert_ok(sensor_ao_init(test_frame_sink));
    zassert_ok(ao_sensor_matrix_start(&s_test_ao_sensor));

    zassert_equal(ao_sensor_matrix_deinit(&s_test_ao_sensor), -EBUSY);

    zassert_ok(ao_sensor_matrix_stop(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_wait_stopped(&s_test_ao_sensor, K_MSEC(1000)));
    zassert_ok(ao_sensor_matrix_deinit(&s_test_ao_sensor));
    zassert_false(s_test_ao_sensor.is_initialized);
}

ZTEST(sensor_ao, test_trigger_requires_running_instance)
{
    zassert_ok(sensor_ao_init(test_frame_sink));

    zassert_equal(ao_sensor_matrix_trigger(&s_test_ao_sensor), -EAGAIN);

    zassert_ok(ao_sensor_matrix_start(&s_test_ao_sensor));
    zassert_ok(ao_sensor_matrix_trigger(&s_test_ao_sensor));
}

/*============================================================================*
 * SUITE REGISTRATION
 *============================================================================*/

ZTEST_SUITE(sensor_ao, NULL, sensor_ao_suite_setup, sensor_ao_before, sensor_ao_after, NULL);
