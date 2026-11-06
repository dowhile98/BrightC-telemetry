/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file default_config_profiles.h
 * @brief Default Configuration and Storage Profiles for HVAC Control and Telemetry.
 *
 * Centralizes baseline configurations for Hybrid, Control-Only, and Telemetry-Only operating modes.
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
#include "common/modbus_types.h"
#include "common/network_types.h"
#include "common/sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Hardware Device Addressing Reference Constants                            */
/* ========================================================================= */

#define DEFAULT_MODBUS_DEV_ID_MBSL32DI        3U /**< Slave ID / Dev ID for 32-ch Digital Input */
#define DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY 4U /**< Slave ID / Dev ID for 32-ch Relay Module */

#define DEFAULT_DI_CHANNELS_HYBRID_COUNT        34U
#define DEFAULT_DI_CHANNELS_CONTROL_ONLY_COUNT  32U

/* ========================================================================= */
/* Profile Selection Enumeration                                             */
/* ========================================================================= */

typedef enum {
    HVAC_PROFILE_HYBRID_4UNITS = 0,       /**< Hybrid Mode: 4 Units (2+2) + Telemetry */
    HVAC_PROFILE_CONTROL_ONLY_8UNITS = 1, /**< Control-Only: 8 Units (4+4) */
    HVAC_PROFILE_TELEMETRY_ONLY = 2,      /**< Telemetry-Only: 0 Actuator Units */
    HVAC_PROFILE_MAX
} hvac_profile_id_t;

/* ========================================================================= */
/* External Constant Profile Tables                                          */
/* ========================================================================= */

extern const config_hvac_t k_default_hvac_config_hybrid_4units;
extern const config_hvac_t k_default_hvac_config_control_only_8units;
extern const config_hvac_t k_default_hvac_config_telemetry_only;

extern const di_channel_config_t k_default_di_channels_hybrid[DEFAULT_DI_CHANNELS_HYBRID_COUNT];
extern const di_channel_config_t k_default_di_channels_control_only[DEFAULT_DI_CHANNELS_CONTROL_ONLY_COUNT];

extern const modbus_bus_cfg_t k_default_modbus_bus_cfg[MODBUS_BUS_MAX];
extern const modbus_slave_descriptor_t k_default_modbus_slaves[];
extern const size_t k_default_modbus_slaves_count;

extern const net_eth_config_t k_default_net_eth_cfg;
extern const net_wifi_config_t k_default_net_wifi_cfg;

/* ========================================================================= */
/* Profile & Policy Query APIs                                               */
/* ========================================================================= */

/**
 * @brief Retrieve default HVAC configuration structure for an operation mode.
 *
 * @param mode Operation mode (HVAC_MODE_HYBRID_FULL, HVAC_MODE_HVAC_CONTROL_ONLY, HVAC_MODE_TELEMETRY_ONLY).
 * @return Pointer to immutable default configuration structure, or NULL if mode invalid.
 */
const config_hvac_t *default_config_get_hvac_profile(hvac_operation_mode_t mode);

/**
 * @brief Retrieve default Digital Input channel mapping table for an operation mode.
 *
 * @param[in]  mode      Operation mode.
 * @param[out] out_count Optional pointer receiving number of channels.
 * @return Pointer to immutable default channel configuration array.
 */
const di_channel_config_t *default_config_get_di_profile(hvac_operation_mode_t mode,
                                                        size_t *out_count);

/**
 * @brief Retrieve default physical Modbus bus configuration.
 *
 * @param[in] bus_id Physical bus ID (0 .. MODBUS_BUS_MAX - 1).
 * @return Pointer to bus configuration, or NULL if bus_id invalid.
 */
const modbus_bus_cfg_t *default_config_get_modbus_bus_cfg(uint8_t bus_id);

/**
 * @brief Retrieve default Modbus slaves descriptor table.
 *
 * @param[out] out_count Optional pointer receiving number of slave descriptors.
 * @return Pointer to array of slave descriptors.
 */
const modbus_slave_descriptor_t *default_config_get_modbus_slaves(size_t *out_count);

/**
 * @brief Retrieve default Ethernet configuration.
 *
 * @return Pointer to immutable default Ethernet configuration.
 */
const net_eth_config_t *default_config_get_net_eth_cfg(void);

/**
 * @brief Retrieve default Wi-Fi configuration.
 *
 * @return Pointer to immutable default Wi-Fi configuration.
 */
const net_wifi_config_t *default_config_get_net_wifi_cfg(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_DEFAULT_CONFIG_PROFILES_H_ */
