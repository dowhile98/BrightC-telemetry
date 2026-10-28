/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modbus Slave Device Configuration & Health Manager.
 * Prepared for microconf & EEPROM persistence.
 */

#ifndef APP_MODBUS_SLAVE_MANAGER_H_
#define APP_MODBUS_SLAVE_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "common/modbus_slave_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_OFFLINE_BACKOFF_INTERVAL_MS 10000U
#define MODBUS_CONSECUTIVE_FAILS_THRESHOLD 3U

/**
 * @brief Initialize the slave manager with factory defaults.
 */
int modbus_slave_manager_init(void);

/**
 * @brief Register or update a slave device configuration.
 */
int modbus_slave_manager_register(const modbus_slave_descriptor_t *desc);

/**
 * @brief Get device descriptor by logical ID.
 */
int modbus_slave_manager_get(uint8_t device_id, modbus_slave_descriptor_t *out_desc);

/**
 * @brief Dynamically reassign a device to a different RS485 bus (Failover support).
 */
int modbus_slave_manager_set_bus(uint8_t device_id, modbus_bus_id_t new_bus_id);

/**
 * @brief Retrieve all active device IDs assigned to a specific physical bus.
 */
int modbus_slave_manager_get_bus_devices(modbus_bus_id_t bus_id, uint8_t *out_device_ids, 
                                        size_t max_count, size_t *out_count);

/**
 * @brief Update device health status after a transaction attempt.
 */
void modbus_slave_manager_record_result(uint8_t device_id, bool success, uint64_t now_ms);

/**
 * @brief Check if a device is ready for its next scheduled poll (incorporates offline backoff).
 */
bool modbus_slave_manager_is_ready_to_poll(uint8_t device_id, uint64_t now_ms);

/**
 * @brief Get runtime state/health for a device.
 */
int modbus_slave_manager_get_runtime_state(uint8_t device_id, modbus_slave_runtime_state_t *out_state);

/**
 * @brief Export all device configurations to a raw buffer (for microconf / EEPROM storage).
 */
int modbus_slave_manager_export_config(void *buffer, size_t max_size, size_t *out_size);

/**
 * @brief Import device configurations from a raw buffer (from microconf / EEPROM storage).
 */
int modbus_slave_manager_import_config(const void *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* APP_MODBUS_SLAVE_MANAGER_H_ */
