/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the fake_sensor_matrix test double.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "fake_sensor_matrix.h"

ZTEST_SUITE(fake_sensor_matrix_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(fake_sensor_matrix_suite, test_fake_sensor_matrix_basic_operations)
{
    fake_sensor_matrix_reset();
    i_sensor_matrix_t port = fake_sensor_matrix_get_port();
    zassert_true(i_sensor_matrix_is_bound(&port), "Fake port must be bound");

    /* Set custom frame in fake */
    thermal_matrix_frame_t expected_frame;
    memset(&expected_frame, 0, sizeof(expected_frame));
    expected_frame.timestamp_us = 99887766ULL;
    expected_frame.configured_pairs = 6;
    expected_frame.pair_valid_mask = 0x003F;
    expected_frame.t_inj[0] = 12.3f;
    fake_sensor_matrix_set_frame(&expected_frame);

    /* Read frame via port */
    thermal_matrix_frame_t out_frame;
    int err = i_sensor_matrix_get_frame(&port, &out_frame);
    zassert_equal(err, 0, "i_sensor_matrix_get_frame failed");
    zassert_equal(out_frame.timestamp_us, 99887766ULL, "Timestamp mismatch");
    zassert_equal(out_frame.configured_pairs, 6, "Pairs mismatch");
    zassert_within(out_frame.t_inj[0], 12.3f, 0.001f, "Inj 0 mismatch");

    const fake_sensor_matrix_state_t *state = fake_sensor_matrix_get_state();
    zassert_equal(state->get_frame_calls, 1, "Calls count must be 1");
}

ZTEST(fake_sensor_matrix_suite, test_fake_sensor_matrix_channel_config)
{
    fake_sensor_matrix_reset();
    i_sensor_matrix_t port = fake_sensor_matrix_get_port();

    /* Read default pilot config of CWT CH1 */
    sensor_channel_cfg_t cfg;
    int err = i_sensor_matrix_get_channel_config(&port, PHYS_CH_CWT_1, &cfg);
    zassert_equal(err, 0, "get_channel_config failed");
    zassert_equal(cfg.role, SENSOR_ROLE_TEMP_SUPPLY_AIR, "CWT CH1 default role");

    /* Modify config via port */
    cfg.cal_offset_c_centi = 220;
    cfg.role = SENSOR_ROLE_TEMP_ROOM_AMBIENT;
    err = i_sensor_matrix_set_channel_config(&port, PHYS_CH_CWT_1, &cfg);
    zassert_equal(err, 0, "set_channel_config failed");

    /* Verify updated */
    sensor_channel_cfg_t readback;
    err = i_sensor_matrix_get_channel_config(&port, PHYS_CH_CWT_1, &readback);
    zassert_equal(err, 0, "get_channel_config after set failed");
    zassert_equal(readback.role, SENSOR_ROLE_TEMP_ROOM_AMBIENT, "Role updated");
    zassert_equal(readback.cal_offset_c_centi, 220, "Offset updated");

    const fake_sensor_matrix_state_t *state = fake_sensor_matrix_get_state();
    zassert_equal(state->set_cfg_calls, 1, "set_cfg_calls must be 1");
    zassert_equal(state->get_cfg_calls, 2, "get_cfg_calls must be 2");
}

ZTEST(fake_sensor_matrix_suite, test_fake_sensor_matrix_auto_map_on_update)
{
    fake_sensor_matrix_reset();
    i_sensor_matrix_t port = fake_sensor_matrix_get_port();

    /* Enable auto map */
    fake_sensor_matrix_set_auto_map(true);

    /* Setup raw readings */
    raw_sensor_readings_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.timestamp_us = 55443322ULL;
    raw.cwt_comm_ok = true;
    raw.cwt_valid_mask = 0x0003; /* CWT 1 and 2 valid (Supply 0, Supply 1) */
    raw.cwt_temperatures_c[0] = 11.5f;
    raw.cwt_temperatures_c[1] = 11.8f;
    fake_sensor_matrix_set_raw_readings(&raw);

    /* Trigger update via port */
    int err = i_sensor_matrix_update(&port);
    zassert_equal(err, 0, "Update failed");

    /* Check mapped frame */
    thermal_matrix_frame_t frame;
    err = i_sensor_matrix_get_frame(&port, &frame);
    zassert_equal(err, 0, "get_frame failed");
    zassert_equal(frame.timestamp_us, 55443322ULL, "Timestamp mapped");
    zassert_within(frame.t_inj[0], 11.5f, 0.01f, "Inj 0 mapped");
    zassert_within(frame.t_inj[1], 11.8f, 0.01f, "Inj 1 mapped");
}
