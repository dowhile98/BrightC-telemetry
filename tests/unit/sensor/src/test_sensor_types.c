/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for sensor types, calibration helpers and abstract port validation.
 */

#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

#include "common/sensor_types.h"
#include "interfaces/i_sensor_matrix.h"

ZTEST_SUITE(sensor_types_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(sensor_types_suite, test_physical_channel_enumeration_and_validity)
{
    zassert_equal(PHYS_CH_CWT_1, 0, "CWT CH1 must be 0");
    zassert_equal(PHYS_CH_CWT_16, 15, "CWT CH16 must be 15");
    zassert_equal(PHYS_CH_INTERNAL_RTD_1, 16, "RTD CH1 must be 16");
    zassert_equal(PHYS_CH_INTERNAL_RTD_5, 20, "RTD CH5 must be 20");
    zassert_equal(PHYS_CH_ANALOG_J9_CH1, 21, "J9 CH1 must be 21");
    zassert_equal(PHYS_CH_ANALOG_J9_CH10, 30, "J9 CH10 must be 30");

    /* Extended CWT Modbus Concentrators */
    zassert_equal(PHYS_CH_CWT2_1, 31, "CWT2 CH1 must be 31");
    zassert_equal(PHYS_CH_CWT2_16, 46, "CWT2 CH16 must be 46");
    zassert_equal(PHYS_CH_CWT3_1, 47, "CWT3 CH1 must be 47");
    zassert_equal(PHYS_CH_CWT3_16, 62, "CWT3 CH16 must be 62");
    zassert_equal(PHYS_CH_CWT4_1, 63, "CWT4 CH1 must be 63");
    zassert_equal(PHYS_CH_CWT4_16, 78, "CWT4 CH16 must be 78");
    zassert_equal(PHYS_CH_MAX, 79, "Total physical channels must be 79");
    zassert_equal(SENSOR_PHYS_CH_MAX, 79, "SENSOR_PHYS_CH_MAX must be 79");

    for (int i = 0; i < 79; i++) {
        zassert_true(physical_channel_is_valid((physical_channel_id_t)i), "Channel %d should be valid", i);
    }

    zassert_false(physical_channel_is_valid(PHYS_CH_MAX), "PHYS_CH_MAX must not be valid");
    zassert_false(physical_channel_is_valid((physical_channel_id_t)79), "79 must not be valid");
    zassert_false(physical_channel_is_valid((physical_channel_id_t)80), "80 must not be valid");
    zassert_false(physical_channel_is_valid((physical_channel_id_t)255), "255 must not be valid");

    /* Helper physical_channel_is_cwt() validation */
    uint8_t conc = 0xFF;
    uint8_t ch = 0xFF;

    /* CWT Concentrator 1 (0..15) */
    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT_1, &conc, &ch), "CWT_1 is CWT");
    zassert_equal(conc, 0, "CWT_1 conc is 0");
    zassert_equal(ch, 0, "CWT_1 ch is 0");

    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT_16, &conc, &ch), "CWT_16 is CWT");
    zassert_equal(conc, 0, "CWT_16 conc is 0");
    zassert_equal(ch, 15, "CWT_16 ch is 15");

    /* Internal RTD and J9 are NOT CWT */
    zassert_false(physical_channel_is_cwt(PHYS_CH_INTERNAL_RTD_1, &conc, &ch), "RTD 1 is not CWT");
    zassert_false(physical_channel_is_cwt(PHYS_CH_INTERNAL_RTD_5, &conc, &ch), "RTD 5 is not CWT");
    zassert_false(physical_channel_is_cwt(PHYS_CH_ANALOG_J9_CH1, &conc, &ch), "J9 CH1 is not CWT");
    zassert_false(physical_channel_is_cwt(PHYS_CH_ANALOG_J9_CH10, &conc, &ch), "J9 CH10 is not CWT");

    /* CWT Concentrator 2 (31..46) */
    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT2_1, &conc, &ch), "CWT2_1 is CWT");
    zassert_equal(conc, 1, "CWT2_1 conc is 1");
    zassert_equal(ch, 0, "CWT2_1 ch is 0");

    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT2_16, &conc, &ch), "CWT2_16 is CWT");
    zassert_equal(conc, 1, "CWT2_16 conc is 1");
    zassert_equal(ch, 15, "CWT2_16 ch is 15");

    /* CWT Concentrator 3 (47..62) */
    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT3_1, &conc, &ch), "CWT3_1 is CWT");
    zassert_equal(conc, 2, "CWT3_1 conc is 2");
    zassert_equal(ch, 0, "CWT3_1 ch is 0");

    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT3_16, &conc, &ch), "CWT3_16 is CWT");
    zassert_equal(conc, 2, "CWT3_16 conc is 2");
    zassert_equal(ch, 15, "CWT3_16 ch is 15");

    /* CWT Concentrator 4 (63..78) */
    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT4_1, &conc, &ch), "CWT4_1 is CWT");
    zassert_equal(conc, 3, "CWT4_1 conc is 3");
    zassert_equal(ch, 0, "CWT4_1 ch is 0");

    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT4_16, &conc, &ch), "CWT4_16 is CWT");
    zassert_equal(conc, 3, "CWT4_16 conc is 3");
    zassert_equal(ch, 15, "CWT4_16 ch is 15");

    /* Out of bounds / NULL pointers */
    zassert_false(physical_channel_is_cwt(PHYS_CH_MAX, &conc, &ch), "PHYS_CH_MAX is not CWT");
    zassert_false(physical_channel_is_cwt((physical_channel_id_t)100, NULL, NULL), "100 is not CWT");
    zassert_true(physical_channel_is_cwt(PHYS_CH_CWT_1, NULL, NULL), "NULL output ptrs handled safely");
}

ZTEST(sensor_types_suite, test_sensor_role_enumeration_and_validity)
{
    zassert_equal(SENSOR_ROLE_DISABLED, 0, "Role disabled must be 0");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_DISABLED), "Disabled role valid");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_TEMP_SUPPLY_AIR), "Supply air valid");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_TEMP_RETURN_AIR), "Return air valid");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_TEMP_ROOM_AMBIENT), "Room ambient valid");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_TEMP_OUTDOOR_AMBIENT), "Outdoor ambient valid");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_HUMIDITY_RETURN), "Humidity return valid");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_PRESSURE_REFRIGERANT), "Pressure refrigerant valid");
    zassert_true(sensor_role_is_valid(SENSOR_ROLE_PRESSURE_AIR_FILTER), "Pressure filter valid");

    zassert_false(sensor_role_is_valid(SENSOR_ROLE_MAX), "SENSOR_ROLE_MAX must not be valid");
    zassert_false(sensor_role_is_valid((sensor_role_t)99), "99 must not be valid");
}

ZTEST(sensor_types_suite, test_channel_descriptor_packed_size)
{
    /* Rule 4.2 in architecture: Channel descriptor is packed into exactly 8 bytes for EEPROM persistence */
    zassert_equal(sizeof(sensor_channel_cfg_t), 8, "sensor_channel_cfg_t must be exactly 8 bytes packed");
}

ZTEST(sensor_types_suite, test_calibration_helpers_offset)
{
    zassert_within(sensor_cal_offset_to_c(0), 0.0f, 0.001f, "Zero offset");
    zassert_within(sensor_cal_offset_to_c(125), 1.25f, 0.001f, "+1.25 C");
    zassert_within(sensor_cal_offset_to_c(-250), -2.50f, 0.001f, "-2.50 C");

    zassert_equal(sensor_cal_offset_from_c(0.0f), 0, "Roundtrip 0.0");
    zassert_equal(sensor_cal_offset_from_c(1.25f), 125, "Roundtrip 1.25");
    zassert_equal(sensor_cal_offset_from_c(-2.50f), -250, "Roundtrip -2.50");
}

ZTEST(sensor_types_suite, test_calibration_helpers_gain)
{
    /* 0 and 10000 both evaluate to nominal 1.0f multiplier */
    zassert_within(sensor_cal_gain_to_factor(0), 1.0f, 0.0001f, "Zero gain means 1.0");
    zassert_within(sensor_cal_gain_to_factor(SENSOR_CAL_GAIN_UNITY), 1.0f, 0.0001f, "10000 gain means 1.0");

    /* 10500 = +5% gain */
    zassert_within(sensor_cal_gain_to_factor(10500), 1.05f, 0.0001f, "10500 gain means 1.05");

    /* 9500 = -5% gain */
    zassert_within(sensor_cal_gain_to_factor(9500), 0.95f, 0.0001f, "9500 gain means 0.95");

    zassert_equal(sensor_cal_gain_from_factor(1.0f), 10000, "Factor 1.0f -> 10000");
    zassert_equal(sensor_cal_gain_from_factor(1.05f), 10500, "Factor 1.05f -> 10500");
}

ZTEST(sensor_types_suite, test_sensor_matrix_port_wrapper_validation)
{
    i_sensor_matrix_t unbound_port = {0};
    thermal_matrix_frame_t frame;
    raw_sensor_readings_t raw;
    sensor_channel_cfg_t cfg;

    zassert_false(i_sensor_matrix_is_bound(&unbound_port), "Unbound port check");
    zassert_false(i_sensor_matrix_is_bound(NULL), "Null port check");

    /* Unbound port calls return -EINVAL */
    zassert_equal(i_sensor_matrix_get_frame(&unbound_port, &frame), -EINVAL, "Unbound get_frame");
    zassert_equal(i_sensor_matrix_get_raw_readings(&unbound_port, &raw), -EINVAL, "Unbound get_raw");
    zassert_equal(i_sensor_matrix_set_channel_config(&unbound_port, PHYS_CH_CWT_1, &cfg), -EINVAL, "Unbound set_cfg");
    zassert_equal(i_sensor_matrix_get_channel_config(&unbound_port, PHYS_CH_CWT_1, &cfg), -EINVAL, "Unbound get_cfg");
    zassert_equal(i_sensor_matrix_update(&unbound_port), -EINVAL, "Unbound update");

    /* Bound port with null arguments returns -EINVAL */
    int dummy_state = 1;
    i_sensor_matrix_api_t dummy_api = {0};
    i_sensor_matrix_t bound_port = {
        .api = &dummy_api,
        .impl = &dummy_state,
    };

    zassert_true(i_sensor_matrix_is_bound(&bound_port), "Bound port check");
    zassert_equal(i_sensor_matrix_get_frame(&bound_port, NULL), -EINVAL, "Null out_frame");
    zassert_equal(i_sensor_matrix_get_raw_readings(&bound_port, NULL), -EINVAL, "Null out_raw");
    zassert_equal(i_sensor_matrix_set_channel_config(&bound_port, PHYS_CH_MAX, &cfg), -EINVAL, "Invalid channel");
    zassert_equal(i_sensor_matrix_set_channel_config(&bound_port, PHYS_CH_CWT_1, NULL), -EINVAL, "Null cfg");
    zassert_equal(i_sensor_matrix_get_channel_config(&bound_port, PHYS_CH_MAX, &cfg), -EINVAL, "Invalid channel");
    zassert_equal(i_sensor_matrix_get_channel_config(&bound_port, PHYS_CH_CWT_1, NULL), -EINVAL, "Null out_cfg");
}
