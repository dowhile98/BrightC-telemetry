/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file default_config_profiles.c
 * @brief Default Configuration and Storage Profiles compilation unit.
 *
 * Centralizes baseline configurations for all operational profiles (Hybrid, Control-only, Telemetry-only),
 * default Modbus bus/slave descriptors, and default network configurations.
 * Adheres strictly to Clean Architecture: pure ISO C99/C11 domain constants,
 * zero mutable file-scope variables, zero Zephyr dependencies.
 */

#include "common/default_config_profiles.h"

/* ========================================================================= */
/* Default HVAC Configuration: Hybrid Mode Baseline (4 Units, 2+2 Rotation) */
/* ========================================================================= */

const config_hvac_t k_default_hvac_config_hybrid_4units = {
    .operation_mode = (uint8_t)HVAC_MODE_HYBRID_FULL,
    .total_units = 4U,
    .total_groups = 1U,
    .fire_interlock = {
        .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO,
        .device_id = 1U,
        .channel = 4U, /* alm_in_17 on mcp23s17_1 pin 4 */
    },
    .bypass_switch = {
        .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO,
        .device_id = 0U,
        .channel = (uint16_t)ONCHIP_DI_SW_BYPASS, /* On-chip MCU GPIOD 11 */
    },
    .groups = {
        [0] = {
            .temp_min = 20.0f,
            .temp_max = 24.0f,
            .temp_alarm = 28.0f,
            .temp_bypass = 32.0f,
            .time_to_cool_s = 1800U,
            .delay_on_s = 10U,
            .fan_delay_s = 30U,
            .pressure_retry_s = 180U,
            .manual_timeout_s = 3600U,
            .rotation_interval_hours = 24U,
            .sensor_mask = 0x001FU, /* Bits 0..4: 5 on-board internal RTDs */
            .unit_mask = 0x0FU,     /* Units 0..3 */
            .active_turn_count = 2U, /* 2 active + 2 standby (2+2) */
            .max_allowed_faults = 3U,
            .enable_fan_delay = 1U,
            .hum_min = 40U,
            .hum_max = 70U,
        },
    },
    .units = {
        /* Unit 0 */
        [0] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 0U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 1U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 2U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 0U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 1U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 2U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 3U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 0U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 1U },
        },
        /* Unit 1 */
        [1] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 3U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 4U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 5U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 4U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 5U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 6U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 7U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 2U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 3U },
        },
        /* Unit 2 */
        [2] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 6U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 7U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 8U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 8U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 9U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 10U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = 11U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 4U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 5U },
        },
        /* Unit 3 */
        [3] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 9U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 10U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 11U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 1U, .channel = 0U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 1U, .channel = 1U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 1U, .channel = 2U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 1U, .channel = 3U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 6U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 7U },
        },
    },
};

/* ========================================================================= */
/* Default HVAC Configuration: Control-Only Mode (8 Units, 4+4 Rotation)     */
/* ========================================================================= */

const config_hvac_t k_default_hvac_config_control_only_8units = {
    .operation_mode = (uint8_t)HVAC_MODE_HVAC_CONTROL_ONLY,
    .total_units = 8U,
    .total_groups = 1U,
    .fire_interlock = {
        .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,
        .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI,
        .channel = 16U,
    },
    .bypass_switch = {
        .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,
        .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI,
        .channel = 17U,
    },
    .groups = {
        [0] = {
            .temp_min = 20.0f,
            .temp_max = 24.0f,
            .temp_alarm = 28.0f,
            .temp_bypass = 32.0f,
            .time_to_cool_s = 1800U,
            .delay_on_s = 10U,
            .fan_delay_s = 30U,
            .pressure_retry_s = 180U,
            .manual_timeout_s = 3600U,
            .rotation_interval_hours = 24U,
            .sensor_mask = 0x001FU,
            .unit_mask = 0xFFU,     /* Units 0..7 */
            .active_turn_count = 4U, /* 4 active + 4 standby (4+4) */
            .max_allowed_faults = 3U,
            .enable_fan_delay = 1U,
            .hum_min = 40U,
            .hum_max = 70U,
        },
    },
    .units = {
        /* Units 0..3: Internal relays on mcp23s17_2 */
        [0] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 0U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 1U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 2U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 0U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 1U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 2U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 3U },
        },
        [1] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 3U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 4U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 5U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 4U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 5U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 6U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 7U },
        },
        [2] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 6U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 7U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 8U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 8U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 9U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 10U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 11U },
        },
        [3] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 9U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 10U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 2U, .channel = 11U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 12U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 13U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 14U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 15U },
        },
        /* Units 4..7: Expanded units on Waveshare 32CH relay (channels 0..11) */
        [4] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 0U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 1U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 2U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 18U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 19U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 20U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 21U },
        },
        [5] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 3U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 4U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 5U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 22U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 23U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 24U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 25U },
        },
        [6] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 6U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 7U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 8U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 26U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 27U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 28U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 29U },
        },
        [7] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 9U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 10U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 11U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 30U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 31U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_NONE, .device_id = 0U, .channel = 0U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_NONE, .device_id = 0U, .channel = 0U },
        },
    },
};

/* ========================================================================= */
/* Default HVAC Configuration: Telemetry-Only Mode (0 Active Actuators)     */
/* ========================================================================= */

const config_hvac_t k_default_hvac_config_telemetry_only = {
    .operation_mode = (uint8_t)HVAC_MODE_TELEMETRY_ONLY,
    .total_units = 0U,
    .total_groups = 0U,
    .fire_interlock = {
        .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO,
        .device_id = 1U,
        .channel = 4U,
    },
    .bypass_switch = {
        .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO,
        .device_id = 0U,
        .channel = (uint16_t)ONCHIP_DI_SW_BYPASS,
    },
};

/* ========================================================================= */
/* Default Digital Input Channels: Hybrid Mode Baseline (34 Channels)       */
/* ========================================================================= */

const di_channel_config_t k_default_di_channels_hybrid[DEFAULT_DI_CHANNELS_HYBRID_COUNT] = {
    /* Inputs 1..12 (channels 0..11): mcp23s17_0 pins 0..11 (Units 0..2 alarms) */
    [0]  = { .role = DI_ROLE_UNIT_0_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 0U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [1]  = { .role = DI_ROLE_UNIT_0_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 1U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [2]  = { .role = DI_ROLE_UNIT_0_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 2U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [3]  = { .role = DI_ROLE_UNIT_0_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 3U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [4]  = { .role = DI_ROLE_UNIT_1_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 4U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [5]  = { .role = DI_ROLE_UNIT_1_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 5U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [6]  = { .role = DI_ROLE_UNIT_1_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 6U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [7]  = { .role = DI_ROLE_UNIT_1_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 7U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [8]  = { .role = DI_ROLE_UNIT_2_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 8U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [9]  = { .role = DI_ROLE_UNIT_2_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 9U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [10] = { .role = DI_ROLE_UNIT_2_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 10U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [11] = { .role = DI_ROLE_UNIT_2_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 0U, .pin_index = 11U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Inputs 13..24 (channels 12..23): mcp23s17_1 pins 0..11 */
    /* Unit 3 alarms (pins 0..3) */
    [12] = { .role = DI_ROLE_UNIT_3_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 0U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [13] = { .role = DI_ROLE_UNIT_3_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 1U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [14] = { .role = DI_ROLE_UNIT_3_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 2U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [15] = { .role = DI_ROLE_UNIT_3_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 3U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Input 17 (channel 16): Fire Alarm Interlock (alm_in_17, mcp23s17_1 pin 4) */
    [16] = { .role = DI_ROLE_SMOKE_FIRE,        .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 4U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Input 18 (channel 17): Door switch (alm_in_18, mcp23s17_1 pin 5) */
    [17] = { .role = DI_ROLE_DOOR_OPEN,         .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 5U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Inputs 19..24 (channels 18..23): Internal Aux / Telemetry inputs (alm_in_19..24, mcp23s17_1 pins 6..11) */
    [18] = { .role = DI_ROLE_USER_1,            .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 6U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [19] = { .role = DI_ROLE_USER_2,            .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 7U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [20] = { .role = DI_ROLE_USER_3,            .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 8U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [21] = { .role = DI_ROLE_USER_4,            .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 9U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [22] = { .role = DI_ROLE_WATER_LEAK,        .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 10U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [23] = { .role = DI_ROLE_SURGE_PROTECTOR,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = 1U, .pin_index = 11U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* On-chip MCU Inputs (user button GPIOC 13, bypass switch GPIOD 11) */
    [24] = { .role = DI_ROLE_USER_BUTTON,       .source_type = DI_SOURCE_ONCHIP_GPIO,  .source_dev_id = (uint8_t)ONCHIP_DI_USER_BUTTON, .pin_index = 0U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 30U,                   .enabled = true },
    [25] = { .role = DI_ROLE_BYPASS_SWITCH,     .source_type = DI_SOURCE_ONCHIP_GPIO,  .source_dev_id = (uint8_t)ONCHIP_DI_SW_BYPASS,   .pin_index = 0U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 50U,                   .enabled = true },

    /* External Modbus Expansion (MBSL32DI): Fan & Compressor Feedbacks */
    [26] = { .role = DI_ROLE_FAN_1_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 0U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [27] = { .role = DI_ROLE_COMPRESSOR_1_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 1U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [28] = { .role = DI_ROLE_FAN_2_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 2U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [29] = { .role = DI_ROLE_COMPRESSOR_2_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 3U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [30] = { .role = DI_ROLE_FAN_3_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 4U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [31] = { .role = DI_ROLE_COMPRESSOR_3_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 5U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [32] = { .role = DI_ROLE_FAN_4_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 6U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [33] = { .role = DI_ROLE_COMPRESSOR_4_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 7U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
};

/* ========================================================================= */
/* Default Digital Input Channels: Control-Only Mode (32 Channels, 8 Units)  */
/* ========================================================================= */

const di_channel_config_t k_default_di_channels_control_only[DEFAULT_DI_CHANNELS_CONTROL_ONLY_COUNT] = {
    /* Inputs 1..16 (channels 0..15): Units 0..3 alarms */
    [0]  = { .role = DI_ROLE_UNIT_0_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 0U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [1]  = { .role = DI_ROLE_UNIT_0_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 1U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [2]  = { .role = DI_ROLE_UNIT_0_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 2U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [3]  = { .role = DI_ROLE_UNIT_0_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 3U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [4]  = { .role = DI_ROLE_UNIT_1_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 4U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [5]  = { .role = DI_ROLE_UNIT_1_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 5U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [6]  = { .role = DI_ROLE_UNIT_1_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 6U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [7]  = { .role = DI_ROLE_UNIT_1_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 7U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [8]  = { .role = DI_ROLE_UNIT_2_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 8U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [9]  = { .role = DI_ROLE_UNIT_2_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 9U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [10] = { .role = DI_ROLE_UNIT_2_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 10U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [11] = { .role = DI_ROLE_UNIT_2_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 11U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [12] = { .role = DI_ROLE_UNIT_3_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 12U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [13] = { .role = DI_ROLE_UNIT_3_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 13U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [14] = { .role = DI_ROLE_UNIT_3_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 14U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [15] = { .role = DI_ROLE_UNIT_3_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 15U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Input 17 (channel 16): Fire Alarm Interlock */
    [16] = { .role = DI_ROLE_SMOKE_FIRE,        .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 16U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Input 18 (channel 17): Door switch */
    [17] = { .role = DI_ROLE_DOOR_OPEN,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 17U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Inputs 19..32 (channels 18..31): Alarms for Units 4..7 */
    [18] = { .role = DI_ROLE_UNIT_4_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 18U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [19] = { .role = DI_ROLE_UNIT_4_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 19U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [20] = { .role = DI_ROLE_UNIT_4_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 20U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [21] = { .role = DI_ROLE_UNIT_4_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 21U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [22] = { .role = DI_ROLE_UNIT_5_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 22U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [23] = { .role = DI_ROLE_UNIT_5_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 23U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [24] = { .role = DI_ROLE_UNIT_5_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 24U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [25] = { .role = DI_ROLE_UNIT_5_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 25U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [26] = { .role = DI_ROLE_UNIT_6_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 26U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [27] = { .role = DI_ROLE_UNIT_6_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 27U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [28] = { .role = DI_ROLE_UNIT_6_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 28U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [29] = { .role = DI_ROLE_UNIT_6_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 29U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [30] = { .role = DI_ROLE_UNIT_7_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 30U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [31] = { .role = DI_ROLE_UNIT_7_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 31U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
};

/* ========================================================================= */
/* Default Modbus Physical Bus Line Configuration                            */
/* ========================================================================= */

const modbus_bus_cfg_t k_default_modbus_bus_cfg[MODBUS_BUS_MAX] = {
    [MODBUS_BUS_RS485_1] = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
    [MODBUS_BUS_RS485_2] = {
        .baudrate = 9600U,
        .parity = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
};

/* ========================================================================= */
/* Default Modbus Slaves Table                                               */
/* ========================================================================= */

const modbus_slave_descriptor_t k_default_modbus_slaves[] = {
    {
        .device_id = 1U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 1U,
        .dev_type = MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
    {
        .device_id = 2U,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = 2U,
        .dev_type = MODBUS_DEV_TYPE_JSY_MK339,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
    {
        .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = DEFAULT_MODBUS_DEV_ID_MBSL32DI,
        .dev_type = MODBUS_DEV_TYPE_MBSL32DI,
        .poll_interval_ms = 1000U,
        .timeout_ms = 300U,
        .is_enabled = true,
    },
    {
        .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY,
        .bus_id = MODBUS_BUS_RS485_1,
        .slave_addr = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY,
        .dev_type = MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH,
        .poll_interval_ms = 1000U,
        .timeout_ms = DEFAULT_MODBUS_WAVESHARE_TIMEOUT_MS,
        .is_enabled = true,
    },
};

const size_t k_default_modbus_slaves_count =
    sizeof(k_default_modbus_slaves) / sizeof(k_default_modbus_slaves[0]);

/* ========================================================================= */
/* Default Network Configurations                                            */
/* ========================================================================= */

const net_eth_config_t k_default_net_eth_cfg = {
    .ip_mode = NET_IP_MODE_DHCP,
    .auto_dhcpv4 = 1U,
    .ip_addr = 0xC0A80164,     /* 192.168.1.100 */
    .subnet_mask = 0xFFFFFF00, /* 255.255.255.0 */
    .gateway = 0xC0A80101,     /* 192.168.1.1 */
};

const net_wifi_config_t k_default_net_wifi_cfg = {
    .mode = NET_WIFI_MODE_AP,
    .sta = {
        .ssid = "BrightC-Field",
        .psk = "brightc12345",
        .security = NET_WIFI_SEC_WPA2_PSK,
        .ip_mode = NET_IP_MODE_DHCP,
    },
    .ap = {
        .ssid = CONFIG_BRIGHTC_WIFI_DEFAULT_SSID,
        .psk = CONFIG_BRIGHTC_WIFI_DEFAULT_PSK,
        .security = NET_WIFI_SEC_WPA2_PSK,
        .channel = 9U,
        .max_clients = 5U,
        .ap_ip_addr = CONFIG_BRIGHTC_WIFI_DEFAULT_IP,
        .ap_subnet_mask = 0xFFFFFF00, /* 255.255.255.0 */
    },
};

/* ========================================================================= */
/* Public Policy & Profile Getters                                           */
/* ========================================================================= */

const config_hvac_t *default_config_get_hvac_profile(hvac_operation_mode_t mode)
{
    switch (mode) {
    case HVAC_MODE_HYBRID_FULL:
        return &k_default_hvac_config_hybrid_4units;
    case HVAC_MODE_HVAC_CONTROL_ONLY:
        return &k_default_hvac_config_control_only_8units;
    case HVAC_MODE_TELEMETRY_ONLY:
        return &k_default_hvac_config_telemetry_only;
    default:
        return NULL;
    }
}

const di_channel_config_t *default_config_get_di_profile(hvac_operation_mode_t mode,
                                                        size_t *out_count)
{
    if (mode == HVAC_MODE_HVAC_CONTROL_ONLY) {
        if (out_count != NULL) {
            *out_count = DEFAULT_DI_CHANNELS_CONTROL_ONLY_COUNT;
        }
        return k_default_di_channels_control_only;
    }

    /* Hybrid and Telemetry-Only use hybrid mapping */
    if (out_count != NULL) {
        *out_count = DEFAULT_DI_CHANNELS_HYBRID_COUNT;
    }
    return k_default_di_channels_hybrid;
}

const modbus_bus_cfg_t *default_config_get_modbus_bus_cfg(uint8_t bus_id)
{
    if (bus_id >= (uint8_t)MODBUS_BUS_MAX) {
        return NULL;
    }
    return &k_default_modbus_bus_cfg[bus_id];
}

const modbus_slave_descriptor_t *default_config_get_modbus_slaves(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = k_default_modbus_slaves_count;
    }
    return k_default_modbus_slaves;
}

const net_eth_config_t *default_config_get_net_eth_cfg(void)
{
    return &k_default_net_eth_cfg;
}

const net_wifi_config_t *default_config_get_net_wifi_cfg(void)
{
    return &k_default_net_wifi_cfg;
}

/* ========================================================================= */
/* Default Configurations for Device, Calibration, Sensor, and Baseline */
/* ========================================================================= */

const config_device_t k_default_device_cfg = {
    .serial_number = "BRIGHTC-V3-0001",
    .hardware_rev = "REV_F",
    .device_alias = "BrightC-Telemetry-Controller",
};

const config_calibration_t k_default_calibration_cfg = {
    .temp_offsets_x10 = {0},
    .analog_offsets = {0},
    .analog_gains = {
        SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY,
        SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY,
        SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY,
        SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY, SENSOR_CAL_GAIN_UNITY,
    },
};

const config_sensor_cfg_t k_default_sensor_cfg = {
    .count = 0U,
    .channels = {{0}},
};

const config_baseline_t k_default_baseline_cfg = {
    .beta = {0.0, 0.0, 0.0, 0.0},
    .eta_base = 1.0,
    .delta_t_base = {0.0f},
    .t_inj_base = {0.0f},
};

const config_device_t *default_config_get_device_cfg(void)
{
    return &k_default_device_cfg;
}

const config_calibration_t *default_config_get_calibration_cfg(void)
{
    return &k_default_calibration_cfg;
}

const config_sensor_cfg_t *default_config_get_sensor_cfg(void)
{
    return &k_default_sensor_cfg;
}

const config_baseline_t *default_config_get_baseline_cfg(void)
{
    return &k_default_baseline_cfg;
}

