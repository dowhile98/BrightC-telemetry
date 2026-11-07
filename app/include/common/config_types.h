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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logical configuration categories persisted in non-volatile storage.
 *
 * Reduced to the 5 essential site-commissioning domains per Architectural Decision
 * Record (ADR-6). Hardware pinouts, Modbus topologies, and sensor channels are
 * declared compile-time in firmware.
 */
typedef enum {
    CONFIG_DOMAIN_DEVICE = 0,    /**< Device serial number, hardware rev, station alias */
    CONFIG_DOMAIN_NET_ETH,       /**< Ethernet interface configuration (IP, DHCP, MAC, Gateway) */
    CONFIG_DOMAIN_NET_WIFI,      /**< Wi-Fi interface configuration (Mode, SSID, Credentials) */
    CONFIG_DOMAIN_HVAC,          /**< Industrial HVAC sequencer operational setpoints & group ranges */
    CONFIG_DOMAIN_CALIBRATION,   /**< Analog input & PT100 temperature calibration offsets */
    CONFIG_DOMAIN_MAX            /**< Upper limit of active storage domains (5) */
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
 * Partitioning of the 32 KB AT24C256C EEPROM (ADR-6):
 * - CONFIG_DOMAIN_DEVICE:      0x0000, 256 B (2 ping-pong slots of 128 B, 2 pages)
 * - CONFIG_DOMAIN_NET_ETH:     0x0100, 128 B (2 ping-pong slots of 64 B, 1 page)
 * - CONFIG_DOMAIN_NET_WIFI:    0x0180, 640 B (2 ping-pong slots of 320 B, 5 pages)
 * - CONFIG_DOMAIN_HVAC:        0x0400, 1152 B (2 ping-pong slots of 576 B, 9 pages)
 * - CONFIG_DOMAIN_CALIBRATION: 0x0880, 384 B (2 ping-pong slots of 192 B, 3 pages)
 * - RESERVED / EVENT LOG:      0x0A00 .. 0x7FFF (29.5 KB available)
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
