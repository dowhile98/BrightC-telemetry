/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Active Object that drives the polling loop of one physical Modbus bus.
 * One instance per bus: the polling client call is blocking, so a single
 * thread shared by both buses would let an offline bus starve the healthy one.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_MODBUS_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_MODBUS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "common/modbus_types.h"
#include "interfaces/i_modbus_client.h"
#include "interfaces/i_slave_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Stack reserved for one polling thread.
 *
 * An estimate, not a measurement: CONFIG_THREAD_ANALYZER is off, so re-measure
 * with CONFIG_THREAD_ANALYZER=y before adding work (float decoding, logging)
 * to the polling loop.
 */
#define AO_MODBUS_STACK_SIZE 2048

/** Pending command capacity of one polling thread. */
#define AO_MODBUS_QUEUE_DEPTH 8

/** Preemptive priority shared by both polling threads. */
#define AO_MODBUS_PRIORITY 7

/** Longest idle wait, so external registry changes are noticed promptly. */
#define AO_MODBUS_MAX_IDLE_WAIT_MS 1000U

/** How long ao_modbus_stop() waits for the thread to leave its loop. */
#define AO_MODBUS_STOP_WAIT_MS 3000U

/** Commands accepted by a polling thread. */
typedef enum {
    AO_MODBUS_EVENT_POLL_NOW = 0, /**< Break the current wait and poll immediately */
    AO_MODBUS_EVENT_STOP          /**< Leave the polling loop */
} modbus_event_type_t;

/**
 * @brief Command posted to a polling thread.
 */
typedef struct {
    uint8_t type; /**< modbus_event_type_t */
} modbus_event_t;

/**
 * @brief One polling thread and everything it needs.
 */
typedef struct {
    uint8_t bus_id;                /**< Bus served by this instance */
    const i_slave_registry_t *registry; /**< Injected registry port (shared) */
    const i_modbus_client_t *client;    /**< Injected client port for this bus */
    struct k_msgq *queue;          /**< Command queue (storage owned by the container) */
    struct k_thread thread;        /**< Thread control block */
    k_thread_stack_t *stack;       /**< Stack storage (owned by the container) */
    size_t stack_size;             /**< Usable stack size in bytes */
    volatile bool is_running;      /**< True between start and the STOP command */
} ao_modbus_t;

/**
 * @brief Prepare an Active Object. Does not start the thread.
 *
 * @param[out] self       Instance to initialise.
 * @param[in]  bus_id     Bus served by this instance.
 * @param[in]  registry   Registry port (shared by both buses).
 * @param[in]  client     Client port of this bus (may be unbound, then the loop idles).
 * @param[in]  queue      Already initialised command queue.
 * @param[in]  stack      Stack storage, @p stack_size bytes.
 * @param[in]  stack_size Usable size of @p stack.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int ao_modbus_init(ao_modbus_t *self, uint8_t bus_id, const i_slave_registry_t *registry,
                   const i_modbus_client_t *client, struct k_msgq *queue, k_thread_stack_t *stack,
                   size_t stack_size);

/**
 * @brief Create the polling thread.
 *
 * Pending commands are discarded first: a STOP left over from a previous run
 * would otherwise be consumed by the new thread, which would exit immediately
 * while start() reported success.
 *
 * @return 0 on success, -EINVAL on bad arguments, -EALREADY when already running.
 */
int ao_modbus_start(ao_modbus_t *self);

/**
 * @brief Post a command without waiting.
 *
 * @return 0 on success, -EINVAL, -ENOMSG when the queue is full.
 */
int ao_modbus_post(ao_modbus_t *self, const modbus_event_t *event);

/**
 * @brief Ask the thread to leave its loop and wait for it to do so.
 *
 * @return 0 on success, -EINVAL on bad arguments, -EBUSY when the thread did
 *         not finish within @ref AO_MODBUS_STOP_WAIT_MS.
 */
int ao_modbus_stop(ao_modbus_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_MODBUS_H_ */
