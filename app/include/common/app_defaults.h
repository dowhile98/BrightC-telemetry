/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file app_defaults.h
 * @brief Baseline defaults and profile accessors for BrightC V3.
 *
 * Replaces the cumbersome default_config_profiles.c compilation unit with
 * lightweight, immutable compile-time defaults.
 *
 * Pure C99/C11: zero Zephyr headers, zero dynamic memory allocations.
 */

#ifndef APP_COMMON_APP_DEFAULTS_H_
#define APP_COMMON_APP_DEFAULTS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/config_types.h"
#include "common/digital_input_types.h"
#include "common/hardware_pin_map.h"
#include "common/hvac_types.h"
#include "common/modbus_device_table.h"
#include "common/network_types.h"
#include "common/sensor_types.h"
#include "infrastructure/adapters/adapter_relay_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ========================================================================= */
    /* Baseline Configuration Structures                                         */
    /* ========================================================================= */

    /** Default Device Identity */
    static const config_device_t k_default_device_cfg = {
        .serial_number = "BC3-STM32H7-001",
        .hardware_rev = "V3.0",
        .device_alias = "BrightC Gateway Node",
    };

    /** Default PT100 and Analog Calibration Constants */
    static const config_calibration_t k_default_calibration_cfg = {
        .temp_offsets_x10 = {0},
        .analog_offsets = {0},
        .analog_gains = {
            10000U, 10000U, 10000U, 10000U, 10000U, 10000U, 10000U, 10000U,
            10000U, 10000U, 10000U, 10000U, 10000U, 10000U, 10000U, 10000U},
    };

    /** Default Site Thermal & HVAC Topology Configuration */
    static const config_thermal_site_t k_default_thermal_site_cfg = {
        .version = 1U,
        .configured_pairs_count = 6U,
        .configured_room_probes = 2U,
        .configured_outdoor_probes = 2U,
        .required_redundancy_units = 2U,
        .site_altitude_m = 0,
        .pair_mapping_table = {
            [0] = { .supply_flat_id = 10U, .return_flat_id = 11U, .hvac_unit_id = 1U, .reserved = 0U },
            [1] = { .supply_flat_id = 12U, .return_flat_id = 13U, .hvac_unit_id = 2U, .reserved = 0U },
            [2] = { .supply_flat_id = 14U, .return_flat_id = 15U, .hvac_unit_id = 3U, .reserved = 0U },
            [3] = { .supply_flat_id = 16U, .return_flat_id = 17U, .hvac_unit_id = 4U, .reserved = 0U },
            [4] = { .supply_flat_id = 30U, .return_flat_id = 31U, .hvac_unit_id = 5U, .reserved = 0U },
            [5] = { .supply_flat_id = 32U, .return_flat_id = 33U, .hvac_unit_id = 6U, .reserved = 0U },
        },
        .room_probes_table = { 18U, 19U },
        .outdoor_probes_table = { 22U, 23U },
        .nominal_airflow_m3h = {
            4500.0f, 4500.0f, 4500.0f, 4500.0f, 4500.0f, 4500.0f, 4500.0f, 4500.0f
        },
        .nominal_capacity_kw = {
            14.0f, 14.0f, 14.0f, 14.0f, 14.0f, 14.0f, 14.0f, 14.0f
        },
        .design_outdoor_temp_c = 35.0f,
        .derating_coefficient = 0.005f,
        .reserved = {0},
    };

    /** Default Machine-Learned Steady-State Baseline Configuration */
    static const config_thermal_baseline_t k_default_thermal_baseline_cfg = {
        .ols_beta_coefficients = { 0.0, 0.0, 0.0, 0.0 },
        .baseline_exergetic_efficiency = 42.0f,
        .ols_condition_number = 0.0f,
        .baseline_pair_delta_t = { 0.0f },
        .baseline_pair_t_inj = { 0.0f },
        .compressor_power_signatures = { 0.0f },
        .training_timestamp_unix = 0U,
        .training_hours_accumulated = 0U,
        .is_valid = 0U,
        .algorithm_version = 1U,
        .reserved = {0},
    };

#ifndef CONFIG_BRIGHTC_WIFI_DEFAULT_SSID
#define CONFIG_BRIGHTC_WIFI_DEFAULT_SSID "BrightC-V3-AP"
#endif

#ifndef CONFIG_BRIGHTC_WIFI_DEFAULT_PSK
#define CONFIG_BRIGHTC_WIFI_DEFAULT_PSK "BrightC#Secure2026"
#endif

#ifndef CONFIG_BRIGHTC_WIFI_DEFAULT_IP
#define CONFIG_BRIGHTC_WIFI_DEFAULT_IP 0xC0A80401U /* 192.168.4.1 */
#endif

    /** Default Ethernet Network Configuration */
    static const net_eth_config_t k_default_net_eth_cfg = {
        .ip_mode = (uint8_t)NET_IP_MODE_DHCP,
        .auto_dhcpv4 = 1U,
        .ip_addr = 0xC0A80164U,     /* 192.168.1.100 */
        .subnet_mask = 0xFFFFFF00U, /* 255.255.255.0 */
        .gateway = 0xC0A80101U,     /* 192.168.1.1 */
    };

    /** Default Wi-Fi Network Configuration */
    static const net_wifi_config_t k_default_net_wifi_cfg = {
        .mode = (uint8_t)NET_WIFI_MODE_AP,
        .sta = {
            .ssid = "BrightC-Field",
            .psk = "brightc12345",
            .security = (uint8_t)NET_WIFI_SEC_WPA2_PSK,
            .ip_mode = (uint8_t)NET_IP_MODE_DHCP,
        },
        .ap = {
            .ssid = CONFIG_BRIGHTC_WIFI_DEFAULT_SSID,
            .psk = CONFIG_BRIGHTC_WIFI_DEFAULT_PSK,
            .security = (uint8_t)NET_WIFI_SEC_WPA2_PSK,
            .channel = 9U,
            .max_clients = 5U,
            .ap_ip_addr = CONFIG_BRIGHTC_WIFI_DEFAULT_IP,
            .ap_subnet_mask = 0xFFFFFF00U, /* 255.255.255.0 */
        },
    };

    /**
     * @brief Default HVAC Configuration: Hybrid Mode Baseline (4 Units, 2+2 Rotation).
     */
    static const config_hvac_t k_default_hvac_config_hybrid_4units = {
        .operation_mode = (uint8_t)HVAC_MODE_HYBRID_FULL,
        .total_units = 4U,
        .total_groups = 1U,
        .bypass_alarm_contact_type = (uint8_t)HVAC_ALARM_CONTACT_NC,
        .alarm_buzzer_muted = 0U,
        .fire_interlock = {
            .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17,
            .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23,
            .channel = 4U, /* alm_in_17 on mcp23s17_1 pin 4 */
        },
        .bypass_switch = {
            .backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO,
            .device_id = 0U,
            .channel = (uint16_t)ONCHIP_DI_SW_BYPASS,
        },
        .groups = {
            [0] = {
                .temp_min = 26.0f,
                .temp_max = 28.0f,
                .temp_alarm = 30.0f,
                .temp_bypass = 32.0f,
                .time_to_cool_s = 1800U,
                .delay_on_s = 10U,
                .fan_delay_s = 30U,
                .pressure_retry_s = 180U,
                .manual_timeout_s = 3600U,
                .rotation_interval_days = 7U,
                .sensor_mask = 0x001FU,  /* Bits 0..4: 5 on-board internal RTDs */
                .unit_mask = 0x0FU,      /* Units 0..3 */
                .active_turn_count = 2U, /* 2 active + 2 standby (2+2) */
                .max_allowed_faults = 3U,
                .enable_fan_delay = 0U,  /* Default OFF: fan always enabled (relay_g = 0) */
                .hum_min = 40U,
                .hum_max = 70U,
                .sensor_start = 1U,
                .sensor_end = 5U,
                .metric_policy = (uint8_t)SENSOR_METRIC_MAX,
                .temp_low_alarm_c = 15,
            },
        },
    };

    /** Default HVAC Configuration: Control-Only Baseline (8 Units) */
    static const config_hvac_t k_default_hvac_config_control_only_8units = {
        .operation_mode = (uint8_t)HVAC_MODE_HVAC_CONTROL_ONLY,
        .total_units = 8U,
        .total_groups = 1U,
        .bypass_alarm_contact_type = (uint8_t)HVAC_ALARM_CONTACT_NC,
        .alarm_buzzer_muted = 0U,
        .fire_interlock = {.backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_MCP23S17, .device_id = HARDWARE_MCP23S17_IDX_ALM_IN_12_23, .channel = 4U},
        .bypass_switch = {.backend = (uint8_t)HVAC_IO_BACKEND_LOCAL_GPIO, .device_id = 0U, .channel = (uint16_t)ONCHIP_DI_SW_BYPASS},
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
                .rotation_interval_days = 7U,
                .sensor_mask = 0x00FFU,
                .unit_mask = 0xFFU,
                .active_turn_count = 4U,
                .max_allowed_faults = 3U,
                .enable_fan_delay = 0U, /* Default OFF: fan always enabled (relay_g = 0) */
                .hum_min = 40U,
                .hum_max = 70U,
                .sensor_start = 1U,
                .sensor_end = 8U,
                .metric_policy = (uint8_t)SENSOR_METRIC_MAX,
                .temp_low_alarm_c = 15,
            },
        },
    };

    /** Default HVAC Configuration: Telemetry-Only Baseline */
    static const config_hvac_t k_default_hvac_config_telemetry_only = {
        .operation_mode = (uint8_t)HVAC_MODE_TELEMETRY_ONLY,
        .total_units = 0U,
        .total_groups = 0U,
        .alarm_buzzer_muted = 0U,
    };

    /* ========================================================================= */
    /* Accessor APIs                                                             */
    /* ========================================================================= */

    static inline const config_device_t *app_defaults_get_device_cfg(void)
    {
        return &k_default_device_cfg;
    }

    static inline const config_calibration_t *app_defaults_get_calibration_cfg(void)
    {
        return &k_default_calibration_cfg;
    }

    static inline const net_eth_config_t *app_defaults_get_net_eth_cfg(void)
    {
        return &k_default_net_eth_cfg;
    }

    static inline const net_wifi_config_t *app_defaults_get_net_wifi_cfg(void)
    {
        return &k_default_net_wifi_cfg;
    }

    static inline const config_hvac_t *app_defaults_get_hvac_profile(hvac_operation_mode_t mode)
    {
        switch (mode)
        {
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

    static inline const di_channel_config_t *app_defaults_get_di_profile(hvac_operation_mode_t mode,
                                                                         size_t *out_count)
    {
        return hardware_pin_map_get_di_profile(mode, out_count);
    }

    static inline const config_thermal_site_t *app_defaults_get_thermal_site_cfg(void)
    {
        return &k_default_thermal_site_cfg;
    }

    static inline const config_thermal_baseline_t *app_defaults_get_thermal_baseline_cfg(void)
    {
        return &k_default_thermal_baseline_cfg;
    }

    /* ========================================================================= */
    /* Backward Compatibility Aliases                                            */
    /* ========================================================================= */

    static inline const config_device_t *default_config_get_device_cfg(void)
    {
        return app_defaults_get_device_cfg();
    }

    static inline const config_calibration_t *default_config_get_calibration_cfg(void)
    {
        return app_defaults_get_calibration_cfg();
    }

    static inline const net_eth_config_t *default_config_get_net_eth_cfg(void)
    {
        return app_defaults_get_net_eth_cfg();
    }

    static inline const net_wifi_config_t *default_config_get_net_wifi_cfg(void)
    {
        return app_defaults_get_net_wifi_cfg();
    }

    static inline const config_hvac_t *default_config_get_hvac_profile(hvac_operation_mode_t mode)
    {
        return app_defaults_get_hvac_profile(mode);
    }

    static inline const di_channel_config_t *default_config_get_di_profile(hvac_operation_mode_t mode,
                                                                           size_t *out_count)
    {
        return hardware_pin_map_get_di_profile(mode, out_count);
    }

    static inline const config_thermal_site_t *default_config_get_thermal_site_cfg(void)
    {
        return app_defaults_get_thermal_site_cfg();
    }

    static inline const config_thermal_baseline_t *default_config_get_thermal_baseline_cfg(void)
    {
        return app_defaults_get_thermal_baseline_cfg();
    }

    static inline const modbus_slave_descriptor_t *default_config_get_modbus_slaves(size_t *out_count)
    {
        return modbus_device_table_get_slaves(out_count);
    }

    static inline const modbus_bus_cfg_t *default_config_get_modbus_bus_cfg(uint8_t bus_id)
    {
        return modbus_device_table_get_bus_cfg((modbus_bus_id_t)bus_id);
    }

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_APP_DEFAULTS_H_ */
