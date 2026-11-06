/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file default_config_profiles.h
 * @brief Default Configuration and Storage Profiles for HVAC Control and Telemetry.
 *
 * Centralizes baseline configurations for Hybrid and Control-Only operating modes.
 * Clean Architecture (§4.1.D): pure ISO C99/C11, zero Zephyr headers,
 * zero mutable file-scope variables, zero dynamic memory allocations.
 */

#ifndef APP_COMMON_DEFAULT_CONFIG_PROFILES_H_
#define APP_COMMON_DEFAULT_CONFIG_PROFILES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/config_types.h"
#include "common/digital_input_types.h"
#include "common/hvac_types.h"
#include "common/sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Hardware Device Addressing Reference Constants                            */
/* ========================================================================= */

#define DEFAULT_MODBUS_DEV_ID_MBSL32DI        3U /**< Slave ID / Dev ID for 32-ch Digital Input */
#define DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY 4U /**< Slave ID / Dev ID for 32-ch Relay Module */

/* ========================================================================= */
/* Profile Selection Enumeration                                             */
/* ========================================================================= */

typedef enum {
    HVAC_PROFILE_HYBRID_4UNITS = 0,       /**< Hybrid Mode: 4 Units (2+2) + Telemetry */
    HVAC_PROFILE_CONTROL_ONLY_8UNITS = 1, /**< Control-Only: 8 Units (4+4) */
    HVAC_PROFILE_MAX
} hvac_profile_id_t;

/* ========================================================================= */
/* Default HVAC Configuration: Hybrid Mode Baseline (4 Units, 2+2 Rotation) */
/* ========================================================================= */

static const config_hvac_t k_default_hvac_config_hybrid_4units __attribute__((unused)) = {
    .operation_mode = (uint8_t)HVAC_MODE_HYBRID_FULL,
    .total_units = 4U,
    .total_groups = 1U,
    .fire_interlock = {
        .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,
        .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI,
        .channel = 16U, /* Input 17 */
    },
    .bypass_switch = {
        .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,
        .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI,
        .channel = 17U, /* Input 18 */
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
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 0U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 1U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 2U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 0U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 1U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 2U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 3U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 18U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 19U },
        },
        /* Unit 1 */
        [1] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 3U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 4U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 5U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 4U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 5U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 6U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 7U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 20U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 21U },
        },
        /* Unit 2 */
        [2] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 6U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 7U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 8U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 8U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 9U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 10U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 11U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 22U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 23U },
        },
        /* Unit 3 */
        [3] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 9U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 10U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 11U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 12U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 13U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 14U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 15U },
            .fb_fan        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 24U },
            .fb_compressor = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 25U },
        },
    },
};

/* ========================================================================= */
/* Default HVAC Configuration: Control-Only Mode (8 Units, 4+4 Rotation)     */
/* ========================================================================= */

static const config_hvac_t k_default_hvac_config_control_only_8units __attribute__((unused)) = {
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
        /* Units 0..3: Same relays and alarms as hybrid */
        [0] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 0U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 1U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 2U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 0U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 1U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 2U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 3U },
        },
        [1] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 3U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 4U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 5U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 4U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 5U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 6U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 7U },
        },
        [2] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 6U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 7U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 8U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 8U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 9U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 10U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 11U },
        },
        [3] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 9U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 10U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 11U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 12U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 13U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 14U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 15U },
        },
        /* Units 4..7: Channels 18..31 allocated for alarms */
        [4] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 12U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 13U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 14U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 18U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 19U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 20U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 21U },
        },
        [5] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 15U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 16U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 17U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 22U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 23U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 24U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 25U },
        },
        [6] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 18U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 19U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 20U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 26U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 27U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 28U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 29U },
        },
        [7] = {
            .relay_aa = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 21U },
            .relay_sm = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 22U },
            .relay_g  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY, .channel = 23U },
            .alarm_hp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 30U },
            .alarm_lp   = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS, .device_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .channel = 31U },
            .alarm_flow = { .backend = (uint8_t)HVAC_IO_BACKEND_NONE, .device_id = 0U, .channel = 0U },
            .alarm_volt = { .backend = (uint8_t)HVAC_IO_BACKEND_NONE, .device_id = 0U, .channel = 0U },
        },
    },
};

/* ========================================================================= */
/* Default Digital Input Channels: Hybrid Mode Baseline (32 Channels)       */
/* ========================================================================= */

static const di_channel_config_t k_default_di_channels_hybrid[32] __attribute__((unused)) = {
    /* Inputs 1..16 (channels 0..15): 4 AC units x 4 alarms */
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

    /* Inputs 19..26 (channels 18..25): Fan & Compressor Feedback */
    [18] = { .role = DI_ROLE_FAN_1_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 18U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [19] = { .role = DI_ROLE_COMPRESSOR_1_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 19U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [20] = { .role = DI_ROLE_FAN_2_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 20U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [21] = { .role = DI_ROLE_COMPRESSOR_2_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 21U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [22] = { .role = DI_ROLE_FAN_3_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 22U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [23] = { .role = DI_ROLE_COMPRESSOR_3_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 23U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [24] = { .role = DI_ROLE_FAN_4_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 24U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },
    [25] = { .role = DI_ROLE_COMPRESSOR_4_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 25U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_CONTACTOR_MS, .enabled = true },

    /* Inputs 27..32 (channels 26..31): Reserve / Auxiliary inputs */
    [26] = { .role = DI_ROLE_USER_1,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 26U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [27] = { .role = DI_ROLE_USER_2,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 27U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [28] = { .role = DI_ROLE_USER_3,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 28U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [29] = { .role = DI_ROLE_USER_4,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 29U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    [30] = { .role = DI_ROLE_USER_BUTTON,       .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 30U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 30U,                   .enabled = true },
    [31] = { .role = DI_ROLE_BYPASS_SWITCH,     .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = DEFAULT_MODBUS_DEV_ID_MBSL32DI, .pin_index = 31U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 50U,                   .enabled = true },
};

/* ========================================================================= */
/* Default Digital Input Channels: Control-Only Mode (32 Channels, 8 Units)  */
/* ========================================================================= */

static const di_channel_config_t k_default_di_channels_control_only[32] __attribute__((unused)) = {
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
/* Pure Profile Helper Functions                                             */
/* ========================================================================= */

/**
 * @brief Retrieve HVAC configuration structure for an operation mode / profile.
 *
 * @param mode Operation mode (e.g. HVAC_MODE_HYBRID_FULL, HVAC_MODE_HVAC_CONTROL_ONLY).
 * @return Pointer to immutable default configuration structure.
 */
static inline const config_hvac_t *default_config_get_hvac_profile(hvac_operation_mode_t mode)
{
    switch (mode) {
    case HVAC_MODE_HYBRID_FULL:
        return &k_default_hvac_config_hybrid_4units;
    case HVAC_MODE_HVAC_CONTROL_ONLY:
        return &k_default_hvac_config_control_only_8units;
    default:
        return NULL;
    }
}

/**
 * @brief Retrieve Digital Input table for an operation mode / profile.
 *
 * @param mode      Operation mode.
 * @param out_count Optional pointer receiving number of channels (32).
 * @return Pointer to immutable default channel configuration array.
 */
static inline const di_channel_config_t *default_config_get_di_profile(hvac_operation_mode_t mode,
                                                                      size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = 32U;
    }
    if (mode == HVAC_MODE_HVAC_CONTROL_ONLY) {
        return k_default_di_channels_control_only;
    }
    return k_default_di_channels_hybrid;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_DEFAULT_CONFIG_PROFILES_H_ */
