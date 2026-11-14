/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * Abstract Port Interface for Hardware Backup SRAM (4 KB battery-backed storage).
 * Adheres strictly to Section 4.1.D of doc/STRUCTURE.md:
 * 1) V-Table (i_bkpsram_api_t)
 * 2) Handle (i_bkpsram_t)
 * 3) Validated Wrappers (static inline i_bkpsram_*())
 *
 * Pure C99/C11 - zero Zephyr headers, zero dynamic memory.
 */

#ifndef APP_INTERFACES_I_BKPSRAM_H_
#define APP_INTERFACES_I_BKPSRAM_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/datalogger_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 * 1. V-TABLE CONTRACT (Implementation details, never called directly)
 *============================================================================*/

typedef struct {
    int (*init)(void *impl);
    int (*write_record)(void *impl, alarm_nv_record_t *record);
    int (*get_next_uncommitted)(void *impl, uint32_t cursor, alarm_nv_record_t *out_record, uint32_t *next_cursor);
    int (*mark_committed)(void *impl, uint32_t seq_id);
    uint32_t (*uncommitted_count)(void *impl);
    bool (*is_valid)(void *impl);
} i_bkpsram_api_t;

/*============================================================================*
 * 2. HANDLE (Fat pointer passed to consumers)
 *============================================================================*/

typedef struct {
    const i_bkpsram_api_t *api;
    void                  *impl;
} i_bkpsram_t;

/*============================================================================*
 * 3. VALIDATION PREDICATE & TYPE-SAFE WRAPPERS
 *============================================================================*/

static inline bool i_bkpsram_is_bound(const i_bkpsram_t *port)
{
    return (port != NULL &&
            port->api != NULL &&
            port->impl != NULL &&
            port->api->init != NULL &&
            port->api->write_record != NULL &&
            port->api->get_next_uncommitted != NULL &&
            port->api->mark_committed != NULL &&
            port->api->uncommitted_count != NULL &&
            port->api->is_valid != NULL);
}

static inline int i_bkpsram_init(const i_bkpsram_t *port)
{
    if (!i_bkpsram_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->init(port->impl);
}

static inline int i_bkpsram_write_record(const i_bkpsram_t *port, alarm_nv_record_t *record)
{
    if (!i_bkpsram_is_bound(port) || record == NULL) {
        return -EINVAL;
    }
    return port->api->write_record(port->impl, record);
}

static inline int i_bkpsram_get_next_uncommitted(const i_bkpsram_t *port,
                                                uint32_t cursor,
                                                alarm_nv_record_t *out_record,
                                                uint32_t *next_cursor)
{
    if (!i_bkpsram_is_bound(port) || out_record == NULL || next_cursor == NULL) {
        return -EINVAL;
    }
    return port->api->get_next_uncommitted(port->impl, cursor, out_record, next_cursor);
}

static inline int i_bkpsram_mark_committed(const i_bkpsram_t *port, uint32_t seq_id)
{
    if (!i_bkpsram_is_bound(port)) {
        return -EINVAL;
    }
    return port->api->mark_committed(port->impl, seq_id);
}

static inline uint32_t i_bkpsram_uncommitted_count(const i_bkpsram_t *port)
{
    if (!i_bkpsram_is_bound(port)) {
        return 0U;
    }
    return port->api->uncommitted_count(port->impl);
}

static inline bool i_bkpsram_is_valid(const i_bkpsram_t *port)
{
    if (!i_bkpsram_is_bound(port)) {
        return false;
    }
    return port->api->is_valid(port->impl);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INTERFACES_I_BKPSRAM_H_ */
