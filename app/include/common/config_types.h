/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Domain types, enums, event definitions and memory layout definitions
 * for the storage subsystem.
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_COMMON_CONFIG_TYPES_H_
#define APP_COMMON_CONFIG_TYPES_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logical configuration categories persisted in non-volatile storage.
 */
typedef enum {
    CONFIG_DOMAIN_DEVICE = 0,    /**< Device information and metadata */
    CONFIG_DOMAIN_MODBUS,        /**< Bus and slave configuration */
    CONFIG_DOMAIN_NET_ETH,       /**< Ethernet interface configuration (IP, DHCP, MAC) */
    CONFIG_DOMAIN_NET_WIFI,      /**< Wi-Fi interface configuration (mode, SSID, credentials) */
    CONFIG_DOMAIN_CALIBRATION,   /**< Analog input calibration constants */
    CONFIG_DOMAIN_SENSOR_CFG,    /**< Physical to semantic sensor routing table */
    CONFIG_DOMAIN_BASELINE,      /**< 7-day learned OLS coefficients and thermal baseline */
    CONFIG_DOMAIN_HVAC,          /**< Industrial HVAC sequencer & hardware routing config */
    CONFIG_DOMAIN_MAX            /**< Upper limit of domains */
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
    uint16_t            sub_id;      /**< Optional sub-identifier (e.g. slave index or interface) */
    int32_t             status;      /**< Result code (0 on success, negative errno on error) */
} system_config_event_t;

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
 * According to EEPROM AT24C256C Memory Map (doc/architecture/storage_subsystem.md):
 * - CONFIG_DOMAIN_DEVICE:      0x0000, 1024 B (2 slots of 512 B)
 * - CONFIG_DOMAIN_MODBUS:      0x0400, 2048 B (2 slots of 1024 B)
 * - CONFIG_DOMAIN_NET_ETH:     0x0C00, 1024 B (2 slots of 512 B)
 * - CONFIG_DOMAIN_NET_WIFI:    0x1000, 1024 B (2 slots of 512 B)
 * - CONFIG_DOMAIN_CALIBRATION: 0x1400, 1024 B (2 slots of 512 B)
 * - CONFIG_DOMAIN_SENSOR_CFG:  0x1800, 2048 B (2 slots of 1024 B)
 * - CONFIG_DOMAIN_BASELINE:    0x2000, 2048 B (2 slots of 1024 B)
 * - CONFIG_DOMAIN_HVAC:        0x2800, 2048 B (2 slots of 1024 B)
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
        total = 1024;
        slot = 512;
        break;
    case CONFIG_DOMAIN_MODBUS:
        base = 0x0400;
        total = 2048;
        slot = 1024;
        break;
    case CONFIG_DOMAIN_NET_ETH:
        base = 0x0C00;
        total = 1024;
        slot = 512;
        break;
    case CONFIG_DOMAIN_NET_WIFI:
        base = 0x1000;
        total = 1024;
        slot = 512;
        break;
    case CONFIG_DOMAIN_CALIBRATION:
        base = 0x1400;
        total = 1024;
        slot = 512;
        break;
    case CONFIG_DOMAIN_SENSOR_CFG:
        base = 0x1800;
        total = 2048;
        slot = 1024;
        break;
    case CONFIG_DOMAIN_BASELINE:
        base = 0x2000;
        total = 2048;
        slot = 1024;
        break;
    case CONFIG_DOMAIN_HVAC:
        base = 0x2800;
        total = 2048;
        slot = 1024;
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

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_CONFIG_TYPES_H_ */
