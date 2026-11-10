/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_alarm_domain.c
 * @brief Unit tests for Pure Domain Alarm Entities, Aggregates, Evaluators, and Registry.
 */

#include <zephyr/ztest.h>
#include "common/alarm_types.h"
#include "domain/alarm/alarm_entity.h"
#include "domain/alarm/alarm_aggregate.h"
#include "domain/alarm/alarm_evaluator.h"
#include "domain/alarm/alarm_registry.h"

/*============================================================================*
 * TEST SUITE: ALARM ENTITY (DISCRETE FSM)
 *============================================================================*/

ZTEST(alarm_domain_suite, test_alarm_entity_lifecycle_and_single_shot)
{
    alarm_entity_t entity;
    alarm_transition_event_t evt;
    bool changed;

    alarm_entity_init(&entity, ALARM_ID_AC1_HIGH_PRESSURE, ALARM_SOURCE_DIGITAL_INPUT,
                      ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH, false, false);

    zassert_equal(entity.current_state, ALARM_STATE_INACTIVE, "Must start INACTIVE");
    zassert_false(alarm_entity_is_active(&entity), "Must not be active initially");

    /* 1. Rising Edge: Condition becomes true -> ON_PRESS (ACTIVATED) */
    changed = alarm_entity_update_condition(&entity, true, 450.0f, 1000, &evt);
    zassert_true(changed, "Must report state change on rising edge");
    zassert_equal(entity.current_state, ALARM_STATE_ACTIVE, "State must be ACTIVE");
    zassert_equal(evt.event_type, ALARM_EVENT_ACTIVATED, "Event must be ACTIVATED");
    zassert_equal(evt.alarm_id, ALARM_ID_AC1_HIGH_PRESSURE, "Alarm ID match");
    zassert_equal(evt.previous_state, ALARM_STATE_INACTIVE, "Previous state match");
    zassert_equal(evt.new_state, ALARM_STATE_ACTIVE, "New state match");
    zassert_equal(entity.trigger_count, 1, "Trigger count must be 1");
    zassert_equal(entity.triggered_timestamp_ms, 1000, "Timestamp match");

    /* 2. Persistent condition: Remains true -> No duplicate event */
    changed = alarm_entity_update_condition(&entity, true, 455.0f, 1100, &evt);
    zassert_false(changed, "Must suppress duplicate activation events");
    zassert_equal(entity.current_state, ALARM_STATE_ACTIVE, "State remains ACTIVE");
    zassert_equal(entity.trigger_count, 1, "Trigger count unchanged");

    /* 3. Falling Edge: Condition becomes false -> ON_RELEASE (CLEARED) */
    changed = alarm_entity_update_condition(&entity, false, 300.0f, 2500, &evt);
    zassert_true(changed, "Must report state change on falling edge");
    zassert_equal(entity.current_state, ALARM_STATE_INACTIVE, "State must return to INACTIVE");
    zassert_equal(evt.event_type, ALARM_EVENT_CLEARED, "Event must be CLEARED");
    zassert_equal(entity.cleared_timestamp_ms, 2500, "Cleared timestamp recorded");

    /* 4. Persistent quiescent: Remains false -> No duplicate event */
    changed = alarm_entity_update_condition(&entity, false, 0.0f, 3000, &evt);
    zassert_false(changed, "Must suppress duplicate clear events");
}

ZTEST(alarm_domain_suite, test_alarm_entity_latch_and_ack)
{
    alarm_entity_t entity;
    alarm_transition_event_t evt;
    bool changed;

    /* Entity with latch enabled and ack supported */
    alarm_entity_init(&entity, ALARM_ID_FACILITY_FIRE, ALARM_SOURCE_DIGITAL_INPUT,
                      ALARM_SEVERITY_FATAL, ALARM_PRIORITY_EMERGENCY, true, true);

    /* Activate */
    changed = alarm_entity_update_condition(&entity, true, 1.0f, 1000, &evt);
    zassert_true(changed, "Must activate");
    zassert_equal(entity.current_state, ALARM_STATE_ACTIVE, "ACTIVE");

    /* Operator Acknowledges while active */
    changed = alarm_entity_acknowledge(&entity, 1200, &evt);
    zassert_true(changed, "Must acknowledge");
    zassert_equal(entity.current_state, ALARM_STATE_ACKNOWLEDGED, "Must transition to ACKNOWLEDGED");
    zassert_equal(evt.event_type, ALARM_EVENT_ACKNOWLEDGED, "Event match");
    zassert_true(alarm_entity_is_active(&entity), "Acknowledged alarm is still active");

    /* Physical condition clears -> Because latch is enabled, transitions to LATCHED */
    changed = alarm_entity_update_condition(&entity, false, 0.0f, 2000, &evt);
    zassert_true(changed, "Must transition to latched");
    zassert_equal(entity.current_state, ALARM_STATE_LATCHED, "Must be LATCHED");
    zassert_equal(evt.event_type, ALARM_EVENT_LATCHED, "Event LATCHED");
    zassert_true(alarm_entity_is_active(&entity), "Latched alarm is still active");

    /* Operator Resets Latch */
    changed = alarm_entity_reset_latch(&entity, 2500, &evt);
    zassert_true(changed, "Must reset latch");
    zassert_equal(entity.current_state, ALARM_STATE_INACTIVE, "Returns to INACTIVE");
    zassert_equal(evt.event_type, ALARM_EVENT_CLEARED, "Event CLEARED");
    zassert_false(alarm_entity_is_active(&entity), "Inactive now");
}

/*============================================================================*
 * TEST SUITE: ALARM AGGREGATE (MULTI-SOURCE)
 *============================================================================*/

ZTEST(alarm_domain_suite, test_alarm_aggregate_or_activation_and_recovery)
{
    alarm_aggregate_entity_t agg;
    alarm_transition_event_t evt;
    bool changed;

    /* Group with 4 assigned temperature probes (probes 0, 1, 2, 3 -> mask 0x0F) */
    alarm_aggregate_init(&agg, ALARM_ID_GROUP1_TEMP_LOW, ALARM_SOURCE_SENSOR_ANALOG,
                         ALARM_SEVERITY_WARNING, ALARM_PRIORITY_MEDIUM, 0x0F, false);

    zassert_equal(agg.current_state, ALARM_STATE_INACTIVE, "Initial INACTIVE");
    zassert_equal(alarm_aggregate_get_active_source_count(&agg), 0, "0 sources initially");

    /* Step 1: Probe 1 drops below threshold -> First source trips aggregate */
    changed = alarm_aggregate_update_source(&agg, 1, true, 14.5f, 1000, &evt);
    zassert_true(changed, "First source must trip aggregate");
    zassert_equal(agg.current_state, ALARM_STATE_ACTIVE, "State ACTIVE");
    zassert_equal(evt.event_type, ALARM_EVENT_ACTIVATED, "ON_PRESS event");
    zassert_equal(agg.contributing_sources_mask, (1U << 1), "Source 1 set in mask");
    zassert_equal(alarm_aggregate_get_active_source_count(&agg), 1, "1 source active");

    /* Step 2: Probe 3 also drops below threshold -> Joins active aggregate */
    changed = alarm_aggregate_update_source(&agg, 3, true, 13.8f, 1200, &evt);
    zassert_true(changed, "Mask change must report event");
    zassert_equal(agg.current_state, ALARM_STATE_ACTIVE, "Remains ACTIVE");
    zassert_equal(evt.event_type, ALARM_EVENT_SOURCE_CHANGED, "Source changed event, NOT duplicate activated");
    zassert_equal(agg.contributing_sources_mask, ((1U << 1) | (1U << 3)), "Sources 1 and 3 in mask");
    zassert_equal(alarm_aggregate_get_active_source_count(&agg), 2, "2 sources active");

    /* Step 3: Probe 1 recovers -> Probe 3 still active -> NO artificial clearance! */
    changed = alarm_aggregate_update_source(&agg, 1, false, 16.5f, 1500, &evt);
    zassert_true(changed, "Mask change must report event");
    zassert_equal(agg.current_state, ALARM_STATE_ACTIVE, "Must REMAIN ACTIVE!");
    zassert_equal(evt.event_type, ALARM_EVENT_SOURCE_CHANGED, "Source changed event, NOT cleared");
    zassert_equal(agg.contributing_sources_mask, (1U << 3), "Only source 3 in mask");
    zassert_equal(alarm_aggregate_get_active_source_count(&agg), 1, "1 source active");

    /* Step 4: Probe 3 also recovers -> ALL sources clear -> Emits single CLEARED event */
    changed = alarm_aggregate_update_source(&agg, 3, false, 16.8f, 2000, &evt);
    zassert_true(changed, "Final source recovery must trigger transition");
    zassert_equal(agg.current_state, ALARM_STATE_INACTIVE, "Transitions to INACTIVE");
    zassert_equal(evt.event_type, ALARM_EVENT_CLEARED, "ON_RELEASE event");
    zassert_equal(agg.contributing_sources_mask, 0U, "Mask is 0");
    zassert_equal(alarm_aggregate_get_active_source_count(&agg), 0, "0 sources active");
}

/*============================================================================*
 * TEST SUITE: ALARM EVALUATORS (HYSTERESIS ALGORITHMS)
 *============================================================================*/

ZTEST(alarm_domain_suite, test_alarm_evaluator_temp_hysteresis)
{
    /* Low Temperature: Trip < 15.0C; Clear >= 15.0 + 1.0 = 16.0C */
    alarm_eval_temp_low_t low_eval;
    alarm_eval_temp_low_init(&low_eval, 15.0f, 1.0f);

    zassert_false(alarm_eval_temp_low_update(&low_eval, 22.0f), "22C -> Normal");
    zassert_false(alarm_eval_temp_low_update(&low_eval, 15.1f), "15.1C -> Normal");
    zassert_true(alarm_eval_temp_low_update(&low_eval, 14.9f), "14.9C -> Tripped!");
    /* Within hysteresis band: 15.5C is > 15.0C but < 16.0C -> MUST REMAIN IN ALARM! */
    zassert_true(alarm_eval_temp_low_update(&low_eval, 15.5f), "15.5C -> Must remain alarmed (hysteresis)");
    zassert_true(alarm_eval_temp_low_update(&low_eval, 15.9f), "15.9C -> Still alarmed");
    /* Recovery: >= 16.0C -> Clears */
    zassert_false(alarm_eval_temp_low_update(&low_eval, 16.0f), "16.0C -> Cleared!");

    /* High Temperature: Trip > 28.0C; Clear <= 28.0 - 1.0 = 27.0C */
    alarm_eval_temp_high_t high_eval;
    alarm_eval_temp_high_init(&high_eval, 28.0f, 1.0f);

    zassert_false(alarm_eval_temp_high_update(&high_eval, 24.0f), "24C -> Normal");
    zassert_false(alarm_eval_temp_high_update(&high_eval, 27.9f), "27.9C -> Normal");
    zassert_true(alarm_eval_temp_high_update(&high_eval, 28.2f), "28.2C -> Tripped!");
    /* Within hysteresis band: 27.5C is < 28.0C but > 27.0C -> MUST REMAIN IN ALARM! */
    zassert_true(alarm_eval_temp_high_update(&high_eval, 27.5f), "27.5C -> Must remain alarmed (hysteresis)");
    /* Recovery: <= 27.0C -> Clears */
    zassert_false(alarm_eval_temp_high_update(&high_eval, 27.0f), "27.0C -> Cleared!");
}

ZTEST(alarm_domain_suite, test_alarm_evaluator_voltage_3phase_hysteresis)
{
    /* Under-Voltage: Nominal 220V, dev 8% -> Trip < 202.4V; Clear >= 204.4V (hyst 2.0V) */
    alarm_eval_voltage_low_t v_low;
    alarm_eval_voltage_low_init(&v_low, 220.0f, 8.0f, 2.0f);

    float norm_v[3] = {220.0f, 221.0f, 219.0f};
    zassert_false(alarm_eval_voltage_low_update(&v_low, norm_v, 3), "Nominal -> Normal");

    /* Phase A drops to 200V (< 202.4V) */
    float trip_v[3] = {200.0f, 221.0f, 219.0f};
    zassert_true(alarm_eval_voltage_low_update(&v_low, trip_v, 3), "Phase A 200V -> Tripped!");

    /* Phase A recovers to 203.0V (within hysteresis band < 204.4V) */
    float hyst_v[3] = {203.0f, 221.0f, 219.0f};
    zassert_true(alarm_eval_voltage_low_update(&v_low, hyst_v, 3), "Phase A 203V -> Remains alarmed");

    /* Phase A recovers to 205.0V (>= 204.4V) */
    float clear_v[3] = {205.0f, 221.0f, 219.0f};
    zassert_false(alarm_eval_voltage_low_update(&v_low, clear_v, 3), "Phase A 205V -> Cleared!");

    /* Over-Voltage: Nominal 220V, dev 8% -> Trip > 237.6V; Clear <= 235.6V (hyst 2.0V) */
    alarm_eval_voltage_high_t v_high;
    alarm_eval_voltage_high_init(&v_high, 220.0f, 8.0f, 2.0f);

    zassert_false(alarm_eval_voltage_high_update(&v_high, norm_v, 3), "Nominal -> Normal");

    /* Phase B surges to 240V (> 237.6V) */
    float surge_v[3] = {220.0f, 240.0f, 219.0f};
    zassert_true(alarm_eval_voltage_high_update(&v_high, surge_v, 3), "Phase B 240V -> Tripped!");

    /* Phase B drops to 236.5V (> 235.6V -> Hysteresis) */
    float surge_hyst_v[3] = {220.0f, 236.5f, 219.0f};
    zassert_true(alarm_eval_voltage_high_update(&v_high, surge_hyst_v, 3), "Phase B 236.5V -> Remains alarmed");

    /* Phase B drops to 235.0V (<= 235.6V) */
    float surge_clear_v[3] = {220.0f, 235.0f, 219.0f};
    zassert_false(alarm_eval_voltage_high_update(&v_high, surge_clear_v, 3), "Phase B 235.0V -> Cleared!");
}

/*============================================================================*
 * TEST SUITE: ALARM REGISTRY (CATALOG & EXTENSIBILITY)
 *============================================================================*/

ZTEST(alarm_domain_suite, test_alarm_registry_catalog_and_extension)
{
    alarm_registry_t reg;
    alarm_descriptor_t desc;
    int err;

    alarm_registry_init(&reg);

    /* Verify default catalog populated */
    size_t count = alarm_registry_get_count(&reg);
    zassert_true(count >= 30, "Default catalog must have at least 30 alarms (got %zu)", count);

    /* Retrieve well-known descriptor */
    err = alarm_registry_get(&reg, ALARM_ID_AC1_HIGH_PRESSURE, &desc);
    zassert_equal(err, 0, "Descriptor lookup AC1_HP succeeded");
    zassert_equal(desc.alarm_id, ALARM_ID_AC1_HIGH_PRESSURE, "ID match");
    zassert_equal(desc.default_severity, ALARM_SEVERITY_CRITICAL, "Severity match");

    /* Extend with custom expansion module alarm */
    alarm_descriptor_t custom_desc = {
        .alarm_id = ALARM_ID_CUSTOM_BASE + 1,
        .name = "FAN_EXP_FAULT",
        .description = "Expansion Module Fan Failure",
        .source_category = ALARM_SOURCE_SYSTEM_INFRA,
        .default_severity = ALARM_SEVERITY_WARNING,
        .default_priority = ALARM_PRIORITY_MEDIUM,
        .is_latch_supported = false,
        .is_ack_supported = false,
        .is_aggregated = false
    };

    err = alarm_registry_register(&reg, &custom_desc);
    zassert_equal(err, 0, "Custom alarm registered successfully");
    zassert_equal(alarm_registry_get_count(&reg), count + 1, "Count incremented");

    /* Duplicate registration rejected */
    err = alarm_registry_register(&reg, &custom_desc);
    zassert_equal(err, -EEXIST, "Duplicate ID must return -EEXIST");

    /* Query newly registered alarm */
    err = alarm_registry_get(&reg, ALARM_ID_CUSTOM_BASE + 1, &desc);
    zassert_equal(err, 0, "Lookup of custom alarm succeeded");
    zassert_equal(desc.alarm_id, ALARM_ID_CUSTOM_BASE + 1, "Custom ID match");
}

ZTEST_SUITE(alarm_domain_suite, NULL, NULL, NULL, NULL, NULL);
