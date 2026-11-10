/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_alarm_ao.c
 * @brief Unit & Integration tests for ao_alarm_manager and i_alarm_query_t port.
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include "common/alarm_types.h"
#include "common/indicator_types.h"
#include "application/active_objects/ao_alarm_manager.h"
#include "application/zbus/alarm_channels.h"
#include "application/zbus/indicator_channels.h"

#define TEST_AO_STACK_SIZE 2048
K_THREAD_STACK_DEFINE(s_test_alarm_stack, TEST_AO_STACK_SIZE);

static ao_alarm_manager_t s_alarm_mgr;

static uint32_t s_indicator_feed_count = 0;
static void test_indicator_alarm_feed_cb(const struct zbus_channel *chan)
{
    ARG_UNUSED(chan);
    s_indicator_feed_count++;
}
ZBUS_LISTENER_DEFINE(lis_indicator_alarm_feed, test_indicator_alarm_feed_cb);


ZTEST(alarm_ao_suite, test_ao_alarm_manager_lifecycle)
{
    int ret;

    ret = ao_alarm_manager_init(&s_alarm_mgr, s_test_alarm_stack, TEST_AO_STACK_SIZE, K_PRIO_PREEMPT(5));
    zassert_equal(ret, 0, "Init succeeded");

    ret = ao_alarm_manager_start(&s_alarm_mgr);
    zassert_equal(ret, 0, "Start succeeded");
    zassert_true(s_alarm_mgr.is_running, "Must be running");

    /* Stop */
    ret = ao_alarm_manager_stop(&s_alarm_mgr);
    zassert_equal(ret, 0, "Stop succeeded");

    ret = ao_alarm_manager_wait_stopped(&s_alarm_mgr, K_MSEC(1000));
    zassert_equal(ret, 0, "Wait stopped succeeded");
    zassert_false(s_alarm_mgr.is_running, "Must not be running");

    ret = ao_alarm_manager_deinit(&s_alarm_mgr);
    zassert_equal(ret, 0, "Deinit succeeded");
}

ZTEST(alarm_ao_suite, test_ao_alarm_manager_dispatch_and_query)
{
    int ret;

    ret = ao_alarm_manager_init(&s_alarm_mgr, s_test_alarm_stack, TEST_AO_STACK_SIZE, K_PRIO_PREEMPT(5));
    zassert_equal(ret, 0, "Init");
    ret = ao_alarm_manager_start(&s_alarm_mgr);
    zassert_equal(ret, 0, "Start");

    i_alarm_query_t query = ao_alarm_manager_get_query_port(&s_alarm_mgr);
    zassert_true(i_alarm_query_is_bound(&query), "Query port must be bound");

    /* Query initial state: 0 active alarms */
    zassert_equal(i_alarm_query_get_active_count(&query), 0, "Initially 0 active alarms");
    zassert_false(i_alarm_query_is_active(&query, ALARM_ID_AC1_HIGH_PRESSURE), "AC1 HP inactive");

    /* 1. Post Condition True for AC1 HP */
    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, ALARM_ID_AC1_HIGH_PRESSURE, true, 420.0f);
    zassert_equal(ret, 0, "Post condition succeeded");

    /* Wait for AO thread to process event */
    k_sleep(K_MSEC(60));

    zassert_equal(i_alarm_query_get_active_count(&query), 1, "Now 1 active alarm");
    zassert_true(i_alarm_query_is_active(&query, ALARM_ID_AC1_HIGH_PRESSURE), "AC1 HP is active");
    zassert_equal(i_alarm_query_get_highest_active_severity(&query), ALARM_SEVERITY_CRITICAL, "Highest severity CRITICAL");

    /* Verify Zbus notification via zbus_chan_read */
    alarm_transition_event_t trans_evt;
    ret = zbus_chan_read(&chan_alarm_transition, &trans_evt, K_NO_WAIT);
    zassert_equal(ret, 0, "Read chan_alarm_transition succeeded");
    zassert_equal(trans_evt.alarm_id, ALARM_ID_AC1_HIGH_PRESSURE, "Alarm ID match");
    zassert_equal(trans_evt.event_type, ALARM_EVENT_ACTIVATED, "ACTIVATED event");

    /* Verify System Indicators notification via zbus_chan_read */
    alarm_msg_t ind_msg;
    ret = zbus_chan_read(&chan_system_alarm, &ind_msg, K_NO_WAIT);
    zassert_equal(ret, 0, "Read chan_system_alarm succeeded");
    zassert_equal(ind_msg.alarm_id, ALARM_ID_AC1_HIGH_PRESSURE, "ID match");
    zassert_true(ind_msg.is_active, "is_active true");
    zassert_equal(ind_msg.level, ALARM_LEVEL_CRITICAL, "Indicator level CRITICAL");

    /* Query snapshot */
    alarm_snapshot_entry_t snap;
    ret = i_alarm_query_get_alarm_snapshot(&query, ALARM_ID_AC1_HIGH_PRESSURE, &snap);
    zassert_equal(ret, 0, "Snapshot fetch succeeded");
    zassert_equal(snap.state, ALARM_STATE_ACTIVE, "Snapshot state ACTIVE");
    zassert_equal(snap.trigger_count, 1, "Trigger count 1");

    /* Fetch active IDs */
    uint16_t active_ids[8];
    size_t n_active = i_alarm_query_get_active_alarm_ids(&query, active_ids, 8);
    zassert_equal(n_active, 1, "1 active ID returned");
    zassert_equal(active_ids[0], ALARM_ID_AC1_HIGH_PRESSURE, "Returned ID matches");

    /* 2. Post Condition False for AC1 HP -> Cleared */
    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, ALARM_ID_AC1_HIGH_PRESSURE, false, 300.0f);
    zassert_equal(ret, 0, "Post condition false");

    k_sleep(K_MSEC(60));

    zassert_equal(i_alarm_query_get_active_count(&query), 0, "0 active alarms after clear");
    zassert_false(i_alarm_query_is_active(&query, ALARM_ID_AC1_HIGH_PRESSURE), "AC1 HP no longer active");

    ret = zbus_chan_read(&chan_alarm_transition, &trans_evt, K_NO_WAIT);
    zassert_equal(ret, 0, "Read cleared event succeeded");
    zassert_equal(trans_evt.event_type, ALARM_EVENT_CLEARED, "Event must be CLEARED");

    ret = zbus_chan_read(&chan_system_alarm, &ind_msg, K_NO_WAIT);
    zassert_equal(ret, 0, "Read cleared indicator msg");
    zassert_false(ind_msg.is_active, "Indicator is_active false");

    /* Clean shutdown */
    ao_alarm_manager_stop(&s_alarm_mgr);
    ao_alarm_manager_wait_stopped(&s_alarm_mgr, K_MSEC(1000));
    ao_alarm_manager_deinit(&s_alarm_mgr);
}

ZTEST(alarm_ao_suite, test_ao_alarm_manager_dynamic_registration)
{
    int ret;

    ret = ao_alarm_manager_init(&s_alarm_mgr, s_test_alarm_stack, TEST_AO_STACK_SIZE, K_PRIO_PREEMPT(5));
    zassert_equal(ret, 0, "Init succeeded");
    ret = ao_alarm_manager_start(&s_alarm_mgr);
    zassert_equal(ret, 0, "Start succeeded");

    i_alarm_query_t query = ao_alarm_manager_get_query_port(&s_alarm_mgr);
    zassert_true(i_alarm_query_is_bound(&query), "Query port bound");

    alarm_descriptor_t custom_desc = {
        .alarm_id = 0x0300,
        .name = "CUSTOM_ALARM_TEST",
        .description = "Dynamic Test Alarm",
        .source_category = ALARM_SOURCE_CUSTOM,
        .default_severity = ALARM_SEVERITY_WARNING,
        .default_priority = ALARM_PRIORITY_LOW,
        .is_latch_supported = false,
        .is_ack_supported = true,
        .is_aggregated = false
    };

    ret = ao_alarm_manager_register_descriptor(&s_alarm_mgr, &custom_desc);
    zassert_equal(ret, 0, "Dynamic descriptor registration succeeded");

    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, 0x0300, true, 1.0f);
    zassert_equal(ret, 0, "Post condition true succeeded");

    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&query, 0x0300), "Custom alarm 0x0300 must be active");

    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, 0x0300, false, 0.0f);
    zassert_equal(ret, 0, "Post condition false succeeded");

    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&query, 0x0300), "Custom alarm 0x0300 must be inactive");

    /* Clean shutdown */
    ao_alarm_manager_stop(&s_alarm_mgr);
    ao_alarm_manager_wait_stopped(&s_alarm_mgr, K_MSEC(1000));
    ao_alarm_manager_deinit(&s_alarm_mgr);
}

ZTEST(alarm_ao_suite, test_ao_alarm_manager_active_duration)
{
    int ret;

    ret = ao_alarm_manager_init(&s_alarm_mgr, s_test_alarm_stack, TEST_AO_STACK_SIZE, K_PRIO_PREEMPT(5));
    zassert_equal(ret, 0, "Init succeeded");
    ret = ao_alarm_manager_start(&s_alarm_mgr);
    zassert_equal(ret, 0, "Start succeeded");

    i_alarm_query_t query = ao_alarm_manager_get_query_port(&s_alarm_mgr);
    zassert_true(i_alarm_query_is_bound(&query), "Query port bound");

    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, ALARM_ID_INFRA_ETHERNET_DOWN, true, 1.0f);
    zassert_equal(ret, 0, "Post condition true");

    k_sleep(K_MSEC(100));

    uint32_t dur = i_alarm_query_get_active_duration(&query, ALARM_ID_INFRA_ETHERNET_DOWN);
    zassert_true(dur >= 90U && dur <= 500U,
                 "Active duration must be between 90ms and 500ms (got %u)", dur);

    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, ALARM_ID_INFRA_ETHERNET_DOWN, false, 0.0f);
    zassert_equal(ret, 0, "Post condition false");

    k_sleep(K_MSEC(50));

    dur = i_alarm_query_get_active_duration(&query, ALARM_ID_INFRA_ETHERNET_DOWN);
    zassert_equal(dur, 0U, "Cleared alarm must return 0 active duration (got %u)", dur);

    /* Clean shutdown */
    ao_alarm_manager_stop(&s_alarm_mgr);
    ao_alarm_manager_wait_stopped(&s_alarm_mgr, K_MSEC(1000));
    ao_alarm_manager_deinit(&s_alarm_mgr);
}

ZTEST(alarm_ao_suite, test_ao_alarm_manager_restart_and_input_validation)
{
    int ret;

    ret = ao_alarm_manager_init(&s_alarm_mgr, s_test_alarm_stack, TEST_AO_STACK_SIZE, K_PRIO_PREEMPT(5));
    zassert_equal(ret, 0, "Init succeeded");
    ret = ao_alarm_manager_start(&s_alarm_mgr);
    zassert_equal(ret, 0, "Start succeeded");

    /* Test input validation */
    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, ALARM_ID_NONE, true, 0.0f);
    zassert_equal(ret, -EINVAL, "ALARM_ID_NONE must return -EINVAL");

    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, 0x9999, true, 0.0f);
    zassert_equal(ret, -ENOENT, "Unregistered 0x9999 must return -ENOENT");

    /* Test restart */
    ret = ao_alarm_manager_stop(&s_alarm_mgr);
    zassert_equal(ret, 0, "Stop succeeded");

    ret = ao_alarm_manager_wait_stopped(&s_alarm_mgr, K_MSEC(1000));
    zassert_equal(ret, 0, "Wait stopped succeeded");
    zassert_false(s_alarm_mgr.is_running, "Must not be running after wait_stopped");

    ret = ao_alarm_manager_start(&s_alarm_mgr);
    zassert_equal(ret, 0, "Restart succeeded");
    zassert_true(s_alarm_mgr.is_running, "Must be running after restart");

    i_alarm_query_t query = ao_alarm_manager_get_query_port(&s_alarm_mgr);
    ret = ao_alarm_manager_post_condition(&s_alarm_mgr, ALARM_ID_AC1_HIGH_PRESSURE, true, 400.0f);
    zassert_equal(ret, 0, "Post condition after restart succeeded");

    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&query, ALARM_ID_AC1_HIGH_PRESSURE),
                 "Alarm must be active after restart processing");

    /* Finally clean shutdown with stop, wait_stopped, deinit */
    ao_alarm_manager_stop(&s_alarm_mgr);
    ao_alarm_manager_wait_stopped(&s_alarm_mgr, K_MSEC(1000));
    ao_alarm_manager_deinit(&s_alarm_mgr);
}

ZTEST_SUITE(alarm_ao_suite, NULL, NULL, NULL, NULL, NULL);
