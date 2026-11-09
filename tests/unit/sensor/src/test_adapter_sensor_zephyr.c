/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for Zephyr sensor matrix adapter (adapter_sensor_zephyr.c):
 * - uint16_t ADC counts (> 32767 counts without wrapping negative)
 * - 1st-order IIR bilinear low-pass filter (initialization on first sample, smoothing)
 * - Consecutive failure debouncing and recovery
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <drivers/adc_dma_engine.h>
#include "infrastructure/adapters/adapter_sensor_zephyr.h"

/* Mock ADC DMA state */
static uint16_t s_mock_adc_samples[15];
static size_t   s_mock_channel_count = 15;
static int      s_mock_trigger_result = 0;
static int      s_mock_wait_result = 0;
static uint32_t s_mock_trigger_calls = 0;
static uint32_t s_mock_wait_calls = 0;

static int mock_trigger_scan(const struct device *dev)
{
    ARG_UNUSED(dev);
    s_mock_trigger_calls++;
    return s_mock_trigger_result;
}

static int mock_wait_scan(const struct device *dev, k_timeout_t timeout)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(timeout);
    s_mock_wait_calls++;
    return s_mock_wait_result;
}

static int mock_get_raw_sample(const struct device *dev, uint8_t channel_idx, uint16_t *out_counts)
{
    ARG_UNUSED(dev);
    if (out_counts == NULL || channel_idx >= s_mock_channel_count) {
        return -EINVAL;
    }
    *out_counts = s_mock_adc_samples[channel_idx];
    return 0;
}

static size_t mock_get_channel_count(const struct device *dev)
{
    ARG_UNUSED(dev);
    return s_mock_channel_count;
}

static const struct adc_dma_driver_api s_mock_api = {
    .trigger_scan = mock_trigger_scan,
    .wait_scan = mock_wait_scan,
    .get_raw_sample = mock_get_raw_sample,
    .get_channel_count = mock_get_channel_count,
};

static struct device_state s_mock_dev_state = {
    .init_res = 0,
    .initialized = true,
};

static const struct device s_mock_adc_dev = {
    .name = "mock_adc_dma",
    .api = &s_mock_api,
    .state = &s_mock_dev_state,
};

ZTEST_SUITE(adapter_sensor_zephyr_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(adapter_sensor_zephyr_suite, test_adapter_init_and_port_binding)
{
    adapter_sensor_zephyr_t adapter;
    i_sensor_matrix_t port;

    /* Bad args */
    zassert_equal(adapter_sensor_zephyr_init(NULL, &s_mock_adc_dev, 500), -EINVAL);
    zassert_equal(adapter_sensor_zephyr_init(&adapter, NULL, 500), -EINVAL);

    /* Valid init */
    zassert_ok(adapter_sensor_zephyr_init(&adapter, &s_mock_adc_dev, 500));
    zassert_equal(adapter.scan_interval_ms, 500);

    /* Port binding */
    zassert_ok(adapter_sensor_zephyr_port(&adapter, &port));
    zassert_true(i_sensor_matrix_is_bound(&port));
}

ZTEST(adapter_sensor_zephyr_suite, test_uint16_adc_samples_and_first_sample_transient_free)
{
    adapter_sensor_zephyr_t adapter;
    i_sensor_matrix_t port;

    zassert_ok(adapter_sensor_zephyr_init(&adapter, &s_mock_adc_dev, 500));
    zassert_ok(adapter_sensor_zephyr_port(&adapter, &port));

    /* Reset mock call counters */
    s_mock_trigger_calls = 0;
    s_mock_wait_calls = 0;
    s_mock_trigger_result = 0;
    s_mock_wait_result = 0;

    /* Set RTD 0 (channel 0) to 40000 counts (> 32767).
     * With uint16_t, this MUST NOT wrap to negative!
     */
    s_mock_adc_samples[0] = 40000U;
    /* Set J9 CH1 (channel 5) to 50000 counts */
    s_mock_adc_samples[5] = 50000U;

    /* Update cycle 1 */
    zassert_ok(i_sensor_matrix_update(&port));
    zassert_equal(s_mock_trigger_calls, 1);
    zassert_equal(s_mock_wait_calls, 1);

    raw_sensor_readings_t raw;
    zassert_ok(i_sensor_matrix_get_raw_readings(&port, &raw));

    /* Verify first sample initialization: output must EQUAL input (zero startup transient) */
    zassert_within(raw.internal_rtd_raw_adc[0], 40000.0f, 0.001f,
                   "First sample must seed filter without startup transient");
    zassert_within(raw.analog_j9_raw_adc[0], 50000.0f, 0.001f,
                   "First sample on J9 must seed filter without startup transient");

    /* Update cycle 2: Step input to 41000 counts.
     * Ts = 0.5s, Fc = 1.0Hz:
     * rc = 1.0 / (2 * pi * 1.0) = 0.159155s
     * b = 0.5 / (0.5 + 2 * 0.159155) = 0.5 / 0.81831 = 0.611015
     * a = (0.5 - 2 * 0.159155) / 0.81831 = 0.18169 / 0.81831 = 0.22203
     * y[1] = b * (x[1] + x[0]) - a * y[0]
     *      = 0.611015 * (41000 + 40000) - 0.22203 * 40000
     *      = 0.611015 * 81000 - 8881.2 = 49492.2 - 8881.2 = 40611.0 counts
     */
    s_mock_adc_samples[0] = 41000U;
    zassert_ok(i_sensor_matrix_update(&port));
    zassert_ok(i_sensor_matrix_get_raw_readings(&port, &raw));

    /* Verify low-pass filtering smoothly transitions between 40000 and 41000 */
    zassert_true(raw.internal_rtd_raw_adc[0] > 40000.0f && raw.internal_rtd_raw_adc[0] < 41000.0f,
                 "Filtered count must be between 40000 and 41000 (was %f)",
                 (double)raw.internal_rtd_raw_adc[0]);
    zassert_within(raw.internal_rtd_raw_adc[0], 40611.0f, 5.0f,
                   "IIR bilinear formula output matches theoretical calculation");
}

ZTEST(adapter_sensor_zephyr_suite, test_consecutive_failure_debouncing_and_filter_reset)
{
    adapter_sensor_zephyr_t adapter;
    i_sensor_matrix_t port;

    zassert_ok(adapter_sensor_zephyr_init(&adapter, &s_mock_adc_dev, 500));
    zassert_ok(adapter_sensor_zephyr_port(&adapter, &port));

    /* Provide valid sample */
    s_mock_adc_samples[0] = 20000U;
    s_mock_trigger_result = 0;
    s_mock_wait_result = 0;
    zassert_ok(i_sensor_matrix_update(&port));

    raw_sensor_readings_t raw;
    zassert_ok(i_sensor_matrix_get_raw_readings(&port, &raw));
    zassert_equal(raw.internal_rtd_valid_mask & 0x01, 0x01);

    /* Simulate ADC scan failures */
    s_mock_trigger_result = -EIO;

    /* Failure 1: debounced (keeps last valid mask) */
    i_sensor_matrix_update(&port);
    zassert_equal(adapter.consecutive_failures, 1);
    zassert_ok(i_sensor_matrix_get_raw_readings(&port, &raw));
    zassert_equal(raw.internal_rtd_valid_mask & 0x01, 0x01);

    /* Failure 2: debounced */
    i_sensor_matrix_update(&port);
    zassert_equal(adapter.consecutive_failures, 2);

    /* Failure 3: threshold exceeded -> masks invalidated */
    i_sensor_matrix_update(&port);
    zassert_equal(adapter.consecutive_failures, 3);
    zassert_ok(i_sensor_matrix_get_raw_readings(&port, &raw));
    zassert_equal(raw.internal_rtd_valid_mask, 0U);
    zassert_false(adapter.rtd_filter[0].initialized, "Filter must be reset on prolonged comm loss");

    /* Recovery: fresh sample must re-seed filter without transient */
    s_mock_trigger_result = 0;
    s_mock_adc_samples[0] = 30000U;
    zassert_ok(i_sensor_matrix_update(&port));
    zassert_equal(adapter.consecutive_failures, 0);
    zassert_ok(i_sensor_matrix_get_raw_readings(&port, &raw));
    zassert_within(raw.internal_rtd_raw_adc[0], 30000.0f, 0.001f,
                   "After recovery, filter seeds cleanly with new sample");
}
