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
#include "interfaces/i_temperature_bank.h"
#include "common/energy_types.h"

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

#ifdef CONFIG_APP_MODBUS_INTER_POLL_DELAY_MS
#define AO_MODBUS_INTER_POLL_DELAY_MS CONFIG_APP_MODBUS_INTER_POLL_DELAY_MS
#else
#define AO_MODBUS_INTER_POLL_DELAY_MS 30U
#endif


/** Commands accepted by a polling thread. */
typedef enum {
    AO_MODBUS_EVENT_START = 1,    /**< Enter active polling loop */
    AO_MODBUS_EVENT_STOP,         /**< Leave the polling loop */
    AO_MODBUS_EVENT_TERMINATE,    /**< Terminate thread permanently */
    AO_MODBUS_EVENT_POLL_NOW,     /**< Break the current wait and poll immediately */
    AO_MODBUS_EVENT_WRITE_COIL,   /**< Asynchronous write to a relay coil (FC05/FC0F) */
} modbus_event_type_t;

/**
 * @brief Command posted to a polling thread.
 */
typedef struct {
    uint8_t type; /**< modbus_event_type_t */

    /* Valid when type == AO_MODBUS_EVENT_WRITE_COIL */
    uint8_t device_id;      /**< Logical device id (from the registry) */
    uint8_t slave_addr;     /**< Modbus unit identifier */
    uint16_t coil_addr;     /**< Coil address (0x0000-0x001F for Waveshare 32CH) */
    uint16_t coil_value;    /**< 0xFF00=ON, 0x0000=OFF, 0x5500=toggle */
    int32_t timeout_ms;     /**< Response timeout */
} modbus_event_t;

/** Callback invoked when MBSL32DI inputs are read. */
typedef void (*ao_modbus_mbsl32di_cb_t)(void *ctx, uint8_t device_id, uint32_t raw_inputs);

/** Callback invoked when CWT-TM-16PT temperature bank is read. */
typedef void (*ao_modbus_cwt_tm16pt_cb_t)(void *ctx, uint8_t device_id, const temperature_bank_16ch_t *bank);

/** Callback invoked when JSY-MK-339 electrical reading is parsed. */
typedef void (*ao_modbus_jsy_mk339_cb_t)(void *ctx, uint8_t device_id, const energy_reading_t *reading);

/** Callback invoked when bus health or consecutive failures change state. */
typedef void (*ao_modbus_bus_fault_cb_t)(void *ctx, uint8_t bus_id, bool is_fault);

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

    ao_modbus_mbsl32di_cb_t mbsl32di_cb; /**< Callback for digital inputs */
    void *mbsl32di_cb_ctx;               /**< Callback user context */

    ao_modbus_cwt_tm16pt_cb_t cwt_tm16pt_cb; /**< Callback for temperature bank */
    void *cwt_tm16pt_cb_ctx;                 /**< Callback user context */

    ao_modbus_jsy_mk339_cb_t jsy_mk339_cb;   /**< Callback for energy meter */
    void *jsy_mk339_cb_ctx;                   /**< Callback user context */

    ao_modbus_bus_fault_cb_t bus_fault_cb;   /**< Callback for bus fault state changes */
    void *bus_fault_cb_ctx;                  /**< Callback user context */
    uint32_t consecutive_fails;              /**< Consecutive poll failures on this bus */
    bool bus_fault_active;                   /**< Whether bus fault alarm condition is active */

    /* Synchronization */
    struct k_sem stopped_sem;      /**< Signaled when inner loop exits */
    bool is_initialized;           /**< Initialized flag */
} ao_modbus_t;

/**
 * @brief Prepare an Active Object. Spawns thread once in gated state.
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
 * @brief Start the polling loop.
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
 * @brief Request the thread to leave its inner polling loop.
 *
 * @return 0 on success, -EINVAL on bad arguments.
 */
int ao_modbus_stop(ao_modbus_t *self);

/**
 * @brief Wait for the Active Object thread to leave its inner polling loop.
 *
 * @param[in] self     Active object handle.
 * @param[in] timeout  Maximum duration to wait.
 * @return 0 on success, -EAGAIN on timeout, -EINVAL on invalid handle.
 */
int ao_modbus_wait_stopped(ao_modbus_t *self, k_timeout_t timeout);

/**
 * @brief Deinitialize the Active Object and terminate its thread.
 *
 * @param[in] self  Active object handle.
 * @return 0 on success, negative errno on failure.
 */
int ao_modbus_deinit(ao_modbus_t *self);

/**
 * @brief Register callback for MBSL32DI digital input readings.
 */
void ao_modbus_set_mbsl32di_callback(ao_modbus_t *self, ao_modbus_mbsl32di_cb_t cb, void *ctx);

/**
 * @brief Register callback for CWT-TM-16PT temperature bank readings.
 */
void ao_modbus_set_cwt_tm16pt_callback(ao_modbus_t *self, ao_modbus_cwt_tm16pt_cb_t cb, void *ctx);

/**
 * @brief Register callback for JSY-MK-339 electrical energy readings.
 */
void ao_modbus_set_jsy_mk339_callback(ao_modbus_t *self, ao_modbus_jsy_mk339_cb_t cb, void *ctx);

/**
 * @brief Register callback for Modbus bus fault state changes.
 */
void ao_modbus_set_bus_fault_callback(ao_modbus_t *self, ao_modbus_bus_fault_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_MODBUS_H_ */
