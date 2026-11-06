/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Active Object that orchestrates Ethernet and WiFi interfaces.
 * Applies default configurations, monitors link status, responds to runtime
 * configuration changes, and publishes status to Zbus channels.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_NETWORK_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_NETWORK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "common/network_types.h"
#include "interfaces/i_network.h"
#include "application/zbus/network_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Stack reserved for the network Active Object thread.
 */
#define AO_NETWORK_STACK_SIZE 4096

/** Pending command capacity of the network AO command queue. */
#define AO_NETWORK_QUEUE_DEPTH 8

/** Preemptive priority for the network Active Object. */
#define AO_NETWORK_PRIORITY 7

/** Period between status polls/checks in milliseconds. */
#define AO_NETWORK_POLL_INTERVAL_MS 500U

/** How long ao_network_stop() waits for the thread to exit. */
#define AO_NETWORK_STOP_WAIT_MS 3000U

/** Events/commands accepted by ao_network. */
typedef enum {
    AO_NETWORK_EVENT_START = 1,       /**< Start inner processing loop */
    AO_NETWORK_EVENT_STOP,            /**< Request to stop Active Object thread */
    AO_NETWORK_EVENT_TERMINATE,       /**< Terminate thread permanently */
    AO_NETWORK_EVENT_POLL_STATUS,     /**< Trigger immediate status poll */
    AO_NETWORK_EVENT_CONFIG_ETH,      /**< Reconfigure Ethernet interface */
    AO_NETWORK_EVENT_CONFIG_WIFI,     /**< Reconfigure WiFi interface */
} ao_network_event_type_t;

/** Command event payload posted to ao_network queue. */
typedef struct {
    uint8_t type; /**< ao_network_event_type_t */
    union {
        net_eth_config_t  eth_cfg;
        net_wifi_config_t wifi_cfg;
    } data;
} ao_network_event_t;

/**
 * @brief Active Object network coordinator context.
 */
typedef struct {
    const i_network_t *net_eth;    /**< Ethernet port handle (injected) */
    const i_network_t *net_wifi;   /**< WiFi port handle (injected) */
    struct k_msgq *queue;          /**< Command message queue */
    struct k_thread thread;        /**< Thread control block */
    k_thread_stack_t *stack;       /**< Stack storage (file scope owned) */
    size_t stack_size;             /**< Usable stack size */
    volatile bool is_running;      /**< Running flag */

    /* Synchronization */
    struct k_sem stopped_sem;      /**< Signaled when inner loop exits */
    bool is_initialized;           /**< Initialized flag */

    /* Active configuration caches */
    net_eth_config_t  eth_cfg;
    net_wifi_config_t wifi_cfg;

    /* Last known status for change detection */
    net_status_t last_eth_status;
    net_status_t last_wifi_status;
    bool has_eth_status;
    bool has_wifi_status;
} ao_network_t;

/**
 * @brief Initialize ao_network instance. Spawns thread once in gated state.
 *
 * @param[out] self              Instance to initialize.
 * @param[in]  net_eth           Ethernet port handle.
 * @param[in]  net_wifi          WiFi port handle.
 * @param[in]  initial_eth_cfg   Initial Ethernet configuration (optional, pure domain defaults if NULL).
 * @param[in]  initial_wifi_cfg  Initial WiFi configuration (optional, pure domain defaults if NULL).
 * @param[in]  queue             Initialized message queue.
 * @param[in]  stack             Stack buffer allocated at file scope.
 * @param[in]  stack_size        Stack size in bytes.
 *
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int ao_network_init(ao_network_t *self,
                    const i_network_t *net_eth,
                    const i_network_t *net_wifi,
                    const net_eth_config_t *initial_eth_cfg,
                    const net_wifi_config_t *initial_wifi_cfg,
                    struct k_msgq *queue,
                    k_thread_stack_t *stack,
                    size_t stack_size);

/**
 * @brief Start ao_network Active Object thread and initialize interfaces with defaults.
 *
 * @param[in,out] self Initialized ao_network instance.
 * @return 0 on success, -EINVAL or -EALREADY.
 */
int ao_network_start(ao_network_t *self);

/**
 * @brief Post command to ao_network without blocking.
 *
 * @param[in,out] self  Active object instance.
 * @param[in]     event Event to post.
 * @return 0 on success, -EINVAL or -ENOMSG if queue is full.
 */
int ao_network_post(ao_network_t *self, const ao_network_event_t *event);

/**
 * @brief Request ao_network thread to stop its inner processing loop.
 *
 * @param[in,out] self Active object instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_network_stop(ao_network_t *self);

/**
 * @brief Wait for ao_network thread to exit its inner processing loop.
 *
 * @param[in] self     Active object handle.
 * @param[in] timeout  Maximum duration to wait.
 * @return 0 on success, -EAGAIN on timeout, -EINVAL on invalid handle.
 */
int ao_network_wait_stopped(ao_network_t *self, k_timeout_t timeout);

/**
 * @brief Deinitialize ao_network and terminate its thread.
 *
 * @param[in] self  Active object handle.
 * @return 0 on success, negative errno on failure.
 */
int ao_network_deinit(ao_network_t *self);

/**
 * @brief Perform a single synchronous step/poll of both interfaces and publish if status changed.
 * Useful for deterministic testing or non-threaded invocation.
 *
 * @param[in,out] self Active object instance.
 * @return 0 on success, negative errno on failure.
 */
int ao_network_sync_step(ao_network_t *self);

/**
 * @brief Get the authoritative cached Ethernet configuration.
 *
 * @param[in]  self    Active object instance.
 * @param[out] out_cfg Destination buffer.
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int ao_network_get_eth_config(const ao_network_t *self, net_eth_config_t *out_cfg);

/**
 * @brief Get the authoritative cached WiFi configuration.
 *
 * @param[in]  self    Active object instance.
 * @param[out] out_cfg Destination buffer.
 * @return 0 on success, -EINVAL on NULL arguments.
 */
int ao_network_get_wifi_config(const ao_network_t *self, net_wifi_config_t *out_cfg);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_NETWORK_H_ */
