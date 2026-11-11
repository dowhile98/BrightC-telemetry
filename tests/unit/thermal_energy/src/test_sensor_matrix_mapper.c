/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file test_sensor_matrix_mapper.c
 * @brief Unit tests for sensor_matrix_mapper normalizer adapter.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "infrastructure/adapters/sensor_matrix_mapper.h"
#include "common/app_defaults.h"

ZTEST_SUITE(sensor_matrix_mapper_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(sensor_matrix_mapper_suite, test_mapper_defensive_null_parameters)
{
    thermal_energy_capture_t cap;
    float temps[64] = {0};

    zassert_equal(sensor_matrix_map_temperatures(NULL, NULL, 0, &cap), -EINVAL);
    zassert_equal(sensor_matrix_map_temperatures(NULL, temps, 0, NULL), -EINVAL);
    zassert_equal(sensor_matrix_map_humidity(12.0f, true, NULL), -EINVAL);
    zassert_equal(sensor_matrix_map_electrical(NULL, NULL), -EINVAL);
    zassert_equal(sensor_matrix_map_contacts(HVAC_MODE_TELEMETRY_ONLY, NULL, 0, NULL), -EINVAL);
    zassert_equal(sensor_matrix_map_capture(NULL, &cap), -EINVAL);
    zassert_equal(sensor_matrix_map_capture(NULL, NULL), -EINVAL);
}

ZTEST(sensor_matrix_mapper_suite, test_mapper_temperatures_mapping)
{
    config_thermal_site_t cfg;
    memcpy(&cfg, app_defaults_get_thermal_site_cfg(), sizeof(cfg));

    /* Pilot config uses:
     * Pair 0: supply 10, ret 11
     * Pair 1: supply 12, ret 13
     * Pair 2: supply 14, ret 15
     * Pair 3: supply 16, ret 17
     * Pair 4: supply 30, ret 31
     * Pair 5: supply 32, ret 33
     * Room: 18, 19
     * Outdoor: 22, 23
     */
    float flat_temps[64] = {0};
    uint64_t valid_mask = 0ULL;

    const uint8_t ids[] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22, 23, 30, 31, 32, 33};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        uint8_t c = ids[i];
        uint8_t idx = c - 1;
        flat_temps[idx] = 10.0f + (float)c;
        valid_mask |= (1ULL << idx);
    }

    thermal_energy_capture_t cap = {0};
    int ret = sensor_matrix_map_temperatures(&cfg, flat_temps, valid_mask, &cap);

    zassert_equal(ret, 0);
    zassert_equal(cap.configured_pairs, 6U);
    zassert_equal(cap.inj_valid_mask, 0x3FU, "All 6 supply probes must be valid");
    zassert_equal(cap.ret_valid_mask, 0x3FU, "All 6 return probes must be valid");
    zassert_equal(cap.pair_valid_mask, 0x3FU, "All 6 pairs must be valid");

    zassert_within(cap.t_inj[0], 20.0f, 0.001f); /* flat ID 10 -> idx 9 -> 10 + 10 = 20.0 */
    zassert_within(cap.t_ret[0], 21.0f, 0.001f); /* flat ID 11 -> idx 10 -> 10 + 11 = 21.0 */

    zassert_equal(cap.configured_room_probes, 2U);
    zassert_equal(cap.room_valid_mask, 0x03U);
    zassert_within(cap.t_room[0], 28.0f, 0.001f); /* flat ID 18 -> idx 17 -> 10 + 18 = 28.0 */

    zassert_equal(cap.configured_outdoor_probes, 2U);
    zassert_equal(cap.outdoor_valid_mask, 0x03U);
    zassert_within(cap.t_outdoor[0], 32.0f, 0.001f); /* flat ID 22 -> idx 21 -> 10 + 22 = 32.0 */
}

ZTEST(sensor_matrix_mapper_suite, test_mapper_temperatures_partial_fault)
{
    config_thermal_site_t cfg;
    memcpy(&cfg, app_defaults_get_thermal_site_cfg(), sizeof(cfg));

    float flat_temps[64] = {0};
    uint64_t valid_mask = 0ULL;

    /* Populate pair 0 supply (ID 10, idx 9), but leave pair 0 return (ID 16, idx 15) UNSET */
    flat_temps[9] = 14.5f;
    valid_mask |= (1ULL << 9);

    thermal_energy_capture_t cap = {0};
    int ret = sensor_matrix_map_temperatures(&cfg, flat_temps, valid_mask, &cap);

    zassert_equal(ret, 0);
    zassert_true((cap.inj_valid_mask & 0x01U) != 0U, "Supply probe 0 must be valid");
    zassert_false((cap.ret_valid_mask & 0x01U) != 0U, "Return probe 0 must be invalid");
    zassert_false((cap.pair_valid_mask & 0x01U) != 0U, "Pair 0 must not be consensus valid");
}

ZTEST(sensor_matrix_mapper_suite, test_mapper_humidity_transducer_processing)
{
    thermal_energy_capture_t cap = {0};

    /* Nominal 12.0 mA -> 50.0% RH */
    sensor_matrix_map_humidity(12.0f, true, &cap);
    zassert_equal(cap.humidity_quality, QUALITY_VALID);
    zassert_within(cap.humidity_return_pct, 50.0f, 0.01f);
    zassert_within(cap.humidity_loop_ma, 12.0f, 0.01f);

    /* Loop fault < 3.5 mA */
    sensor_matrix_map_humidity(2.0f, true, &cap);
    zassert_equal(cap.humidity_quality, QUALITY_FAILED);
    zassert_within(cap.humidity_return_pct, 0.0f, 0.01f);

    /* Clamp deadband 3.8 mA */
    sensor_matrix_map_humidity(3.8f, true, &cap);
    zassert_equal(cap.humidity_quality, QUALITY_SUSPECT);
    zassert_within(cap.humidity_return_pct, 0.0f, 0.01f);

    /* Invalid flag */
    sensor_matrix_map_humidity(12.0f, false, &cap);
    zassert_equal(cap.humidity_quality, QUALITY_FAILED);
    zassert_within(cap.humidity_return_pct, 0.0f, 0.01f);
}

ZTEST(sensor_matrix_mapper_suite, test_mapper_electrical_ingestion)
{
    thermal_energy_capture_t cap = {0};
    energy_reading_t elec = {0};

    elec.is_valid = true;
    elec.timestamp_us = 5000000ULL;
    elec.total_active_power_kw = 15.25f;
    elec.total_power_factor = 0.94f;
    elec.total_active_energy_kwh = 12500.5;
    elec.voltage_v[0] = 220.1f;
    elec.voltage_v[1] = 221.0f;
    elec.voltage_v[2] = 219.8f;
    elec.current_a[0] = 23.1f;
    elec.current_a[1] = 22.9f;
    elec.current_a[2] = 23.5f;

    int ret = sensor_matrix_map_electrical(&elec, &cap);
    zassert_equal(ret, 0);
    zassert_equal(cap.elec_quality, QUALITY_VALID);
    zassert_equal(cap.elec_timestamp_ms, 5000ULL);
    zassert_within(cap.total_active_power_kw, 15.25f, 0.001f);
    zassert_within(cap.power_factor, 0.94f, 0.001f);
    zassert_within((float)cap.total_active_energy_kwh, 12500.5f, 0.1f);
    zassert_within(cap.voltage_v[0], 220.1f, 0.001f);
    zassert_within(cap.current_a[0], 23.1f, 0.001f);

    /* Null or invalid electrical reading */
    sensor_matrix_map_electrical(NULL, &cap);
    zassert_equal(cap.elec_quality, QUALITY_FAILED);
    zassert_within(cap.total_active_power_kw, 0.0f, 0.001f);
}

ZTEST(sensor_matrix_mapper_suite, test_mapper_contacts_telemetry_only_mode)
{
    thermal_energy_capture_t cap = {0};
    di_snapshot_msg_t di = {0};
    di.timestamp_ms = 12345ULL;
    di.channel_count = 24U;

    /* Unit 0: comp pin 0 active, fan pin 1 active (active_mask bits 0, 1)
     * Unit 1: comp pin 2 inactive, fan pin 3 active (active_mask bit 3)
     */
    di.active_mask = (1ULL << 0) | (1ULL << 1) | (1ULL << 3);

    int ret = sensor_matrix_map_contacts(HVAC_MODE_TELEMETRY_ONLY, &di, 0, &cap);
    zassert_equal(ret, 0);
    zassert_equal(cap.contact_timestamp_ms, 12345ULL);

    /* Unit 0 has both fan and comp active */
    zassert_true((cap.comp_state_mask & (1U << 0)) != 0U, "Unit 0 comp must be ON");
    zassert_true((cap.fan_state_mask & (1U << 0)) != 0U, "Unit 0 fan must be ON");

    /* Unit 1 has only fan active */
    zassert_false((cap.comp_state_mask & (1U << 1)) != 0U, "Unit 1 comp must be OFF");
    zassert_true((cap.fan_state_mask & (1U << 1)) != 0U, "Unit 1 fan must be ON");

    /* Quality masks must cover monitored units */
    zassert_equal(cap.comp_quality_mask, 0xFFU);
    zassert_equal(cap.fan_quality_mask, 0xFFU);
}

ZTEST(sensor_matrix_mapper_suite, test_mapper_contacts_hybrid_control_mode)
{
    thermal_energy_capture_t cap = {0};

    /* In Hybrid mode with direct MBSL32DI bits:
     * Fans 1..8: bits 16..23 (set bit 16 for fan 0, bit 17 for fan 1)
     * Compressors 1..8: bits 24..31 (set bit 24 for comp 0)
     */
    uint32_t mbsl32di_bits = (1UL << 16) | (1UL << 17) | (1UL << 24);

    int ret = sensor_matrix_map_contacts(HVAC_MODE_HYBRID_FULL, NULL, mbsl32di_bits, &cap);
    zassert_equal(ret, 0);

    zassert_equal(cap.fan_state_mask, 0x03U, "Fans 0 and 1 must be active");
    zassert_equal(cap.comp_state_mask, 0x01U, "Compressor 0 must be active");
    zassert_equal(cap.fan_quality_mask, 0xFFU);
    zassert_equal(cap.comp_quality_mask, 0xFFU);
}

ZTEST(sensor_matrix_mapper_suite, test_mapper_full_capture_assembly)
{
    thermal_capture_source_t src = {0};
    src.generation_id = 999U;
    src.timestamp_ms = 88888ULL;
    src.temp_timestamp_ms = 88880ULL;

    float flat_temps[64] = {0};
    uint64_t valid_mask = 0ULL;

    /* J9 CH1 (flat ID 50 -> index 49) = 16.0 mA -> 75% RH */
    flat_temps[49] = 16.0f;
    valid_mask |= (1ULL << 49);

    /* Pair 0: supply ID 10 (idx 9), return ID 11 (idx 10) */
    flat_temps[9] = 13.0f;
    flat_temps[10] = 25.0f;
    valid_mask |= (1ULL << 9) | (1ULL << 10);

    src.flat_temps = flat_temps;
    src.flat_valid_mask = valid_mask;
    src.hvac_mode = HVAC_MODE_TELEMETRY_ONLY;
    src.mbsl32di_raw_mask = (1UL << 0) | (1UL << 1); /* Unit 0 comp and fan ON */

    thermal_energy_capture_t cap = {0};
    int ret = sensor_matrix_map_capture(&src, &cap);

    zassert_equal(ret, 0);
    zassert_equal(cap.generation_id, 999U);
    zassert_equal(cap.capture_timestamp_ms, 88888ULL);
    zassert_within(cap.t_inj[0], 13.0f, 0.001f);
    zassert_within(cap.t_ret[0], 25.0f, 0.001f);
    zassert_true((cap.pair_valid_mask & 0x01U) != 0U);

    /* Humidity automatically ingested from flat ID 50 */
    zassert_equal(cap.humidity_quality, QUALITY_VALID);
    zassert_within(cap.humidity_return_pct, 75.0f, 0.01f);
    zassert_within(cap.humidity_loop_ma, 16.0f, 0.01f);

    /* Contacts */
    zassert_true((cap.comp_state_mask & 0x01U) != 0U);
    zassert_true((cap.fan_state_mask & 0x01U) != 0U);
}
