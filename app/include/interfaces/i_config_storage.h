/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract port interface for persistent configuration storage.
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-table (i_config_storage_api_t)
 * 2) Handle (i_config_storage_t)
 * 3) Validated wrappers (static inline i_config_storage_*())
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_CONFIG_STORAGE_H_
#define APP_INTERFACES_I_CONFIG_STORAGE_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE (implementation detail, never called directly by application)
 *============================================================================*/

typedef struct {
    /**
     * @brief Initialize storage backend hardware/subsystem.
     *
     * @param impl Pointer to driver/adapter context.
     * @return 0 on success, negative errno on failure.
     */
    int (*init)(void *impl);

    /**
     * @brief Save domain payload to non-volatile storage.
     *
     * @param impl Pointer to driver/adapter context.
     * @param domain Configuration category domain.
     * @param data Pointer to data buffer to persist.
     * @param size Size in bytes of data buffer.
     * @return 0 on success, negative errno on failure.
     */
    int (*save)(void *impl, config_domain_t domain, const void *data, size_t size);

    /**
     * @brief Load domain payload from non-volatile storage.
     *
     * @param impl Pointer to driver/adapter context.
     * @param domain Configuration category domain.
     * @param out_data Output buffer to copy persisted data into.
     * @param max_size Maximum capacity of out_data buffer.
     * @param out_size Optional pointer to receive actual loaded size.
     * @return 0 on success, negative errno on failure.
     */
    int (*load)(void *impl, config_domain_t domain, void *out_data, size_t max_size, size_t *out_size);

    /**
     * @brief Reset domain in non-volatile storage to defaults or erase slot.
     *
     * @param impl Pointer to driver/adapter context.
     * @param domain Configuration category domain.
     * @return 0 on success, negative errno on failure.
     */
    int (*reset_defaults)(void *impl, config_domain_t domain);
} i_config_storage_api_t;

/*============================================================================*
 * 2. HANDLE (fat pointer passed around to consumers)
 *============================================================================*/

typedef struct {
    const i_config_storage_api_t *api;
    void *impl;
} i_config_storage_t;

/*============================================================================*
 * 3. VALIDATED WRAPPERS (public interface used by application layer)
 *============================================================================*/

/**
 * @brief Check whether an i_config_storage_t handle is bound to a valid implementation.
 */
static inline bool i_config_storage_is_bound(const i_config_storage_t *storage)
{
    return (storage != NULL &&
            storage->api != NULL &&
            storage->impl != NULL &&
            storage->api->init != NULL &&
            storage->api->save != NULL &&
            storage->api->load != NULL &&
            storage->api->reset_defaults != NULL);
}

/**
 * @brief Initialize storage hardware through the abstract interface.
 */
static inline int i_config_storage_init(const i_config_storage_t *storage)
{
    if (!i_config_storage_is_bound(storage)) {
        return -EINVAL;
    }
    return storage->api->init(storage->impl);
}

/**
 * @brief Persist domain configuration through the abstract interface.
 */
static inline int i_config_storage_save(const i_config_storage_t *storage,
                                       config_domain_t domain,
                                       const void *data,
                                       size_t size)
{
    if (!i_config_storage_is_bound(storage)) {
        return -EINVAL;
    }
    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }
    if (data == NULL || size == 0) {
        return -EINVAL;
    }
    return storage->api->save(storage->impl, domain, data, size);
}

/**
 * @brief Load domain configuration through the abstract interface.
 */
static inline int i_config_storage_load(const i_config_storage_t *storage,
                                       config_domain_t domain,
                                       void *out_data,
                                       size_t max_size,
                                       size_t *out_size)
{
    if (!i_config_storage_is_bound(storage)) {
        return -EINVAL;
    }
    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }
    if (out_data == NULL || max_size == 0) {
        return -EINVAL;
    }
    return storage->api->load(storage->impl, domain, out_data, max_size, out_size);
}

/**
 * @brief Reset domain configuration through the abstract interface.
 */
static inline int i_config_storage_reset_defaults(const i_config_storage_t *storage,
                                                 config_domain_t domain)
{
    if (!i_config_storage_is_bound(storage)) {
        return -EINVAL;
    }
    if (!config_domain_is_valid(domain)) {
        return -EINVAL;
    }
    return storage->api->reset_defaults(storage->impl, domain);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_CONFIG_STORAGE_H_ */
