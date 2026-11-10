/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file ao_alarm_manager.h
 * @brief Active Object: Centralized Alarm Lifecycle Manager.
 *
 * Implements the Canonical 5-Phase Active Object Lifecycle:
 *   1. init: Allocates structures, sets dormant state.
 *   2. start: Posts start command, activates inner thread loop.
 *   3. stop: Posts stop command, sets is_running = false.
 *   4. wait_stopped: Blocks on stopped_sem until thread halts.
 *   5. deinit: Frees kernel resources.
 *
 * Adheres strictly to:
 *   - Invariant 4: MPU Stack Guard (thread stack passed from composition root).
 *   - Invariant 5: Zero dynamic memory allocation.
 *   - Invariant 6: Zero file-scope mutable state.
 *   - Invariant 1: Exposes fat-pointer i_alarm_query_t.
 */

#ifndef APP_APPLICATION_ACTIVE_OBJECTS_AO_ALARM_MANAGER_H_
#define APP_APPLICATION_ACTIVE_OBJECTS_AO_ALARM_MANAGER_H_

#include <zephyr/kernel.h>
#include "common/alarm_types.h"
#include "interfaces/i_alarm_query.h"
#include "domain/alarm/alarm_entity.h"
#include "domain/alarm/alarm_aggregate.h"
#include "domain/alarm/alarm_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_AO_ALARM_MANAGER_STACK_SIZE
#define CONFIG_AO_ALARM_MANAGER_STACK_SIZE 2048
#endif

#ifndef AO_ALARM_MANAGER_STACK_SIZE
#define AO_ALARM_MANAGER_STACK_SIZE CONFIG_AO_ALARM_MANAGER_STACK_SIZE
#endif

#ifndef CONFIG_AO_ALARM_MANAGER_QUEUE_DEPTH
#define CONFIG_AO_ALARM_MANAGER_QUEUE_DEPTH 64
#endif

#ifndef AO_ALARM_MANAGER_QUEUE_DEPTH
#define AO_ALARM_MANAGER_QUEUE_DEPTH CONFIG_AO_ALARM_MANAGER_QUEUE_DEPTH
#endif

typedef enum {
    AO_ALARM_CMD_NONE = 0,
    AO_ALARM_CMD_UPDATE_DISCRETE,
    AO_ALARM_CMD_UPDATE_AGGREGATE,
    AO_ALARM_CMD_ACKNOWLEDGE,
    AO_ALARM_CMD_RESET_LATCH,
    AO_ALARM_CMD_STOP
} ao_alarm_cmd_type_t;

typedef struct {
    ao_alarm_cmd_type_t cmd;
    uint16_t            alarm_id;
    uint8_t             source_index;
    bool                is_asserted;
    float               trigger_val;
    uint64_t            timestamp_ms;
} ao_alarm_msg_t;

/**
 * @brief Active Object instance context structure.
 */
typedef struct {
    /* Thread & Stack */
    struct k_thread       thread;
    k_tid_t               tid;
    k_thread_stack_t     *stack;
    size_t                stack_size;
    int                   priority;

    /* Synchronization */
    struct k_msgq         msgq;
    struct k_sem          stopped_sem;
    struct k_mutex        lock;
    bool                  is_running;

    /* Message queue static storage buffer */
    ao_alarm_msg_t        msgq_buffer[AO_ALARM_MANAGER_QUEUE_DEPTH];

    /* Domain Entities & Descriptors */
    alarm_registry_t      registry;
    alarm_entity_t        entities[CONFIG_ALARM_MANAGER_MAX_ALARMS];
    alarm_aggregate_entity_t aggregates[CONFIG_ALARM_MANAGER_MAX_ALARMS];

    /* Thread-safe live snapshot cache for PULL query port */
    alarm_snapshot_entry_t snapshot[CONFIG_ALARM_MANAGER_MAX_ALARMS];
    size_t                active_alarm_count;
    alarm_severity_t      highest_active_severity;

    /* Exposed PULL Port */
    i_alarm_query_t       query_port;
} ao_alarm_manager_t;

/*============================================================================*
 * CANONICAL 5-PHASE LIFECYCLE APIS
 *============================================================================*/

/**
 * @brief Initialize Active Object context, queues, and domain entities.
 */
int ao_alarm_manager_init(ao_alarm_manager_t *self,
                          k_thread_stack_t *stack,
                          size_t stack_size,
                          int priority);

/**
 * @brief Start the Active Object processing thread.
 */
int ao_alarm_manager_start(ao_alarm_manager_t *self);

/**
 * @brief Request thread termination.
 */
int ao_alarm_manager_stop(ao_alarm_manager_t *self);

/**
 * @brief Block until thread cleanly halts.
 */
int ao_alarm_manager_wait_stopped(ao_alarm_manager_t *self, k_timeout_t timeout);

/**
 * @brief Deinitialize Active Object resources.
 */
int ao_alarm_manager_deinit(ao_alarm_manager_t *self);

/*============================================================================*
 * PUBLIC CONDITION POSTING APIS (NON-BLOCKING)
 *============================================================================*/

/**
 * @brief Post a condition update for a discrete alarm ID.
 */
int ao_alarm_manager_post_condition(ao_alarm_manager_t *self,
                                    uint16_t alarm_id,
                                    bool is_asserted,
                                    float trigger_val);

/**
 * @brief Post a condition update for an assigned source of an aggregated alarm ID.
 */
int ao_alarm_manager_post_source_condition(ao_alarm_manager_t *self,
                                           uint16_t agg_alarm_id,
                                           uint8_t source_idx,
                                           bool is_asserted,
                                           float trigger_val);

/**
 * @brief Post an operator acknowledgment for an active alarm ID.
 */
int ao_alarm_manager_post_ack(ao_alarm_manager_t *self, uint16_t alarm_id);

/**
 * @brief Post a latch reset for an alarm ID.
 */
int ao_alarm_manager_post_reset_latch(ao_alarm_manager_t *self, uint16_t alarm_id);

/**
 * @brief Dynamically register a new alarm descriptor at runtime (FR-23).
 *
 * Thread-safe API that registers the descriptor into the registry, instantiates
 * its corresponding entity/aggregate FSM, and initializes live snapshot tracking.
 *
 * @param self Active Object handle.
 * @param desc Pointer to descriptor to register.
 * @return 0 on success, -EINVAL on invalid input, -EEXIST if already registered, -ENOSPC if full.
 */
int ao_alarm_manager_register_descriptor(ao_alarm_manager_t *self,
                                         const alarm_descriptor_t *desc);

/**
 * @brief Retrieve current or total active duration of an alarm in milliseconds (FR-03).
 */
uint32_t ao_alarm_manager_get_active_duration(ao_alarm_manager_t *self, uint16_t alarm_id);

/**
 * @brief Retrieve the fat-pointer query interface handle.
 */
i_alarm_query_t ao_alarm_manager_get_query_port(ao_alarm_manager_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_APPLICATION_ACTIVE_OBJECTS_AO_ALARM_MANAGER_H_ */
