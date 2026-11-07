/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file hardware_pin_map.h
 * @brief Declarative compile-time pinout and terminal block mapping for BrightC V3.
 *
 * Establishes immutable, deterministic hardware declarations matching the physical
 * board silkscreen and devicetree (boards/daecge/brightc-controllerv3/brightc-controllerv3.dts).
 * Pure ISO C99/C11: zero Zephyr headers, zero dynamic memory allocations.
 */

#ifndef APP_COMMON_HARDWARE_PIN_MAP_H_
#define APP_COMMON_HARDWARE_PIN_MAP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/digital_input_types.h"
#include "common/hvac_types.h"
#include "common/modbus_device_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* On-Board MCP23S17 SPI Expander Indices                                    */
/* ========================================================================= */

#define HARDWARE_MCP23S17_IDX_ALM_IN_0_11     0U /**< &mcp23s17_0: Digital Alarms 1..12 */
#define HARDWARE_MCP23S17_IDX_ALM_IN_12_23    1U /**< &mcp23s17_1: Digital Alarms 13..24 */
#define HARDWARE_MCP23S17_IDX_HVAC_RELAYS     2U /**< &mcp23s17_2: Local HVAC Relays (AA, SM, G) */
#define HARDWARE_MCP23S17_IDX_ALARM_RELAYS    3U /**< &mcp23s17_3: Alarm Output Relays 1..8 */

/* ========================================================================= */
/* Total Channel Constants                                                   */
/* ========================================================================= */

#define HARDWARE_LOCAL_DI_COUNT               24U
#define HARDWARE_ONCHIP_DI_COUNT              2U
#define HARDWARE_REMOTE_DI_COUNT              32U
#define HARDWARE_SYSTEM_DI_COUNT              (HARDWARE_LOCAL_DI_COUNT + HARDWARE_ONCHIP_DI_COUNT + HARDWARE_REMOTE_DI_COUNT) /**< 58 channels */
#define HARDWARE_TOTAL_DI_COUNT               HARDWARE_SYSTEM_DI_COUNT

#define HARDWARE_LOCAL_HVAC_UNITS_COUNT       4U
#define HARDWARE_REMOTE_HVAC_UNITS_COUNT      4U
#define HARDWARE_TOTAL_HVAC_UNITS_COUNT       (HARDWARE_LOCAL_HVAC_UNITS_COUNT + HARDWARE_REMOTE_HVAC_UNITS_COUNT) /**< 8 units */

/* ========================================================================= */
/* Local On-Board Digital Inputs Table (Placa V3 Optocouplers)               */
/* ========================================================================= */

/**
 * @brief Immutable channel configuration for the 24 local on-board digital inputs.
 *
 * Mapped 1:1 with devicetree nodes alm_in_1..24 on &mcp23s17_0 and &mcp23s17_1.
 */
static const di_channel_config_t k_brightc_local_di_table[HARDWARE_LOCAL_DI_COUNT] = {
    /* --- Unit 1 Alarms (mcp23s17_0, pins 0..3) --- */
    {
        .role          = DI_ROLE_UNIT_0_ALARM_HP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 0U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_0_ALARM_LP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 1U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_0_ALARM_FLOW,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 2U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_0_ALARM_VOLT,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 3U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },

    /* --- Unit 2 Alarms (mcp23s17_0, pins 4..7) --- */
    {
        .role          = DI_ROLE_UNIT_1_ALARM_HP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 4U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_1_ALARM_LP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 5U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_1_ALARM_FLOW,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 6U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_1_ALARM_VOLT,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 7U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },

    /* --- Unit 3 Alarms (mcp23s17_0, pins 8..11) --- */
    {
        .role          = DI_ROLE_UNIT_2_ALARM_HP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 8U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_2_ALARM_LP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 9U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_2_ALARM_FLOW,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 10U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_2_ALARM_VOLT,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,
        .pin_index     = 11U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },

    /* --- Unit 4 Alarms (mcp23s17_1, pins 0..3) --- */
    {
        .role          = DI_ROLE_UNIT_3_ALARM_HP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 0U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_3_ALARM_LP,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 1U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_3_ALARM_FLOW,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 2U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_UNIT_3_ALARM_VOLT,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 3U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },

    /* --- Facility & Environmental Safety Alarms (mcp23s17_1, pins 4..8) --- */
    {
        .role          = DI_ROLE_SMOKE_FIRE,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 4U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_DOOR_OPEN,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 5U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_WATER_LEAK,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 6U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_SURGE_PROTECTOR,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 7U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_PHASE_MONITOR,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 8U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },

    /* --- Spare Local Digital Inputs (mcp23s17_1, pins 9..11) --- */
    {
        .role          = DI_ROLE_USER_1,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 9U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = false,
    },
    {
        .role          = DI_ROLE_USER_2,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 10U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = false,
    },
    {
        .role          = DI_ROLE_USER_3,
        .source_type   = DI_SOURCE_INTERNAL_SPI,
        .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
        .pin_index     = 11U,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = false,
    },
};

/* ========================================================================= */
/* Remote Modbus Digital Inputs Table (MBSL32DI, 32 Channels)                */
/* ========================================================================= */

/**
 * @brief Immutable channel configuration for the 32 remote Modbus digital inputs.
 *
 * Mapped across Modbus slave MBSL32DI (Bus 2, Addr 4):
 * - Bits 0..15: Alarms for Units 5..8 (HP, LP, Fan, Comp)
 * - Bits 16..23: Fan Run Feedbacks 1..8 (U_vent_ON)
 * - Bits 24..31: Compressor Run Feedbacks 1..8 (c1..c8)
 */
static const di_channel_config_t k_brightc_remote_di_table[HARDWARE_REMOTE_DI_COUNT] = {
    /* --- Unit 5 Alarms (Bits 0..3) --- */
    { .role = DI_ROLE_UNIT_4_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 0U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_4_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 1U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_4_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 2U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_4_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 3U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* --- Unit 6 Alarms (Bits 4..7) --- */
    { .role = DI_ROLE_UNIT_5_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 4U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_5_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 5U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_5_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 6U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_5_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 7U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* --- Unit 7 Alarms (Bits 8..11) --- */
    { .role = DI_ROLE_UNIT_6_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 8U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_6_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 9U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_6_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 10U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_6_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 11U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* --- Unit 8 Alarms (Bits 12..15) --- */
    { .role = DI_ROLE_UNIT_7_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 12U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_7_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 13U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_7_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 14U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_7_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 15U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* --- Telemetry: Evaporator Fans 1..8 Auxiliary Run Feedbacks (Bits 16..23) --- */
    { .role = DI_ROLE_FAN_1_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 16U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_FAN_2_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 17U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_FAN_3_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 18U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_FAN_4_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 19U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_4,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 20U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Fan 5 */
    { .role = DI_ROLE_USER_5,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 21U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Fan 6 */
    { .role = DI_ROLE_USER_6,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 22U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Fan 7 */
    { .role = DI_ROLE_USER_7,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 23U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Fan 8 */

    /* --- Telemetry: Compressor 1..8 Auxiliary Run Feedbacks (Bits 24..31) --- */
    { .role = DI_ROLE_COMPRESSOR_1_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 24U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_COMPRESSOR_2_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 25U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_COMPRESSOR_3_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 26U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_COMPRESSOR_4_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 27U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_8,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 28U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Comp 5 */
    { .role = DI_ROLE_USER_9,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 29U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Comp 6 */
    { .role = DI_ROLE_THERMAL_OVERLOAD_FAN, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 30U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Comp 7 */
    { .role = DI_ROLE_THERMAL_OVERLOAD_COMP,.source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 31U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true }, /* Comp 8 */
};

/* ========================================================================= */
/* HVAC Unit Routing Table (Units 1..8 Local & Remote)                       */
/* ========================================================================= */

/**
 * @brief Immutable hardware actuation and alarm routing for all 8 supported HVAC units.
 *
 * - Units 0..3 (Local 1..4): Actuation on &mcp23s17_2, Alarms on local DIs 0..15.
 * - Units 4..7 (Remote 5..8): Actuation on Waveshare Modbus relay, Alarms on MBSL32DI.
 */
static const hvac_unit_hw_config_t k_brightc_hvac_hw_table[HARDWARE_TOTAL_HVAC_UNITS_COUNT] = {
    /* --- Local Unit 1 --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 9U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 10U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 8U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 0U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 1U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 2U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 3U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 24U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 16U },
    },

    /* --- Local Unit 2 --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 12U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 13U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 11U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 4U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 5U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 6U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 7U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 25U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 17U },
    },

    /* --- Local Unit 3 --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 15U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 0U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 14U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 8U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 9U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 10U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11, .channel = 11U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 26U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 18U },
    },

    /* --- Local Unit 4 --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 2U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 3U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_HVAC_RELAYS, .channel = 1U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .channel = 0U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .channel = 1U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .channel = 2U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .channel = 3U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 27U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 19U },
    },

    /* --- Remote Unit 5 (Waveshare relay channels 0..2) --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 0U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 1U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 2U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 0U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 1U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 2U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 3U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 28U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 20U },
    },

    /* --- Remote Unit 6 (Waveshare relay channels 3..5) --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 3U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 4U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 5U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 4U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 5U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 6U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 7U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 29U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 21U },
    },

    /* --- Remote Unit 7 (Waveshare relay channels 6..8) --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 6U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 7U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 8U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 8U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 9U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 10U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 11U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 30U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 22U },
    },

    /* --- Remote Unit 8 (Waveshare relay channels 9..11) --- */
    {
        .relay_aa       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 9U },
        .relay_sm       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 10U },
        .relay_g        = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_WAVESHARE_RELAY,        .channel = 11U },
        .alarm_hp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 12U },
        .alarm_lp       = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 13U },
        .alarm_flow     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 14U },
        .alarm_volt     = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 15U },
        .fb_compressor  = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 31U },
        .fb_fan         = { .backend = (uint8_t)HVAC_IO_BACKEND_REMOTE_MODBUS,  .device_id = MODBUS_ADDR_MBSL32DI,               .channel = 23U },
    },
};

/* ========================================================================= */
/* Consolidated System Digital Inputs Table (58 Channels: Local + On-Chip + Remote) */
/* ========================================================================= */

/**
 * @brief Unified system-wide digital input channel mapping (58 channels <= DI_CHANNEL_MAX).
 *
 * - Channels 0..23: 24 Local optocoupled inputs via MCP23S17_0 and MCP23S17_1.
 * - Channels 24..25: 2 On-chip microcontroller inputs (User button and Physical bypass switch).
 * - Channels 26..57: 32 Remote RS-485 Modbus digital inputs via MBSL32DI.
 */
static const di_channel_config_t k_brightc_system_di_table[HARDWARE_SYSTEM_DI_COUNT] = {
    /* --- Channels 0..23: 24 Local SPI MCP23S17 Inputs --- */
    /* Unit 0 Alarms (mcp23s17_0, pins 0..3) */
    { .role = DI_ROLE_UNIT_0_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 0U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_0_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 1U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_0_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 2U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_0_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 3U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Unit 1 Alarms (mcp23s17_0, pins 4..7) */
    { .role = DI_ROLE_UNIT_1_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 4U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_1_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 5U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_1_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 6U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_1_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 7U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Unit 2 Alarms (mcp23s17_0, pins 8..11) */
    { .role = DI_ROLE_UNIT_2_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 8U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_2_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 9U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_2_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 10U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_2_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_0_11,  .pin_index = 11U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Unit 3 Alarms (mcp23s17_1, pins 0..3) */
    { .role = DI_ROLE_UNIT_3_ALARM_HP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 0U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_3_ALARM_LP,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 1U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_3_ALARM_FLOW, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 2U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_UNIT_3_ALARM_VOLT, .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 3U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Facility & Environmental Safety Alarms (mcp23s17_1, pins 4..8) */
    { .role = DI_ROLE_SMOKE_FIRE,        .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 4U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_DOOR_OPEN,         .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 5U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_WATER_LEAK,        .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 6U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_SURGE_PROTECTOR,   .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 7U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },
    { .role = DI_ROLE_PHASE_MONITOR,     .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 8U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = true },

    /* Spare Local Digital Inputs (mcp23s17_1, pins 9..11) */
    { .role = DI_ROLE_USER_1,            .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 9U,  .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = false },
    { .role = DI_ROLE_USER_2,            .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 10U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = false },
    { .role = DI_ROLE_USER_3,            .source_type = DI_SOURCE_INTERNAL_SPI, .source_dev_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .pin_index = 11U, .active_level = DI_ACTIVE_LOW, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = DI_DEBOUNCE_ALARM_MS, .enabled = false },

    /* --- Channels 24..25: 2 On-Chip Microcontroller GPIOs --- */
    {
        .role          = DI_ROLE_USER_BUTTON,
        .source_type   = DI_SOURCE_ONCHIP_GPIO,
        .source_dev_id = 0U,
        .pin_index     = (uint8_t)ONCHIP_DI_USER_BUTTON,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },
    {
        .role          = DI_ROLE_BYPASS_SWITCH,
        .source_type   = DI_SOURCE_ONCHIP_GPIO,
        .source_dev_id = 0U,
        .pin_index     = (uint8_t)ONCHIP_DI_SW_BYPASS,
        .active_level  = DI_ACTIVE_LOW,
        .contact_type  = DI_CONTACT_NORMALLY_OPEN,
        .debounce_ms   = DI_DEBOUNCE_ALARM_MS,
        .enabled       = true,
    },

    /* --- Channels 26..57: 32 Remote Modbus DI Channels (MBSL32DI) --- */
    /* Unit 4 Alarms (Bits 0..3) */
    { .role = DI_ROLE_UNIT_4_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 0U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_4_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 1U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_4_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 2U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_4_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 3U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* Unit 5 Alarms (Bits 4..7) */
    { .role = DI_ROLE_UNIT_5_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 4U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_5_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 5U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_5_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 6U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_5_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 7U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* Unit 6 Alarms (Bits 8..11) */
    { .role = DI_ROLE_UNIT_6_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 8U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_6_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 9U,  .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_6_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 10U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_6_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 11U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* Unit 7 Alarms (Bits 12..15) */
    { .role = DI_ROLE_UNIT_7_ALARM_HP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 12U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_7_ALARM_LP,   .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 13U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_7_ALARM_FLOW, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 14U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_UNIT_7_ALARM_VOLT, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 15U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* Evaporator Fans 1..8 Auxiliary Run Feedbacks (Bits 16..23) */
    { .role = DI_ROLE_FAN_1_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 16U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_FAN_2_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 17U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_FAN_3_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 18U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_FAN_4_AUX,         .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 19U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_4,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 20U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_5,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 21U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_6,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 22U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_7,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 23U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },

    /* Compressor 1..8 Auxiliary Run Feedbacks (Bits 24..31) */
    { .role = DI_ROLE_COMPRESSOR_1_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 24U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_COMPRESSOR_2_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 25U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_COMPRESSOR_3_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 26U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_COMPRESSOR_4_AUX,  .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 27U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_8,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 28U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_USER_9,            .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 29U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_THERMAL_OVERLOAD_FAN, .source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 30U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
    { .role = DI_ROLE_THERMAL_OVERLOAD_COMP,.source_type = DI_SOURCE_EXTERNAL_MODBUS, .source_dev_id = MODBUS_ADDR_MBSL32DI, .pin_index = 31U, .active_level = DI_ACTIVE_HIGH, .contact_type = DI_CONTACT_NORMALLY_OPEN, .debounce_ms = 0U, .enabled = true },
};

/* ========================================================================= */
/* Accessor APIs                                                             */
/* ========================================================================= */

/**
 * @brief Retrieve local digital inputs configuration table.
 *
 * @param[out] out_count Optional pointer receiving channel count (24).
 * @return Pointer to immutable local digital inputs array.
 */
static inline const di_channel_config_t *hardware_pin_map_get_local_di(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = HARDWARE_LOCAL_DI_COUNT;
    }
    return k_brightc_local_di_table;
}

/**
 * @brief Retrieve complete consolidated system digital inputs configuration table.
 *
 * @param[out] out_count Optional pointer receiving channel count (58).
 * @return Pointer to immutable system digital inputs array.
 */
static inline const di_channel_config_t *hardware_pin_map_get_full_di(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = HARDWARE_SYSTEM_DI_COUNT;
    }
    return k_brightc_system_di_table;
}

/**
 * @brief Synonym for hardware_pin_map_get_full_di().
 *
 * @param[out] out_count Optional pointer receiving channel count (58).
 * @return Pointer to immutable system digital inputs array.
 */
static inline const di_channel_config_t *hardware_pin_map_get_system_di(size_t *out_count)
{
    return hardware_pin_map_get_full_di(out_count);
}

/**
 * @brief Retrieve remote Modbus digital inputs configuration table.
 *
 * @param[out] out_count Optional pointer receiving channel count (32).
 * @return Pointer to immutable remote digital inputs array.
 */
static inline const di_channel_config_t *hardware_pin_map_get_remote_di(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = HARDWARE_REMOTE_DI_COUNT;
    }
    return k_brightc_remote_di_table;
}

/**
 * @brief Retrieve HVAC unit hardware configuration table.
 *
 * @param[out] out_count Optional pointer receiving total units count (8).
 * @return Pointer to immutable HVAC hardware configurations array.
 */
static inline const hvac_unit_hw_config_t *hardware_pin_map_get_hvac_units(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = HARDWARE_TOTAL_HVAC_UNITS_COUNT;
    }
    return k_brightc_hvac_hw_table;
}

/**
 * @brief Retrieve hardware routing for a single HVAC unit (0..7).
 *
 * @param[in] unit_idx Zero-based unit index (0..7).
 * @return Pointer to unit routing structure, or NULL if unit_idx >= 8.
 */
static inline const hvac_unit_hw_config_t *hardware_pin_map_get_unit(uint8_t unit_idx)
{
    if (unit_idx >= HARDWARE_TOTAL_HVAC_UNITS_COUNT) {
        return NULL;
    }
    return &k_brightc_hvac_hw_table[unit_idx];
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_HARDWARE_PIN_MAP_H_ */
