/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file modbus_device_table.h
 * @brief Declarative compile-time Modbus bus and slave topology for BrightC V3.
 *
 * Replaces dynamic runtime EEPROM slave configuration with immutable,
 * deterministic hardware topology definitions adhering to Clean Architecture (§4.1.D).
 * Pure ISO C99/C11: zero Zephyr headers, zero dynamic memory allocations.
 */

#ifndef APP_COMMON_MODBUS_DEVICE_TABLE_H_
#define APP_COMMON_MODBUS_DEVICE_TABLE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/modbus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Standard Physical Bus Configurations                                      */
/* ========================================================================= */

/** Default baud rate for Bus 1 (Telemetry & Instrumentation). */
#define MODBUS_BUS_1_DEFAULT_BAUDRATE 9600U

/** Default baud rate for Bus 2 (HVAC Actuation & Remote I/O). */
#define MODBUS_BUS_2_DEFAULT_BAUDRATE 19200U

/**
 * @brief Immutable electrical bus configuration for RS485-1 and RS485-2.
 */
static const modbus_bus_cfg_t k_brightc_modbus_bus_table[MODBUS_BUS_MAX] = {
    [MODBUS_BUS_RS485_1] = {
        .baudrate  = MODBUS_BUS_1_DEFAULT_BAUDRATE,
        .parity    = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
    [MODBUS_BUS_RS485_2] = {
        .baudrate  = MODBUS_BUS_2_DEFAULT_BAUDRATE,
        .parity    = MODBUS_PARITY_NONE,
        .stop_bits = 1U,
        .data_bits = 8U,
    },
};

/* ========================================================================= */
/* Standard Field Device Slave Addresses & Identifiers                       */
/* ========================================================================= */

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_JSY_MK339
#define CONFIG_BRIGHTC_MODBUS_ADDR_JSY_MK339 1U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_1
#define CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_1 2U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_2
#define CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_2 3U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_MBSL32DI
#define CONFIG_BRIGHTC_MODBUS_ADDR_MBSL32DI 4U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_WAVESHARE_RELAY
#define CONFIG_BRIGHTC_MODBUS_ADDR_WAVESHARE_RELAY 5U
#endif

#define MODBUS_ADDR_JSY_MK339         CONFIG_BRIGHTC_MODBUS_ADDR_JSY_MK339
#define MODBUS_ADDR_CWT_TM16PT_1      CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_1
#define MODBUS_ADDR_CWT_TM16PT_2      CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_2
#define MODBUS_ADDR_MBSL32DI          CONFIG_BRIGHTC_MODBUS_ADDR_MBSL32DI
#define MODBUS_ADDR_WAVESHARE_RELAY   CONFIG_BRIGHTC_MODBUS_ADDR_WAVESHARE_RELAY

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_JSY_MK339)
#define MODBUS_ENABLE_JSY_MK339 true
#elif defined(CONFIG_BRIGHTC_MODBUS_KCONFIG_PRESENT)
#define MODBUS_ENABLE_JSY_MK339 false
#else
#define MODBUS_ENABLE_JSY_MK339 true
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_CWT_TM16PT_1)
#define MODBUS_ENABLE_CWT_TM16PT_1 true
#elif defined(CONFIG_BRIGHTC_MODBUS_KCONFIG_PRESENT)
#define MODBUS_ENABLE_CWT_TM16PT_1 false
#else
#define MODBUS_ENABLE_CWT_TM16PT_1 true
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_CWT_TM16PT_2)
#define MODBUS_ENABLE_CWT_TM16PT_2 true
#elif defined(CONFIG_BRIGHTC_MODBUS_KCONFIG_PRESENT)
#define MODBUS_ENABLE_CWT_TM16PT_2 false
#elif defined(CONFIG_ZTEST)
#define MODBUS_ENABLE_CWT_TM16PT_2 true
#else
#define MODBUS_ENABLE_CWT_TM16PT_2 false
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_MBSL32DI)
#define MODBUS_ENABLE_MBSL32DI true
#elif defined(CONFIG_BRIGHTC_MODBUS_KCONFIG_PRESENT)
#define MODBUS_ENABLE_MBSL32DI false
#else
#define MODBUS_ENABLE_MBSL32DI true
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_WAVESHARE_RELAY)
#define MODBUS_ENABLE_WAVESHARE_RELAY true
#elif defined(CONFIG_BRIGHTC_MODBUS_KCONFIG_PRESENT)
#define MODBUS_ENABLE_WAVESHARE_RELAY false
#else
#define MODBUS_ENABLE_WAVESHARE_RELAY true
#endif

/* Backward compatibility aliases */
#define DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY MODBUS_ADDR_WAVESHARE_RELAY
#define DEFAULT_MODBUS_WAVESHARE_TIMEOUT_MS   300U

/**
 * @brief Total number of standard field slaves in the BrightC topology.
 */
#define MODBUS_STANDARD_SLAVES_COUNT 5U

/**
 * @brief Immutable standard Modbus slave descriptors table.
 */
static const modbus_slave_descriptor_t k_brightc_modbus_slave_table[MODBUS_STANDARD_SLAVES_COUNT] = {
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
        .bus_id            = (uint8_t)MODBUS_BUS_RS485_1,
        .slave_addr        = MODBUS_ADDR_MBSL32DI,
        .dev_type          = (uint8_t)MODBUS_DEV_TYPE_MBSL32DI,
        .poll_interval_ms  = 200U,  /* Rapid sampling for contactors & interlocks */
        .timeout_ms        = 150U,
        .is_enabled        = MODBUS_ENABLE_MBSL32DI,
    },
    {
        .device_id         = MODBUS_ADDR_WAVESHARE_RELAY,
        .bus_id            = (uint8_t)MODBUS_BUS_RS485_1,
        .slave_addr        = MODBUS_ADDR_WAVESHARE_RELAY,
        .dev_type          = (uint8_t)MODBUS_DEV_TYPE_WAVESHARE_RELAY_32CH,
        .poll_interval_ms  = 1000U,
        .timeout_ms        = 250U,
        .is_enabled        = MODBUS_ENABLE_WAVESHARE_RELAY,
    },
};

/* ========================================================================= */
/* Helper Functions                                                          */
/* ========================================================================= */

/**
 * @brief Retrieve default electrical configuration for a Modbus bus.
 *
 * @param[in] bus_id Logical bus ID (MODBUS_BUS_RS485_1 or MODBUS_BUS_RS485_2).
 * @return Pointer to immutable bus configuration, or NULL if bus_id invalid.
 */
static inline const modbus_bus_cfg_t *modbus_device_table_get_bus_cfg(modbus_bus_id_t bus_id)
{
    if ((uint8_t)bus_id >= (uint8_t)MODBUS_BUS_MAX) {
        return NULL;
    }
    return &k_brightc_modbus_bus_table[bus_id];
}

/**
 * @brief Retrieve standard Modbus slave descriptors table.
 *
 * @param[out] out_count Optional pointer receiving number of descriptors.
 * @return Pointer to immutable descriptor array.
 */
static inline const modbus_slave_descriptor_t *modbus_device_table_get_slaves(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = MODBUS_STANDARD_SLAVES_COUNT;
    }
    return k_brightc_modbus_slave_table;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_MODBUS_DEVICE_TABLE_H_ */
