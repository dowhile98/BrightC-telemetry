/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the pure polling-schedule and health policy.
 * No UART, no nanoMODBUS, no Zephyr kernel objects.
 */

#include <zephyr/ztest.h>

#include "common/modbus_types.h"
#include "domain/services/slave_scheduler.h"

#define POLL_INTERVAL_MS 1000U
#define TIMEOUT_MS       300U

static modbus_slave_descriptor_t make_desc(uint16_t poll_interval_ms, bool enabled)
{
    modbus_slave_descriptor_t desc = {
        .device_id = 1U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1U,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = poll_interval_ms,
        .timeout_ms = TIMEOUT_MS,
        .is_enabled = enabled,
    };

    return desc;
}

ZTEST_SUITE(slave_scheduler_suite, NULL, NULL, NULL, NULL, NULL);

/* Case 1: a slave that has never been polled is due immediately. */
ZTEST(slave_scheduler_suite, test_never_polled_is_due)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    zassert_false(rt.has_polled, "Fresh runtime must not claim a completed poll");
    zassert_true(slave_scheduler_is_due(&desc, &rt, 0U), "Never polled slave must be due");
    zassert_true(slave_scheduler_is_due(&desc, &rt, 123456U),
                 "Never polled slave stays due at any time");
}

/* Case 2: elapsed below the poll interval is not due. */
ZTEST(slave_scheduler_suite, test_elapsed_below_interval_not_due)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);
    slave_scheduler_record(&rt, MODBUS_RESULT_OK, 5000U);

    zassert_false(slave_scheduler_is_due(&desc, &rt, 5000U), "Just polled is not due");
    zassert_false(slave_scheduler_is_due(&desc, &rt, 5999U), "999 ms elapsed is not due");
}

/* Case 3: elapsed equal to or above the interval is due. */
ZTEST(slave_scheduler_suite, test_elapsed_reaching_interval_is_due)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);
    slave_scheduler_record(&rt, MODBUS_RESULT_OK, 5000U);

    zassert_true(slave_scheduler_is_due(&desc, &rt, 6000U), "Exact interval elapsed must be due");
    zassert_true(slave_scheduler_is_due(&desc, &rt, 7000U), "Beyond interval must be due");
}

/* Case 4: one and two consecutive failures degrade but keep the normal cadence. */
ZTEST(slave_scheduler_suite, test_one_and_two_failures_degrade_with_normal_interval)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    slave_scheduler_record(&rt, MODBUS_RESULT_TIMEOUT, 1000U);
    zassert_equal(rt.health, MODBUS_HEALTH_DEGRADED, "One failure must degrade");
    zassert_equal(rt.consecutive_fails, 1U, "Failure counter mismatch");
    zassert_equal(rt.timeout_count, 1U, "Timeout counter mismatch");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), POLL_INTERVAL_MS,
                  "Degraded slave keeps the normal interval");
    zassert_true(slave_scheduler_is_due(&desc, &rt, 2000U), "Degraded slave polls normally");

    slave_scheduler_record(&rt, MODBUS_RESULT_TIMEOUT, 2000U);
    zassert_equal(rt.health, MODBUS_HEALTH_DEGRADED, "Two failures stay degraded");
    zassert_equal(rt.consecutive_fails, 2U, "Failure counter mismatch");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), POLL_INTERVAL_MS,
                  "Still not in backoff");
}

/* Case 5: the third consecutive failure enters offline backoff. */
ZTEST(slave_scheduler_suite, test_third_failure_enters_offline_backoff)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    for (uint32_t i = 0U; i < MODBUS_CONSECUTIVE_FAILS_THRESHOLD; i++) {
        slave_scheduler_record(&rt, MODBUS_RESULT_TIMEOUT, 1000U + (i * 1000U));
    }

    zassert_equal(rt.health, MODBUS_HEALTH_OFFLINE_BACKOFF, "Threshold must trigger backoff");
    zassert_equal(rt.consecutive_fails, MODBUS_CONSECUTIVE_FAILS_THRESHOLD,
                  "Failure counter mismatch");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), MODBUS_OFFLINE_BACKOFF_INTERVAL_MS,
                  "Backoff must use the slow interval");

    zassert_false(slave_scheduler_is_due(&desc, &rt, 4500U),
                  "Backoff must suppress the normal 1000 ms cadence");
    zassert_true(slave_scheduler_is_due(&desc, &rt, 3000U + MODBUS_OFFLINE_BACKOFF_INTERVAL_MS),
                 "Backoff must release a probe after the slow interval");
}

/* Case 6: success after backoff restores ONLINE and clears the counters. */
ZTEST(slave_scheduler_suite, test_success_after_backoff_recovers_online)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    for (uint32_t i = 0U; i < MODBUS_CONSECUTIVE_FAILS_THRESHOLD; i++) {
        slave_scheduler_record(&rt, MODBUS_RESULT_TIMEOUT, 1000U + (i * 1000U));
    }
    zassert_equal(rt.health, MODBUS_HEALTH_OFFLINE_BACKOFF, "Precondition: in backoff");

    slave_scheduler_record(&rt, MODBUS_RESULT_OK, 13550U);

    zassert_equal(rt.health, MODBUS_HEALTH_ONLINE, "Success must restore ONLINE");
    zassert_equal(rt.consecutive_fails, 0U, "Consecutive failures must reset");
    zassert_equal(rt.successful_polls, 1U, "Success counter mismatch");
    zassert_equal(rt.last_success_timestamp_ms, 13550U, "Success timestamp mismatch");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), POLL_INTERVAL_MS,
                  "Recovered slave returns to the normal interval");
}

/* Case 7: an exception response proves the device is alive. */
ZTEST(slave_scheduler_suite, test_exception_does_not_degrade)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);
    slave_scheduler_record(&rt, MODBUS_RESULT_OK, 1000U);

    for (uint32_t i = 0U; i < (MODBUS_CONSECUTIVE_FAILS_THRESHOLD + 2U); i++) {
        slave_scheduler_record(&rt, MODBUS_RESULT_EXCEPTION, 2000U + (i * 1000U));
    }

    zassert_equal(rt.health, MODBUS_HEALTH_ONLINE, "A device that answers stays ONLINE");
    zassert_equal(rt.consecutive_fails, 0U, "Exception must not count as a communication failure");
    zassert_equal(rt.timeout_count, 0U, "Exception must not count as a timeout");
    zassert_equal(rt.crc_error_count, 0U, "Exception must not count as a CRC error");
    zassert_equal(rt.exception_count, MODBUS_CONSECUTIVE_FAILS_THRESHOLD + 2U,
                  "Exception counter mismatch");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), POLL_INTERVAL_MS,
                  "A device that answers keeps the normal cadence");
}

/* Case 8: a corrupt frame is a communication failure, not a timeout. */
ZTEST(slave_scheduler_suite, test_crc_error_counts_as_communication_failure)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    slave_scheduler_record(&rt, MODBUS_RESULT_CRC, 1000U);

    zassert_equal(rt.crc_error_count, 1U, "CRC counter mismatch");
    zassert_equal(rt.timeout_count, 0U, "CRC must not be counted as a timeout");
    zassert_equal(rt.consecutive_fails, 1U, "CRC must count as a communication failure");
    zassert_equal(rt.health, MODBUS_HEALTH_DEGRADED, "CRC must degrade the slave");

    for (uint32_t i = 0U; i < (MODBUS_CONSECUTIVE_FAILS_THRESHOLD - 1U); i++) {
        slave_scheduler_record(&rt, MODBUS_RESULT_CRC, 2000U + (i * 1000U));
    }
    zassert_equal(rt.health, MODBUS_HEALTH_OFFLINE_BACKOFF, "Sustained CRC errors must back off");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), MODBUS_OFFLINE_BACKOFF_INTERVAL_MS,
                  "Sustained CRC errors must also slow the schedule down");
}

/* A locally rejected request never reached the wire: no health impact at all. */
ZTEST(slave_scheduler_suite, test_invalid_result_leaves_health_untouched)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);
    slave_scheduler_record(&rt, MODBUS_RESULT_OK, 1000U);

    slave_scheduler_record(&rt, MODBUS_RESULT_INVALID, 1500U);

    zassert_equal(rt.health, MODBUS_HEALTH_ONLINE, "INVALID must not degrade a healthy slave");
    zassert_equal(rt.consecutive_fails, 0U, "INVALID must not count as a failure");
    zassert_equal(rt.timeout_count, 0U, "INVALID must not count as a timeout");
    zassert_false(rt.in_flight, "INVALID must still release the claim");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), POLL_INTERVAL_MS,
                  "INVALID must not change the cadence");
}

/* INVALID is deterministic: retrying immediately would spin the poll loop. */
ZTEST(slave_scheduler_suite, test_invalid_result_does_not_spin_the_scheduler)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), POLL_INTERVAL_MS,
                  "Precondition: normal cadence");

    /* A slave that has never answered must not stay due after a rejection. */
    slave_scheduler_record(&rt, MODBUS_RESULT_INVALID, 500U);

    zassert_false(slave_scheduler_is_due(&desc, &rt, 500U),
                  "A rejected request must not be retried in a tight loop");
    zassert_false(slave_scheduler_is_due(&desc, &rt, 1499U),
                  "The normal interval still applies after a rejection");
    zassert_true(slave_scheduler_is_due(&desc, &rt, 1500U),
                 "The slave is polled again after the interval");
    zassert_equal(rt.health, MODBUS_HEALTH_ONLINE, "INVALID must not change health");
    zassert_equal(rt.consecutive_fails, 0U, "INVALID must not count as a failure");
}

/* Case 9: unsigned 64-bit arithmetic survives a counter wrap. */
ZTEST(slave_scheduler_suite, test_timestamp_wraparound_has_no_spurious_due)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    const uint64_t last_poll = UINT64_MAX - 100U;
    slave_scheduler_record(&rt, MODBUS_RESULT_OK, last_poll);

    /* 151 ms later across the wrap: still inside the interval. */
    zassert_false(slave_scheduler_is_due(&desc, &rt, 50U),
                  "Wrapped elapsed of 151 ms must not be due");

    /* Exactly the interval later, across the wrap. */
    zassert_true(slave_scheduler_is_due(&desc, &rt, last_poll + POLL_INTERVAL_MS),
                 "Wrapped elapsed of 1000 ms must be due");
}

/* Case 10: a non-monotonic timestamp must not corrupt the health record. */
ZTEST(slave_scheduler_suite, test_backwards_timestamp_does_not_corrupt_state)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    slave_scheduler_record(&rt, MODBUS_RESULT_OK, 10000U);
    zassert_equal(rt.last_poll_timestamp_ms, 10000U, "Precondition: last poll stored");

    slave_scheduler_record(&rt, MODBUS_RESULT_OK, 9000U);

    zassert_equal(rt.health, MODBUS_HEALTH_ONLINE, "Health must stay ONLINE");
    zassert_equal(rt.consecutive_fails, 0U, "Counters must not go negative");
    zassert_true(rt.last_poll_timestamp_ms >= 10000U,
                 "A backwards timestamp must not rewind the schedule");
    zassert_true(rt.last_success_timestamp_ms >= 10000U,
                 "A backwards timestamp must not rewind the success record");
    zassert_equal(slave_scheduler_interval_ms(&desc, &rt), POLL_INTERVAL_MS,
                  "A backwards timestamp must not change the cadence");
}

/* Claiming: an in-flight slave is hidden until the claim goes stale. */
ZTEST(slave_scheduler_suite, test_in_flight_claim_blocks_until_stale)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    rt.in_flight = true;
    rt.last_attempt_ms = 1000U;

    zassert_false(slave_scheduler_is_due(&desc, &rt, 1000U), "Fresh claim must block");
    zassert_false(slave_scheduler_is_due(&desc, &rt,
                                         1000U + MODBUS_CLAIM_STALE_MS - 1U),
                  "Claim still fresh just below the stale threshold");
    zassert_true(slave_scheduler_is_due(&desc, &rt, 1000U + MODBUS_CLAIM_STALE_MS),
                 "Stale claim must be reclaimable");
}

/* A disabled slave is never scheduled. */
ZTEST(slave_scheduler_suite, test_disabled_slave_is_never_due)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, false);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    zassert_false(slave_scheduler_is_due(&desc, &rt, 0U), "Disabled slave must never be due");
    zassert_false(slave_scheduler_is_due(&desc, &rt, 1000000U),
                  "Disabled slave must never be due, even long after boot");
}

/* Defensive behaviour on bad arguments. */
ZTEST(slave_scheduler_suite, test_null_arguments_are_safe)
{
    modbus_slave_descriptor_t desc = make_desc(POLL_INTERVAL_MS, true);
    modbus_slave_runtime_state_t rt;

    slave_scheduler_reset(&rt);

    zassert_equal(slave_scheduler_interval_ms(NULL, &rt), 0U, "NULL descriptor must yield 0");
    zassert_equal(slave_scheduler_interval_ms(&desc, NULL), 0U, "NULL runtime must yield 0");
    zassert_false(slave_scheduler_is_due(NULL, &rt, 0U), "NULL descriptor is never due");
    zassert_false(slave_scheduler_is_due(&desc, NULL, 0U), "NULL runtime is never due");

    slave_scheduler_reset(NULL);
    slave_scheduler_record(NULL, MODBUS_RESULT_OK, 0U);
}
