/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_alarm_detectors.c
 * @brief Unit tests for DI, Temperature, Voltage, and HVAC Alarm Detectors.
 */

#include <zephyr/ztest.h>

#include "common/alarm_types.h"
#include "common/hvac_types.h"
#include "application/active_objects/ao_alarm_manager.h"
#include "infrastructure/adapters/adapter_alarm_detector_di.h"
#include "infrastructure/adapters/adapter_alarm_detector_temp.h"
#include "infrastructure/adapters/adapter_alarm_detector_volt.h"
#include "infrastructure/adapters/adapter_alarm_detector_hvac.h"

#define DET_TEST_STACK_SIZE 2048
K_THREAD_STACK_DEFINE(s_det_test_stack, DET_TEST_STACK_SIZE);

static ao_alarm_manager_t s_mgr;
static i_alarm_query_t s_query;

static void *detectors_setup(void)
{
    ao_alarm_manager_init(&s_mgr, s_det_test_stack, DET_TEST_STACK_SIZE, K_PRIO_PREEMPT(5));
    ao_alarm_manager_start(&s_mgr);
    s_query = ao_alarm_manager_get_query_port(&s_mgr);
    return NULL;
}

static void detectors_after(void *data)
{
    ARG_UNUSED(data);
    /* Clear active alarms if any */
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_AC1_HIGH_PRESSURE, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_AC5_HIGH_PRESSURE, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_FACILITY_FIRE, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_GROUP1_TEMP_LOW, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_GROUP2_TEMP_HIGH, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_GROUP3_TEMP_HIGH, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_LINE_UNDER_VOLTAGE, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_LINE_OVER_VOLTAGE, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_ENERGY_METER_COMM_FAULT, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_HVAC_G1_BYPASS_ACTIVE, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_HVAC_G3_BYPASS_ACTIVE, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_HVAC_UNIT1_LOCKOUT, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_HVAC_UNIT8_LOCKOUT, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_SENSOR_GROUP1_FAULT, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_SENSOR_GROUP2_FAULT, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_SENSOR_GROUP4_FAULT, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_GROUP2_TEMP_BOOST, false, 0.0f);
    ao_alarm_manager_post_condition(&s_mgr, ALARM_ID_INFRA_MODBUS_FAULT, false, 0.0f);
    k_sleep(K_MSEC(40));
}

ZTEST_SUITE(alarm_detectors_suite, NULL, detectors_setup, NULL, detectors_after, NULL);

ZTEST(alarm_detectors_suite, test_detector_di_contact_mapping)
{
    adapter_alarm_detector_di_t det_di;
    adapter_alarm_detector_di_init(&det_di, &s_mgr);

    /* 1. AC1 High Pressure Contact trips */
    di_transition_event_t evt_hp = {
        .role = DI_ROLE_UNIT_0_ALARM_HP,
        .new_state = DI_STATE_ACTIVE,
        .prev_state = DI_STATE_INACTIVE,
        .timestamp_ms = 1000
    };
    adapter_alarm_detector_di_process_transition(&det_di, &evt_hp);

    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_AC1_HIGH_PRESSURE), "AC1 HP must be active");

    /* Contact opens */
    evt_hp.new_state = DI_STATE_INACTIVE;
    evt_hp.prev_state = DI_STATE_ACTIVE;
    adapter_alarm_detector_di_process_transition(&det_di, &evt_hp);

    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_AC1_HIGH_PRESSURE), "AC1 HP must clear");

    /* 2. AC5 High Pressure Contact trips (Unit 4 - Extended 8-unit support) */
    di_transition_event_t evt_ac5 = {
        .role = DI_ROLE_UNIT_4_ALARM_HP,
        .new_state = DI_STATE_ACTIVE,
        .prev_state = DI_STATE_INACTIVE,
        .timestamp_ms = 1500
    };
    adapter_alarm_detector_di_process_transition(&det_di, &evt_ac5);

    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_AC5_HIGH_PRESSURE), "AC5 HP must be active");

    evt_ac5.new_state = DI_STATE_INACTIVE;
    adapter_alarm_detector_di_process_transition(&det_di, &evt_ac5);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_AC5_HIGH_PRESSURE), "AC5 HP clears");

    /* 3. Fire Contact trips */
    di_transition_event_t evt_fire = {
        .role = DI_ROLE_SMOKE_FIRE,
        .new_state = DI_STATE_ACTIVE,
        .prev_state = DI_STATE_INACTIVE,
        .timestamp_ms = 2000
    };
    adapter_alarm_detector_di_process_transition(&det_di, &evt_fire);

    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_FACILITY_FIRE), "FIRE must be active");
    zassert_equal(i_alarm_query_get_highest_active_severity(&s_query), ALARM_SEVERITY_FATAL, "Severity FATAL");

    /* Fire clears */
    evt_fire.new_state = DI_STATE_INACTIVE;
    adapter_alarm_detector_di_process_transition(&det_di, &evt_fire);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_FACILITY_FIRE), "FIRE clears");
}

ZTEST(alarm_detectors_suite, test_detector_temp_snapshot_and_fault)
{
    adapter_alarm_detector_temp_t det_temp;
    /* 4 groups:
     * Group 1 (mask 0x01, probe 0), Group 2 (mask 0x02, probe 1)
     * Group 3 (mask 0x04, probe 2), Group 4 (mask 0x08, probe 3)
     * Thresholds: Low 15.0C, High 28.0C, Hyst 1.0C */
    const uint16_t masks[HVAC_MAX_GROUPS] = { 0x0001U, 0x0002U, 0x0004U, 0x0008U };
    adapter_alarm_detector_temp_init(&det_temp, &s_mgr, masks, 15.0f, 28.0f, 1.0f);

    sensor_snapshot_msg_t snap = {
        .valid_mask = 0x0FU, /* Probes 0..3 valid */
        .temperatures = { 22.0f, 22.5f, 23.0f, 21.0f },
        .timestamp_ms = 1000,
        .sequence = 1
    };

    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));

    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP1_TEMP_LOW), "22C -> Low temp inactive");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP1_TEMP_HIGH), "22C -> High temp inactive");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP2_TEMP_HIGH), "22C -> Group 2 high temp inactive");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP3_TEMP_HIGH), "23C -> Group 3 high temp inactive");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP4_TEMP_LOW), "21C -> Group 4 low temp inactive");

    /* Probe 0 drops to 14.5C (< 15.0C) -> Low temp trips! */
    snap.temperatures[0] = 14.5f;
    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP1_TEMP_LOW), "14.5C -> Low temp active");

    /* Probe 0 warms to 15.5C (hysteresis band < 16.0C) -> Remains active */
    snap.temperatures[0] = 15.5f;
    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP1_TEMP_LOW), "15.5C -> Still alarmed (hysteresis)");

    /* CRITICAL TEST: Disconnect probe 0 while in alarm!
     * Low temp alarm MUST REMAIN ACTIVE (disconnected sensor must NOT clear existing alarm).
     * Sensor fault alarm must also trip! */
    snap.valid_mask = 0x0EU; /* Probe 0 invalid (bit 0 = 0) */
    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP1_TEMP_LOW),
                 "Low temp must NOT be cleared by sensor disconnection!");
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_SENSOR_GROUP1_FAULT),
                 "Sensor fault for Group 1 must be active");

    /* Probe 0 reconnects with normal temperature 17.0C (>= 16.0C) -> Clears! */
    snap.valid_mask = 0x0FU;
    snap.temperatures[0] = 17.0f;
    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP1_TEMP_LOW), "17.0C -> Cleared");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_SENSOR_GROUP1_FAULT), "Sensor fault clears");

    /* Group 2 probe 1 exceeds 28.0C -> Group 2 High Temp trips! */
    snap.temperatures[1] = 29.5f;
    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP2_TEMP_HIGH), "Group 2 High temp active");

    /* Group 3 probe 2 exceeds 28.0C -> Group 3 High Temp trips! */
    snap.temperatures[2] = 30.0f;
    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP3_TEMP_HIGH), "Group 3 High temp active");

    /* Group 4 probe 3 disconnects -> Group 4 Sensor Fault trips! */
    snap.valid_mask = 0x07U; /* Probe 3 invalid (bit 3 = 0) */
    adapter_alarm_detector_temp_process_snapshot(&det_temp, &snap);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_SENSOR_GROUP4_FAULT), "Group 4 Sensor fault active");
}

ZTEST(alarm_detectors_suite, test_detector_volt_reading)
{
    adapter_alarm_detector_volt_t det_volt;
    /* Nominal 220V, dev 8% (trip: 202.4V / 237.6V), hyst 2.0V */
    adapter_alarm_detector_volt_init(&det_volt, &s_mgr, 220.0f, 8.0f, 2.0f);

    energy_reading_t rd = {
        .is_valid = true,
        .voltage_v = { 220.0f, 221.0f, 219.0f },
        .timestamp_us = 1000000ULL
    };

    adapter_alarm_detector_volt_process_reading(&det_volt, &rd);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_LINE_UNDER_VOLTAGE), "Normal voltage");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_ENERGY_METER_COMM_FAULT), "Meter comm valid");

    /* Phase B (index 1) drops to 195V while Phase A is 220V -> Under-voltage trips on Phase B! */
    rd.voltage_v[1] = 195.0f;
    adapter_alarm_detector_volt_process_reading(&det_volt, &rd);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_LINE_UNDER_VOLTAGE), "Under-voltage active on Phase B");

    /* Phase B recovers to 206V -> Clears! */
    rd.voltage_v[1] = 206.0f;
    adapter_alarm_detector_volt_process_reading(&det_volt, &rd);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_LINE_UNDER_VOLTAGE), "Under-voltage cleared");

    /* Energy meter communication drops (is_valid == false) -> Meter comm fault trips! */
    rd.is_valid = false;
    adapter_alarm_detector_volt_process_reading(&det_volt, &rd);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_ENERGY_METER_COMM_FAULT),
                 "Meter comm fault must be active when reading invalid");

    /* Meter reconnects */
    rd.is_valid = true;
    adapter_alarm_detector_volt_process_reading(&det_volt, &rd);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_ENERGY_METER_COMM_FAULT),
                 "Meter comm fault clears when valid");
}

ZTEST(alarm_detectors_suite, test_detector_hvac_bypass_and_lockout)
{
    adapter_alarm_detector_hvac_t det_hvac;
    adapter_alarm_detector_hvac_init(&det_hvac, &s_mgr);

    hvac_status_msg_t status;
    memset(&status, 0, sizeof(status));

    status.group_state[0] = GROUP_STATE_COOLING;
    status.group_state[1] = GROUP_STATE_COOLING;
    status.group_state[2] = GROUP_STATE_COOLING;
    status.group_state[3] = GROUP_STATE_COOLING;
    status.unit_state[0] = UNIT_STATE_RUNNING;
    status.unit_state[7] = UNIT_STATE_RUNNING;

    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G1_BYPASS_ACTIVE), "Normal cooling G1");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G2_BYPASS_ACTIVE), "Normal cooling G2");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G3_BYPASS_ACTIVE), "Normal cooling G3");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G4_BYPASS_ACTIVE), "Normal cooling G4");

    /* 1. HVAC Group 1 enters Bypass */
    status.group_state[0] = GROUP_STATE_BYPASS;
    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G1_BYPASS_ACTIVE), "G1 Bypass alarm active");

    /* Group 3 also enters Bypass */
    status.group_state[2] = GROUP_STATE_BYPASS;
    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G3_BYPASS_ACTIVE), "G3 Bypass alarm active");

    /* Returns to cooling */
    status.group_state[0] = GROUP_STATE_COOLING;
    status.group_state[2] = GROUP_STATE_COOLING;
    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G1_BYPASS_ACTIVE), "G1 Bypass cleared");
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_G3_BYPASS_ACTIVE), "G3 Bypass cleared");

    /* 2. Group 2 enters COOL_BOOST */
    status.group_state[1] = GROUP_STATE_COOL_BOOST;
    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP2_TEMP_BOOST), "Group 2 Boost alarm active");

    /* Group 2 returns to normal cooling */
    status.group_state[1] = GROUP_STATE_COOLING;
    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_GROUP2_TEMP_BOOST), "Group 2 Boost alarm cleared");

    /* 3. Unit 8 enters Lockout (Extended 8-unit support) */
    status.unit_state[7] = UNIT_STATE_BLOCKED_ALARM;
    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_true(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_UNIT8_LOCKOUT), "Unit 8 lockout active");

    /* Unit 8 lockout clears */
    status.unit_state[7] = UNIT_STATE_RUNNING;
    adapter_alarm_detector_hvac_process_status(&det_hvac, &status);
    k_sleep(K_MSEC(50));
    zassert_false(i_alarm_query_is_active(&s_query, ALARM_ID_HVAC_UNIT8_LOCKOUT), "Unit 8 lockout cleared");
}
