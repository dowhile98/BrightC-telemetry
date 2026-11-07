/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_sensor_matrix.h
 * @brief Active Object that drives the periodic sensor matrix ingestion cycle.
 *
 * Implements the canonical Pattern A (Periodic Polling) lifecycle described in
 * doc/architecture/active_object_architecture.md:
 *   UNINITIALIZED -> READY -> RUNNING -> STOPPING -> READY -> UNINITIALIZED
 *
 * The AO owns no hardware: it drives the injected i_sensor_matrix_t port, which
 * the composition root binds to the ADC DMA adapter.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_SENSOR_MATRIX_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_SENSOR_MATRIX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "common/sensor_types.h"
#include "interfaces/i_sensor_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Thread stack size reserved for the Sensor Matrix Active Object. */
#define AO_SENSOR_MATRIX_STACK_SIZE 2048

/** Queue depth for incoming control events. */
#define AO_SENSOR_MATRIX_QUEUE_DEPTH 8

/** Preemptive priority for the Sensor Matrix Active Object thread. */
#define AO_SENSOR_MATRIX_PRIORITY 6

/** Default periodic acquisition period: 500 ms = 2 Hz. */
#define AO_SENSOR_MATRIX_PERIOD_MS 500U

/** Timeout to wait for the Active Object thread to exit on stop. */
#define AO_SENSOR_MATRIX_STOP_WAIT_MS 1000U

/**
 * @brief Callback invoked after every successful mapping cycle.
 *
 * Allows the composition root to route a fresh frame towards HVAC and the
 * presentation facade without the Active Object depending on either of them.
 *
 * @param[in] ctx   Opaque context supplied at init time.
 * @param[in] frame Freshly mapped semantic frame (valid only during the call).
 */
typedef void (*ao_sensor_matrix_frame_sink_t)(void *ctx,
                                              const thermal_matrix_frame_t *frame);

/**
 * @brief Command event types for ao_sensor_matrix queue.
 */
typedef enum {
    AO_SENSOR_MATRIX_EVENT_UPDATE_NOW = 0, /**< Trigger an immediate acquisition cycle */
    AO_SENSOR_MATRIX_EVENT_STOP            /**< Terminate Active Object inner loop */
} ao_sensor_matrix_event_type_t;

/**
 * @brief Event message structure for ao_sensor_matrix queue.
 */
typedef struct {
    ao_sensor_matrix_event_type_t type;
} ao_sensor_matrix_event_t;

/**
 * @brief Construction parameters for the Sensor Matrix Active Object.
 */
typedef struct {
    const i_sensor_matrix_t       *matrix;        /**< Injected sensor matrix port */
    ao_sensor_matrix_frame_sink_t  frame_sink;    /**< Optional frame consumer */
    void                          *frame_sink_ctx;/**< Opaque context for frame_sink */
    uint32_t                       poll_period_ms;/**< 0 selects AO_SENSOR_MATRIX_PERIOD_MS */
    k_thread_stack_t              *stack;         /**< File-scope stack buffer */
    size_t                         stack_size;    /**< Usable stack size in bytes */
} ao_sensor_matrix_config_t;

/**
 * @brief Active Object Sensor Matrix coordinator context.
 */
typedef struct {
    const i_sensor_matrix_t       *matrix;
    ao_sensor_matrix_frame_sink_t  frame_sink;
    void                          *frame_sink_ctx;
    uint32_t                       poll_period_ms;

    struct k_msgq    *queue;          /**< Command queue */
    struct k_thread   thread;         /**< Kernel thread control block */
    k_thread_stack_t *stack;          /**< Thread stack pointer (never embedded) */
    size_t            stack_size;     /**< Thread stack size */
    struct k_sem      run_sem;        /**< Outer gate semaphore */
    struct k_sem      stopped_sem;    /**< Stop confirmation semaphore */
    volatile bool     is_running;     /**< Inner loop guard */
    volatile bool     terminate;      /**< Outer loop exit flag */
    bool              is_initialized; /**< Initialization guard */
    uint64_t          last_scan_ms;   /**< Uptime of preceding scan */
    uint32_t          error_count;    /**< Consecutive failed acquisition cycles */
} ao_sensor_matrix_t;

/**
 * @brief Phase 1: Initialize the Sensor Matrix Active Object.
 *
 * Spawns the worker thread once in gated state waiting on run_sem.
 *
 * @param[out] self   Instance to initialize.
 * @param[in]  config Construction parameters.
 * @param[in]  queue  Pre-allocated message queue (depth >= AO_SENSOR_MATRIX_QUEUE_DEPTH).
 * @return 0 on success, negative errno on invalid arguments.
 */
int ao_sensor_matrix_init(ao_sensor_matrix_t *self,
                          const ao_sensor_matrix_config_t *config,
                          struct k_msgq *queue);

/**
 * @brief Phase 2: Start the Sensor Matrix Active Object thread.
 *
 * Unblocks the gate semaphore without recreating the thread.
 *
 * @param[in,out] self Initialized Active Object instance.
 * @return 0 on success, -EALREADY if already running, -EINVAL on invalid state.
 */
int ao_sensor_matrix_start(ao_sensor_matrix_t *self);

/**
 * @brief Request an immediate acquisition cycle.
 *
 * @param[in,out] self Active Object instance.
 * @return 0 on success, -EAGAIN if not running, negative errno on error.
 */
int ao_sensor_matrix_trigger(ao_sensor_matrix_t *self);

/**
 * @brief Phase 3: Execute one synchronous acquisition cycle.
 *
 * Callable directly from unit tests without a running thread.
 *
 * @param[in,out] self       Active Object instance.
 * @param[in]     elapsed_ms Elapsed time in milliseconds since the preceding cycle.
 * @return 0 on success, negative errno on failure.
 */
int ao_sensor_matrix_step(ao_sensor_matrix_t *self, uint32_t elapsed_ms);

/**
 * @brief Phase 4: Stop the Sensor Matrix Active Object gracefully.
 *
 * @param[in,out] self Active Object instance.
 * @return 0 on success, negative errno on error.
 */
int ao_sensor_matrix_stop(ao_sensor_matrix_t *self);

/**
 * @brief Wait until the Sensor Matrix Active Object confirms it has stopped.
 *
 * @param[in,out] self    Active Object instance.
 * @param[in]     timeout Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout, negative errno on error.
 */
int ao_sensor_matrix_wait_stopped(ao_sensor_matrix_t *self, k_timeout_t timeout);

/**
 * @brief Phase 5: Deinitialize the Sensor Matrix Active Object.
 *
 * @param[in,out] self Active Object instance in stopped state.
 * @return 0 on success, -EBUSY if running, negative errno on error.
 */
int ao_sensor_matrix_deinit(ao_sensor_matrix_t *self);

/**
 * @brief Check whether the Active Object thread is running.
 *
 * @param[in] self Active Object instance.
 * @return true if running, false otherwise.
 */
static inline bool ao_sensor_matrix_is_running(const ao_sensor_matrix_t *self)
{
    return (self != NULL && self->is_running);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_SENSOR_MATRIX_H_ */
