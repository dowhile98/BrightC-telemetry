/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Active Object coordinator for persistent storage.
 * Subscribes to / receives configuration change notifications, coalesces
 * multiple writes using a debounce timer (500 ms), retrieves data from
 * registered domain providers, writes to non-volatile storage via
 * i_config_storage_t, and publishes completion events to Zbus.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_STORAGE_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_STORAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "common/config_types.h"
#include "interfaces/i_config_storage.h"
#include "application/zbus/config_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Stack size reserved for the storage Active Object thread. */
#define AO_STORAGE_STACK_SIZE 2048

/** Queue depth of storage AO command queue. */
#define AO_STORAGE_QUEUE_DEPTH 16

/** Preemptive priority for the storage Active Object. */
#define AO_STORAGE_PRIORITY 7

/** Debounce time window in milliseconds before physically persisting to storage. */
#define AO_STORAGE_DEBOUNCE_MS 500U

/** Timeout to wait for storage thread to exit on stop. */
#define AO_STORAGE_STOP_WAIT_MS 3000U

/** Internal bounce buffer capacity for domain serialization. */
#define AO_STORAGE_BOUNCE_BUFFER_SIZE 2048

/** Events/commands accepted by ao_storage message queue. */
typedef enum {
    AO_STORAGE_EVENT_START = 1,     /**< Start processing inner work loop */
    AO_STORAGE_EVENT_STOP = 2,      /**< Stop inner work loop */
    AO_STORAGE_EVENT_TERMINATE = 3, /**< Terminate outer loop */
    AO_STORAGE_EVENT_DIRTY = 4,     /**< Mark domain dirty and start/restart debounce timer */
    AO_STORAGE_EVENT_FLUSH_NOW = 5, /**< Flush all dirty domains immediately */
} ao_storage_event_type_t;

/** Event payload posted to ao_storage message queue. */
typedef struct {
    ao_storage_event_type_t type;     /**< Command type */
    config_domain_t         domain;   /**< Target domain (for DIRTY) */
    uint16_t                sub_id;   /**< Sub-identifier */
} ao_storage_event_t;

/**
 * @brief Provider callback function to retrieve current domain data from RAM into buffer.
 *
 * @param ctx       Provider user context pointer.
 * @param domain    Target domain being serialized/retrieved.
 * @param out_buf   Destination buffer.
 * @param max_size  Capacity of destination buffer.
 * @param out_size  Output pointer to receive actual written size in bytes.
 * @return 0 on success, negative errno on failure.
 */
typedef int (*ao_storage_domain_provider_fn)(void *ctx,
                                            config_domain_t domain,
                                            void *out_buf,
                                            size_t max_size,
                                            size_t *out_size);

typedef struct {
    ao_storage_domain_provider_fn fn;
    void *ctx;
} ao_storage_provider_entry_t;

/**
 * @brief Active Object storage coordinator context.
 */
typedef struct {
    const i_config_storage_t *storage;   /**< Injected storage port handle */
    struct k_msgq            *queue;     /**< Command message queue */
    struct k_thread           thread;    /**< Thread control block */
    k_thread_stack_t         *stack;     /**< Stack buffer (file scope owned) */
    size_t                    stack_size;/**< Stack size in bytes */
    struct k_sem              stopped_sem;/**< Stop / termination confirmation semaphore */
    volatile bool             is_running;/**< Running flag */
    bool                      is_initialized;/**< Initialized flag */

    uint32_t                  dirty_mask;/**< Bitmask of domains pending persistence */
    struct k_timer            debounce_timer; /**< Debounce timer */

    ao_storage_provider_entry_t providers[CONFIG_DOMAIN_MAX]; /**< Registered providers */
    uint8_t                   bounce_buf[AO_STORAGE_BOUNCE_BUFFER_SIZE]; /**< Instance bounce buffer */
} ao_storage_t;

/**
 * @brief Initialize ao_storage instance.
 *
 * Spawns the worker thread once in gated state waiting for AO_STORAGE_EVENT_START on the queue.
 *
 * @param[out] self        Instance to initialize.
 * @param[in]  storage     Injected non-volatile storage port handle.
 * @param[in]  queue       Initialized message queue.
 * @param[in]  stack       Stack buffer allocated at file scope.
 * @param[in]  stack_size  Stack size in bytes.
 *
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int ao_storage_init(ao_storage_t *self,
                    const i_config_storage_t *storage,
                    struct k_msgq *queue,
                    k_thread_stack_t *stack,
                    size_t stack_size);

/**
 * @brief Register a domain data provider callback.
 *
 * @param[in,out] self      ao_storage instance.
 * @param[in]     domain    Target domain.
 * @param[in]     provider  Callback function to retrieve domain data from RAM.
 * @param[in]     ctx       User context passed to callback.
 *
 * @return 0 on success, -EINVAL on invalid domain or null arguments.
 */
int ao_storage_register_provider(ao_storage_t *self,
                                config_domain_t domain,
                                ao_storage_domain_provider_fn provider,
                                void *ctx);

/**
 * @brief Start ao_storage thread by posting AO_STORAGE_EVENT_START.
 *
 * @param[in,out] self Initialized ao_storage instance.
 * @return 0 on success, -EINVAL or -EALREADY.
 */
int ao_storage_start(ao_storage_t *self);

/**
 * @brief Post command to ao_storage message queue.
 *
 * @param[in,out] self  Active object instance.
 * @param[in]     event Event to post.
 * @return 0 on success, -EINVAL or -ENOMSG if queue is full.
 */
int ao_storage_post(ao_storage_t *self, const ao_storage_event_t *event);

/**
 * @brief Mark domain as dirty and trigger/restart debounce timer.
 * Helper function that posts AO_STORAGE_EVENT_DIRTY.
 *
 * @param[in,out] self   Active object instance.
 * @param[in]     domain Target domain.
 * @param[in]     sub_id Sub identifier.
 * @return 0 on success, -EINVAL or -ENOMSG.
 */
int ao_storage_mark_dirty(ao_storage_t *self, config_domain_t domain, uint16_t sub_id);

/**
 * @brief Trigger immediate flush of all dirty domains without waiting for debounce timer.
 *
 * @param[in,out] self Active object instance.
 * @return 0 on success, -EINVAL or -ENOMSG.
 */
int ao_storage_flush_now(ao_storage_t *self);

/**
 * @brief Flush all dirty domains synchronously in current context.
 * Useful for deterministic testing or shutdown procedures.
 *
 * @param[in,out] self Active object instance.
 * @return Number of domains persisted (>= 0), or negative errno on error.
 */
int ao_storage_sync_flush(ao_storage_t *self);

/**
 * @brief Stop ao_storage thread gracefully by setting is_running to false and posting STOP.
 *
 * Does not destroy or abort the thread.
 *
 * @param[in,out] self Active object instance.
 * @return 0 on success, -EINVAL if invalid.
 */
int ao_storage_stop(ao_storage_t *self);

/**
 * @brief Wait until ao_storage confirms it has stopped.
 *
 * @param[in,out] self    Active object instance.
 * @param[in]     timeout Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout, negative errno on error.
 */
int ao_storage_wait_stopped(ao_storage_t *self, k_timeout_t timeout);

/**
 * @brief Deinitialize ao_storage instance and terminate thread.
 *
 * @param[in,out] self Active object instance in stopped state.
 * @return 0 on success, -EBUSY if running, negative errno on error.
 */
int ao_storage_deinit(ao_storage_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_STORAGE_H_ */
