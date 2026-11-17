/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <stdio.h>

#include <snmp/snmp.h>
#include <snmp/snmp_core.h>
#include <snmp/snmp_trap.h>

#include "infrastructure/adapters/adapter_snmp_traps.h"
#include "common/alarm_types.h"
#include "interfaces/i_alarm_query.h"

/* =========================================================================
 * Mock Alarm Query Implementation
 * ========================================================================= */

static alarm_bitmask_snapshot_t s_mock_bitmask;
static uint16_t s_mock_queried_id = 0;
static const char *s_mock_desc_text = "Test Alarm Description";

static size_t mock_get_active_count(void *impl)
{
    ARG_UNUSED(impl);
    return 1;
}

static alarm_severity_t mock_get_highest_severity(void *impl)
{
    ARG_UNUSED(impl);
    return ALARM_SEVERITY_CRITICAL;
}

static bool mock_is_active(void *impl, uint16_t id)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(id);
    return true;
}

static int mock_get_alarm_state(void *impl, uint16_t id, alarm_state_t *out_state)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(id);
    if (out_state != NULL) {
        *out_state = ALARM_STATE_ACTIVE;
    }
    return 0;
}

static int mock_get_alarm_snapshot(void *impl, uint16_t id, alarm_snapshot_entry_t *out_snap)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(id);
    if (out_snap != NULL) {
        memset(out_snap, 0, sizeof(*out_snap));
        out_snap->alarm_id = id;
        out_snap->state = ALARM_STATE_ACTIVE;
    }
    return 0;
}

static size_t mock_get_active_alarm_ids(void *impl, uint16_t *out_ids, size_t max_count)
{
    ARG_UNUSED(impl);
    if (out_ids != NULL && max_count > 0) {
        out_ids[0] = s_mock_queried_id;
        return 1;
    }
    return 0;
}

static int mock_get_descriptor(void *impl, uint16_t id, alarm_descriptor_t *out_desc)
{
    ARG_UNUSED(impl);
    s_mock_queried_id = id;
    if (out_desc != NULL) {
        memset(out_desc, 0, sizeof(*out_desc));
        out_desc->alarm_id = id;
        out_desc->description = s_mock_desc_text;
        out_desc->default_severity = ALARM_SEVERITY_CRITICAL;
    }
    return 0;
}

static uint32_t mock_get_active_duration(void *impl, uint16_t id)
{
    ARG_UNUSED(impl);
    ARG_UNUSED(id);
    return 120;
}

static int mock_get_bitmask_snapshot(void *impl, alarm_bitmask_snapshot_t *out_snapshot)
{
    ARG_UNUSED(impl);
    if (out_snapshot != NULL) {
        *out_snapshot = s_mock_bitmask;
    }
    return 0;
}

static const i_alarm_query_api_t s_mock_alarm_query_api = {
    .get_active_count = mock_get_active_count,
    .get_highest_active_severity = mock_get_highest_severity,
    .is_active = mock_is_active,
    .get_alarm_state = mock_get_alarm_state,
    .get_alarm_snapshot = mock_get_alarm_snapshot,
    .get_active_alarm_ids = mock_get_active_alarm_ids,
    .get_descriptor = mock_get_descriptor,
    .get_active_duration = mock_get_active_duration,
    .get_bitmask_snapshot = mock_get_bitmask_snapshot,
};

static i_alarm_query_t make_mock_alarm_query(void)
{
    i_alarm_query_t port = {
        .api = &s_mock_alarm_query_api,
        .impl = (void *)0x1234,
    };
    return port;
}

/* =========================================================================
 * Test Suite Setup
 * ========================================================================= */

static struct snmp_agent s_agent;
static adapter_snmp_traps_t s_traps_ctx;

static void *snmp_traps_setup(void)
{
    int rc = snmp_agent_init(&s_agent, NULL);
    zassert_equal(rc, 0, "snmp_agent_init failed");
    return NULL;
}

static void snmp_traps_before(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&s_traps_ctx, 0, sizeof(s_traps_ctx));
    memset(&s_mock_bitmask, 0, sizeof(s_mock_bitmask));
    s_mock_queried_id = 0;
}

ZTEST_SUITE(snmp_traps_suite, NULL, snmp_traps_setup, snmp_traps_before, NULL, NULL);

/* =========================================================================
 * Test Cases
 * ========================================================================= */

ZTEST(snmp_traps_suite, test_adapter_init_validation)
{
    i_alarm_query_t query = make_mock_alarm_query();

    /* NULL context */
    int rc = adapter_snmp_traps_init(NULL, &s_agent, query);
    zassert_equal(rc, -EINVAL, "Must reject NULL context");

    /* NULL agent */
    rc = adapter_snmp_traps_init(&s_traps_ctx, NULL, query);
    zassert_equal(rc, -EINVAL, "Must reject NULL agent");

    /* Unbound query port is permitted (query methods are guarded by i_alarm_query_is_bound at runtime) */
    i_alarm_query_t unbound_query = { .api = NULL, .impl = NULL };
    rc = adapter_snmp_traps_init(&s_traps_ctx, &s_agent, unbound_query);
    zassert_equal(rc, 0, "Unbound query port must be accepted");

    /* Valid initialization */
    rc = adapter_snmp_traps_init(&s_traps_ctx, &s_agent, query);
    zassert_equal(rc, 0, "Initialization failed");
    zassert_true(s_traps_ctx.initialized, "Must be marked initialized");
    zassert_equal(s_traps_ctx.tokens, ADAPTER_SNMP_RATE_BURST_MAX, "Tokens must equal burst max");
    zassert_equal(s_traps_ctx.count, 0, "Queue must be empty");
    zassert_equal(s_traps_ctx.dropped_count, 0, "Dropped count must be 0");
}

ZTEST(snmp_traps_suite, test_post_event_and_drain)
{
    i_alarm_query_t query = make_mock_alarm_query();
    int rc = adapter_snmp_traps_init(&s_traps_ctx, &s_agent, query);
    zassert_equal(rc, 0, "Init failed");

    /* Post alarm triggered event */
    alarm_transition_event_t ev1 = {
        .alarm_id = ALARM_ID_GROUP1_TEMP_HIGH,
        .event_type = ALARM_EVENT_ACTIVATED,
        .timestamp_ms = 1234567,
    };

    rc = adapter_snmp_traps_post_event(&s_traps_ctx, &ev1);
    zassert_equal(rc, 0, "Post event 1 failed");

    /* Post alarm cleared event */
    alarm_transition_event_t ev2 = {
        .alarm_id = ALARM_ID_GROUP1_TEMP_HIGH,
        .event_type = ALARM_EVENT_CLEARED,
        .timestamp_ms = 1240000,
    };

    rc = adapter_snmp_traps_post_event(&s_traps_ctx, &ev2);
    zassert_equal(rc, 0, "Post event 2 failed");

    /* Wait for system workqueue to drain events */
    k_msleep(50);

    /* Queue should be fully drained */
    k_mutex_lock(&s_traps_ctx.lock, K_FOREVER);
    zassert_equal(s_traps_ctx.count, 0, "Queue must be empty after workqueue processing");
    zassert_equal(s_traps_ctx.dropped_count, 0, "No drops expected");
    k_mutex_unlock(&s_traps_ctx.lock);
}

ZTEST(snmp_traps_suite, test_queue_overflow_protection)
{
    i_alarm_query_t query = make_mock_alarm_query();
    int rc = adapter_snmp_traps_init(&s_traps_ctx, &s_agent, query);
    zassert_equal(rc, 0, "Init failed");

    /* Fill queue completely while holding lock to prevent workqueue from draining */
    k_mutex_lock(&s_traps_ctx.lock, K_FOREVER);

    for (uint32_t i = 0; i < ADAPTER_SNMP_TRAP_QUEUE_SIZE; i++) {
        alarm_transition_event_t ev = {
            .alarm_id = (uint16_t)(ALARM_ID_GROUP1_TEMP_HIGH + i),
            .event_type = ALARM_EVENT_ACTIVATED,
            .timestamp_ms = 1000 + i,
        };
        s_traps_ctx.queue[s_traps_ctx.tail] = ev;
        s_traps_ctx.tail = (uint8_t)((s_traps_ctx.tail + 1U) % ADAPTER_SNMP_TRAP_QUEUE_SIZE);
        s_traps_ctx.count++;
    }
    zassert_equal(s_traps_ctx.count, ADAPTER_SNMP_TRAP_QUEUE_SIZE, "Queue must be full");

    k_mutex_unlock(&s_traps_ctx.lock);

    /* Next post should succeed via drop-oldest policy, increment dropped_count, and set summary_pending */
    alarm_transition_event_t overflow_ev = {
        .alarm_id = 0x0099,
        .event_type = ALARM_EVENT_ACTIVATED,
        .timestamp_ms = 9999,
    };

    rc = adapter_snmp_traps_post_event(&s_traps_ctx, &overflow_ev);
    zassert_equal(rc, 0, "Post on full queue must succeed using drop-oldest policy");
    zassert_equal(s_traps_ctx.dropped_count, 1, "Dropped count must increment");
    zassert_true(s_traps_ctx.summary_pending, "Summary pending must be flagged");

    /* Allow workqueue to drain */
    k_msleep(50);
}

ZTEST(snmp_traps_suite, test_send_test_trap)
{
    i_alarm_query_t query = make_mock_alarm_query();
    int rc = adapter_snmp_traps_init(&s_traps_ctx, &s_agent, query);
    zassert_equal(rc, 0, "Init failed");

    /* Call test trap dispatch */
    rc = adapter_snmp_traps_send_test(&s_traps_ctx);
    zassert_equal(rc, 0, "adapter_snmp_traps_send_test must return 0");

    /* NULL context check */
    rc = adapter_snmp_traps_send_test(NULL);
    zassert_equal(rc, -EINVAL, "Must reject NULL context");
}
