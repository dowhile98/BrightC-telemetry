/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain types, enums, event definitions and memory layout definitions
 * for the persistent storage subsystem (AT24C256C EEPROM).
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_COMMON_CONFIG_TYPES_H_
#define APP_COMMON_CONFIG_TYPES_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/thermal_energy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logical configuration categories persisted in non-volatile storage.
 *
 * Persistence domains per Architectural Decision Records (ADR-6 & REVF Baseline).
 * Hardware pinouts, Modbus topologies, and sensor channels are declared
 * compile-time in firmware.
 */
typedef enum {
    CONFIG_DOMAIN_DEVICE = 0,       /**< Device serial number, hardware rev, station alias */
    CONFIG_DOMAIN_NET_ETH,          /**< Ethernet interface configuration (IP, DHCP, MAC, Gateway) */
    CONFIG_DOMAIN_NET_WIFI,         /**< Wi-Fi interface configuration (Mode, SSID, Credentials) */
    CONFIG_DOMAIN_HVAC,             /**< Industrial HVAC sequencer operational setpoints & group ranges */
    CONFIG_DOMAIN_CALIBRATION,      /**< Analog input & PT100 temperature calibration offsets */
    CONFIG_DOMAIN_THERMAL_SITE,     /**< Site thermal & HVAC topology configuration */
    CONFIG_DOMAIN_THERMAL_BASELINE, /**< Machine-learned steady-state baseline signatures */
    CONFIG_DOMAIN_MAX               /**< Upper limit of active storage domains (7) */
} config_domain_t;

/**
 * @brief Storage asynchronous event notification types.
 */
typedef enum {
    CONFIG_EVENT_DIRTY = 0,      /**< RAM configuration modified, storage sync requested */
    CONFIG_EVENT_PERSISTED,      /**< RAM configuration successfully saved to non-volatile medium */
    CONFIG_EVENT_ERROR           /**< Storage write or read operation failed */
} config_event_type_t;

/**
 * @brief Storage event message published across Zbus.
 */
typedef struct {
    config_domain_t     domain;      /**< Target domain */
    config_event_type_t event_type;  /**< Event type (DIRTY, PERSISTED, ERROR) */
    uint16_t            sub_id;      /**< Optional sub-identifier */
    int32_t             status;      /**< Result code (0 on success, negative errno on error) */
} system_config_event_t;

/* ========================================================================= */
/* Domain Payload Structures Persisted in EEPROM                             */
/* ========================================================================= */

/**
 * @brief Device identity & hardware revision configuration.
 */
typedef struct {
    char serial_number[32];
    char hardware_rev[16];
    char device_alias[32];
} config_device_t;

/**
 * @brief Analog & temperature calibration trim offsets.
 */
typedef struct {
    int16_t  temp_offsets_x10[21]; /**< PT100 cable calibration offsets in 0.1 deg C */
    int16_t  analog_offsets[16];   /**< ADC channel raw offsets */
    uint16_t analog_gains[16];     /**< Gain scale factors (10000 = 1.0) */
} config_calibration_t;

/**
 * @brief Site thermal & HVAC topology configuration persisted in EEPROM (Domain 5).
 *
 * Persisted in ping-pong slot at 0x0A00 (768 B capacity, payload exactly 184 B).
 */
typedef struct {
    uint16_t               version;                   /**< Schema version (1) */
    uint8_t                configured_pairs_count;    /**< Active thermal pairs N (1..16, default 6) */
    uint8_t                configured_room_probes;    /**< Active room probes M (1..16, default 2) */
    uint8_t                configured_outdoor_probes; /**< Active outdoor probes L (1..8, default 2) */
    uint8_t                required_redundancy_units; /**< Redundancy units U_req (1..8, default 2) */
    int16_t                site_altitude_m;           /**< Elevation above sea level [-100..4000 m] */
    thermal_pair_mapping_t pair_mapping_table[THERMAL_MAX_PAIRS]; /**< 16 * 4 = 64 bytes */
    uint8_t                room_probes_table[THERMAL_MAX_ROOM_PROBES]; /**< 16 bytes: flat sensor IDs (1..59) */
    uint8_t                outdoor_probes_table[THERMAL_MAX_OUTDOOR_PROBES]; /**< 8 bytes: flat sensor IDs (1..59) */
    float                  nominal_airflow_m3h[THERMAL_MAX_HVAC_UNITS]; /**< 8 * 4 = 32 bytes [500..25000] */
    float                  nominal_capacity_kw[THERMAL_MAX_HVAC_UNITS]; /**< 8 * 4 = 32 bytes [2.0..100.0] */
    float                  design_outdoor_temp_c;     /**< Design high ambient temp (AHRI 35.0 deg C) */
    float                  derating_coefficient;      /**< High-ambient derating slope (default 0.005) */
    uint8_t                reserved[16];              /**< Expansion padding to 184 bytes */
} config_thermal_site_t;

_Static_assert(sizeof(config_thermal_site_t) == 184U,
               "config_thermal_site_t must be exactly 184 bytes");

/**
 * @brief Machine-learned steady-state baseline parameters persisted in EEPROM (Domain 6).
 *
 * Persisted in ping-pong slot at 0x1000 (512 B capacity, payload exactly 216 B).
 * Naturally 8-byte aligned due to double-precision OLS regression coefficients.
 */
typedef struct {
    double   ols_beta_coefficients[4];                  /**< 32 B: [beta_0, beta_1, beta_2, beta_3] */
    float    baseline_exergetic_efficiency;             /**< 4 B: Reference Second Law efficiency [%] (20..65%) */
    float    ols_condition_number;                      /**< 4 B: OLS matrix condition number */
    float    baseline_pair_delta_t[THERMAL_MAX_PAIRS];  /**< 64 B: Clean reference Delta T per pair [deg C] */
    float    baseline_pair_t_inj[THERMAL_MAX_PAIRS];    /**< 64 B: Clean reference supply temp per pair [deg C] */
    float    compressor_power_signatures[THERMAL_MAX_HVAC_UNITS]; /**< 32 B: Step power per compressor [kW] */
    uint32_t training_timestamp_unix;                   /**< 4 B: UNIX timestamp when 168h training committed */
    uint16_t training_hours_accumulated;                /**< 2 B: Steady-state hours accumulated (168) */
    uint8_t  is_valid;                                  /**< 1 B: 1 if trained baseline is valid, 0 if untrained */
    uint8_t  algorithm_version;                         /**< 1 B: Algorithm revision (1) */
    uint8_t  reserved[8];                               /**< 8 B: Future alignment/expansion padding */
} config_thermal_baseline_t;

_Static_assert(sizeof(config_thermal_baseline_t) == 216U,
               "config_thermal_baseline_t must be exactly 216 bytes");

/* ========================================================================= */
/* Pure Validation & Layout Functions                                        */
/* ========================================================================= */

/**
 * @brief Validate if domain enumerator is within defined bounds.
 *
 * @param domain Domain to check.
 * @return true if valid, false otherwise.
 */
static inline bool config_domain_is_valid(config_domain_t domain)
{
    return (domain >= CONFIG_DOMAIN_DEVICE && domain < CONFIG_DOMAIN_MAX);
}

/**
 * @brief Validate if event type enumerator is valid.
 *
 * @param type Event type to check.
 * @return true if valid, false otherwise.
 */
static inline bool config_event_type_is_valid(config_event_type_t type)
{
    return (type == CONFIG_EVENT_DIRTY ||
            type == CONFIG_EVENT_PERSISTED ||
            type == CONFIG_EVENT_ERROR);
}

/**
 * @brief Retrieve memory layout for a given configuration domain.
 *
 * Partitioning of the 32 KB AT24C256C EEPROM (ADR-6 & REVF Baseline):
 * - CONFIG_DOMAIN_DEVICE:           0x0000, 256 B (2 ping-pong slots of 128 B, 2 pages)
 * - CONFIG_DOMAIN_NET_ETH:          0x0100, 128 B (2 ping-pong slots of 64 B, 1 page)
 * - CONFIG_DOMAIN_NET_WIFI:         0x0180, 640 B (2 ping-pong slots of 320 B, 5 pages)
 * - CONFIG_DOMAIN_HVAC:             0x0400, 1152 B (2 ping-pong slots of 576 B, 9 pages)
 * - CONFIG_DOMAIN_CALIBRATION:      0x0880, 384 B (2 ping-pong slots of 192 B, 3 pages)
 * - CONFIG_DOMAIN_THERMAL_SITE:     0x0A00, 1536 B (2 ping-pong slots of 768 B, 12 pages)
 * - CONFIG_DOMAIN_THERMAL_BASELINE: 0x1000, 1024 B (2 ping-pong slots of 512 B, 8 pages)
 * - RESERVED / EVENT LOG:           0x1400 .. 0x7FFF (27.5 KB available)
 *
 * @param domain           Target domain.
 * @param out_base_offset  Output pointer for starting offset in bytes.
 * @param out_total_size   Output pointer for total allocated size in bytes.
 * @param out_slot_size    Output pointer for size of a single ping-pong slot.
 * @return 0 on success, -EINVAL on invalid domain or null pointer.
 */
static inline int config_domain_get_layout(config_domain_t domain,
                                          size_t *out_base_offset,
                                          size_t *out_total_size,
                                          size_t *out_slot_size)
{
    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }

    size_t base = 0;
    size_t total = 0;
    size_t slot = 0;

    switch (domain) {
    case CONFIG_DOMAIN_DEVICE:
        base = 0x0000;
        total = 256;
        slot = 128;
        break;
    case CONFIG_DOMAIN_NET_ETH:
        base = 0x0100;
        total = 128;
        slot = 64;
        break;
    case CONFIG_DOMAIN_NET_WIFI:
        base = 0x0180;
        total = 640;
        slot = 320;
        break;
    case CONFIG_DOMAIN_HVAC:
        base = 0x0400;
        total = 1152;
        slot = 576;
        break;
    case CONFIG_DOMAIN_CALIBRATION:
        base = 0x0880;
        total = 384;
        slot = 192;
        break;
    case CONFIG_DOMAIN_THERMAL_SITE:
        base = 0x0A00;
        total = 1536;
        slot = 768;
        break;
    case CONFIG_DOMAIN_THERMAL_BASELINE:
        base = 0x1000;
        total = 1024;
        slot = 512;
        break;
    default:
        return -EINVAL;
    }

    if (out_base_offset != NULL) {
        *out_base_offset = base;
    }
    if (out_total_size != NULL) {
        *out_total_size = total;
    }
    if (out_slot_size != NULL) {
        *out_slot_size = slot;
    }

    return 0;
}

/**
 * @brief Validate site thermal configuration parameters.
 *
 * @param cfg Configuration structure to validate.
 * @return true if all parameters adhere to physical sanity bounds, false otherwise.
 */
static inline bool config_thermal_site_is_valid(const config_thermal_site_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    if (cfg->configured_pairs_count < 1U || cfg->configured_pairs_count > THERMAL_MAX_PAIRS) {
        return false;
    }
    if (cfg->configured_room_probes < 1U || cfg->configured_room_probes > THERMAL_MAX_ROOM_PROBES) {
        return false;
    }
    if (cfg->configured_outdoor_probes < 1U || cfg->configured_outdoor_probes > THERMAL_MAX_OUTDOOR_PROBES) {
        return false;
    }
    if (cfg->required_redundancy_units < 1U || cfg->required_redundancy_units > THERMAL_MAX_HVAC_UNITS) {
        return false;
    }
    if (cfg->site_altitude_m < -100 || cfg->site_altitude_m > 4000) {
        return false;
    }
    if (cfg->design_outdoor_temp_c < 10.0f || cfg->design_outdoor_temp_c > 65.0f) {
        return false;
    }
    if (cfg->derating_coefficient < 0.0f || cfg->derating_coefficient > 0.1f) {
        return false;
    }

    for (uint8_t k = 0U; k < cfg->configured_pairs_count; k++) {
        uint8_t sup = cfg->pair_mapping_table[k].supply_flat_id;
        uint8_t ret = cfg->pair_mapping_table[k].return_flat_id;
        uint8_t unit = cfg->pair_mapping_table[k].hvac_unit_id;
        if (sup < 1U || sup > 59U || ret < 1U || ret > 59U || sup == ret) {
            return false;
        }
        if (unit < 1U || unit > THERMAL_MAX_HVAC_UNITS) {
            return false;
        }
    }

    for (uint8_t m = 0U; m < cfg->configured_room_probes; m++) {
        uint8_t id = cfg->room_probes_table[m];
        if (id < 1U || id > 59U) {
            return false;
        }
    }

    for (uint8_t l = 0U; l < cfg->configured_outdoor_probes; l++) {
        uint8_t id = cfg->outdoor_probes_table[l];
        if (id < 1U || id > 59U) {
            return false;
        }
    }

    for (uint8_t u = 0U; u < THERMAL_MAX_HVAC_UNITS; u++) {
        if (cfg->nominal_airflow_m3h[u] < 500.0f || cfg->nominal_airflow_m3h[u] > 25000.0f) {
            return false;
        }
        if (cfg->nominal_capacity_kw[u] < 2.0f || cfg->nominal_capacity_kw[u] > 100.0f) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Validate steady-state learned baseline parameters.
 *
 * @param cfg Configuration structure to validate.
 * @return true if valid (either clean untrained state or valid trained baseline), false on corruption.
 */
static inline bool config_thermal_baseline_is_valid(const config_thermal_baseline_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    /* If untrained/uncommissioned, state is valid but inactive */
    if (cfg->is_valid == 0U) {
        return true;
    }
    if (cfg->is_valid != 1U) {
        return false;
    }
    if (cfg->baseline_exergetic_efficiency < 20.0f || cfg->baseline_exergetic_efficiency > 65.0f) {
        return false;
    }
    if (cfg->training_hours_accumulated < 168U) {
        return false;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_CONFIG_TYPES_H_ */
