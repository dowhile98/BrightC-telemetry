/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Single definition of the compile-time Modbus slave topology. Keeping it in
 * one translation unit avoids the per-TU copies a `static const` table in a
 * header produces, and makes the topology easy to audit.
 */

#include "common/modbus_device_table.h"

/*
 * `device_id` is deliberately equal to the unit address: hardware_pin_map.h
 * uses MODBUS_ADDR_MBSL32DI as the source device id of the remote digital
 * inputs, so the registry key and the physical address must stay in sync.
 */
const modbus_slave_descriptor_t k_brightc_modbus_slave_table[MODBUS_STANDARD_SLAVES_COUNT] = {
    /* RS485-1: Telemetry & Environmental Instrumentation */
    {
        .device_id         = MODBUS_ADDR_JSY_MK339,
        .bus_id            = (uint8_t)MODBUS_BUS_RS485_1,
        .slave_addr        = MODBUS_ADDR_JSY_MK339,
        .dev_type          = (uint8_t)MODBUS_DEV_TYPE_JSY_MK339,
        .poll_interval_ms  = 1000U,
        .timeout_ms        = 250U,
        .is_enabled        = MODBUS_ENABLE_JSY_MK339,
    },
    {
        .device_id         = MODBUS_ADDR_CWT_TM16PT_1,
        .bus_id            = (uint8_t)MODBUS_BUS_RS485_1,
        .slave_addr        = MODBUS_ADDR_CWT_TM16PT_1,
        .dev_type          = (uint8_t)MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms  = 1000U,
        .timeout_ms        = 300U,
        .is_enabled        = MODBUS_ENABLE_CWT_TM16PT_1,
    },
    {
        .device_id         = MODBUS_ADDR_CWT_TM16PT_2,
        .bus_id            = (uint8_t)MODBUS_BUS_RS485_1,
        .slave_addr        = MODBUS_ADDR_CWT_TM16PT_2,
        .dev_type          = (uint8_t)MODBUS_DEV_TYPE_CWT_TM16PT,
        .poll_interval_ms  = 1000U,
        .timeout_ms        = 300U,
        .is_enabled        = MODBUS_ENABLE_CWT_TM16PT_2,
    },

    /* RS485-2: Critical HVAC Actuation & Remote I/O */
    {
        .device_id         = MODBUS_ADDR_MBSL32DI,
        .bus_id            = (uint8_t)MODBUS_BUS_RS485_2,
        .slave_addr        = MODBUS_ADDR_MBSL32DI,
        .dev_type          = (uint8_t)MODBUS_DEV_TYPE_MBSL32DI,
        .poll_interval_ms  = 200U,  /* Rapid sampling for contactors & interlocks */
        .timeout_ms        = 150U,
        .is_enabled        = MODBUS_ENABLE_MBSL32DI,
    },
    {
        .device_id         = MODBUS_ADDR_WAVESHARE_RELAY,
        .bus_id            = (uint8_t)MODBUS_BUS_RS485_2,
        .slave_addr        = MODBUS_ADDR_WAVESHARE_RELAY,
        .dev_type          = (uint8_t)MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH,
        .poll_interval_ms  = 1000U,
        .timeout_ms        = 250U,
        .is_enabled        = MODBUS_ENABLE_WAVESHARE_RELAY,
    },
};
