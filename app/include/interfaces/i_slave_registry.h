/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Port: Modbus slave registry.
 * Contract for the component that owns the slave table, the per-bus polling
 * schedule and the health bookkeeping. Implemented by modbus_registry (runtime)
 * and by an in-memory fake in tests/fakes.
 */

#ifndef APP_INTERFACES_I_SLAVE_REGISTRY_H_
#define APP_INTERFACES_I_SLAVE_REGISTRY_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "common/modbus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Operations exposed by a slave registry implementation.
 *
 * Every callback receives the opaque implementation pointer as @p impl; the
 * implementation is responsible for its own synchronisation. Callers never
 * hold a lock across two operations.
 *
 * These pointers are implementation detail: application code reaches the
 * registry only through the `i_slave_registry_*()` wrappers declared at the
 * end of this header. Only an implementation (or the composition root while
 * binding one) refers to this table.
 */
typedef struct {
    /**
     * @brief Insert a new slave or update an existing one by device_id.
     *
     * @return 0 on success, -EINVAL on invalid descriptor, -ENOSPC when the bus
     *         or the global table is full, -EADDRINUSE when another device
     *         already uses the same address on the same bus, -ENOENT when the
     *         device exists but cannot be updated.
     */
    int (*register_device)(void *impl, const modbus_slave_descriptor_t *desc);

    /**
     * @brief Atomically claim the next slave of a bus that is due for polling.
     *
     * The call marks the slave as in-flight and updates its attempt timestamp,
     * so a second caller cannot claim it concurrently. The claim is released by
     * @ref record. A claim older than the implementation-defined stale timeout
     * is reclaimed automatically.
     *
     * @param[in]  impl    Implementation.
     * @param[in]  bus_id  Bus to scan (modbus_bus_id_t).
     * @param[in]  now_ms  Monotonic time in milliseconds.
     * @param[out] out     Target to poll; untouched when nothing is due.
     *
     * @return 0 when a target was claimed, -ENOENT when nothing is due,
     *         -EINVAL on bad arguments.
     */
    int (*claim_next_due)(void *impl, uint8_t bus_id, uint64_t now_ms, modbus_poll_target_t *out);

    /**
     * @brief Release a claim and fold the transaction outcome into health.
     *
     * @return 0 on success, -ENOENT when the device is unknown.
     */
    int (*record)(void *impl, uint8_t device_id, modbus_transaction_result_t result,
                  uint64_t now_ms);

    /**
     * @brief Time until the next slave of a bus becomes due.
     *
     * @param[out] out_ms Milliseconds to wait; 0 when a slave is already due.
     *
     * @return 0 on success, -EINVAL on bad arguments, -ENOENT when the bus has
     *         no enabled slave.
     */
    int (*next_due_in)(void *impl, uint8_t bus_id, uint64_t now_ms, uint32_t *out_ms);

    /**
     * @brief Reassign a device to another bus (field failover).
     *
     * Fails when the target bus is full or already contains the same slave
     * address, so a failover can never create an address collision.
     *
     * @return 0 on success, -ENOENT unknown device, -EINVAL invalid bus,
     *         -ENOSPC target bus full, -EADDRINUSE address already present.
     */
    int (*set_bus)(void *impl, uint8_t device_id, uint8_t new_bus_id);

    /**
     * @brief Read the electrical configuration of a bus.
     *
     * @return 0 on success, -EINVAL invalid bus.
     */
    int (*get_bus_cfg)(void *impl, uint8_t bus_id, modbus_bus_cfg_t *out);

    /**
     * @brief Copy every device of a bus into a caller-provided array.
     *
     * @param[out] out        Destination array of at least @p max entries.
     * @param[in]  max        Capacity of @p out.
     * @param[out] out_count  Number of entries written.
     *
     * @return 0 on success, -EINVAL on bad arguments.
     */
    int (*snapshot)(void *impl, uint8_t bus_id, modbus_slave_snapshot_t *out, size_t max,
                    size_t *out_count);
} i_slave_registry_api_t;

/**
 * @brief Registry handle: vtable plus opaque implementation pointer.
 *
 * Callers must use the `i_slave_registry_*()` inline wrappers below and never
 * dereference @ref api directly: the wrappers are the only place where the
 * handle and the arguments are validated before dispatch. Direct access is
 * reserved for the composition root while binding an implementation.
 */
typedef struct {
    const i_slave_registry_api_t *api; /**< Operation table (never NULL when bound) */
    void *impl;                        /**< Concrete registry instance */
} i_slave_registry_t;

/**
 * @brief Check that a registry handle is bound to an implementation.
 *
 * @param[in] registry Handle to test (may be NULL).
 * @return true when both the vtable and the implementation are present.
 */
static inline bool i_slave_registry_is_bound(const i_slave_registry_t *registry)
{
    return registry != NULL && registry->api != NULL && registry->impl != NULL;
}

/*============================================================================*
 * VALIDATED OPERATIONS
 *
 * Every operation is a `static inline` wrapper: it rejects an unbound handle
 * and structurally invalid arguments with -EINVAL and only then dispatches
 * through the vtable. Semantic or stateful validation (is the bus full, does
 * the device exist, is it already claimed) stays in the implementation, which
 * is the only layer that can answer it.
 *============================================================================*/

/**
 * @brief Insert or update a slave device.
 *
 * @param[in] registry Bound registry handle.
 * @param[in] desc     Descriptor to register; validated before dispatch.
 *
 * @return 0 on success, -EINVAL on an unbound handle or an inconsistent
 *         descriptor, or the implementation error (-ENOSPC, -EADDRINUSE...).
 */
static inline int i_slave_registry_register_device(const i_slave_registry_t *registry,
                                                   const modbus_slave_descriptor_t *desc)
{
    if (!i_slave_registry_is_bound(registry) || !modbus_slave_descriptor_is_valid(desc)) {
        return -EINVAL;
    }

    return registry->api->register_device(registry->impl, desc);
}

/**
 * @brief Claim the next slave of a bus that is due for polling.
 *
 * @param[in]  registry Bound registry handle.
 * @param[in]  bus_id   Bus to scan.
 * @param[in]  now_ms   Monotonic time in milliseconds.
 * @param[out] out      Target to poll; must not be NULL.
 *
 * @return 0 when a target was claimed, -EINVAL on bad arguments, -ENOENT when
 *         nothing is due.
 */
static inline int i_slave_registry_claim_next_due(const i_slave_registry_t *registry,
                                                  uint8_t bus_id, uint64_t now_ms,
                                                  modbus_poll_target_t *out)
{
    if (!i_slave_registry_is_bound(registry) || !modbus_bus_id_is_valid(bus_id) || out == NULL) {
        return -EINVAL;
    }

    return registry->api->claim_next_due(registry->impl, bus_id, now_ms, out);
}

/**
 * @brief Release a claim and fold the outcome into the health record.
 *
 * @param[in] registry  Bound registry handle.
 * @param[in] device_id Logical id returned by @ref i_slave_registry_claim_next_due.
 * @param[in] result    Transaction outcome.
 * @param[in] now_ms    Monotonic time in milliseconds.
 *
 * @return 0 on success, -EINVAL on bad arguments, -ENOENT for an unknown device.
 */
static inline int i_slave_registry_record(const i_slave_registry_t *registry, uint8_t device_id,
                                          modbus_transaction_result_t result, uint64_t now_ms)
{
    if (!i_slave_registry_is_bound(registry) || device_id == 0U ||
        !modbus_transaction_result_is_valid(result)) {
        return -EINVAL;
    }

    return registry->api->record(registry->impl, device_id, result, now_ms);
}

/**
 * @brief Time until the next slave of a bus becomes due.
 *
 * @param[in]  registry Bound registry handle.
 * @param[in]  bus_id   Bus to inspect.
 * @param[in]  now_ms   Monotonic time in milliseconds.
 * @param[out] out_ms   Milliseconds to wait (0 when a slave is already due).
 *
 * @return 0 on success, -EINVAL on bad arguments, -ENOENT when the bus has no
 *         enabled slave.
 */
static inline int i_slave_registry_next_due_in(const i_slave_registry_t *registry, uint8_t bus_id,
                                               uint64_t now_ms, uint32_t *out_ms)
{
    if (!i_slave_registry_is_bound(registry) || !modbus_bus_id_is_valid(bus_id) ||
        out_ms == NULL) {
        return -EINVAL;
    }

    return registry->api->next_due_in(registry->impl, bus_id, now_ms, out_ms);
}

/**
 * @brief Reassign a device to another bus (field failover).
 *
 * @param[in] registry    Bound registry handle.
 * @param[in] device_id   Device to move.
 * @param[in] new_bus_id  Destination bus.
 *
 * @return 0 on success, -EINVAL on bad arguments, -ENOENT for an unknown
 *         device, -ENOSPC when the target bus is full, -EADDRINUSE when the
 *         target bus already carries that slave address.
 */
static inline int i_slave_registry_set_bus(const i_slave_registry_t *registry, uint8_t device_id,
                                           uint8_t new_bus_id)
{
    if (!i_slave_registry_is_bound(registry) || device_id == 0U ||
        !modbus_bus_id_is_valid(new_bus_id)) {
        return -EINVAL;
    }

    return registry->api->set_bus(registry->impl, device_id, new_bus_id);
}

/**
 * @brief Read the electrical configuration of a bus.
 *
 * @param[in]  registry Bound registry handle.
 * @param[in]  bus_id   Bus to read.
 * @param[out] out      Destination; must not be NULL.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
static inline int i_slave_registry_get_bus_cfg(const i_slave_registry_t *registry, uint8_t bus_id,
                                                modbus_bus_cfg_t *out)
{
    if (!i_slave_registry_is_bound(registry) || !modbus_bus_id_is_valid(bus_id) || out == NULL) {
        return -EINVAL;
    }

    return registry->api->get_bus_cfg(registry->impl, bus_id, out);
}

/**
 * @brief Copy every device of a bus into a caller-provided array.
 *
 * @param[in]  registry  Bound registry handle.
 * @param[in]  bus_id    Bus to copy.
 * @param[out] out       Destination array of at least @p max entries.
 * @param[in]  max       Capacity of @p out; must be greater than zero.
 * @param[out] out_count Number of entries written.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
static inline int i_slave_registry_snapshot(const i_slave_registry_t *registry, uint8_t bus_id,
                                            modbus_slave_snapshot_t *out, size_t max,
                                            size_t *out_count)
{
    if (!i_slave_registry_is_bound(registry) || !modbus_bus_id_is_valid(bus_id) || out == NULL ||
        out_count == NULL || max == 0U) {
        return -EINVAL;
    }

    return registry->api->snapshot(registry->impl, bus_id, out, max, out_count);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_SLAVE_REGISTRY_H_ */
