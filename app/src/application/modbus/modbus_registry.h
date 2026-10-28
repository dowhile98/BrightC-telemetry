/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Application-layer slave registry: owns the slave table, the per-bus polling
 * schedule and the health bookkeeping. All locking is internal; no caller ever
 * sees the mutex.
 */

#ifndef APP_APPLICATION_MODBUS_MODBUS_REGISTRY_H_
#define APP_APPLICATION_MODBUS_MODBUS_REGISTRY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "common/modbus_types.h"
#include "interfaces/i_slave_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Format version of the exported configuration blob. */
#define MODBUS_REGISTRY_EXPORT_VERSION 1U

/** Fixed part of the exported blob: version, bus count and bus configurations. */
#define MODBUS_REGISTRY_EXPORT_HEADER_SIZE \
    (2U + (size_t) MODBUS_BUS_MAX * 7U + 2U)

/** Per-record size in the exported blob. */
#define MODBUS_REGISTRY_EXPORT_RECORD_SIZE 9U

/** Worst-case size of the exported blob for a full table. */
#define MODBUS_REGISTRY_EXPORT_SIZE_MAX \
    (MODBUS_REGISTRY_EXPORT_HEADER_SIZE + (size_t) MODBUS_MAX_TOTAL_SLAVES * \
         MODBUS_REGISTRY_EXPORT_RECORD_SIZE)

/**
 * @brief Registry instance. Allocated by the composition root, never static.
 */
typedef struct {
    modbus_slave_snapshot_t entries[MODBUS_MAX_TOTAL_SLAVES]; /**< Slave table */
    size_t count;                                             /**< Used slots */
    modbus_bus_cfg_t bus_cfg[MODBUS_BUS_MAX];                 /**< Per-bus line config */
    uint8_t cursor[MODBUS_BUS_MAX];                           /**< Round-robin cursor */
    struct k_mutex lock;                                      /**< Internal exclusion */
} modbus_registry_t;

/**
 * @brief Initialise the registry with the configuration of every bus.
 *
 * @param[out] self Registry instance to initialise.
 * @param[in]  cfg  One configuration per bus, indexed by @ref modbus_bus_id_t.
 *
 * @return 0 on success, -EINVAL on bad arguments or an invalid bus configuration.
 */
int modbus_registry_init(modbus_registry_t *self, const modbus_bus_cfg_t cfg[MODBUS_BUS_MAX]);

/**
 * @brief Insert a new slave or update an existing one by device id.
 *
 * An update that keeps the same bus and slave address preserves the runtime
 * statistics; changing either of them resets the record because it now
 * describes a different device on the wire.
 *
 * @return 0 on success, -EINVAL, -ENOSPC, -EADDRINUSE.
 */
int modbus_registry_register(modbus_registry_t *self, const modbus_slave_descriptor_t *desc);

/**
 * @brief Atomically claim the next slave of a bus that is due for polling.
 *
 * @return 0 when a target was claimed, -ENOENT when nothing is due, -EINVAL.
 */
int modbus_registry_claim_next_due(modbus_registry_t *self, uint8_t bus_id, uint64_t now_ms,
                                   modbus_poll_target_t *out);

/**
 * @brief Release a claim and fold the transaction outcome into the health record.
 *
 * @return 0 on success, -ENOENT for an unknown device.
 */
int modbus_registry_record(modbus_registry_t *self, uint8_t device_id,
                           modbus_transaction_result_t result, uint64_t now_ms);

/**
 * @brief Reassign a device to another bus (field failover).
 *
 * @return 0 on success, -ENOENT, -EINVAL, -ENOSPC, -EADDRINUSE.
 */
int modbus_registry_set_bus(modbus_registry_t *self, uint8_t device_id, uint8_t new_bus_id);

/**
 * @brief Read the electrical configuration applied to a bus.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int modbus_registry_get_bus_cfg(const modbus_registry_t *self, uint8_t bus_id,
                                modbus_bus_cfg_t *out);

/**
 * @brief Copy one descriptor by device id.
 *
 * @return 0 on success, -ENOENT when the device is unknown.
 */
int modbus_registry_get(const modbus_registry_t *self, uint8_t device_id,
                        modbus_slave_descriptor_t *out);

/**
 * @brief Copy every device of a bus into a caller-provided array.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int modbus_registry_snapshot(modbus_registry_t *self, uint8_t bus_id,
                             modbus_slave_snapshot_t *out, size_t max, size_t *out_count);

/**
 * @brief Serialise the bus configuration and the slave table.
 *
 * The blob is versioned and little-endian, independent of struct padding and
 * of the CPU endianness.
 *
 * @return 0 on success, -EINVAL on bad arguments, -ENOSPC when @p max is small.
 */
int modbus_registry_export(const modbus_registry_t *self, void *buf, size_t max,
                           size_t *out_size);

/**
 * @brief Replace the bus configuration and the slave table from a blob.
 *
 * The whole blob is validated before anything is mutated: a rejected import
 * leaves the registry untouched.
 *
 * @return 0 on success, -EINVAL on a corrupt, unsupported or inconsistent blob.
 */
int modbus_registry_import(modbus_registry_t *self, const void *buf, size_t size);

/**
 * @brief Time until the next slave of a bus becomes due.
 *
 * Lets a polling thread sleep exactly as long as needed instead of using a
 * fixed tick.
 *
 * @param[in]  self   Registry instance.
 * @param[in]  bus_id Bus to inspect.
 * @param[in]  now_ms Monotonic time in milliseconds.
 * @param[out] out_ms Milliseconds to wait; 0 when a slave is already due.
 *
 * @return 0 on success, -EINVAL on bad arguments, -ENOENT when the bus has no slaves.
 */
int modbus_registry_next_due_in(const modbus_registry_t *self, uint8_t bus_id, uint64_t now_ms,
                                uint32_t *out_ms);

/**
 * @brief Bind the registry to its port handle.
 *
 * @param[in]  self Registry instance (must outlive the handle).
 * @param[out] out  Handle to fill.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int modbus_registry_port(modbus_registry_t *self, i_slave_registry_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_MODBUS_MODBUS_REGISTRY_H_ */
