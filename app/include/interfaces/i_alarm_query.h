/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file i_alarm_query.h
 * @brief Abstract Port Contract for querying Alarm Subsystem state.
 *
 * Adheres strictly to Invariant 1 (Rule §4.1.D Port Contract & Fat-Pointer Encapsulation).
 * High-level consumers (HMI, Modbus Server, Web Server) query alarms exclusively
 * through this fat-pointer handle without accessing concrete implementations or v-tables.
 */

#ifndef APP_INTERFACES_I_ALARM_QUERY_H_
#define APP_INTERFACES_I_ALARM_QUERY_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/alarm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct i_alarm_query_api i_alarm_query_api_t;

/**
 * @brief Fat-pointer handle encapsulating the query port and instance context.
 */
typedef struct {
    const i_alarm_query_api_t *api;   /**< Operation function table */
    void                      *impl;  /**< Private instance context */
} i_alarm_query_t;

/**
 * @brief Operation function table (v-table) for alarm queries.
 */
struct i_alarm_query_api {
    size_t (*get_active_count)(void *impl);
    alarm_severity_t (*get_highest_active_severity)(void *impl);
    bool (*is_active)(void *impl, uint16_t alarm_id);
    int (*get_alarm_state)(void *impl, uint16_t alarm_id, alarm_state_t *out_state);
    int (*get_alarm_snapshot)(void *impl, uint16_t alarm_id, alarm_snapshot_entry_t *out_snapshot);
    size_t (*get_active_alarm_ids)(void *impl, uint16_t *out_ids, size_t max_count);
    int (*get_descriptor)(void *impl, uint16_t alarm_id, alarm_descriptor_t *out_desc);
};

/*============================================================================*
 * SAFETY VALIDATION PREDICATE & PUBLIC TYPE-SAFE WRAPPERS
 *============================================================================*/

/**
 * @brief Verify that the query port handle is properly bound to an implementation.
 */
static inline bool i_alarm_query_is_bound(const i_alarm_query_t *port)
{
    return (port != NULL &&
            port->api != NULL &&
            port->impl != NULL &&
            port->api->get_active_count != NULL &&
            port->api->get_highest_active_severity != NULL &&
            port->api->is_active != NULL &&
            port->api->get_alarm_state != NULL &&
            port->api->get_alarm_snapshot != NULL &&
            port->api->get_active_alarm_ids != NULL &&
            port->api->get_descriptor != NULL);
}

/**
 * @brief Retrieve total count of currently active or acknowledged alarms.
 */
static inline size_t i_alarm_query_get_active_count(const i_alarm_query_t *port)
{
    if (!i_alarm_query_is_bound(port)) {
        return 0;
    }
    return port->api->get_active_count(port->impl);
}

/**
 * @brief Retrieve highest severity among all currently active alarms.
 */
static inline alarm_severity_t i_alarm_query_get_highest_active_severity(const i_alarm_query_t *port)
{
    if (!i_alarm_query_is_bound(port)) {
        return ALARM_SEVERITY_INFO;
    }
    return port->api->get_highest_active_severity(port->impl);
}

/**
 * @brief Check whether a specific alarm ID is currently active.
 */
static inline bool i_alarm_query_is_active(const i_alarm_query_t *port, uint16_t alarm_id)
{
    if (!i_alarm_query_is_bound(port)) {
        return false;
    }
    return port->api->is_active(port->impl, alarm_id);
}

/**
 * @brief Query live operational state of a specific alarm.
 */
static inline int i_alarm_query_get_alarm_state(const i_alarm_query_t *port,
                                                uint16_t alarm_id,
                                                alarm_state_t *out_state)
{
    if (!i_alarm_query_is_bound(port) || out_state == NULL) {
        return -EINVAL;
    }
    return port->api->get_alarm_state(port->impl, alarm_id, out_state);
}

/**
 * @brief Query full snapshot metrics of an alarm.
 */
static inline int i_alarm_query_get_alarm_snapshot(const i_alarm_query_t *port,
                                                   uint16_t alarm_id,
                                                   alarm_snapshot_entry_t *out_snapshot)
{
    if (!i_alarm_query_is_bound(port) || out_snapshot == NULL) {
        return -EINVAL;
    }
    return port->api->get_alarm_snapshot(port->impl, alarm_id, out_snapshot);
}

/**
 * @brief Fetch list of active alarm IDs up to max_count.
 */
static inline size_t i_alarm_query_get_active_alarm_ids(const i_alarm_query_t *port,
                                                        uint16_t *out_ids,
                                                        size_t max_count)
{
    if (!i_alarm_query_is_bound(port) || out_ids == NULL || max_count == 0) {
        return 0;
    }
    return port->api->get_active_alarm_ids(port->impl, out_ids, max_count);
}

/**
 * @brief Retrieve static descriptor metadata for an alarm.
 */
static inline int i_alarm_query_get_descriptor(const i_alarm_query_t *port,
                                               uint16_t alarm_id,
                                               alarm_descriptor_t *out_desc)
{
    if (!i_alarm_query_is_bound(port) || out_desc == NULL) {
        return -EINVAL;
    }
    return port->api->get_descriptor(port->impl, alarm_id, out_desc);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_ALARM_QUERY_H_ */
