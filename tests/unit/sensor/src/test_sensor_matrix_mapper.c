/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Comprehensive unit tests for sensor_matrix_mapper:
 * - Channel routing and role assignment
 * - Pairing consensus and health masks
 * - Open probe detection
 * - Calibration offset & gain application
 * - Internal RTD Callendar-Van Dusen conversion
 * - Field-swap hot-swap simulation
 * - 4-20 mA NAMUR thresholds and linearization
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "domain/services/sensor_matrix_mapper.h"
#include "transducer_rtd.h"
#include "transducer_current_loop.h"

ZTEST_SUITE(sensor_matrix_mapper_suite, NULL, NULL, NULL, NULL, NULL);

/* Helper to setup nominal pilot profile readings */
static void setup_nominal_pilot_raw(raw_sensor_readings_t *raw)
{
    memset(raw, 0, sizeof(*raw));
    raw->timestamp_us = 12345678000ULL;
    raw->cwt_comm_ok = true;
    raw->cwt_valid_mask = 0xFFFF;      /* All 16 CWT channels valid */
    raw->cwt_open_probe_mask = 0x0000; /* No open probes */

    /* Supply temperatures (Pairs 0 to 5): 12.0 C to 14.5 C */
    raw->cwt_temperatures_c[0] = 12.0f;
    raw->cwt_temperatures_c[1] = 12.5f;
    raw->cwt_temperatures_c[2] = 13.0f;
    raw->cwt_temperatures_c[3] = 13.5f;
    raw->cwt_temperatures_c[4] = 14.0f;
    raw->cwt_temperatures_c[5] = 14.5f;

    /* Return temperatures (Pairs 0 to 5): 22.0 C to 24.5 C */
    raw->cwt_temperatures_c[6] = 22.0f;
    raw->cwt_temperatures_c[7] = 22.5f;
    raw->cwt_temperatures_c[8] = 23.0f;
    raw->cwt_temperatures_c[9] = 23.5f;
    raw->cwt_temperatures_c[10] = 24.0f;
    raw->cwt_temperatures_c[11] = 24.5f;

    /* Room temperatures (0 and 1): 21.0 C, 21.5 C */
    raw->cwt_temperatures_c[12] = 21.0f;
    raw->cwt_temperatures_c[13] = 21.5f;

    /* Outdoor temperatures (0 and 1): 32.0 C, 32.5 C */
    raw->cwt_temperatures_c[14] = 32.0f;
    raw->cwt_temperatures_c[15] = 32.5f;

    /* J9 CH1: Return Humidity at 50% HR (12.0 mA)
     * For shunt 220 Ohm, gain 1.98, Vref 3.3V, scale_factor 3.0:
     * numer / denom * 1000 = 12.0 mA
     * Let's calculate counts:
     * denom = 220 * 1.98 * 65535 = 28549044.0
     * numer = 12.0 * denom / 1000 = 342588.528
     * adc_counts = numer / (3.0 * 3.3) = 342588.528 / 9.9 = 34604.9
     */
    current_loop_circuit_cfg_t circ_cfg;
    transducer_current_loop_get_brightc_v3_default_config(&circ_cfg);
    float denom = circ_cfg.shunt_ohms * circ_cfg.gain * circ_cfg.adc_max;
    float adc_counts = (12.0f * denom / 1000.0f) / (circ_cfg.scale_factor * circ_cfg.v_ref);
    raw->analog_j9_raw_adc[0] = adc_counts;
    raw->analog_j9_valid_mask = 0x0001; /* J9 CH1 valid */
}

ZTEST(sensor_matrix_mapper_suite, test_pilot_profile_initialization)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    int err = sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);
    zassert_equal(err, 0, "Default pilot config init failed");

    /* Verify Supply Probes (Pairs 0-5) */
    for (int k = 0; k < 6; k++) {
        zassert_equal(configs[k].physical_ch, PHYS_CH_CWT_1 + k, "Supply physical ch %d", k);
        zassert_equal(configs[k].role, SENSOR_ROLE_TEMP_SUPPLY_AIR, "Supply role %d", k);
        zassert_equal(configs[k].target_index, k, "Supply target index %d", k);
        zassert_equal(configs[k].cal_offset_c_centi, 0, "Supply cal offset 0");
        zassert_equal(configs[k].cal_gain_ppm, 0, "Supply cal gain 0");
    }
    zassert_equal(configs[0].associated_hvac_unit, 1, "Pair 0 unit 1");
    zassert_equal(configs[1].associated_hvac_unit, 1, "Pair 1 unit 1");
    zassert_equal(configs[2].associated_hvac_unit, 2, "Pair 2 unit 2");
    zassert_equal(configs[3].associated_hvac_unit, 2, "Pair 3 unit 2");
    zassert_equal(configs[4].associated_hvac_unit, 3, "Pair 4 unit 3");
    zassert_equal(configs[5].associated_hvac_unit, 3, "Pair 5 unit 3");

    /* Verify Return Probes (Pairs 0-5) */
    for (int k = 0; k < 6; k++) {
        int idx = 6 + k;
        zassert_equal(configs[idx].physical_ch, PHYS_CH_CWT_1 + idx, "Return physical ch %d", k);
        zassert_equal(configs[idx].role, SENSOR_ROLE_TEMP_RETURN_AIR, "Return role %d", k);
        zassert_equal(configs[idx].target_index, k, "Return target index %d", k);
    }

    /* Verify Room Probes */
    zassert_equal(configs[12].role, SENSOR_ROLE_TEMP_ROOM_AMBIENT, "Room 0 role");
    zassert_equal(configs[12].target_index, 0, "Room 0 index");
    zassert_equal(configs[13].role, SENSOR_ROLE_TEMP_ROOM_AMBIENT, "Room 1 role");
    zassert_equal(configs[13].target_index, 1, "Room 1 index");

    /* Verify Outdoor Probes */
    zassert_equal(configs[14].role, SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT, "Outdoor 0 role");
    zassert_equal(configs[14].target_index, 0, "Outdoor 0 index");
    zassert_equal(configs[15].role, SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT, "Outdoor 1 role");
    zassert_equal(configs[15].target_index, 1, "Outdoor 1 index");

    /* Verify Internal RTD 1-5 disabled */
    for (int i = 0; i < 5; i++) {
        zassert_equal(configs[16 + i].role, SENSOR_ROLE_DISABLED, "Internal RTD %d disabled", i + 1);
        zassert_equal(configs[16 + i].cal_offset_c_centi, 0, "Internal RTD %d offset 0", i + 1);
    }

    /* Verify J9 CH1 return humidity */
    zassert_equal(configs[21].physical_ch, PHYS_CH_ANALOG_J9_CH1, "J9 CH1 channel");
    zassert_equal(configs[21].role, SENSOR_ROLE_HUMIDITY_RETURN, "J9 CH1 role");

    /* Bad args */
    zassert_equal(sensor_matrix_init_default_pilot_config(NULL, SENSOR_PHYS_CH_MAX), -EINVAL, "Null configs");
    zassert_equal(sensor_matrix_init_default_pilot_config(configs, 10), -EINVAL, "Buffer too small");
}

ZTEST(sensor_matrix_mapper_suite, test_pilot_profile_happy_path_mapping)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);

    thermal_matrix_frame_t frame;
    int err = sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_equal(err, 0, "Mapping returned error");

    zassert_equal(frame.timestamp_us, raw.timestamp_us, "Timestamp preserved");
    zassert_equal(frame.configured_pairs, 6, "Must configure 6 pairs");
    zassert_equal(frame.configured_room_probes, 2, "Must configure 2 room probes");
    zassert_equal(frame.configured_outdoor_probes, 2, "Must configure 2 outdoor probes");

    /* Consensus masks: all 6 pairs valid, 0 open */
    zassert_equal(frame.pair_valid_mask, 0x003F, "All 6 pairs must be valid (bits 0..5)");
    zassert_equal(frame.pair_open_probe_mask, 0x0000, "No open probes expected");

    /* Room and Outdoor masks */
    zassert_equal(frame.room_valid_mask, 0x03, "Both room probes valid (bits 0, 1)");
    zassert_equal(frame.outdoor_valid_mask, 0x03, "Both outdoor probes valid (bits 0, 1)");

    /* Temperatures mapped accurately */
    zassert_within(frame.t_inj[0], 12.0f, 0.01f, "Inj 0");
    zassert_within(frame.t_ret[0], 22.0f, 0.01f, "Ret 0");
    zassert_within(frame.t_inj[5], 14.5f, 0.01f, "Inj 5");
    zassert_within(frame.t_ret[5], 24.5f, 0.01f, "Ret 5");

    zassert_within(frame.t_room[0], 21.0f, 0.01f, "Room 0");
    zassert_within(frame.t_room[1], 21.5f, 0.01f, "Room 1");
    zassert_within(frame.t_outdoor[0], 32.0f, 0.01f, "Outdoor 0");
    zassert_within(frame.t_outdoor[1], 32.5f, 0.01f, "Outdoor 1");

    /* HVAC unit mapping */
    zassert_equal(frame.pair_hvac_unit_map[0], 1, "Unit map 0");
    zassert_equal(frame.pair_hvac_unit_map[1], 1, "Unit map 1");
    zassert_equal(frame.pair_hvac_unit_map[2], 2, "Unit map 2");
    zassert_equal(frame.pair_hvac_unit_map[3], 2, "Unit map 3");
    zassert_equal(frame.pair_hvac_unit_map[4], 3, "Unit map 4");
    zassert_equal(frame.pair_hvac_unit_map[5], 3, "Unit map 5");

    /* Humidity */
    zassert_true(frame.humidity_return_valid, "Humidity must be valid");
    zassert_within(frame.humidity_return_pct, 50.0f, 0.5f, "Humidity around 50%");
}

ZTEST(sensor_matrix_mapper_suite, test_pairing_consensus_both_probes_required)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);

    /* Open supply probe of pair 2 (CWT CH3 -> idx 2) */
    raw.cwt_open_probe_mask |= (1U << 2);

    thermal_matrix_frame_t frame;
    int err = sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_equal(err, 0, "Mapping returned error");

    /* Pair 2 must NOT be valid, and open probe bit 2 must be set */
    uint16_t expected_valid = 0x003F & ~(1U << 2);
    zassert_equal(frame.pair_valid_mask, expected_valid, "Pair 2 must be invalid in pair_valid_mask");
    zassert_true((frame.pair_open_probe_mask & (1U << 2)) != 0, "Pair 2 open probe bit must be set");

    /* Open return probe of pair 4 (CWT CH11 -> idx 10) */
    raw.cwt_open_probe_mask |= (1U << 10);

    err = sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_equal(err, 0, "Mapping returned error");

    expected_valid &= ~(1U << 4);
    zassert_equal(frame.pair_valid_mask, expected_valid, "Pair 2 and 4 must be invalid");
    zassert_true((frame.pair_open_probe_mask & (1U << 4)) != 0, "Pair 4 open probe bit must be set");
}

ZTEST(sensor_matrix_mapper_suite, test_modbus_comm_loss_masks_all_cwt_pairs)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);
    raw.cwt_comm_ok = false; /* Modbus query failed */

    thermal_matrix_frame_t frame;
    int err = sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_equal(err, 0, "Mapping returned error");

    /* All 6 pairs invalid, all 6 marked open */
    zassert_equal(frame.pair_valid_mask, 0x0000, "All pairs invalid on comm loss");
    zassert_equal(frame.pair_open_probe_mask, 0x003F, "All pairs open on comm loss");
    zassert_equal(frame.room_valid_mask, 0x00, "Room probes invalid on comm loss");
    zassert_equal(frame.outdoor_valid_mask, 0x00, "Outdoor probes invalid on comm loss");

    /* Humidity on J9 should still be valid because J9 is independent of Modbus! */
    zassert_true(frame.humidity_return_valid, "J9 Humidity independent of Modbus comms");
}

ZTEST(sensor_matrix_mapper_suite, test_temperature_boundary_violation)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);

    /* CWT CH1 reports -25 C (< -20.0 C minimum limit) */
    raw.cwt_temperatures_c[0] = -25.0f;
    /* CWT CH14 reports +85 C (> +80.0 C maximum limit) */
    raw.cwt_temperatures_c[13] = 85.0f;

    thermal_matrix_frame_t frame;
    sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);

    /* Pair 0 must be invalidated and flagged open */
    zassert_false((frame.pair_valid_mask & (1U << 0)), "Pair 0 must be invalid");
    zassert_true((frame.pair_open_probe_mask & (1U << 0)), "Pair 0 flagged open");

    /* Room 1 (CH14) must be invalidated */
    zassert_false((frame.room_valid_mask & (1U << 1)), "Room 1 must be invalid");
    zassert_true((frame.room_valid_mask & (1U << 0)), "Room 0 must still be valid");
}

ZTEST(sensor_matrix_mapper_suite, test_calibration_offset_and_gain_application)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    /* Configure 2-point calibration on Pair 0:
     * Supply (CWT CH1): offset +1.50 C (+150 centi-C), gain unity
     * Return (CWT CH7): offset -0.75 C (-75 centi-C), gain +2.0% (10200)
     */
    configs[0].cal_offset_c_centi = 150;
    configs[0].cal_gain_ppm = 0;

    configs[6].cal_offset_c_centi = -75;
    configs[6].cal_gain_ppm = 10200;

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);
    raw.cwt_temperatures_c[0] = 20.0f; /* Supply raw */
    raw.cwt_temperatures_c[6] = 25.0f; /* Return raw */

    thermal_matrix_frame_t frame;
    sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);

    /* Expected Supply: 20.0 * 1.0 + 1.50 = 21.50 C */
    zassert_within(frame.t_inj[0], 21.50f, 0.01f, "Calibrated supply offset mismatch");

    /* Expected Return: 25.0 * 1.02 + (-0.75) = 25.50 - 0.75 = 24.75 C */
    zassert_within(frame.t_ret[0], 24.75f, 0.01f, "Calibrated return gain+offset mismatch");
}

ZTEST(sensor_matrix_mapper_suite, test_field_swap_cwt_to_internal_rtd)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    /* SIMULATION: CWT CH1 burned out in field.
     * Technician disables CWT CH1, moves wire to INTERNAL_RTD_1 (PT100 with INA826 on Placa V3)
     * and sets its role to SENSOR_ROLE_TEMP_SUPPLY_AIR for Pair 0, Unit 1.
     */
    configs[0].role = SENSOR_ROLE_DISABLED;

    configs[16].physical_ch = (uint8_t)PHYS_CH_INTERNAL_RTD_1;
    configs[16].role = (uint8_t)SENSOR_ROLE_TEMP_SUPPLY_AIR;
    configs[16].target_index = 0;
    configs[16].associated_hvac_unit = 1;
    configs[16].cal_offset_c_centi = 50; /* +0.50 C calibration */
    configs[16].cal_gain_ppm = 0;

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);

    /* Simulate ADC reading for PT100 at ~20.0 C:
     * At 20 C, PT100 R = 100 * (1 + 3.9083e-3 * 20 - 5.775e-7 * 400) = 107.79 Ohm.
     * With BrightC V3 circuit defaults:
     * a = (R / (R + Rref)) * Vref = (107.79 / (107.79 + 5100)) * 3.3 = 0.068305 V
     * adc_counts = ((a - Vbias) * Gain / Vref) * AdcMax
     * = ((0.068305 - 0.0579) * 80.8061 / 3.3) * 65535 = 16700.0 counts
     */
    float simulated_adc = 16700.0f;
    raw.internal_rtd_raw_adc[0] = simulated_adc;
    raw.internal_rtd_valid_mask = 0x01;

    thermal_matrix_frame_t frame;
    int err = sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_equal(err, 0, "Mapping returned error");

    /* Pair 0 must be valid and healthy with the swapped internal RTD */
    zassert_true((frame.pair_valid_mask & (1U << 0)) != 0, "Pair 0 must be valid with internal RTD");
    zassert_false((frame.pair_open_probe_mask & (1U << 0)) != 0, "Pair 0 not open");

    /* Verify temperature converted via Callendar-Van Dusen + 0.50 C offset */
    float expected_temp = 0.0f;
    transducer_rtd_adc_to_celsius(NULL, simulated_adc, &expected_temp);
    expected_temp += 0.50f;

    zassert_within(frame.t_inj[0], expected_temp, 0.05f, "Hot-swapped RTD reading mismatch");
}

ZTEST(sensor_matrix_mapper_suite, test_internal_rtd_open_probe_saturation)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    /* Move Pair 0 Supply to INTERNAL_RTD_1 */
    configs[0].role = SENSOR_ROLE_DISABLED;
    configs[16].physical_ch = (uint8_t)PHYS_CH_INTERNAL_RTD_1;
    configs[16].role = (uint8_t)SENSOR_ROLE_TEMP_SUPPLY_AIR;
    configs[16].target_index = 0;
    configs[16].associated_hvac_unit = 1;

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);

    /* Saturated ADC counts (wire disconnected / pull-up saturation) */
    raw.internal_rtd_raw_adc[0] = 65535.0f;
    raw.internal_rtd_valid_mask = 0x01;

    thermal_matrix_frame_t frame;
    sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);

    /* Pair 0 must be invalid and flagged open */
    zassert_false((frame.pair_valid_mask & (1U << 0)), "Pair 0 must not be valid");
    zassert_true((frame.pair_open_probe_mask & (1U << 0)), "Pair 0 must be flagged open");
}

ZTEST(sensor_matrix_mapper_suite, test_j9_humidity_4_20ma_linearization_and_namur_thresholds)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX];
    sensor_matrix_init_default_pilot_config(configs, SENSOR_PHYS_CH_MAX);

    current_loop_circuit_cfg_t circ_cfg;
    transducer_current_loop_get_brightc_v3_default_config(&circ_cfg);
    float denom = circ_cfg.shunt_ohms * circ_cfg.gain * circ_cfg.adc_max;
    float denom_factor = denom / (1000.0f * circ_cfg.scale_factor * circ_cfg.v_ref);

    raw_sensor_readings_t raw;
    setup_nominal_pilot_raw(&raw);
    thermal_matrix_frame_t frame;

    /* Test 4.0 mA -> 0% HR */
    raw.analog_j9_raw_adc[0] = 4.0f * denom_factor;
    sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_true(frame.humidity_return_valid, "4mA is valid");
    zassert_within(frame.humidity_return_pct, 0.0f, 0.1f, "4mA -> 0%% HR");

    /* Test 20.0 mA -> 100% HR */
    raw.analog_j9_raw_adc[0] = 20.0f * denom_factor;
    sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_true(frame.humidity_return_valid, "20mA is valid");
    zassert_within(frame.humidity_return_pct, 100.0f, 0.1f, "20mA -> 100%% HR");

    /* Test Wire break (< 3.5 mA: e.g. 2.0 mA) -> invalid */
    raw.analog_j9_raw_adc[0] = 2.0f * denom_factor;
    sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_false(frame.humidity_return_valid, "2mA wire break must be invalid");
    zassert_equal(frame.humidity_return_pct, 0.0f, "Invalid humidity forced to 0");

    /* Test Short circuit (> 21.0 mA: e.g. 23.0 mA) -> invalid */
    raw.analog_j9_raw_adc[0] = 23.0f * denom_factor;
    sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, &frame);
    zassert_false(frame.humidity_return_valid, "23mA short circuit must be invalid");
}

ZTEST(sensor_matrix_mapper_suite, test_null_and_invalid_arguments)
{
    sensor_channel_cfg_t configs[SENSOR_PHYS_CH_MAX] = {0};
    raw_sensor_readings_t raw = {0};
    thermal_matrix_frame_t frame = {0};

    zassert_equal(sensor_matrix_map(NULL, configs, SENSOR_PHYS_CH_MAX, &frame), -EINVAL, "Null raw");
    zassert_equal(sensor_matrix_map(&raw, NULL, SENSOR_PHYS_CH_MAX, &frame), -EINVAL, "Null configs");
    zassert_equal(sensor_matrix_map(&raw, configs, SENSOR_PHYS_CH_MAX, NULL), -EINVAL, "Null frame");
}
