/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_thermal_energy_port.c
 * @brief Unit tests for i_thermal_energy port contract and fake double.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "interfaces/i_thermal_energy.h"
#include "fake_thermal_energy.h"

static void thermal_energy_port_suite_before(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_thermal_energy_reset();
}

ZTEST_SUITE(thermal_energy_port_suite, NULL, NULL, thermal_energy_port_suite_before, NULL, NULL);

ZTEST(thermal_energy_port_suite, test_port_unbound_and_null_checks)
{
    i_thermal_energy_t null_port = {0};
    thermal_energy_result_t res;
    thermal_energy_capture_t cap;
    thermal_hourly_record_t rec;
    config_thermal_baseline_t base;

    zassert_false(i_thermal_energy_is_bound(NULL), "NULL port must not be bound");
    zassert_false(i_thermal_energy_is_bound(&null_port), "Empty port must not be bound");

    /* Defensive calls on unbound port */
    zassert_equal(i_thermal_energy_get_latest_result(&null_port, &res), -EINVAL);
    zassert_equal(i_thermal_energy_get_latest_capture(&null_port, &cap), -EINVAL);
    zassert_equal(i_thermal_energy_get_hourly_history(&null_port, 0, &rec), -EINVAL);
    zassert_equal(i_thermal_energy_get_baseline(&null_port, &base), -EINVAL);
    zassert_equal(i_thermal_energy_trigger_baseline_recalibration(&null_port), -EINVAL);
}

ZTEST(thermal_energy_port_suite, test_port_bound_null_output_buffers)
{
    i_thermal_energy_t port = fake_thermal_energy_get_port();
    zassert_true(i_thermal_energy_is_bound(&port), "Fake port must be bound");

    zassert_equal(i_thermal_energy_get_latest_result(&port, NULL), -EINVAL);
    zassert_equal(i_thermal_energy_get_latest_capture(&port, NULL), -EINVAL);
    zassert_equal(i_thermal_energy_get_hourly_history(&port, 0, NULL), -EINVAL);
    zassert_equal(i_thermal_energy_get_baseline(&port, NULL), -EINVAL);
}

ZTEST(thermal_energy_port_suite, test_get_latest_result_roundtrip)
{
    i_thermal_energy_t port = fake_thermal_energy_get_port();
    thermal_energy_result_t mock_res = {0};
    mock_res.generation_id = 42U;
    mock_res.q_total = 28.5f;
    mock_res.cop_real = 3.65f;
    mock_res.overall_quality = QUALITY_VALID;

    fake_thermal_energy_set_latest_result(&mock_res);

    thermal_energy_result_t out_res = {0};
    int ret = i_thermal_energy_get_latest_result(&port, &out_res);

    zassert_equal(ret, 0, "get_latest_result must return 0");
    zassert_equal(out_res.generation_id, 42U);
    zassert_within(out_res.q_total, 28.5f, 0.001f);
    zassert_within(out_res.cop_real, 3.65f, 0.001f);
    zassert_equal(out_res.overall_quality, QUALITY_VALID);

    const fake_thermal_energy_state_t *st = fake_thermal_energy_get_state();
    zassert_equal(st->get_latest_result_calls, 1U);
}

ZTEST(thermal_energy_port_suite, test_get_latest_capture_roundtrip)
{
    i_thermal_energy_t port = fake_thermal_energy_get_port();
    thermal_energy_capture_t mock_cap = {0};
    mock_cap.generation_id = 101U;
    mock_cap.configured_pairs = 6U;
    mock_cap.t_inj[0] = 14.5f;
    mock_cap.t_ret[0] = 24.0f;
    mock_cap.humidity_return_pct = 48.0f;
    mock_cap.total_active_power_kw = 8.2f;

    fake_thermal_energy_set_latest_capture(&mock_cap);

    thermal_energy_capture_t out_cap = {0};
    int ret = i_thermal_energy_get_latest_capture(&port, &out_cap);

    zassert_equal(ret, 0, "get_latest_capture must return 0");
    zassert_equal(out_cap.generation_id, 101U);
    zassert_within(out_cap.t_inj[0], 14.5f, 0.001f);
    zassert_within(out_cap.t_ret[0], 24.0f, 0.001f);
    zassert_within(out_cap.humidity_return_pct, 48.0f, 0.001f);

    const fake_thermal_energy_state_t *st = fake_thermal_energy_get_state();
    zassert_equal(st->get_latest_capture_calls, 1U);
}

ZTEST(thermal_energy_port_suite, test_get_hourly_history_records)
{
    i_thermal_energy_t port = fake_thermal_energy_get_port();

    thermal_hourly_record_t mock_rec = {0};
    mock_rec.timestamp_unix = 1700000000U;
    mock_rec.active_power_kw = 7.5f;
    mock_rec.sensible_capacity_kw = 22.0f;

    fake_thermal_energy_set_hourly_record(0, &mock_rec);

    thermal_hourly_record_t out_rec = {0};
    int ret = i_thermal_energy_get_hourly_history(&port, 0, &out_rec);
    zassert_equal(ret, 0);
    zassert_equal(out_rec.timestamp_unix, 1700000000U);
    zassert_within(out_rec.active_power_kw, 7.5f, 0.001f);

    /* Unpopulated record must return -ENOENT */
    ret = i_thermal_energy_get_hourly_history(&port, 5, &out_rec);
    zassert_equal(ret, -ENOENT);

    /* Out of bounds hours_ago must return -ENOENT */
    ret = i_thermal_energy_get_hourly_history(&port, 100, &out_rec);
    zassert_equal(ret, -ENOENT);
}

ZTEST(thermal_energy_port_suite, test_baseline_and_recalibration)
{
    i_thermal_energy_t port = fake_thermal_energy_get_port();

    config_thermal_baseline_t mock_base = {0};
    mock_base.is_valid = 1U;
    mock_base.training_hours_accumulated = 168U;
    mock_base.baseline_exergetic_efficiency = 44.5f;

    fake_thermal_energy_set_baseline(&mock_base);

    config_thermal_baseline_t out_base = {0};
    int ret = i_thermal_energy_get_baseline(&port, &out_base);
    zassert_equal(ret, 0);
    zassert_equal(out_base.is_valid, 1U);
    zassert_equal(out_base.training_hours_accumulated, 168U);
    zassert_within(out_base.baseline_exergetic_efficiency, 44.5f, 0.001f);

    ret = i_thermal_energy_trigger_baseline_recalibration(&port);
    zassert_equal(ret, 0);

    const fake_thermal_energy_state_t *st = fake_thermal_energy_get_state();
    zassert_equal(st->trigger_recalibration_calls, 1U);
}

ZTEST(thermal_energy_port_suite, test_override_return_codes)
{
    i_thermal_energy_t port = fake_thermal_energy_get_port();
    thermal_energy_result_t res;

    fake_thermal_energy_set_latest_result_ret(-EBUSY);
    zassert_equal(i_thermal_energy_get_latest_result(&port, &res), -EBUSY);

    fake_thermal_energy_set_recalibration_ret(-EALREADY);
    zassert_equal(i_thermal_energy_trigger_baseline_recalibration(&port), -EALREADY);
}
