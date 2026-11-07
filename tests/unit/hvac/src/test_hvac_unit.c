/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_hvac_unit.c
 * @brief Unit tests for HVAC unit entity, fan purge sub-FSM, and i_hvac_unit port.
 */

#include <zephyr/ztest.h>
#include <errno.h>

#include "common/hvac_types.h"
#include "interfaces/i_hvac_unit.h"
#include "domain/models/hvac_unit_entity.h"
#include "fake_hvac_unit.h"

static hvac_unit_entity_t s_test_unit;

static void hvac_unit_before(void *data)
{
    ARG_UNUSED(data);
    fake_hvac_unit_reset();
    hvac_unit_entity_init(&s_test_unit, 0U, 3U, 5U, 60U, 180U, true);
}

ZTEST_SUITE(hvac_unit_suite, NULL, NULL, hvac_unit_before, NULL, NULL);

/*============================================================================*
 * 1. Staggered Startup Delay Countdown Tests
 *============================================================================*/

ZTEST(hvac_unit_suite, test_unit_staggered_startup_delay)
{
    bool relay_aa = false;
    bool relay_sm = false;
    bool relay_g = false;

    /* Verify initial state is OFF */
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Initial state should be OFF");
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_false(relay_aa, "Compressor must be OFF");
    zassert_true(relay_sm, "Smart mode must be active");
    zassert_false(relay_g, "Fan must be OFF");
    zassert_true(hvac_tri_state_is_equal(hvac_unit_entity_get_tri_state(&s_test_unit), HVAC_TRI_STATE_OFF), "Tri-state must be OFF");

    /* Request start with 10s staggered delay */
    bool ok = hvac_unit_entity_request_start_staggered(&s_test_unit, 10U);
    zassert_true(ok, "Start request with delay must be accepted");
    zassert_equal(s_test_unit.state, UNIT_STATE_STARTING, "State should be STARTING");
    zassert_equal(s_test_unit.startup_timer_s, 10U, "Timer should be set to 10s");

    /* Advance 9 ticks */
    for (int i = 0; i < 9; i++) {
        hvac_unit_entity_tick_1s(&s_test_unit);
        zassert_equal(s_test_unit.state, UNIT_STATE_STARTING, "State should still be STARTING at tick %d", i);
        hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
        zassert_false(relay_aa, "Compressor must remain OFF during delay");
    }
    zassert_equal(s_test_unit.startup_timer_s, 1U, "Timer should have 1s left");

    /* 10th tick triggers startup */
    hvac_unit_entity_tick_1s(&s_test_unit);
    zassert_equal(s_test_unit.state, UNIT_STATE_RUNNING, "State must transition to RUNNING");
    zassert_equal(s_test_unit.startup_timer_s, 0U, "Timer must be 0");
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_true(relay_aa, "Compressor must be ON");
    zassert_true(relay_sm, "Smart mode must be active");
    zassert_false(relay_g, "Purge fan must be OFF while running");
    zassert_true(hvac_tri_state_is_equal(hvac_unit_entity_get_tri_state(&s_test_unit), HVAC_TRI_STATE_ON), "Tri-state must be ON");

    /* Zero delay immediate start */
    hvac_unit_entity_init(&s_test_unit, 1U, 3U, 0U, 60U, 180U, true);
    ok = hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    zassert_true(ok, "Zero-delay start must be accepted");
    zassert_equal(s_test_unit.state, UNIT_STATE_RUNNING, "State must immediately become RUNNING");
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_true(relay_aa, "Compressor must be ON immediately");
}

/*============================================================================*
 * 2. Fan Purge Sub-FSM Countdown and Relay State Generation Tests
 *============================================================================*/

ZTEST(hvac_unit_suite, test_unit_fan_purge_sub_fsm)
{
    bool relay_aa = false;
    bool relay_sm = false;
    bool relay_g = false;

    /* Initialize unit with 5s fan purge delay */
    hvac_unit_entity_init(&s_test_unit, 0U, 3U, 0U, 5U, 180U, true);
    hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    zassert_equal(s_test_unit.state, UNIT_STATE_RUNNING, "Unit must be RUNNING");

    /* Request stop with fan delay enabled */
    hvac_unit_entity_request_stop(&s_test_unit, true);
    zassert_equal(s_test_unit.state, UNIT_STATE_STOPPING, "State should be STOPPING");
    zassert_equal(s_test_unit.fan_state, HVAC_FAN_STATE_WAIT_DELAY, "Fan state should be WAIT_DELAY");
    zassert_equal(s_test_unit.fan_delay_timer_s, 5U, "Purge timer should be 5s");

    /* Compressor must be off immediately, fan not yet engaged */
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_false(relay_aa, "Compressor must be OFF");
    zassert_true(relay_sm, "Smart mode must be active");
    zassert_false(relay_g, "Fan relay must be OFF during wait delay");

    /* Advance 4 ticks */
    for (int i = 0; i < 4; i++) {
        hvac_unit_entity_tick_1s(&s_test_unit);
        zassert_equal(s_test_unit.state, UNIT_STATE_STOPPING, "Still in STOPPING");
        hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
        zassert_false(relay_g, "Fan must remain OFF during delay");
    }

    /* 5th tick: expiration engages fan purge relay */
    hvac_unit_entity_tick_1s(&s_test_unit);
    zassert_equal(s_test_unit.fan_state, HVAC_FAN_STATE_SMART_IDLE_ON, "Fan state should be SMART_IDLE_ON");
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Unit state should be OFF");
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_false(relay_aa, "Compressor must be OFF");
    zassert_true(relay_sm, "Smart mode must be active");
    zassert_true(relay_g, "Fan relay relay_g must be energized (true)");

    /* Stop with fan delay disabled clears fan immediately */
    hvac_unit_entity_request_stop(&s_test_unit, false);
    zassert_equal(s_test_unit.fan_state, HVAC_FAN_STATE_DISABLED, "Fan must be disabled");
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_false(relay_g, "Fan relay must be OFF");
}

/*============================================================================*
 * 3. Pressure / Electrical Alarm Lockout (180s) and Retry Counter Tests
 *============================================================================*/

ZTEST(hvac_unit_suite, test_unit_pressure_alarm_lockout)
{
    bool relay_aa = false;
    bool relay_sm = false;
    bool relay_g = false;

    /* Start unit */
    hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    zassert_equal(s_test_unit.state, UNIT_STATE_RUNNING, "Unit must be RUNNING");

    /* Trigger high pressure alarm */
    hvac_unit_entity_update_alarms(&s_test_unit, true, false, false, false);

    /* Compressor must shut down immediately and enter 180s lockout */
    zassert_equal(s_test_unit.state, UNIT_STATE_BLOCKED_ALARM, "State must be BLOCKED_ALARM");
    zassert_equal(s_test_unit.fault_counter, 1U, "Fault counter must increment to 1");
    zassert_equal(s_test_unit.retry_timer_s, 180U, "Retry timer must be 180s");
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_false(relay_aa, "Compressor must shut down immediately");

    /* Start request must be rejected during lockout */
    bool start_ok = hvac_unit_entity_request_start(&s_test_unit);
    zassert_false(start_ok, "Start request must fail while in BLOCKED_ALARM");

    /* Tick 179 seconds */
    for (int i = 0; i < 179; i++) {
        hvac_unit_entity_tick_1s(&s_test_unit);
        zassert_equal(s_test_unit.state, UNIT_STATE_BLOCKED_ALARM, "Must remain blocked");
    }
    zassert_equal(s_test_unit.retry_timer_s, 1U, "1 second remaining");

    /* If alarm is still active at 180s, unit stays blocked */
    hvac_unit_entity_tick_1s(&s_test_unit);
    zassert_equal(s_test_unit.retry_timer_s, 0U, "Timer expired");
    zassert_equal(s_test_unit.state, UNIT_STATE_BLOCKED_ALARM, "Must stay blocked while alarm active");

    /* Now clear the alarm contact */
    hvac_unit_entity_update_alarms(&s_test_unit, false, false, false, false);
    hvac_unit_entity_tick_1s(&s_test_unit);
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Unit must transition to OFF when alarms cleared");

    /* Now start request can succeed */
    start_ok = hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    zassert_true(start_ok, "Start request must now succeed");
    zassert_equal(s_test_unit.state, UNIT_STATE_RUNNING, "Unit must be RUNNING");
}

/*============================================================================*
 * 4. Unit Eviction (UNIT_STATE_FAULT) after max_allowed_faults
 *============================================================================*/

ZTEST(hvac_unit_suite, test_unit_eviction_after_max_faults)
{
    /* s_test_unit has max_allowed_faults = 3 */

    /* Fault 1 */
    hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    hvac_unit_entity_update_alarms(&s_test_unit, true, false, false, false);
    zassert_equal(s_test_unit.state, UNIT_STATE_BLOCKED_ALARM, "Fault 1: BLOCKED_ALARM");
    zassert_equal(s_test_unit.fault_counter, 1U, "Fault count = 1");
    hvac_unit_entity_update_alarms(&s_test_unit, false, false, false, false);
    for (int i = 0; i < 180; i++) {
        hvac_unit_entity_tick_1s(&s_test_unit);
    }
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Fault 1 recovered to OFF");

    /* Fault 2 */
    hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    hvac_unit_entity_update_alarms(&s_test_unit, false, true, false, false); /* low pressure */
    zassert_equal(s_test_unit.state, UNIT_STATE_BLOCKED_ALARM, "Fault 2: BLOCKED_ALARM");
    zassert_equal(s_test_unit.fault_counter, 2U, "Fault count = 2");
    hvac_unit_entity_update_alarms(&s_test_unit, false, false, false, false);
    for (int i = 0; i < 180; i++) {
        hvac_unit_entity_tick_1s(&s_test_unit);
    }
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Fault 2 recovered to OFF");

    /* Fault 3 (Reaches max_allowed_faults = 3) */
    hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    hvac_unit_entity_update_alarms(&s_test_unit, false, false, true, false); /* supply fault */
    zassert_equal(s_test_unit.state, UNIT_STATE_FAULT, "Fault 3: Must enter permanent UNIT_STATE_FAULT");
    zassert_equal(s_test_unit.fault_counter, 3U, "Fault count = 3");

    /* Permanent eviction checks */
    hvac_unit_entity_update_alarms(&s_test_unit, false, false, false, false);
    for (int i = 0; i < 300; i++) {
        hvac_unit_entity_tick_1s(&s_test_unit);
    }
    zassert_equal(s_test_unit.state, UNIT_STATE_FAULT, "Must remain in UNIT_STATE_FAULT permanently");

    /* All subsequent start requests must be rejected */
    bool ok = hvac_unit_entity_request_start(&s_test_unit);
    zassert_false(ok, "Evicted unit cannot start");
    ok = hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    zassert_false(ok, "Evicted unit cannot start with staggered delay");

    bool relay_aa = false;
    bool relay_sm = false;
    bool relay_g = false;
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_false(relay_aa, "Compressor must remain OFF");
    zassert_false(relay_g, "Fan must remain OFF");
}

ZTEST(hvac_unit_suite, test_idle_unit_increment_fault_guard)
{
    /* R-06: increment_fault state guard must ignore calls when unit is OFF/IDLE */
    hvac_unit_entity_init(&s_test_unit, 0U, 3U, 10U, 30U, 180U, true);
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Unit should start in OFF/IDLE");
    zassert_equal(s_test_unit.fault_counter, 0U, "Initial fault count must be 0");

    hvac_unit_entity_increment_fault(&s_test_unit);
    zassert_equal(s_test_unit.fault_counter, 0U, "Fault count must not increment for OFF unit");
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Unit must remain in OFF");

    hvac_unit_entity_increment_fault(&s_test_unit);
    hvac_unit_entity_increment_fault(&s_test_unit);
    hvac_unit_entity_increment_fault(&s_test_unit);
    zassert_equal(s_test_unit.fault_counter, 0U, "Fault count must still be 0");
    zassert_not_equal(s_test_unit.state, UNIT_STATE_FAULT, "Unit must not enter FAULT state");
}

/*============================================================================*
 * 5. Bypass Mode (relay_sm = 0) Tests
 *============================================================================*/

ZTEST(hvac_unit_suite, test_unit_bypass_mode)
{
    bool relay_aa = false;
    bool relay_sm = false;
    bool relay_g = false;

    /* Start unit */
    hvac_unit_entity_request_start_staggered(&s_test_unit, 0U);
    zassert_equal(s_test_unit.state, UNIT_STATE_RUNNING, "Unit running");

    /* Force bypass */
    hvac_unit_entity_set_bypass(&s_test_unit);
    zassert_equal(s_test_unit.state, UNIT_STATE_BYPASS, "State must be BYPASS");
    zassert_true(hvac_tri_state_is_equal(hvac_unit_entity_get_tri_state(&s_test_unit), HVAC_TRI_STATE_BYPASS), "Tri-state must be BYPASS");

    /* Relays in bypass: relay_sm = 0, relay_aa = 0, relay_g = 0 */
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_false(relay_sm, "relay_sm must be 0 (transferred to mechanical bypass)");
    zassert_false(relay_aa, "relay_aa must be 0");
    zassert_false(relay_g, "relay_g must be 0");

    /* Start requests in bypass must be rejected */
    bool ok = hvac_unit_entity_request_start(&s_test_unit);
    zassert_false(ok, "Cannot start unit in bypass");

    /* Clearing bypass restores smart mode */
    hvac_unit_entity_request_stop(&s_test_unit, false);
    zassert_equal(s_test_unit.state, UNIT_STATE_OFF, "Cleared bypass returns to OFF");
    hvac_unit_entity_get_relays(&s_test_unit, &relay_aa, &relay_sm, &relay_g);
    zassert_true(relay_sm, "relay_sm is restored to 1");
}

/*============================================================================*
 * 6. Port Interface (Rule §4.1.D) and Fake Tests
 *============================================================================*/

ZTEST(hvac_unit_suite, test_i_hvac_unit_port_and_fake)
{
    i_hvac_unit_t port = fake_hvac_unit_get_port();
    zassert_true(i_hvac_unit_is_bound(&port), "Port must be bound");

    /* Test unbounded handle checks */
    i_hvac_unit_t unbound = { .api = NULL, .impl = NULL };
    zassert_false(i_hvac_unit_is_bound(&unbound), "Unbound handle check");
    zassert_equal(i_hvac_unit_set_tri_state(&unbound, 0U, HVAC_TRI_STATE_ON), -EINVAL, "Unbound set must fail");
    hvac_tri_state_t tri = HVAC_TRI_STATE_OFF;
    zassert_equal(i_hvac_unit_get_tri_state(&unbound, 0U, &tri), -EINVAL, "Unbound get must fail");

    /* Out of bounds unit ID */
    zassert_equal(i_hvac_unit_set_tri_state(&port, HVAC_MAX_UNITS_TOTAL, HVAC_TRI_STATE_ON), -EINVAL, "Out of bounds unit ID");
    zassert_equal(i_hvac_unit_get_tri_state(&port, HVAC_MAX_UNITS_TOTAL, &tri), -EINVAL, "Out of bounds unit ID");

    /* Invalid tri-state value */
    hvac_tri_state_t invalid_tri = { .relay_aa = true, .relay_sm = false, .relay_g = false };
    zassert_equal(i_hvac_unit_set_tri_state(&port, 0U, invalid_tri), -EINVAL, "Invalid tri-state must fail");

    /* NULL out_tri pointer */
    zassert_equal(i_hvac_unit_get_tri_state(&port, 0U, NULL), -EINVAL, "NULL pointer must fail");

    /* Valid set and get through wrappers */
    int ret = i_hvac_unit_set_tri_state(&port, 2U, HVAC_TRI_STATE_ON);
    zassert_equal(ret, 0, "Set tri-state must succeed");
    zassert_true(hvac_tri_state_is_equal(fake_hvac_unit_get_unit_state(2U), HVAC_TRI_STATE_ON), "Fake state must match");

    ret = i_hvac_unit_get_tri_state(&port, 2U, &tri);
    zassert_equal(ret, 0, "Get tri-state must succeed");
    zassert_true(hvac_tri_state_is_equal(tri, HVAC_TRI_STATE_ON), "Read value must match");

    /* Error injection */
    fake_hvac_unit_set_errors(-EIO, -ENODEV);
    ret = i_hvac_unit_set_tri_state(&port, 2U, HVAC_TRI_STATE_OFF);
    zassert_equal(ret, -EIO, "Injected set error must propagate");
    ret = i_hvac_unit_get_tri_state(&port, 2U, &tri);
    zassert_equal(ret, -ENODEV, "Injected get error must propagate");
}
