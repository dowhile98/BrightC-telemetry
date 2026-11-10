/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_digital_input.c
 * @brief Comprehensive unit test suite for Digital Input & Contact Alarm Subsystem.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "common/digital_input_types.h"
#include "common/hardware_pin_map.h"
#include "common/app_defaults.h"
#include "interfaces/i_digital_input.h"
#include "domain/services/digital_input_aggregator.h"
#include "application/zbus/digital_input_channels.h"
#include "application/active_objects/ao_digital_input.h"
#include "fake_digital_input.h"

/*============================================================================*
 * TEST SUITE SETUP & FIXTURE
 *============================================================================*/

K_THREAD_STACK_DEFINE(s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE);
static struct k_msgq s_test_ao_di_msgq;
static uint8_t s_test_ao_di_queue_buf[AO_DI_QUEUE_DEPTH * sizeof(ao_di_event_t)];
static ao_digital_input_t s_test_ao_di;

static uint32_t s_test_hvac_di_feed_calls;
static di_snapshot_msg_t s_test_last_di_snapshot;

static void test_hvac_di_feed_cb(const struct zbus_channel *chan)
{
    if (chan == &chan_digital_input_snapshot) {
        const di_snapshot_msg_t *msg = zbus_chan_const_msg(chan);
        if (msg != NULL) {
            s_test_hvac_di_feed_calls++;
            s_test_last_di_snapshot = *msg;
        }
    }
}

ZBUS_LISTENER_DEFINE(lis_hvac_di_feed, test_hvac_di_feed_cb);

static void test_alarm_di_feed_cb(const struct zbus_channel *chan) { ARG_UNUSED(chan); }
ZBUS_LISTENER_DEFINE(lis_alarm_di_feed, test_alarm_di_feed_cb);

static void *digital_input_suite_setup(void)
{
    k_msgq_init(&s_test_ao_di_msgq, s_test_ao_di_queue_buf,
                sizeof(ao_di_event_t), AO_DI_QUEUE_DEPTH);
    return NULL;
}

static void digital_input_suite_before(void *fixture)
{
    ARG_UNUSED(fixture);
    fake_digital_input_reset();
    k_msgq_purge(&s_test_ao_di_msgq);
    s_test_hvac_di_feed_calls = 0U;
    memset(&s_test_last_di_snapshot, 0, sizeof(s_test_last_di_snapshot));
}

ZTEST_SUITE(digital_input_suite, NULL, digital_input_suite_setup, digital_input_suite_before, NULL, NULL);

/*============================================================================*
 * 1. SOFTWARE INTEGRATOR DEBOUNCE FILTER TESTS
 *============================================================================*/

ZTEST(digital_input_suite, test_debounce_filter_initialization)
{
    debounce_filter_t filter;
    debounce_filter_init(&filter, 100U, false);

    zassert_equal(filter.debounce_ms, 100U, "Debounce ms must be 100");
    zassert_false(filter.stable_state, "Initial stable state must be false");
    zassert_false(filter.raw_last_state, "Initial raw state must be false");
    zassert_equal(filter.stable_counter_ms, 0U, "Initial stable counter must be 0");

    /* Null pointer check */
    debounce_filter_init(NULL, 100U, false);
}

ZTEST(digital_input_suite, test_debounce_filter_glitch_rejection)
{
    debounce_filter_t filter;
    debounce_filter_init(&filter, 100U, false);

    bool stable = false;
    bool changed = false;

    /* Sample 1 for 40 ms (two 20ms ticks) */
    changed = debounce_filter_update(&filter, true, 20U, &stable);
    zassert_false(changed, "Must not transition at 20 ms");
    zassert_false(stable, "Stable must remain false");

    changed = debounce_filter_update(&filter, true, 20U, &stable);
    zassert_false(changed, "Must not transition at 40 ms");
    zassert_false(stable, "Stable must remain false");

    /* Glitch: drops back to 0 for one 20ms tick */
    changed = debounce_filter_update(&filter, false, 20U, &stable);
    zassert_false(changed, "Must not transition on glitch drop");
    zassert_false(stable, "Stable must remain false");
    zassert_equal(filter.stable_counter_ms, 0U, "Integrator accumulator must reset to 0 on glitch");

    /* Sample 1 again: edge detected at tick 1, counter resets to 0 */
    debounce_filter_update(&filter, true, 20U, &stable); /* edge: counter = 0 */
    debounce_filter_update(&filter, true, 20U, &stable); /* counter = 20 */
    changed = debounce_filter_update(&filter, true, 20U, &stable); /* counter = 40 */
    zassert_false(changed, "Must not transition at 40 ms after reset");
    zassert_false(stable, "Stable must remain false");

    /* Now sustained for full 100 ms (another 3 ticks of 20 ms = 60, 80, 100 ms) */
    debounce_filter_update(&filter, true, 20U, &stable); /* counter = 60 */
    debounce_filter_update(&filter, true, 20U, &stable); /* counter = 80 */
    changed = debounce_filter_update(&filter, true, 20U, &stable); /* counter = 100 */
    zassert_true(changed, "Must transition after uninterrupted 100 ms stability");
    zassert_true(stable, "Stable state must now be true");
    zassert_true(filter.stable_state, "Filter state must be true");

    /* Subsequent ticks at true should NOT re-trigger changed */
    changed = debounce_filter_update(&filter, true, 20U, &stable);
    zassert_false(changed, "Subsequent steady samples must not report transition");
    zassert_true(stable, "Stable state must remain true");
}

ZTEST(digital_input_suite, test_debounce_filter_zero_bypass_mode)
{
    debounce_filter_t filter;
    /* Debounce ms = 0 represents pre-filtered Modbus / remote sources */
    debounce_filter_init(&filter, 0U, false);

    bool stable = false;
    bool changed = false;

    /* Next sample is true: must commit immediately on same cycle */
    changed = debounce_filter_update(&filter, true, 20U, &stable);
    zassert_true(changed, "Bypass mode (0 ms) must transition immediately");
    zassert_true(stable, "Stable state must be true on same cycle");
    zassert_true(filter.stable_state, "Filter stable state must be true");

    /* Same sample: no change */
    changed = debounce_filter_update(&filter, true, 20U, &stable);
    zassert_false(changed, "No transition when state does not change");
    zassert_true(stable, "Stable state must remain true");

    /* Transitions back to false immediately */
    changed = debounce_filter_update(&filter, false, 20U, &stable);
    zassert_true(changed, "Bypass mode must transition to false immediately");
    zassert_false(stable, "Stable state must be false");
    zassert_false(filter.stable_state, "Filter stable state must be false");
}

ZTEST(digital_input_suite, test_debounce_filter_null_guard)
{
    bool stable = true;
    bool changed = debounce_filter_update(NULL, true, 20U, &stable);
    zassert_false(changed, "Null filter must return false");
    zassert_false(stable, "Null filter must set out_stable to false");
}

/*============================================================================*
 * 2. POLARITY NORMALIZATION & PURE VALIDATORS TESTS
 *============================================================================*/

ZTEST(digital_input_suite, test_polarity_normalization_permutations)
{
    /* 1. Active High + Normally Open (Standard contactor feedback) */
    zassert_true(di_normalize_raw_level(true, DI_ACTIVE_HIGH, DI_CONTACT_NORMALLY_OPEN),
                 "High on Active-High NO must be asserted");
    zassert_false(di_normalize_raw_level(false, DI_ACTIVE_HIGH, DI_CONTACT_NORMALLY_OPEN),
                  "Low on Active-High NO must be deasserted");

    /* 2. Active High + Normally Closed (Safety loop opening asserts alarm) */
    zassert_false(di_normalize_raw_level(true, DI_ACTIVE_HIGH, DI_CONTACT_NORMALLY_CLOSED),
                  "High (closed) on Active-High NC must be normal/deasserted");
    zassert_true(di_normalize_raw_level(false, DI_ACTIVE_HIGH, DI_CONTACT_NORMALLY_CLOSED),
                 "Low (open loop) on Active-High NC must be alarm asserted");

    /* 3. Active Low + Normally Open (GND assert contact) */
    zassert_true(di_normalize_raw_level(false, DI_ACTIVE_LOW, DI_CONTACT_NORMALLY_OPEN),
                 "Low on Active-Low NO must be asserted");
    zassert_false(di_normalize_raw_level(true, DI_ACTIVE_LOW, DI_CONTACT_NORMALLY_OPEN),
                  "High on Active-Low NO must be deasserted");

    /* 4. Active Low + Normally Closed */
    zassert_false(di_normalize_raw_level(false, DI_ACTIVE_LOW, DI_CONTACT_NORMALLY_CLOSED),
                  "Low on Active-Low NC must be normal/deasserted");
    zassert_true(di_normalize_raw_level(true, DI_ACTIVE_LOW, DI_CONTACT_NORMALLY_CLOSED),
                 "High on Active-Low NC must be alarm asserted");
}

ZTEST(digital_input_suite, test_pure_type_validators)
{
    /* di_role_is_valid */
    zassert_true(di_role_is_valid(DI_ROLE_FAN_1_AUX), "Fan 1 aux must be valid");
    zassert_true(di_role_is_valid(DI_ROLE_COMPRESSOR_1_AUX), "Compressor 1 aux must be valid");
    zassert_true(di_role_is_valid(DI_ROLE_HIGH_PRESSURE_1), "High pressure 1 must be valid");
    zassert_true(di_role_is_valid(DI_ROLE_USER_4), "User 4 must be valid");
    zassert_true(di_role_is_valid(DI_ROLE_USER_BUTTON), "User button must be valid");
    zassert_true(di_role_is_valid(DI_ROLE_BYPASS_SWITCH), "Bypass switch must be valid");
    zassert_false(di_role_is_valid(DI_ROLE_MAX), "DI_ROLE_MAX must be invalid");
    zassert_false(di_role_is_valid((di_role_t)(DI_ROLE_MAX + 10)), "Out of range role must be invalid");

    /* di_source_type_is_valid */
    zassert_true(di_source_type_is_valid(DI_SOURCE_INTERNAL_SPI), "SPI source must be valid");
    zassert_true(di_source_type_is_valid(DI_SOURCE_EXTERNAL_MODBUS), "Modbus source must be valid");
    zassert_true(di_source_type_is_valid(DI_SOURCE_ONCHIP_GPIO), "On-chip GPIO source must be valid");
    zassert_true(di_source_type_is_valid(DI_SOURCE_MOCK), "Mock source must be valid");
    zassert_false(di_source_type_is_valid((di_source_type_t)99), "Invalid source must be rejected");

    /* di_active_level_is_valid */
    zassert_true(di_active_level_is_valid(DI_ACTIVE_HIGH), "Active high must be valid");
    zassert_true(di_active_level_is_valid(DI_ACTIVE_LOW), "Active low must be valid");
    zassert_false(di_active_level_is_valid((di_active_level_t)42), "Invalid active level must be rejected");

    /* di_contact_type_is_valid */
    zassert_true(di_contact_type_is_valid(DI_CONTACT_NORMALLY_OPEN), "NO must be valid");
    zassert_true(di_contact_type_is_valid(DI_CONTACT_NORMALLY_CLOSED), "NC must be valid");
    zassert_false(di_contact_type_is_valid((di_contact_type_t)100), "Invalid contact type must be rejected");

    /* di_state_is_valid */
    zassert_true(di_state_is_valid(DI_STATE_INACTIVE), "INACTIVE must be valid");
    zassert_true(di_state_is_valid(DI_STATE_ACTIVE), "ACTIVE must be valid");
    zassert_false(di_state_is_valid((di_state_t)2), "Invalid state must be rejected");
}

/*============================================================================*
 * 3. DOMAIN AGGREGATOR SERVICE TESTS
 *============================================================================*/

typedef struct {
    bool raw_spi_pin_0;
    bool raw_modbus_bit_3;
    bool raw_spi_pin_1;
} test_reader_fixture_t;

static int test_raw_reader_cb(void *ctx,
                              di_source_type_t source,
                              uint8_t dev_id,
                              uint8_t pin_idx,
                              bool *out_raw)
{
    test_reader_fixture_t *fix = (test_reader_fixture_t *)ctx;
    if (fix == NULL || out_raw == NULL) {
        return -EINVAL;
    }

    if (source == DI_SOURCE_INTERNAL_SPI && dev_id == 0U && pin_idx == 0U) {
        *out_raw = fix->raw_spi_pin_0;
        return 0;
    }
    if (source == DI_SOURCE_EXTERNAL_MODBUS && dev_id == 1U && pin_idx == 3U) {
        *out_raw = fix->raw_modbus_bit_3;
        return 0;
    }
    if (source == DI_SOURCE_INTERNAL_SPI && dev_id == 0U && pin_idx == 1U) {
        *out_raw = fix->raw_spi_pin_1;
        return 0;
    }

    return -ENODEV;
}

ZTEST(digital_input_suite, test_di_aggregator_multi_source_and_dual_debounce)
{
    di_aggregator_t aggregator;
    test_reader_fixture_t fix = {
        .raw_spi_pin_0 = false,
        .raw_modbus_bit_3 = false,
        .raw_spi_pin_1 = true, /* NC contact closed = safe/deasserted */
    };

    /* Channel configuration:
     * Ch 0: COMPRESSOR_1_AUX (SPI, debounce 500 ms per Rev F §2.3, ACTIVE_HIGH, NO)
     * Ch 1: FAN_1_AUX (Modbus, debounce 0 ms bypass, ACTIVE_HIGH, NO)
     * Ch 2: HIGH_PRESSURE_1 (SPI, debounce 60 ms, ACTIVE_HIGH, NC)
     */
    di_channel_config_t configs[] = {
        {
            .role = DI_ROLE_COMPRESSOR_1_AUX,
            .source_type = DI_SOURCE_INTERNAL_SPI,
            .source_dev_id = 0U,
            .pin_index = 0U,
            .active_level = DI_ACTIVE_HIGH,
            .contact_type = DI_CONTACT_NORMALLY_OPEN,
            .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, /* 500 ms */
            .enabled = true,
        },
        {
            .role = DI_ROLE_FAN_1_AUX,
            .source_type = DI_SOURCE_EXTERNAL_MODBUS,
            .source_dev_id = 1U,
            .pin_index = 3U,
            .active_level = DI_ACTIVE_HIGH,
            .contact_type = DI_CONTACT_NORMALLY_OPEN,
            .debounce_ms = 0U, /* 0 ms bypass */
            .enabled = true,
        },
        {
            .role = DI_ROLE_HIGH_PRESSURE_1,
            .source_type = DI_SOURCE_INTERNAL_SPI,
            .source_dev_id = 0U,
            .pin_index = 1U,
            .active_level = DI_ACTIVE_HIGH,
            .contact_type = DI_CONTACT_NORMALLY_CLOSED,
            .debounce_ms = DI_DEBOUNCE_ALARM_MS, /* 60 ms */
            .enabled = true,
        },
    };

    int ret = di_aggregator_init(&aggregator, configs, 3U, test_raw_reader_cb, &fix);
    zassert_equal(ret, 0, "di_aggregator_init must succeed");

    /* Initial state verification */
    bool is_active = false;
    ret = di_aggregator_get_state(&aggregator, DI_ROLE_COMPRESSOR_1_AUX, &is_active);
    zassert_equal(ret, 0, "get_state must succeed");
    zassert_false(is_active, "Compressor must start inactive");

    ret = di_aggregator_get_state(&aggregator, DI_ROLE_FAN_1_AUX, &is_active);
    zassert_equal(ret, 0, "get_state must succeed");
    zassert_false(is_active, "Fan must start inactive");

    /* Step 1: Modbus Fan 1 transitions to true on wire, SPI pin 0 has a 40ms noise glitch */
    fix.raw_modbus_bit_3 = true;
    fix.raw_spi_pin_0 = true; /* start of glitch */

    di_transition_event_t events[8];
    size_t event_count = 0;

    /* Scan cycle 1 (elapsed 20 ms) */
    ret = di_aggregator_scan(&aggregator, 20U, events, 8U, &event_count);
    zassert_equal(ret, 0, "Scan 1 must succeed");

    /* Fan 1 (0 ms bypass) MUST transition immediately on cycle 1 */
    ret = di_aggregator_get_state(&aggregator, DI_ROLE_FAN_1_AUX, &is_active);
    zassert_equal(ret, 0, "get_state must succeed");
    zassert_true(is_active, "Fan 1 must be active immediately (0 ms bypass)");

    /* Compressor 1 (500 ms debounce) MUST NOT transition yet */
    ret = di_aggregator_get_state(&aggregator, DI_ROLE_COMPRESSOR_1_AUX, &is_active);
    zassert_equal(ret, 0, "get_state must succeed");
    zassert_false(is_active, "Compressor 1 must remain inactive at 20 ms");

    /* Event count must be 1 (for Fan 1) */
    zassert_equal(event_count, 1U, "Exactly one event expected for Fan 1");
    zassert_equal(events[0].role, DI_ROLE_FAN_1_AUX, "Event role must be Fan 1");
    zassert_equal(events[0].new_state, DI_STATE_ACTIVE, "New state must be active");
    zassert_equal(events[0].prev_state, DI_STATE_INACTIVE, "Prev state must be inactive");

    /* Scan cycle 2: noise glitch on SPI pin 0 ends after 20 ms more (total 40 ms) */
    fix.raw_spi_pin_0 = false;
    ret = di_aggregator_scan(&aggregator, 20U, events, 8U, &event_count);
    zassert_equal(ret, 0, "Scan 2 must succeed");
    zassert_equal(event_count, 0U, "No events expected on Scan 2");

    ret = di_aggregator_get_state(&aggregator, DI_ROLE_COMPRESSOR_1_AUX, &is_active);
    zassert_false(is_active, "Compressor 1 must still be inactive (glitch was rejected)");

    /* Step 2: Now compressor sustains true continuously for 500 ms */
    fix.raw_spi_pin_0 = true;
    /* Edge scan (counter=0) + 24 steady scans (counter reaches 480 ms) */
    for (int i = 0; i < 25; i++) {
        ret = di_aggregator_scan(&aggregator, 20U, events, 8U, &event_count);
        zassert_equal(ret, 0, "Scan must succeed");
        ret = di_aggregator_get_state(&aggregator, DI_ROLE_COMPRESSOR_1_AUX, &is_active);
        zassert_false(is_active, "Compressor must not transition before 500 ms");
    }

    /* 26th scan = 500 ms reached! */
    ret = di_aggregator_scan(&aggregator, 20U, events, 8U, &event_count);
    zassert_equal(ret, 0, "26th scan must succeed");
    ret = di_aggregator_get_state(&aggregator, DI_ROLE_COMPRESSOR_1_AUX, &is_active);
    zassert_true(is_active, "Compressor must be confirmed active at 500 ms");
    zassert_equal(event_count, 1U, "One event expected for compressor transition");
    zassert_equal(events[0].role, DI_ROLE_COMPRESSOR_1_AUX, "Role must be compressor");

    /* Step 3: Active bitmask validation */
    uint64_t mask = 0ULL;
    ret = di_aggregator_get_active_mask(&aggregator, &mask);
    zassert_equal(ret, 0, "get_active_mask must succeed");
    uint64_t expected_mask = (1ULL << DI_ROLE_COMPRESSOR_1_AUX) | (1ULL << DI_ROLE_FAN_1_AUX);
    zassert_equal(mask, expected_mask, "Active mask must reflect both active channels");

    /* Step 4: Active duration accumulation */
    /* Run 50 more scans of 20 ms = 1000 ms elapsed */
    for (int i = 0; i < 50; i++) {
        di_aggregator_scan(&aggregator, 20U, NULL, 0U, NULL);
    }
    di_channel_status_t comp_status;
    ret = di_aggregator_get_status(&aggregator, DI_ROLE_COMPRESSOR_1_AUX, &comp_status);
    zassert_equal(ret, 0, "get_status must succeed");
    zassert_true(comp_status.is_active, "Compressor must be active");
    zassert_equal(comp_status.transition_count, 1U, "Compressor transition count must be 1");
    zassert_true(comp_status.active_duration_s >= 1U, "Active duration must be at least 1 second");

    /* Step 5: Test NC contact alarm assertion (open loop on High Pressure switch) */
    fix.raw_spi_pin_1 = false; /* wire broken / switch opened: edge scan (counter=0) + 3 scans (counter=60) */
    di_aggregator_scan(&aggregator, 20U, NULL, 0U, NULL); /* counter = 0 */
    di_aggregator_scan(&aggregator, 20U, NULL, 0U, NULL); /* counter = 20 */
    di_aggregator_scan(&aggregator, 20U, NULL, 0U, NULL); /* counter = 40 */
    di_aggregator_scan(&aggregator, 20U, events, 8U, &event_count); /* counter = 60 */

    ret = di_aggregator_get_state(&aggregator, DI_ROLE_HIGH_PRESSURE_1, &is_active);
    zassert_equal(ret, 0, "get_state must succeed");
    zassert_true(is_active, "High Pressure 1 alarm must be asserted when NC contact opens");
}

ZTEST(digital_input_suite, test_di_aggregator_error_and_boundary_handling)
{
    di_aggregator_t aggregator;
    di_channel_config_t cfg = {
        .role = DI_ROLE_DOOR_OPEN,
        .source_type = DI_SOURCE_MOCK,
        .debounce_ms = 10U,
        .enabled = true,
    };

    /* Invalid init arguments */
    zassert_equal(di_aggregator_init(NULL, &cfg, 1U, NULL, NULL), -EINVAL, "Null self must fail");
    zassert_equal(di_aggregator_init(&aggregator, NULL, 1U, NULL, NULL), -EINVAL, "Null configs must fail");
    zassert_equal(di_aggregator_init(&aggregator, &cfg, 0U, NULL, NULL), -EINVAL, "Zero count must fail");
    zassert_equal(di_aggregator_init(&aggregator, &cfg, DI_CHANNEL_MAX + 1U, NULL, NULL), -EINVAL, "Over max must fail");

    /* Initialize validly */
    zassert_equal(di_aggregator_init(&aggregator, &cfg, 1U, NULL, NULL), 0, "Init must succeed");

    /* Query non-configured channel */
    bool state = false;
    zassert_equal(di_aggregator_get_state(&aggregator, DI_ROLE_WATER_LEAK, &state), -ENOENT,
                  "Unconfigured role must return -ENOENT");

    /* Query with invalid role */
    zassert_equal(di_aggregator_get_state(&aggregator, DI_ROLE_MAX, &state), -EINVAL,
                  "DI_ROLE_MAX must return -EINVAL");

    /* Query with NULL pointer */
    zassert_equal(di_aggregator_get_state(&aggregator, DI_ROLE_DOOR_OPEN, NULL), -EINVAL,
                  "NULL pointer must return -EINVAL");

    /* Direct raw set and query */
    zassert_equal(di_aggregator_set_raw_by_role(&aggregator, DI_ROLE_DOOR_OPEN, true), 0,
                  "set_raw_by_role must succeed");
    zassert_equal(di_aggregator_set_raw_by_role(&aggregator, DI_ROLE_WATER_LEAK, true), -ENOENT,
                  "set_raw_by_role on unconfigured role must return -ENOENT");

    bool raw_val = false;
    zassert_equal(di_aggregator_get_raw(&aggregator, DI_ROLE_DOOR_OPEN, &raw_val), 0, "get_raw must succeed");
    /* Note: get_raw reflects the last scanned raw_level in status */
    di_aggregator_scan(&aggregator, 20U, NULL, 0U, NULL);
    zassert_equal(di_aggregator_get_raw(&aggregator, DI_ROLE_DOOR_OPEN, &raw_val), 0, "get_raw must succeed");
    zassert_true(raw_val, "Raw value must be true after scan");
}

/*============================================================================*
 * 4. PORT INTERFACE & FAKE DIGITAL INPUT TESTS
 *============================================================================*/

ZTEST(digital_input_suite, test_i_digital_input_fake_adapter_and_wrappers)
{
    i_digital_input_t port = fake_digital_input_port();
    zassert_true(i_digital_input_is_bound(&port), "Fake port must be bound");

    /* Unbound port tests */
    i_digital_input_t unbound_port = { .api = NULL, .impl = NULL };
    bool state = false;
    zassert_false(i_digital_input_is_bound(&unbound_port), "Unbound port must report false");
    zassert_equal(i_digital_input_init(&unbound_port, NULL, 0U), -EINVAL, "Unbound init must return -EINVAL");
    zassert_equal(i_digital_input_scan(&unbound_port, 20U), -EINVAL, "Unbound scan must return -EINVAL");
    zassert_equal(i_digital_input_get_state(&unbound_port, DI_ROLE_FAN_1_AUX, &state), -EINVAL, "Unbound get_state must fail");

    /* Configure via port interface */
    di_channel_config_t configs[] = {
        {
            .role = DI_ROLE_WATER_LEAK,
            .source_type = DI_SOURCE_MOCK,
            .active_level = DI_ACTIVE_HIGH,
            .contact_type = DI_CONTACT_NORMALLY_OPEN,
            .debounce_ms = 40U,
            .enabled = true,
        },
        {
            .role = DI_ROLE_SMOKE_FIRE,
            .source_type = DI_SOURCE_MOCK,
            .active_level = DI_ACTIVE_HIGH,
            .contact_type = DI_CONTACT_NORMALLY_OPEN,
            .debounce_ms = 0U,
            .enabled = true,
        },
    };

    int ret = i_digital_input_init(&port, configs, 2U);
    zassert_equal(ret, 0, "i_digital_input_init must succeed");

    const fake_digital_input_state_t *fake_st = fake_digital_input_get_state();
    zassert_equal(fake_st->init_calls, 1U, "init_calls must be 1");

    /* Check initial states */
    ret = i_digital_input_get_state(&port, DI_ROLE_WATER_LEAK, &state);
    zassert_equal(ret, 0, "get_state must succeed");
    zassert_false(state, "Initial state must be false");

    /* Force state in fake directly */
    ret = fake_digital_input_force_state(DI_ROLE_SMOKE_FIRE, true);
    zassert_equal(ret, 0, "force_state must succeed");

    ret = i_digital_input_get_state(&port, DI_ROLE_SMOKE_FIRE, &state);
    zassert_equal(ret, 0, "get_state must succeed");
    zassert_true(state, "Smoke Fire state must be true after force");

    /* Test raw level manipulation and simulated step */
    fake_digital_input_set_raw_level(DI_ROLE_WATER_LEAK, true);
    i_digital_input_scan(&port, 20U); /* edge: counter = 0 */
    i_digital_input_get_state(&port, DI_ROLE_WATER_LEAK, &state);
    zassert_false(state, "Water leak must not be active at edge");

    i_digital_input_scan(&port, 20U); /* counter = 20 ms */
    i_digital_input_get_state(&port, DI_ROLE_WATER_LEAK, &state);
    zassert_false(state, "Water leak must not be active at 20 ms (debounce is 40 ms)");

    i_digital_input_scan(&port, 20U); /* counter = 40 ms */
    i_digital_input_get_state(&port, DI_ROLE_WATER_LEAK, &state);
    zassert_true(state, "Water leak must be active at 40 ms");

    /* Query active mask */
    uint64_t mask = 0ULL;
    ret = i_digital_input_get_active_mask(&port, &mask);
    zassert_equal(ret, 0, "get_active_mask must succeed");
    uint64_t expected = (1ULL << DI_ROLE_WATER_LEAK) | (1ULL << DI_ROLE_SMOKE_FIRE);
    zassert_equal(mask, expected, "Active mask must match both active roles");

    /* Error injection test */
    fake_digital_input_set_errors(-EIO, 0, 0);
    zassert_equal(i_digital_input_init(&port, configs, 2U), -EIO, "Injected -EIO must propagate");
}

ZTEST(digital_input_suite, test_aggregator_port_export)
{
    di_aggregator_t aggregator;
    di_channel_config_t cfg = {
        .role = DI_ROLE_DOOR_OPEN,
        .source_type = DI_SOURCE_MOCK,
        .debounce_ms = 0U,
        .enabled = true,
    };
    di_aggregator_init(&aggregator, &cfg, 1U, NULL, NULL);

    i_digital_input_t port = di_aggregator_port(&aggregator);
    zassert_true(i_digital_input_is_bound(&port), "Aggregator port must be bound");

    bool state = false;
    int ret = i_digital_input_get_state(&port, DI_ROLE_DOOR_OPEN, &state);
    zassert_equal(ret, 0, "Port get_state must succeed");
    zassert_false(state, "Door state must initially be false");

    di_aggregator_set_raw_by_role(&aggregator, DI_ROLE_DOOR_OPEN, true);
    ret = i_digital_input_scan(&port, 20U);
    zassert_equal(ret, 0, "Port scan must succeed");

    ret = i_digital_input_get_state(&port, DI_ROLE_DOOR_OPEN, &state);
    zassert_equal(ret, 0, "Port get_state must succeed");
    zassert_true(state, "Door state must be true after scan with 0 ms debounce");
}

/*============================================================================*
 * 5. ZBUS CHANNEL NOTIFICATION TESTS
 *============================================================================*/

ZTEST(digital_input_suite, test_zbus_digital_input_channel_publish_and_read)
{
    di_transition_event_t tx_event = {
        .role = DI_ROLE_SMOKE_FIRE,
        .new_state = DI_STATE_ACTIVE,
        .prev_state = DI_STATE_INACTIVE,
        .timestamp_ms = 12345ULL,
    };

    int ret = zbus_chan_pub(&chan_digital_input_state, &tx_event, K_MSEC(50));
    zassert_equal(ret, 0, "zbus_chan_pub to chan_digital_input_state must succeed");

    di_transition_event_t rx_event;
    memset(&rx_event, 0, sizeof(rx_event));
    ret = zbus_chan_read(&chan_digital_input_state, &rx_event, K_MSEC(50));
    zassert_equal(ret, 0, "zbus_chan_read from chan_digital_input_state must succeed");

    zassert_equal(rx_event.role, DI_ROLE_SMOKE_FIRE, "Received role must match");
    zassert_equal(rx_event.new_state, DI_STATE_ACTIVE, "Received new_state must match");
    zassert_equal(rx_event.prev_state, DI_STATE_INACTIVE, "Received prev_state must match");
    zassert_equal(rx_event.timestamp_ms, 12345ULL, "Received timestamp must match");
}

/*============================================================================*
 * 6. ACTIVE OBJECT DIGITAL INPUT COORDINATOR TESTS
 *============================================================================*/

ZTEST(digital_input_suite, test_ao_digital_input_init_and_validation)
{
    di_aggregator_t agg;

    /* NULL pointers and invalid params check */
    zassert_equal(ao_digital_input_init(NULL, &agg, &s_test_ao_di_msgq, s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE),
                  -EINVAL, "NULL self must fail");
    zassert_equal(ao_digital_input_init(&s_test_ao_di, NULL, &s_test_ao_di_msgq, s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE),
                  -EINVAL, "NULL aggregator must fail");
    zassert_equal(ao_digital_input_init(&s_test_ao_di, &agg, NULL, s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE),
                  -EINVAL, "NULL queue must fail");
    zassert_equal(ao_digital_input_init(&s_test_ao_di, &agg, &s_test_ao_di_msgq, NULL, AO_DIGITAL_INPUT_STACK_SIZE),
                  -EINVAL, "NULL stack must fail");
    zassert_equal(ao_digital_input_init(&s_test_ao_di, &agg, &s_test_ao_di_msgq, s_test_ao_di_stack, 0U),
                  -EINVAL, "Zero stack size must fail");

    /* Valid initialization */
    int ret = ao_digital_input_init(&s_test_ao_di, &agg, &s_test_ao_di_msgq, s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE);
    zassert_equal(ret, 0, "ao_digital_input_init must succeed");
    zassert_false(ao_digital_input_is_running(&s_test_ao_di), "AO must not be running initially");
    zassert_equal(ao_digital_input_scan_now(&s_test_ao_di), -EAGAIN, "scan_now before start must return -EAGAIN");

    zassert_equal(ao_digital_input_deinit(&s_test_ao_di), 0, "deinit must succeed");
}

ZTEST(digital_input_suite, test_ao_digital_input_step_and_zbus_emission)
{
    di_channel_config_t configs[2] = {
        {
            .role = DI_ROLE_USER_BUTTON,
            .source_type = DI_SOURCE_ONCHIP_GPIO,
            .active_level = DI_ACTIVE_HIGH,
            .contact_type = DI_CONTACT_NORMALLY_OPEN,
            .debounce_ms = 0U,
            .enabled = true,
        },
        {
            .role = DI_ROLE_BYPASS_SWITCH,
            .source_type = DI_SOURCE_ONCHIP_GPIO,
            .active_level = DI_ACTIVE_HIGH,
            .contact_type = DI_CONTACT_NORMALLY_OPEN,
            .debounce_ms = 40U,
            .enabled = true,
        },
    };

    di_aggregator_t agg;
    int ret = di_aggregator_init(&agg, configs, 2U, NULL, NULL);
    zassert_equal(ret, 0, "di_aggregator_init must succeed");

    ret = ao_digital_input_init(&s_test_ao_di, &agg, &s_test_ao_di_msgq, s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE);
    zassert_equal(ret, 0, "ao_digital_input_init must succeed");

    /* First step with no changes: 0 transitions */
    int trans_count = ao_digital_input_step(&s_test_ao_di, 20U);
    zassert_equal(trans_count, 0, "No transitions expected on initial quiescent step");

    /* Trigger USER_BUTTON (0 ms debounce -> immediate transition) */
    di_aggregator_set_raw_by_role(&agg, DI_ROLE_USER_BUTTON, true);
    trans_count = ao_digital_input_step(&s_test_ao_di, 20U);
    zassert_equal(trans_count, 1, "Exactly 1 transition expected for USER_BUTTON");

    /* Verify Zbus emission */
    di_transition_event_t evt;
    memset(&evt, 0, sizeof(evt));
    ret = zbus_chan_read(&chan_digital_input_state, &evt, K_MSEC(50));
    zassert_equal(ret, 0, "zbus_chan_read must succeed");
    zassert_equal(evt.role, DI_ROLE_USER_BUTTON, "Emitted event role must be DI_ROLE_USER_BUTTON");
    zassert_equal(evt.new_state, DI_STATE_ACTIVE, "New state must be ACTIVE");
    zassert_equal(evt.prev_state, DI_STATE_INACTIVE, "Prev state must be INACTIVE");

    /* Trigger BYPASS_SWITCH (40 ms debounce -> needs two 20ms steps after edge) */
    di_aggregator_set_raw_by_role(&agg, DI_ROLE_BYPASS_SWITCH, true);
    trans_count = ao_digital_input_step(&s_test_ao_di, 20U); /* counter = 0 */
    zassert_equal(trans_count, 0, "No transition at edge (0 ms accumulator)");

    trans_count = ao_digital_input_step(&s_test_ao_di, 20U); /* counter = 20 ms < 40 ms */
    zassert_equal(trans_count, 0, "No transition at 20 ms accumulator");

    trans_count = ao_digital_input_step(&s_test_ao_di, 20U); /* counter = 40 ms >= 40 ms */
    zassert_equal(trans_count, 1, "Transition expected when reaching 40 ms threshold");

    memset(&evt, 0, sizeof(evt));
    ret = zbus_chan_read(&chan_digital_input_state, &evt, K_MSEC(50));
    zassert_equal(ret, 0, "zbus_chan_read must succeed");
    zassert_equal(evt.role, DI_ROLE_BYPASS_SWITCH, "Emitted event role must be DI_ROLE_BYPASS_SWITCH");
    zassert_equal(evt.new_state, DI_STATE_ACTIVE, "New state must be ACTIVE");
    zassert_equal(evt.prev_state, DI_STATE_INACTIVE, "Prev state must be INACTIVE");

    zassert_equal(ao_digital_input_deinit(&s_test_ao_di), 0, "deinit must succeed");
}

ZTEST(digital_input_suite, test_ao_digital_input_lifecycle_and_scan_now)
{
    di_channel_config_t config = {
        .role = DI_ROLE_USER_BUTTON,
        .source_type = DI_SOURCE_ONCHIP_GPIO,
        .active_level = DI_ACTIVE_HIGH,
        .contact_type = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms = 0U,
        .enabled = true,
    };

    di_aggregator_t agg;
    int ret = di_aggregator_init(&agg, &config, 1U, NULL, NULL);
    zassert_equal(ret, 0, "di_aggregator_init must succeed");

    ret = ao_digital_input_init(&s_test_ao_di, &agg, &s_test_ao_di_msgq, s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE);
    zassert_equal(ret, 0, "ao_digital_input_init must succeed");

    /* Start Active Object thread */
    ret = ao_digital_input_start(&s_test_ao_di);
    zassert_equal(ret, 0, "ao_digital_input_start must succeed");
    zassert_true(ao_digital_input_is_running(&s_test_ao_di), "AO must be running");

    /* Start again must return -EALREADY */
    zassert_equal(ao_digital_input_start(&s_test_ao_di), -EALREADY, "Double start must return -EALREADY");

    /* Change raw pin level and command immediate scan */
    di_aggregator_set_raw_by_role(&agg, DI_ROLE_USER_BUTTON, true);
    ret = ao_digital_input_scan_now(&s_test_ao_di);
    zassert_equal(ret, 0, "ao_digital_input_scan_now must succeed");

    /* Give the AO thread a few milliseconds to process the queued scan */
    k_msleep(50);

    /* Read event emitted to Zbus */
    di_transition_event_t evt;
    memset(&evt, 0, sizeof(evt));
    ret = zbus_chan_read(&chan_digital_input_state, &evt, K_MSEC(100));
    zassert_equal(ret, 0, "zbus_chan_read must succeed after scan_now");
    zassert_equal(evt.role, DI_ROLE_USER_BUTTON, "Role must be USER_BUTTON");
    zassert_equal(evt.new_state, DI_STATE_ACTIVE, "State must be ACTIVE");

    /* Graceful stop */
    ret = ao_digital_input_stop(&s_test_ao_di);
    zassert_equal(ret, 0, "ao_digital_input_stop must succeed");
    zassert_equal(ao_digital_input_wait_stopped(&s_test_ao_di, K_MSEC(500)), 0, "wait_stopped must succeed");
    zassert_false(ao_digital_input_is_running(&s_test_ao_di), "AO must no longer be running");

    /* Stop again must be safe and idempotent */
    ret = ao_digital_input_stop(&s_test_ao_di);
    zassert_equal(ret, 0, "Repeated ao_digital_input_stop must return 0");

    /* Deinit */
    zassert_equal(ao_digital_input_deinit(&s_test_ao_di), 0, "ao_digital_input_deinit must succeed");
}

/*============================================================================*
 * 7. ZBUS SNAPSHOT & HVAC FEED INTEGRATION TESTS
 *============================================================================*/

ZTEST(digital_input_suite, test_di_aggregator_pack_snapshot)
{
    di_aggregator_t agg;
    di_snapshot_msg_t snap;

    zassert_equal(di_aggregator_pack_snapshot(NULL, &snap), -EINVAL);
    zassert_equal(di_aggregator_pack_snapshot(&agg, NULL), -EINVAL);

    di_channel_config_t cfgs[2] = {
        {
            .role = DI_ROLE_SMOKE_FIRE,
            .source_type = DI_SOURCE_INTERNAL_SPI,
            .active_level = DI_ACTIVE_LOW,
            .contact_type = DI_CONTACT_NORMALLY_CLOSED,
            .debounce_ms = 0U,
            .enabled = true,
        },
        {
            .role = DI_ROLE_UNIT_0_ALARM_HP,
            .source_type = DI_SOURCE_INTERNAL_SPI,
            .active_level = DI_ACTIVE_LOW,
            .contact_type = DI_CONTACT_NORMALLY_OPEN,
            .debounce_ms = 0U,
            .enabled = true,
        }
    };

    zassert_ok(di_aggregator_init(&agg, cfgs, 2U, NULL, NULL));
    /* Inject raw active levels */
    di_aggregator_set_raw_by_index(&agg, 0U, true); /* NC + Active Low -> true = open/alarm */
    di_aggregator_set_raw_by_index(&agg, 1U, false); /* NO + Active Low -> false = closed/alarm */

    size_t evts = 0;
    di_transition_event_t evt_buf[4];
    zassert_ok(di_aggregator_scan(&agg, 100U, evt_buf, 4U, &evts));

    zassert_ok(di_aggregator_pack_snapshot(&agg, &snap));
    zassert_equal(snap.channel_count, 2U);
    zassert_equal(snap.roles[0], DI_ROLE_SMOKE_FIRE);
    zassert_equal(snap.roles[1], DI_ROLE_UNIT_0_ALARM_HP);
    zassert_true(snap.is_active[0]);
    zassert_true(snap.is_active[1]);
    zassert_equal(snap.active_mask, 0x03ULL, "Bits 0 and 1 must be set in active_mask");
    zassert_equal(snap.raw_mask & (1ULL << 0), (1ULL << 0), "Bit 0 raw level set");
}

ZTEST(digital_input_suite, test_ao_digital_input_publishes_snapshot_to_zbus)
{
    di_channel_config_t config = {
        .role = DI_ROLE_SMOKE_FIRE,
        .source_type = DI_SOURCE_ONCHIP_GPIO,
        .active_level = DI_ACTIVE_HIGH,
        .contact_type = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms = 0U,
        .enabled = true,
    };

    di_aggregator_t agg;
    zassert_ok(di_aggregator_init(&agg, &config, 1U, NULL, NULL));
    zassert_ok(ao_digital_input_init(&s_test_ao_di, &agg, &s_test_ao_di_msgq, s_test_ao_di_stack, AO_DIGITAL_INPUT_STACK_SIZE));

    di_aggregator_set_raw_by_index(&agg, 0U, true);

    int count = ao_digital_input_step(&s_test_ao_di, 20U);
    zassert_equal(count, 1, "One transition expected");

    /* Verify listener was notified via chan_digital_input_snapshot */
    zassert_true(s_test_hvac_di_feed_calls >= 1U, "lis_hvac_di_feed must be called");
    zassert_equal(s_test_last_di_snapshot.channel_count, 1U);
    zassert_equal(s_test_last_di_snapshot.roles[0], DI_ROLE_SMOKE_FIRE);
    zassert_true(s_test_last_di_snapshot.is_active[0]);
    zassert_equal(s_test_last_di_snapshot.active_mask, 1ULL);

    /* Verify chan_digital_input_snapshot readable via zbus_chan_read */
    di_snapshot_msg_t read_snap;
    zassert_ok(zbus_chan_read(&chan_digital_input_snapshot, &read_snap, K_NO_WAIT));
    zassert_true(read_snap.is_active[0]);

    zassert_ok(ao_digital_input_deinit(&s_test_ao_di));
}

ZTEST(digital_input_suite, test_system_di_profile_58_channels)
{
    size_t count = 0U;
    const di_channel_config_t *profile = default_config_get_di_profile(HVAC_MODE_HYBRID_FULL, &count);
    zassert_not_null(profile, "Profile must not be NULL");
    zassert_equal(count, HARDWARE_SYSTEM_DI_COUNT, "Total channels must be 58");

    /* Verify On-Chip channels */
    zassert_equal(profile[24].role, DI_ROLE_USER_BUTTON, "CH24 must be USER_BUTTON");
    zassert_equal(profile[24].source_type, DI_SOURCE_ONCHIP_GPIO, "CH24 source must be ONCHIP");
    zassert_equal(profile[24].pin_index, (uint8_t)ONCHIP_DI_USER_BUTTON, "CH24 pin must be ONCHIP_DI_USER_BUTTON");

    zassert_equal(profile[25].role, DI_ROLE_BYPASS_SWITCH, "CH25 must be BYPASS_SWITCH");
    zassert_equal(profile[25].source_type, DI_SOURCE_ONCHIP_GPIO, "CH25 source must be ONCHIP");
    zassert_equal(profile[25].pin_index, (uint8_t)ONCHIP_DI_SW_BYPASS, "CH25 pin must be ONCHIP_DI_SW_BYPASS");

    /* Verify Remote Unit 4..7 alarm channels */
    zassert_equal(profile[26].role, DI_ROLE_UNIT_4_ALARM_HP, "CH26 must be Unit 4 HP");
    zassert_equal(profile[26].source_type, DI_SOURCE_EXTERNAL_MODBUS, "CH26 source must be Modbus");
    zassert_equal(profile[27].role, DI_ROLE_UNIT_4_ALARM_LP, "CH27 must be Unit 4 LP");
    zassert_equal(profile[28].role, DI_ROLE_UNIT_4_ALARM_FLOW, "CH28 must be Unit 4 Flow");
    zassert_equal(profile[29].role, DI_ROLE_UNIT_4_ALARM_VOLT, "CH29 must be Unit 4 Volt");

    zassert_equal(profile[38].role, DI_ROLE_UNIT_7_ALARM_HP, "CH38 must be Unit 7 HP");
    zassert_equal(profile[39].role, DI_ROLE_UNIT_7_ALARM_LP, "CH39 must be Unit 7 LP");
    zassert_equal(profile[40].role, DI_ROLE_UNIT_7_ALARM_FLOW, "CH40 must be Unit 7 Flow");
    zassert_equal(profile[41].role, DI_ROLE_UNIT_7_ALARM_VOLT, "CH41 must be Unit 7 Volt");

    /* Initialize aggregator with the 58-channel profile */
    di_aggregator_t agg;
    int err = di_aggregator_init(&agg, profile, count, NULL, NULL);
    zassert_equal(err, 0, "Aggregator init with 58 channels must succeed");
    zassert_equal(agg.channel_count, 58U, "Aggregator channel count must be 58");

    /* Check packing snapshot */
    di_snapshot_msg_t snapshot;
    err = di_aggregator_pack_snapshot(&agg, &snapshot);
    zassert_equal(err, 0, "Snapshot pack must succeed");
    zassert_equal(snapshot.channel_count, 58U, "Snapshot channel count must be 58");
    zassert_equal(snapshot.roles[25], DI_ROLE_BYPASS_SWITCH, "Snapshot CH25 must be BYPASS_SWITCH");
    zassert_equal(snapshot.roles[26], DI_ROLE_UNIT_4_ALARM_HP, "Snapshot CH26 must be Unit 4 HP");

    /* Test raw level to active state for bypass switch and Unit 4 HP */
    /* Bypass switch is ACTIVE_LOW: raw 0 => active true */
    di_aggregator_set_raw_by_role(&agg, DI_ROLE_BYPASS_SWITCH, false);
    /* Debounce is 60ms */
    for (int t = 0; t < 4; t++) {
        di_aggregator_scan(&agg, 20U, NULL, 0, NULL);
    }
    bool bypass_active = false;
    zassert_ok(di_aggregator_get_state(&agg, DI_ROLE_BYPASS_SWITCH, &bypass_active));
    zassert_true(bypass_active, "Bypass switch must be active when pin pulled low");

    /* Unit 4 HP is ACTIVE_HIGH: raw 1 => active true (debounce 0ms) */
    di_aggregator_set_raw_by_role(&agg, DI_ROLE_UNIT_4_ALARM_HP, true);
    di_aggregator_scan(&agg, 20U, NULL, 0, NULL);
    bool u4_hp_active = false;
    zassert_ok(di_aggregator_get_state(&agg, DI_ROLE_UNIT_4_ALARM_HP, &u4_hp_active));
    zassert_true(u4_hp_active, "Unit 4 HP must be active");
}
