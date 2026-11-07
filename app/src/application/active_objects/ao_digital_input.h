/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_digital_input.h
 * @brief Active Object coordinator for Digital Inputs and Contact Alarms.
 *
 * Drives periodic 20 ms (50 Hz) sampling, debounce filtering, and polarity
 * normalization via di_aggregator_t, publishing state transition events to
 * Zbus chan_digital_input_state.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_DIGITAL_INPUT_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_DIGITAL_INPUT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "common/digital_input_types.h"
#include "domain/services/digital_input_aggregator.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Thread stack size reserved for the Digital Input Active Object. */
#define AO_DIGITAL_INPUT_STACK_SIZE 2048

/** Queue depth for incoming control events. */
#define AO_DI_QUEUE_DEPTH 8

/** Preemptive priority for the Digital Input Active Object thread. */
#define AO_DIGITAL_INPUT_PRIORITY 6

/** Default periodic scan rate: 20 ms = 50 Hz. */
#define AO_DIGITAL_INPUT_SCAN_PERIOD_MS 20U

/** Timeout to wait for the Active Object thread to exit on stop. */
#define AO_DIGITAL_INPUT_STOP_WAIT_MS 1000U

/**
 * @brief Command event types for ao_digital_input.
 */
typedef enum {
    AO_DI_EVENT_SCAN_NOW = 0, /**< Trigger an immediate scan cycle */
    AO_DI_EVENT_STOP          /**< Terminate Active Object thread */
} ao_di_event_type_t;

/**
 * @brief Event message structure for ao_digital_input queue.
 */
typedef struct {
    ao_di_event_type_t type;
} ao_di_event_t;

/**
 * @brief Active Object Digital Input coordinator context.
 */
typedef struct {
    di_aggregator_t  *aggregator;     /**< Aggregator domain service instance */
    struct k_msgq    *queue;          /**< Command queue */
    struct k_thread   thread;         /**< Kernel thread control block */
    k_thread_stack_t *stack;          /**< Thread stack pointer */
    size_t            stack_size;     /**< Thread stack size */
    struct k_sem      run_sem;        /**< Outer gate semaphore */
    struct k_sem      stopped_sem;    /**< Stop confirmation semaphore */
    volatile bool     is_running;     /**< Active execution flag */
    volatile bool     terminate;      /**< Outer loop exit flag */
    bool              is_initialized; /**< Initialization guard */
    uint64_t          last_scan_ms;   /**< Uptime of preceding scan */
    uint32_t          sequence;       /**< Monotonically increasing snapshot sequence */
} ao_digital_input_t;

/**
 * @brief Initialize the Digital Input Active Object.
 *
 * Spawns the thread once in gated state waiting on run_sem.
 *
 * @param[out] self        Instance to initialize.
 * @param[in]  aggregator  Aggregator domain service instance.
 * @param[in]  queue       Pre-allocated message queue (depth >= AO_DI_QUEUE_DEPTH).
 * @param[in]  stack       File-scope allocated thread stack buffer.
 * @param[in]  stack_size  Size of the stack buffer in bytes.
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_digital_input_init(ao_digital_input_t *self,
                          di_aggregator_t *aggregator,
                          struct k_msgq *queue,
                          k_thread_stack_t *stack,
                          size_t stack_size);

/**
 * @brief Start the Digital Input Active Object thread.
 *
 * Unblocks the gate semaphore without recreating the thread.
 *
 * @param[in,out] self Initialized Active Object instance.
 * @return 0 on success, -EALREADY if already running, -EINVAL on invalid args.
 */
int ao_digital_input_start(ao_digital_input_t *self);

/**
 * @brief Request immediate digital input scan cycle.
 *
 * @param[in,out] self Active Object instance.
 * @return 0 on success, -EAGAIN if not running, negative errno on error.
 */
int ao_digital_input_scan_now(ao_digital_input_t *self);

/**
 * @brief Stop the Digital Input Active Object thread gracefully.
 *
 * Sets is_running to false and unblocks the inner loop. Does not abort or destroy thread.
 *
 * @param[in,out] self Active Object instance.
 * @return 0 on success, negative errno on error.
 */
int ao_digital_input_stop(ao_digital_input_t *self);

/**
 * @brief Wait until the Digital Input Active Object confirms it has stopped.
 *
 * @param[in,out] self    Active Object instance.
 * @param[in]     timeout Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout, negative errno on error.
 */
int ao_digital_input_wait_stopped(ao_digital_input_t *self, k_timeout_t timeout);

/**
 * @brief Deinitialize the Digital Input Active Object and terminate worker thread.
 *
 * @param[in,out] self Active Object instance in stopped state.
 * @return 0 on success, -EBUSY if running, negative errno on error.
 */
int ao_digital_input_deinit(ao_digital_input_t *self);

/**
 * @brief Execute one synchronous scan cycle (exposed for deterministic testing).
 *
 * Calls di_aggregator_scan() and publishes any transitions to Zbus chan_digital_input_state.
 *
 * @param[in,out] self       Active Object instance.
 * @param[in]     elapsed_ms Elapsed time in milliseconds since preceding scan.
 * @return Number of state transitions detected and published (>= 0), or negative errno.
 */
int ao_digital_input_step(ao_digital_input_t *self, uint32_t elapsed_ms);

/**
 * @brief Check whether the Active Object thread is running.
 *
 * @param[in] self Active Object instance.
 * @return true if running, false otherwise.
 */
static inline bool ao_digital_input_is_running(const ao_digital_input_t *self)
{
    return (self != NULL && self->is_running);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_DIGITAL_INPUT_H_ */
