/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <stdio.h>

#include <snmp/snmp.h>
#include <snmp/snmp_core.h>
#include <snmp/snmp_mib.h>
#include <snmp/snmp_ber.h>

#include "infrastructure/adapters/adapter_snmp_telemetry.h"
#include "common/sensor_types.h"
#include "common/energy_types.h"
#include "common/thermal_energy_types.h"
#include "application/services/app_facade.h"

#define DAECGE_ENTERPRISE_ID 3134U
#define SNMP_OID_BASE 1, 3, 6, 1, 4, 1, DAECGE_ENTERPRISE_ID, 2, 3

/* =========================================================================
 * Mock Implementations for Facade Ports
 * ========================================================================= */

static thermal_energy_capture_t s_mock_thermal_capture;
static app_facade_hvac_status_t s_mock_hvac_status;
static energy_reading_t         s_mock_energy_reading;
static raw_sensor_readings_t    s_mock_raw_sensor;

static bool s_mock_thermal_ok = true;
static bool s_mock_hvac_ok = true;
static bool s_mock_energy_ok = true;
static bool s_mock_sensor_ok = true;

int app_facade_thermal_get_capture(thermal_energy_capture_t *out_capture)
{
    if (!s_mock_thermal_ok || out_capture == NULL) {
        return -EINVAL;
    }
    *out_capture = s_mock_thermal_capture;
    return 0;
}

int app_facade_hvac_get_status(app_facade_hvac_status_t *out_status)
{
    if (!s_mock_hvac_ok || out_status == NULL) {
        return -EINVAL;
    }
    *out_status = s_mock_hvac_status;
    return 0;
}

int app_facade_energy_get_reading(energy_reading_t *out_reading)
{
    if (!s_mock_energy_ok || out_reading == NULL) {
        return -EINVAL;
    }
    *out_reading = s_mock_energy_reading;
    return 0;
}

int app_facade_sensor_get_raw(raw_sensor_readings_t *out_raw)
{
    if (!s_mock_sensor_ok || out_raw == NULL) {
        return -EINVAL;
    }
    *out_raw = s_mock_raw_sensor;
    return 0;
}

/* =========================================================================
 * Test Suite Setup & Fixtures
 * ========================================================================= */

static struct snmp_agent s_agent;

static void *snmp_telemetry_setup(void)
{
    int rc = snmp_agent_init(&s_agent, NULL);
    zassert_equal(rc, 0, "snmp_agent_init failed: %d", rc);

    rc = adapter_snmp_telemetry_init(&s_agent);
    zassert_equal(rc, 0, "adapter_snmp_telemetry_init failed: %d", rc);

    return NULL;
}

static void snmp_telemetry_before(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&s_mock_thermal_capture, 0, sizeof(s_mock_thermal_capture));
    memset(&s_mock_hvac_status, 0, sizeof(s_mock_hvac_status));
    memset(&s_mock_energy_reading, 0, sizeof(s_mock_energy_reading));
    memset(&s_mock_raw_sensor, 0, sizeof(s_mock_raw_sensor));

    s_mock_thermal_ok = true;
    s_mock_hvac_ok = true;
    s_mock_energy_ok = true;
    s_mock_sensor_ok = true;
}

ZTEST_SUITE(snmp_telemetry_suite, NULL, snmp_telemetry_setup, snmp_telemetry_before, NULL, NULL);

/* =========================================================================
 * Test Cases
 * ========================================================================= */

ZTEST(snmp_telemetry_suite, test_scalar_process_temperatures)
{
    /* Setup thermal capture mock data */
    s_mock_thermal_capture.inj_valid_mask = 0xFFFF;
    s_mock_thermal_capture.ret_valid_mask = 0xFFFF;
    s_mock_thermal_capture.pair_valid_mask = 0xFFFF;
    s_mock_thermal_capture.room_valid_mask = 0x03;
    s_mock_thermal_capture.outdoor_valid_mask = 0x01;
    s_mock_thermal_capture.humidity_return_pct = 52.4f;

    /* Units 1..4 */
    s_mock_thermal_capture.t_inj[0] = 12.5f;
    s_mock_thermal_capture.t_ret[0] = 24.0f;
    s_mock_thermal_capture.t_inj[1] = 13.0f;
    s_mock_thermal_capture.t_ret[1] = 24.5f;
    s_mock_thermal_capture.t_inj[2] = 13.5f;
    s_mock_thermal_capture.t_ret[2] = 25.0f;
    s_mock_thermal_capture.t_inj[3] = 14.0f;
    s_mock_thermal_capture.t_ret[3] = 25.5f;

    /* Units 5..8 */
    s_mock_thermal_capture.t_inj[4] = 14.5f;
    s_mock_thermal_capture.t_ret[4] = 26.0f;
    s_mock_thermal_capture.t_inj[5] = 15.0f;
    s_mock_thermal_capture.t_ret[5] = 26.5f;
    s_mock_thermal_capture.t_inj[6] = 15.5f;
    s_mock_thermal_capture.t_ret[6] = 27.0f;
    s_mock_thermal_capture.t_inj[7] = 16.0f;
    s_mock_thermal_capture.t_ret[7] = 27.5f;

    s_mock_thermal_capture.t_room[0] = 22.3f;
    s_mock_thermal_capture.t_room[1] = 22.8f;
    s_mock_thermal_capture.t_outdoor[0] = 34.6f;

    /* Setup HVAC status mock data (Control Temps for Groups 1..4) */
    s_mock_hvac_status.total_groups = 4;
    s_mock_hvac_status.groups[0].highest_temp = 23.1f;
    s_mock_hvac_status.groups[1].highest_temp = 24.2f;
    s_mock_hvac_status.groups[2].highest_temp = 22.0f;
    s_mock_hvac_status.groups[3].highest_temp = 25.5f;

    struct snmp_varbind vb;

    /* 1. Unit 1 Evap Supply (subid 1) -> 125 */
    struct snmp_oid oid_s1 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 1, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_s1, &vb), 0, "Get failed");
    zassert_equal(vb.type, ASN1_TAG_INTEGER, "Type mismatch");
    zassert_equal(vb.val.int_val, 125, "tempSupplyEvap1 mismatch");

    /* 2. Unit 1 Evap Return (subid 2) -> 240 */
    struct snmp_oid oid_r1 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 2, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 240, "tempReturnEvap1 mismatch");

    /* 3. Unit 1 Evap Delta T (subid 3) -> 115 */
    struct snmp_oid oid_d1 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 3, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_d1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 115, "tempDeltaEvap1 mismatch");

    /* 4. Room Ambient 1 (subid 13) -> 223 */
    struct snmp_oid oid_room1 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 13, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_room1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 223, "tempRoomAmbient1 mismatch");

    /* 5. Return Grille Humidity (subid 16) -> 524 (Gauge32) */
    struct snmp_oid oid_hum = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 16, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_hum, &vb), 0, "Get failed");
    zassert_equal(vb.type, SNMP_TAG_GAUGE32, "Type mismatch for humidity");
    zassert_equal(vb.val.uint_val, 524, "humidityReturnGrille mismatch");

    /* 6. HVAC Control Temps (subids 17..20 -> Salas 1..4) */
    struct snmp_oid oid_ctrl1 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 17, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_ctrl1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 231, "tempControlGroup1 mismatch");

    struct snmp_oid oid_ctrl2 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 18, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_ctrl2, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 242, "tempControlGroup2 mismatch");

    struct snmp_oid oid_ctrl3 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 19, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_ctrl3, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 220, "tempControlGroup3 mismatch");

    struct snmp_oid oid_ctrl4 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 20, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_ctrl4, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 255, "tempControlGroup4 mismatch");

    /* 7. Units 5..8 Evaporator Temperatures (subids 21..32) */
    /* Unit 5 */
    struct snmp_oid oid_s5 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 21, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_s5, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 145, "tempSupplyEvap5 mismatch");

    struct snmp_oid oid_r5 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 22, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_r5, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 260, "tempReturnEvap5 mismatch");

    struct snmp_oid oid_d5 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 23, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_d5, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 115, "tempDeltaEvap5 mismatch");

    /* Unit 8 */
    struct snmp_oid oid_s8 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 30, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_s8, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 160, "tempSupplyEvap8 mismatch");

    struct snmp_oid oid_r8 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 31, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_r8, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 275, "tempReturnEvap8 mismatch");

    struct snmp_oid oid_d8 = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 32, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_d8, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 115, "tempDeltaEvap8 mismatch");
}

ZTEST(snmp_telemetry_suite, test_scalar_process_temperatures_invalid)
{
    /* When mask is 0 or sensor is disconnected, return sentinel -9999 */
    s_mock_thermal_capture.inj_valid_mask = 0;
    s_mock_thermal_capture.ret_valid_mask = 0;
    s_mock_thermal_capture.pair_valid_mask = 0;

    struct snmp_varbind vb;
    struct snmp_oid oid = {.len = 13, .ids = {SNMP_OID_BASE, 2, 2, 1, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, -9999, "Must return sentinel -9999 on invalid sensor");

    /* When facade returns error */
    s_mock_thermal_ok = false;
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, -9999, "Must return sentinel -9999 on facade failure");
}

ZTEST(snmp_telemetry_suite, test_scalar_energy_telemetry)
{
    s_mock_energy_reading.is_valid = true;
    s_mock_energy_reading.frequency_hz = 60.05f;
    s_mock_energy_reading.voltage_v[0] = 220.4f;
    s_mock_energy_reading.voltage_v[1] = 221.1f;
    s_mock_energy_reading.voltage_v[2] = 219.7f;
    s_mock_energy_reading.current_a[0] = 15.250f;
    s_mock_energy_reading.current_a[1] = 14.800f;
    s_mock_energy_reading.current_a[2] = 15.100f;
    s_mock_energy_reading.leakage_current_a = 0.035f;
    s_mock_energy_reading.active_power_kw[0] = 3.200f;
    s_mock_energy_reading.active_power_kw[1] = 3.100f;
    s_mock_energy_reading.active_power_kw[2] = 3.300f;
    s_mock_energy_reading.total_active_power_kw = 9.600f;
    s_mock_energy_reading.total_reactive_power_kvar = 2.150f;
    s_mock_energy_reading.total_apparent_power_kva = 9.850f;
    s_mock_energy_reading.power_factor[0] = 0.952f;
    s_mock_energy_reading.power_factor[1] = 0.941f;
    s_mock_energy_reading.power_factor[2] = 0.963f;
    s_mock_energy_reading.total_power_factor = 0.955f;
    s_mock_energy_reading.total_active_energy_kwh = 12345.67;
    s_mock_energy_reading.total_reactive_energy_kvarh = 987.65;

    struct snmp_varbind vb;

    /* 1. energyCommStatus (subid 1) -> 1 (commOk) */
    struct snmp_oid oid_comm = {.len = 12, .ids = {SNMP_OID_BASE, 6, 1, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_comm, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 1, "energyCommStatus mismatch");

    /* 2. energyLineFrequency (subid 2) -> 601 (60.05 * 10 rounded) */
    struct snmp_oid oid_hz = {.len = 12, .ids = {SNMP_OID_BASE, 6, 2, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_hz, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 601, "energyLineFrequency mismatch");

    /* 3. voltagePhaseA (subid 3) -> 2204 */
    struct snmp_oid oid_va = {.len = 12, .ids = {SNMP_OID_BASE, 6, 3, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_va, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 2204, "voltagePhaseA mismatch");

    /* 4. currentPhaseA (subid 6) -> 153 (tenths of A, Gauge32) */
    struct snmp_oid oid_ia = {.len = 12, .ids = {SNMP_OID_BASE, 6, 6, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_ia, &vb), 0, "Get failed");
    zassert_equal(vb.type, SNMP_TAG_GAUGE32, "Type mismatch for currentPhaseA");
    zassert_equal(vb.val.uint_val, 153, "currentPhaseA mismatch");

    /* 5. totalActivePower (subid 13) -> 9600 W */
    struct snmp_oid oid_ptot = {.len = 12, .ids = {SNMP_OID_BASE, 6, 13, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_ptot, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 9600, "totalActivePower mismatch");

    /* 6. totalPowerFactor (subid 19) -> 96 (-100..100) */
    struct snmp_oid oid_pf = {.len = 12, .ids = {SNMP_OID_BASE, 6, 19, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_pf, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 96, "totalPowerFactor mismatch");

    /* 7. totalActiveEnergyKwh (subid 20) -> 12346 (kWh, Gauge32) */
    struct snmp_oid oid_kwh = {.len = 12, .ids = {SNMP_OID_BASE, 6, 20, 0}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_kwh, &vb), 0, "Get failed");
    zassert_equal(vb.type, SNMP_TAG_GAUGE32, "Type mismatch for totalActiveEnergyKwh");
    zassert_equal(vb.val.uint_val, 12346, "totalActiveEnergyKwh mismatch");
}

ZTEST(snmp_telemetry_suite, test_hvac_group_table)
{
    /* Configure HVAC Group Table data */
    s_mock_hvac_status.total_groups = 4;

    /* Sala 1: Cooling, 23.5 C, setpoints 19/24/26/28, sensors 1..4 (4 sensors), 2 active units */
    s_mock_hvac_status.groups[0].state = GROUP_STATE_COOLING;
    s_mock_hvac_status.groups[0].highest_temp = 23.5f;
    s_mock_hvac_status.groups[0].temp_min = 19.0f;
    s_mock_hvac_status.groups[0].temp_max = 24.0f;
    s_mock_hvac_status.groups[0].temp_alarm = 26.0f;
    s_mock_hvac_status.groups[0].temp_bypass = 28.0f;
    s_mock_hvac_status.groups[0].sensor_start = 1;
    s_mock_hvac_status.groups[0].sensor_end = 4;
    s_mock_hvac_status.groups[0].active_turn_count = 2;

    /* Sala 2: CoolBoost, 26.2 C, setpoints 20/24/26/28, sensors 5..8, 3 active units */
    s_mock_hvac_status.groups[1].state = GROUP_STATE_COOL_BOOST;
    s_mock_hvac_status.groups[1].highest_temp = 26.2f;
    s_mock_hvac_status.groups[1].temp_min = 20.0f;
    s_mock_hvac_status.groups[1].temp_max = 24.0f;
    s_mock_hvac_status.groups[1].temp_alarm = 26.0f;
    s_mock_hvac_status.groups[1].temp_bypass = 28.0f;
    s_mock_hvac_status.groups[1].sensor_start = 5;
    s_mock_hvac_status.groups[1].sensor_end = 8;
    s_mock_hvac_status.groups[1].active_turn_count = 3;

    struct snmp_varbind vb;

    /* Base table entry: 1.3.6.1.4.1.3134.2.3.3.2.1.<col>.<row> (14 subids) */

    /* Row 1, Col 1 (Index) */
    struct snmp_oid oid_c1_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 1, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c1_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 1, "hvacGroupIndex mismatch");

    /* Row 1, Col 2 (Name) */
    struct snmp_oid oid_c2_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 2, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c2_r1, &vb), 0, "Get failed");
    zassert_equal(vb.type, ASN1_TAG_OCTET_STRING, "Type mismatch");
    zassert_mem_equal(vb.val.octet_str.data, "HVAC Group 1 (Sala 1)", vb.val.octet_str.len, "Name mismatch");

    /* Row 1, Col 3 (State): cooling -> 3 + 1 = 4 (or MIB enum cooling(3)) */
    struct snmp_oid oid_c3_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 3, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c3_r1, &vb), 0, "Get failed");
    zassert_true(vb.val.int_val >= 1 && vb.val.int_val <= 7, "State out of enum bounds");

    /* Row 1, Col 4 (Control Temp) -> 235 */
    struct snmp_oid oid_c4_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 4, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c4_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 235, "Control Temp mismatch");

    /* Row 1, Col 5 (TempMin) -> 190 */
    struct snmp_oid oid_c5_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 5, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c5_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 190, "TempMin mismatch");

    /* Row 1, Col 6 (TempMax) -> 240 */
    struct snmp_oid oid_c6_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 6, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c6_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 240, "TempMax mismatch");

    /* Row 1, Col 7 (TempAlarm) -> 260 */
    struct snmp_oid oid_c7_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 7, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c7_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 260, "TempAlarm mismatch");

    /* Row 1, Col 8 (TempBypass) -> 280 */
    struct snmp_oid oid_c8_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 8, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c8_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 280, "TempBypass mismatch");

    /* Row 1, Col 9 (Valid Sensors) -> 4 */
    struct snmp_oid oid_c9_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 9, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c9_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 4, "Valid Sensors count mismatch");

    /* Row 1, Col 10 (Active Units) -> 2 */
    struct snmp_oid oid_c10_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 10, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c10_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 2, "Active Units count mismatch");

    /* Row 2, Col 4 (Control Temp for Sala 2) -> 262 */
    struct snmp_oid oid_c4_r2 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 4, 2}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c4_r2, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 262, "Sala 2 Control Temp mismatch");

    /* Non-existent Row 5 -> NoSuchInstance */
    struct snmp_oid oid_c1_r5 = {.len = 14, .ids = {SNMP_OID_BASE, 3, 2, 1, 1, 5}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c1_r5, &vb), 0, "Get failed");
    zassert_equal(vb.type, SNMP_TAG_EXCEPT_NO_SUCH_INST, "Row 5 must return NoSuchInstance");
}

ZTEST(snmp_telemetry_suite, test_key_sensor_table)
{
    /* Configure raw sensor readings */
    s_mock_raw_sensor.cwt_comm_ok[0] = true;
    s_mock_raw_sensor.cwt_temperatures_c[0][0] = 23.4f;
    s_mock_raw_sensor.cwt_valid_mask[0] = 0x0001;
    s_mock_raw_sensor.cwt_open_probe_mask[0] = 0x0000;

    struct snmp_varbind vb;

    /* Base sensor table entry: 1.3.6.1.4.1.3134.2.3.2.1.1.<col>.<row> (14 subids) */

    /* Row 1, Col 1 (Index) */
    struct snmp_oid oid_c1_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 2, 1, 1, 1, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c1_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 1, "Index mismatch");

    /* Row 1, Col 5 (Value in 0.1 C) -> 234 */
    struct snmp_oid oid_c5_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 2, 1, 1, 5, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c5_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 234, "Value mismatch");

    /* Row 1, Col 6 (Status): 1=normal */
    struct snmp_oid oid_c6_r1 = {.len = 14, .ids = {SNMP_OID_BASE, 2, 1, 1, 6, 1}};
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c6_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 1, "Status normal mismatch");

    /* Disconnect sensor probe -> status=2 (openProbe) */
    s_mock_raw_sensor.cwt_open_probe_mask[0] = 0x0001;
    memset(&vb, 0, sizeof(vb));
    zassert_equal(snmp_mib_get(&s_agent, &oid_c6_r1, &vb), 0, "Get failed");
    zassert_equal(vb.val.int_val, 2, "Status openProbe mismatch");
}
