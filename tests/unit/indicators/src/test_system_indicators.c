/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>
#include <errno.h>

#include "common/indicator_types.h"
#include "interfaces/i_led.h"
#include "interfaces/i_buzzer.h"
#include "fake_led.h"
#include "fake_buzzer.h"
#include "application/active_objects/ao_system_indicator.h"

K_THREAD_STACK_DEFINE(s_test_ao_indicator_stack, 1536);
static struct k_msgq s_test_indicator_msgq;
static uint8_t s_test_indicator_queue_buf[AO_INDICATOR_QUEUE_DEPTH * sizeof(ao_indicator_event_t)];
static ao_system_indicator_t s_test_ao_indicator;

static void test_indicator_alarm_feed_cb(const struct zbus_channel *chan) { ARG_UNUSED(chan); }
ZBUS_LISTENER_DEFINE(lis_indicator_alarm_feed, test_indicator_alarm_feed_cb);

static void *system_indicators_setup(void)
{
    k_msgq_init(&s_test_indicator_msgq, s_test_indicator_queue_buf,
                sizeof(ao_indicator_event_t), AO_INDICATOR_QUEUE_DEPTH);
    return NULL;
}

static void system_indicators_before(void *data)
{
    ARG_UNUSED(data);
    fake_led_reset();
    fake_buzzer_reset();
    k_msgq_purge(&s_test_indicator_msgq);
}

ZTEST_SUITE(system_indicators_suite, NULL, system_indicators_setup, system_indicators_before, NULL, NULL);

/*============================================================================*
 * 1. Domain Types and Validation Predicates Tests
 *============================================================================*/

ZTEST(system_indicators_suite, test_indicator_types_and_predicates)
{
    /* LED state */
    zassert_equal(LED_STATE_OFF, 0, "LED_STATE_OFF should be 0");
    zassert_true(led_state_is_valid(LED_STATE_OFF), "OFF must be valid");
    zassert_true(led_state_is_valid(LED_STATE_ON), "ON must be valid");
    zassert_true(led_state_is_valid(LED_STATE_BLINK_SLOW), "BLINK_SLOW must be valid");
    zassert_true(led_state_is_valid(LED_STATE_BLINK_FAST), "BLINK_FAST must be valid");
    zassert_true(led_state_is_valid(LED_STATE_BLINK_HEARTBEAT), "BLINK_HEARTBEAT must be valid");
    zassert_false(led_state_is_valid(LED_STATE_MAX), "MAX must be invalid");
    zassert_false(led_state_is_valid((led_state_t)-1), "Negative must be invalid");

    /* Buzzer pattern */
    zassert_equal(BUZZER_PATTERN_SILENT, 0, "BUZZER_PATTERN_SILENT should be 0");
    zassert_true(buzzer_pattern_is_valid(BUZZER_PATTERN_SILENT), "SILENT must be valid");
    zassert_true(buzzer_pattern_is_valid(BUZZER_PATTERN_BEEP_SHORT), "BEEP_SHORT must be valid");
    zassert_true(buzzer_pattern_is_valid(BUZZER_PATTERN_BEEP_LONG), "BEEP_LONG must be valid");
    zassert_true(buzzer_pattern_is_valid(BUZZER_PATTERN_ALARM_INTERMITTENT), "ALARM_INTERMITTENT must be valid");
    zassert_true(buzzer_pattern_is_valid(BUZZER_PATTERN_ALARM_CONTINUOUS), "ALARM_CONTINUOUS must be valid");
    zassert_true(buzzer_pattern_is_valid(BUZZER_PATTERN_STARTUP_CHIME), "STARTUP_CHIME must be valid");
    zassert_false(buzzer_pattern_is_valid(BUZZER_PATTERN_MAX), "MAX must be invalid");

    /* Alarm level */
    zassert_equal(ALARM_LEVEL_NONE, 0, "ALARM_LEVEL_NONE should be 0");
    zassert_true(alarm_level_is_valid(ALARM_LEVEL_NONE), "NONE must be valid");
    zassert_true(alarm_level_is_valid(ALARM_LEVEL_INFO), "INFO must be valid");
    zassert_true(alarm_level_is_valid(ALARM_LEVEL_WARNING), "WARNING must be valid");
    zassert_true(alarm_level_is_valid(ALARM_LEVEL_CRITICAL), "CRITICAL must be valid");
    zassert_false(alarm_level_is_valid(ALARM_LEVEL_MAX), "MAX must be invalid");

    /* System health */
    zassert_equal(SYSTEM_HEALTH_HEALTHY, 0, "SYSTEM_HEALTH_HEALTHY should be 0");
    zassert_true(system_health_is_valid(SYSTEM_HEALTH_HEALTHY), "HEALTHY must be valid");
    zassert_true(system_health_is_valid(SYSTEM_HEALTH_DEGRADED), "DEGRADED must be valid");
    zassert_true(system_health_is_valid(SYSTEM_HEALTH_FAULT), "FAULT must be valid");
    zassert_false(system_health_is_valid(SYSTEM_HEALTH_MAX), "MAX must be invalid");

    /* Indicator command */
    zassert_equal(INDICATOR_CMD_AUTO, 0, "INDICATOR_CMD_AUTO should be 0");
    zassert_true(indicator_cmd_is_valid(INDICATOR_CMD_AUTO), "AUTO must be valid");
    zassert_true(indicator_cmd_is_valid(INDICATOR_CMD_FORCE_LED), "FORCE_LED must be valid");
    zassert_true(indicator_cmd_is_valid(INDICATOR_CMD_FORCE_BUZZER), "FORCE_BUZZER must be valid");
    zassert_true(indicator_cmd_is_valid(INDICATOR_CMD_TEST), "TEST must be valid");
    zassert_false(indicator_cmd_is_valid(INDICATOR_CMD_MAX), "MAX must be invalid");
}

/*============================================================================*
 * 2. i_led Interface and Fake LED Tests
 *============================================================================*/

ZTEST(system_indicators_suite, test_i_led_wrappers_and_fakes)
{
    i_led_t unbound_led = {0};
    zassert_false(i_led_is_bound(&unbound_led), "Unbound LED handle must return false");
    zassert_equal(i_led_set_state(&unbound_led, LED_STATE_ON), -EINVAL, "Unbound LED must reject set_state");
    zassert_equal(i_led_get_state(&unbound_led, NULL), -EINVAL, "Unbound LED must reject get_state");
    zassert_equal(i_led_set_blink_rate(&unbound_led, 500, 50), -EINVAL, "Unbound LED must reject set_blink_rate");

    i_led_t led = fake_led_get_port();
    zassert_true(i_led_is_bound(&led), "Fake LED port must be bound");

    /* Parameter validation */
    zassert_equal(i_led_set_state(&led, LED_STATE_MAX), -EINVAL, "Invalid state must return -EINVAL");
    zassert_equal(i_led_get_state(&led, NULL), -EINVAL, "NULL out_state must return -EINVAL");
    zassert_equal(i_led_set_blink_rate(&led, 0, 50), -EINVAL, "0 ms period must return -EINVAL");
    zassert_equal(i_led_set_blink_rate(&led, 1000, 101), -EINVAL, ">100 duty must return -EINVAL");

    /* Normal operations */
    led_state_t read_state = LED_STATE_OFF;
    zassert_equal(i_led_set_state(&led, LED_STATE_ON), 0, "set_state ON must succeed");
    zassert_equal(i_led_get_state(&led, &read_state), 0, "get_state must succeed");
    zassert_equal(read_state, LED_STATE_ON, "Read state should be ON");

    zassert_equal(i_led_set_blink_rate(&led, 800, 25), 0, "set_blink_rate must succeed");
    const fake_led_state_t *st = fake_led_get_state();
    zassert_equal(st->set_state_calls, 1, "set_state_calls should be 1");
    zassert_equal(st->get_state_calls, 1, "get_state_calls should be 1");
    zassert_equal(st->set_blink_rate_calls, 1, "set_blink_rate_calls should be 1");
    zassert_equal(st->blink_period_ms, 800, "Blink period should match");
    zassert_equal(st->blink_duty_cycle_pct, 25, "Blink duty cycle should match");

    /* Error injection */
    fake_led_set_errors(-EIO, -EFAULT, -ENODEV);
    zassert_equal(i_led_set_state(&led, LED_STATE_OFF), -EIO, "Should return simulated -EIO");
    zassert_equal(i_led_get_state(&led, &read_state), -EFAULT, "Should return simulated -EFAULT");
    zassert_equal(i_led_set_blink_rate(&led, 500, 50), -ENODEV, "Should return simulated -ENODEV");
}

/*============================================================================*
 * 3. i_buzzer Interface and Fake Buzzer Tests
 *============================================================================*/

ZTEST(system_indicators_suite, test_i_buzzer_wrappers_and_fakes)
{
    i_buzzer_t unbound = {0};
    zassert_false(i_buzzer_is_bound(&unbound), "Unbound buzzer handle must return false");
    zassert_equal(i_buzzer_play_tone(&unbound, 2000, 100), -EINVAL, "Unbound must reject play_tone");
    zassert_equal(i_buzzer_play_pattern(&unbound, BUZZER_PATTERN_BEEP_SHORT, 2000), -EINVAL, "Unbound must reject play_pattern");
    zassert_equal(i_buzzer_stop(&unbound), -EINVAL, "Unbound must reject stop");
    zassert_equal(i_buzzer_get_active_pattern(&unbound, NULL), -EINVAL, "Unbound must reject get_active_pattern");

    i_buzzer_t buzzer = fake_buzzer_get_port();
    zassert_true(i_buzzer_is_bound(&buzzer), "Fake buzzer must be bound");

    /* Parameter validation */
    zassert_equal(i_buzzer_play_pattern(&buzzer, BUZZER_PATTERN_MAX, 2000), -EINVAL, "Invalid pattern must return -EINVAL");
    zassert_equal(i_buzzer_get_active_pattern(&buzzer, NULL), -EINVAL, "NULL out_pattern must return -EINVAL");

    /* Normal operations */
    buzzer_pattern_t pat = BUZZER_PATTERN_MAX;
    zassert_equal(i_buzzer_play_tone(&buzzer, 1500, 80), 0, "play_tone must succeed");
    const fake_buzzer_state_t *st = fake_buzzer_get_state();
    zassert_equal(st->play_tone_calls, 1, "play_tone_calls should be 1");
    zassert_equal(st->last_frequency_hz, 1500, "Tone frequency should match");
    zassert_equal(st->last_duration_ms, 80, "Tone duration should match");
    zassert_true(st->is_sounding, "Buzzer should be sounding");

    zassert_equal(i_buzzer_play_pattern(&buzzer, BUZZER_PATTERN_ALARM_INTERMITTENT, 1800), 0, "play_pattern must succeed");
    zassert_equal(i_buzzer_get_active_pattern(&buzzer, &pat), 0, "get_active_pattern must succeed");
    zassert_equal(pat, BUZZER_PATTERN_ALARM_INTERMITTENT, "Pattern must match");

    zassert_equal(i_buzzer_stop(&buzzer), 0, "stop must succeed");
    zassert_equal(i_buzzer_get_active_pattern(&buzzer, &pat), 0, "get_active_pattern must succeed");
    zassert_equal(pat, BUZZER_PATTERN_SILENT, "Pattern must be SILENT after stop");
    zassert_false(st->is_sounding, "Buzzer must not be sounding after stop");

    /* Error injection */
    fake_buzzer_set_errors(-EIO, -ENOMEM, -EBUSY, -EFAULT);
    zassert_equal(i_buzzer_play_tone(&buzzer, 1000, 10), -EIO, "Should return simulated -EIO");
    zassert_equal(i_buzzer_play_pattern(&buzzer, BUZZER_PATTERN_BEEP_SHORT, 1000), -ENOMEM, "Should return simulated -ENOMEM");
    zassert_equal(i_buzzer_stop(&buzzer), -EBUSY, "Should return simulated -EBUSY");
    zassert_equal(i_buzzer_get_active_pattern(&buzzer, &pat), -EFAULT, "Should return simulated -EFAULT");
}

/*============================================================================*
 * 4. Active Object Startup Chime Progression Tests
 *============================================================================*/

ZTEST(system_indicators_suite, test_ao_indicator_startup_chime_progression)
{
    i_led_t led = fake_led_get_port();
    i_buzzer_t buzzer = fake_buzzer_get_port();

    zassert_equal(ao_system_indicator_init(&s_test_ao_indicator, &led, &buzzer, &s_test_indicator_msgq,
                                           s_test_ao_indicator_stack,
                                           K_THREAD_STACK_SIZEOF(s_test_ao_indicator_stack)),
                  0, "ao_system_indicator_init must succeed");

    /* Request startup chime */
    ao_indicator_event_t evt = {
        .type = AO_INDICATOR_EVT_STARTUP_CHIME,
    };
    zassert_equal(ao_system_indicator_process_event(&s_test_ao_indicator, &evt), 0, "Process startup chime event");
    s_test_ao_indicator.chime_start_ms = 1000; /* Set fixed start time */

    /* Step at t = 1000 ms: Phase 1 (1500 Hz Note for 80 ms, LED steady ON) */
    zassert_equal(ao_system_indicator_step(&s_test_ao_indicator, 1000), 0, "Step at 1000 ms");
    const fake_led_state_t *led_st = fake_led_get_state();
    const fake_buzzer_state_t *bz_st = fake_buzzer_get_state();
    zassert_equal(led_st->current_state, LED_STATE_ON, "LED must be ON during startup chime");
    zassert_true(bz_st->is_sounding, "Buzzer must be sounding during Note 1");
    zassert_equal(bz_st->last_frequency_hz, 1500, "Tone 1 must be 1500 Hz");

    /* Step at t = 1050 ms: still in Note 1 (80 ms total) */
    zassert_equal(ao_system_indicator_step(&s_test_ao_indicator, 1050), 0, "Step at 1050 ms");
    zassert_true(bz_st->is_sounding, "Buzzer must still be sounding");
    zassert_equal(bz_st->last_frequency_hz, 1500, "Tone 1 must still be 1500 Hz");

    /* Step at t = 1090 ms: Silence phase (between 80 ms and 110 ms) */
    zassert_equal(ao_system_indicator_step(&s_test_ao_indicator, 1090), 0, "Step at 1090 ms");
    zassert_false(bz_st->is_sounding, "Buzzer must be silent in pause");
    zassert_equal(led_st->current_state, LED_STATE_ON, "LED must remain steady ON");

    /* Step at t = 1120 ms: Phase 2 (2500 Hz Note for 150 ms, from 110 ms to 260 ms) */
    zassert_equal(ao_system_indicator_step(&s_test_ao_indicator, 1120), 0, "Step at 1120 ms");
    zassert_true(bz_st->is_sounding, "Buzzer must be sounding during Note 2");
    zassert_equal(bz_st->last_frequency_hz, 2500, "Tone 2 must be 2500 Hz");

    /* Step at t = 1270 ms: Chime completed (> 260 ms) */
    zassert_equal(ao_system_indicator_step(&s_test_ao_indicator, 1270), 0, "Step at 1270 ms");
    zassert_false(bz_st->is_sounding, "Buzzer must be silent after chime finishes");
    zassert_equal(led_st->current_state, LED_STATE_ON, "LED must remain steady ON in HEALTHY state");

    indicator_status_t status;
    zassert_equal(ao_system_indicator_get_status(&s_test_ao_indicator, &status), 0, "Query status");
    zassert_equal(status.led_state, LED_STATE_ON, "Target LED must be steady ON");
    zassert_equal(status.buzzer_pattern, BUZZER_PATTERN_SILENT, "Target buzzer must be SILENT");
    zassert_equal(status.system_health, SYSTEM_HEALTH_HEALTHY, "System health must be HEALTHY");

    zassert_equal(ao_system_indicator_deinit(&s_test_ao_indicator), 0, "Deinit");
}

/*============================================================================*
 * 5. Startup Chime Aborted if Problem Detected on Boot
 *============================================================================*/

ZTEST(system_indicators_suite, test_ao_indicator_startup_chime_aborted_on_alarm)
{
    i_led_t led = fake_led_get_port();
    i_buzzer_t buzzer = fake_buzzer_get_port();

    zassert_equal(ao_system_indicator_init(&s_test_ao_indicator, &led, &buzzer, &s_test_indicator_msgq,
                                           s_test_ao_indicator_stack,
                                           K_THREAD_STACK_SIZEOF(s_test_ao_indicator_stack)),
                  0, "ao_system_indicator_init must succeed");

    /* Trigger startup chime */
    ao_indicator_event_t evt_chime = {
        .type = AO_INDICATOR_EVT_STARTUP_CHIME,
    };
    (void)ao_system_indicator_process_event(&s_test_ao_indicator, &evt_chime);
    s_test_ao_indicator.chime_start_ms = 1000;

    /* A critical alarm arrives! */
    ao_indicator_event_t evt_alarm = {
        .type = AO_INDICATOR_EVT_ALARM,
        .data.alarm = {
            .alarm_id = 42,
            .level = ALARM_LEVEL_CRITICAL,
            .is_active = true,
            .timestamp_ms = 1020,
        },
    };
    (void)ao_system_indicator_process_event(&s_test_ao_indicator, &evt_alarm);

    /* Step at t = 1020 ms: chime must be aborted and critical alarm must take over! */
    zassert_equal(ao_system_indicator_step(&s_test_ao_indicator, 1020), 0, "Step at 1020 ms");
    zassert_false(s_test_ao_indicator.startup_chime_active, "Startup chime must be aborted");

    indicator_status_t status;
    zassert_equal(ao_system_indicator_get_status(&s_test_ao_indicator, &status), 0, "Query status");
    zassert_equal(status.led_state, LED_STATE_BLINK_FAST, "Critical alarm triggers FAST blink");
    zassert_equal(status.buzzer_pattern, BUZZER_PATTERN_ALARM_CONTINUOUS, "Critical alarm triggers continuous buzzer");

    zassert_equal(ao_system_indicator_deinit(&s_test_ao_indicator), 0, "Deinit");
}

/*============================================================================*
 * 6. Alarm Priority State Machine Tests
 *============================================================================*/

ZTEST(system_indicators_suite, test_ao_indicator_alarm_priority)
{
    i_led_t led = fake_led_get_port();
    i_buzzer_t buzzer = fake_buzzer_get_port();

    ao_system_indicator_init(&s_test_ao_indicator, &led, &buzzer, &s_test_indicator_msgq,
                             s_test_ao_indicator_stack,
                             K_THREAD_STACK_SIZEOF(s_test_ao_indicator_stack));

    /* 1. Add WARNING alarm */
    alarm_msg_t warn = {
        .alarm_id = 10,
        .level = ALARM_LEVEL_WARNING,
        .is_active = true,
        .timestamp_ms = 100,
    };
    ao_system_indicator_post_alarm(&s_test_ao_indicator, &warn);
    ao_indicator_event_t evt;
    k_msgq_get(&s_test_indicator_msgq, &evt, K_NO_WAIT);
    ao_system_indicator_process_event(&s_test_ao_indicator, &evt);

    ao_system_indicator_step(&s_test_ao_indicator, 100);
    indicator_status_t status;
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_equal(status.active_alarm_level, ALARM_LEVEL_WARNING, "Active alarm should be WARNING");
    zassert_equal(status.led_state, LED_STATE_BLINK_SLOW, "WARNING triggers SLOW blink");
    zassert_equal(status.buzzer_pattern, BUZZER_PATTERN_ALARM_INTERMITTENT, "WARNING triggers intermittent tone");

    /* 2. Add CRITICAL alarm: CRITICAL > WARNING */
    alarm_msg_t crit = {
        .alarm_id = 20,
        .level = ALARM_LEVEL_CRITICAL,
        .is_active = true,
        .timestamp_ms = 200,
    };
    ao_system_indicator_post_alarm(&s_test_ao_indicator, &crit);
    k_msgq_get(&s_test_indicator_msgq, &evt, K_NO_WAIT);
    ao_system_indicator_process_event(&s_test_ao_indicator, &evt);

    ao_system_indicator_step(&s_test_ao_indicator, 200);
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_equal(status.active_alarm_level, ALARM_LEVEL_CRITICAL, "Active alarm should be CRITICAL");
    zassert_equal(status.led_state, LED_STATE_BLINK_FAST, "CRITICAL triggers FAST blink");
    zassert_equal(status.buzzer_pattern, BUZZER_PATTERN_ALARM_CONTINUOUS, "CRITICAL triggers continuous tone");

    /* 3. Clear CRITICAL alarm: drops back to WARNING */
    crit.is_active = false;
    ao_system_indicator_post_alarm(&s_test_ao_indicator, &crit);
    k_msgq_get(&s_test_indicator_msgq, &evt, K_NO_WAIT);
    ao_system_indicator_process_event(&s_test_ao_indicator, &evt);

    ao_system_indicator_step(&s_test_ao_indicator, 300);
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_equal(status.active_alarm_level, ALARM_LEVEL_WARNING, "Active alarm should drop to WARNING");
    zassert_equal(status.led_state, LED_STATE_BLINK_SLOW, "Falls back to SLOW blink");

    /* 4. Clear WARNING alarm: returns to HEALTHY */
    warn.is_active = false;
    ao_system_indicator_post_alarm(&s_test_ao_indicator, &warn);
    k_msgq_get(&s_test_indicator_msgq, &evt, K_NO_WAIT);
    ao_system_indicator_process_event(&s_test_ao_indicator, &evt);

    ao_system_indicator_step(&s_test_ao_indicator, 400);
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_equal(status.active_alarm_level, ALARM_LEVEL_NONE, "No active alarms");
    zassert_equal(status.led_state, LED_STATE_ON, "Returns to steady HEALTHY LED ON");
    zassert_equal(status.buzzer_pattern, BUZZER_PATTERN_SILENT, "Returns to silent buzzer");

    zassert_equal(ao_system_indicator_deinit(&s_test_ao_indicator), 0, "Deinit");
}

/*============================================================================*
 * 7. Manual Override Command Tests
 *============================================================================*/

ZTEST(system_indicators_suite, test_ao_indicator_manual_override)
{
    i_led_t led = fake_led_get_port();
    i_buzzer_t buzzer = fake_buzzer_get_port();

    ao_system_indicator_init(&s_test_ao_indicator, &led, &buzzer, &s_test_indicator_msgq,
                             s_test_ao_indicator_stack,
                             K_THREAD_STACK_SIZEOF(s_test_ao_indicator_stack));

    /* Force LED command for 1000 ms */
    indicator_cmd_msg_t cmd = {
        .cmd = INDICATOR_CMD_FORCE_LED,
        .led_state = LED_STATE_BLINK_FAST,
        .buzzer_pattern = BUZZER_PATTERN_SILENT,
        .duration_ms = 1000,
    };
    ao_system_indicator_post_command(&s_test_ao_indicator, &cmd);
    ao_indicator_event_t evt;
    k_msgq_get(&s_test_indicator_msgq, &evt, K_NO_WAIT);
    ao_system_indicator_process_event(&s_test_ao_indicator, &evt);
    s_test_ao_indicator.manual_start_ms = 5000;

    ao_system_indicator_step(&s_test_ao_indicator, 5500);
    indicator_status_t status;
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_true(status.manual_override, "Manual override must be active");
    zassert_equal(status.led_state, LED_STATE_BLINK_FAST, "LED forced to BLINK_FAST");

    /* Step past duration: 5000 + 1000 = 6000 ms */
    ao_system_indicator_step(&s_test_ao_indicator, 6050);
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_false(status.manual_override, "Manual override must expire");
    zassert_equal(status.led_state, LED_STATE_ON, "Returns to normal HEALTHY LED ON");

    /* Force AUTO command immediately cancels override */
    cmd.cmd = INDICATOR_CMD_FORCE_BUZZER;
    cmd.buzzer_pattern = BUZZER_PATTERN_ALARM_INTERMITTENT;
    cmd.duration_ms = 0; /* permanent */
    ao_system_indicator_post_command(&s_test_ao_indicator, &cmd);
    k_msgq_get(&s_test_indicator_msgq, &evt, K_NO_WAIT);
    ao_system_indicator_process_event(&s_test_ao_indicator, &evt);

    ao_system_indicator_step(&s_test_ao_indicator, 7000);
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_true(status.manual_override, "Manual override must be active");

    cmd.cmd = INDICATOR_CMD_AUTO;
    ao_system_indicator_post_command(&s_test_ao_indicator, &cmd);
    k_msgq_get(&s_test_indicator_msgq, &evt, K_NO_WAIT);
    ao_system_indicator_process_event(&s_test_ao_indicator, &evt);

    ao_system_indicator_step(&s_test_ao_indicator, 7100);
    ao_system_indicator_get_status(&s_test_ao_indicator, &status);
    zassert_false(status.manual_override, "AUTO must immediately cancel manual override");

    zassert_equal(ao_system_indicator_deinit(&s_test_ao_indicator), 0, "Deinit");
}

/*============================================================================*
 * 8. Active Object Thread Lifecycle Tests
 *============================================================================*/

ZTEST(system_indicators_suite, test_ao_indicator_lifecycle)
{
    i_led_t led = fake_led_get_port();
    i_buzzer_t buzzer = fake_buzzer_get_port();

    /* Init */
    zassert_equal(ao_system_indicator_init(&s_test_ao_indicator, &led, &buzzer, &s_test_indicator_msgq,
                                           s_test_ao_indicator_stack,
                                           K_THREAD_STACK_SIZEOF(s_test_ao_indicator_stack)),
                  0, "Init");

    /* Start */
    zassert_equal(ao_system_indicator_start(&s_test_ao_indicator), 0, "Start");
    zassert_equal(ao_system_indicator_start(&s_test_ao_indicator), -EALREADY, "Double start");

    /* Post alarm while running */
    alarm_msg_t warn = {
        .alarm_id = 99,
        .level = ALARM_LEVEL_WARNING,
        .is_active = true,
        .timestamp_ms = (uint32_t)k_uptime_get(),
    };
    zassert_equal(ao_system_indicator_post_alarm(&s_test_ao_indicator, &warn), 0, "Post alarm");

    /* Wait a couple of ticks (AO_INDICATOR_TICK_MS is 25 ms) */
    k_sleep(K_MSEC(75));

    indicator_status_t status;
    zassert_equal(ao_system_indicator_get_status(&s_test_ao_indicator, &status), 0, "Get status");
    zassert_equal(status.active_alarm_level, ALARM_LEVEL_WARNING, "Alarm level should be WARNING");

    /* Stop */
    zassert_equal(ao_system_indicator_stop(&s_test_ao_indicator), 0, "Stop");
    zassert_equal(ao_system_indicator_wait_stopped(&s_test_ao_indicator, K_MSEC(500)), 0, "Wait stopped");
    zassert_equal(ao_system_indicator_stop(&s_test_ao_indicator), 0, "Stop when stopped");

    /* Deinit */
    zassert_equal(ao_system_indicator_deinit(&s_test_ao_indicator), 0, "Deinit");
}
