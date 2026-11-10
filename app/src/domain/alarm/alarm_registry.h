/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_registry.h
 * @brief Pure Domain Registry: Extensible Static Alarm Descriptor Catalog.
 *
 * ISO C99 only. Zero Zephyr RTOS or vendor hardware dependencies.
 * Zero dynamic memory allocation. Descriptors are stored in a preallocated
 * table up to CONFIG_ALARM_MANAGER_MAX_ALARMS.
 */

#ifndef APP_DOMAIN_ALARM_REGISTRY_H_
#define APP_DOMAIN_ALARM_REGISTRY_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/alarm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Alarm Descriptor Registry instance context.
 */
typedef struct {
    alarm_descriptor_t descriptors[CONFIG_ALARM_MANAGER_MAX_ALARMS];
    size_t             count;
} alarm_registry_t;

/**
 * @brief Initialize the registry with the default well-known alarm catalog.
 */
void alarm_registry_init(alarm_registry_t *self);

/**
 * @brief Register a new alarm descriptor into the registry.
 *
 * @return 0 on success, -EINVAL on invalid parameter, -EEXIST if ID already registered, -ENOSPC if full.
 */
int alarm_registry_register(alarm_registry_t *self, const alarm_descriptor_t *desc);

/**
 * @brief Find slot index of an alarm ID.
 *
 * @return 0-based index or -1 if not found.
 */
int alarm_registry_find_index(const alarm_registry_t *self, uint16_t alarm_id);

/**
 * @brief Retrieve a copy of the descriptor by alarm ID.
 */
int alarm_registry_get(const alarm_registry_t *self, uint16_t alarm_id, alarm_descriptor_t *out_desc);

/**
 * @brief Retrieve total number of registered alarm descriptors.
 */
size_t alarm_registry_get_count(const alarm_registry_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOMAIN_ALARM_REGISTRY_H_ */
