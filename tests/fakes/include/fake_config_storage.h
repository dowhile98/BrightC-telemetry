/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * In-memory Fake config storage port implementation for unit testing.
 * Provides independent RAM buffers per domain, call counters, error injection,
 * and port handle factory.
 */

#ifndef FAKE_CONFIG_STORAGE_H_
#define FAKE_CONFIG_STORAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/config_types.h"
#include "interfaces/i_config_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum buffer size per domain in fake storage (1024 bytes). */
#define FAKE_CONFIG_STORAGE_MAX_SLOT_SIZE 1024U

typedef struct {
    uint8_t data[FAKE_CONFIG_STORAGE_MAX_SLOT_SIZE];
    size_t size;
    bool has_data;
    uint32_t save_count;
    uint32_t load_count;
    uint32_t reset_count;
} fake_config_domain_slot_t;

typedef struct {
    /* Call counters */
    uint32_t init_calls;
    uint32_t save_calls;
    uint32_t load_calls;
    uint32_t reset_defaults_calls;

    /* Injected return codes (0 for normal behavior) */
    int simulated_init_error;
    int simulated_save_error;
    int simulated_load_error;
    int simulated_reset_error;

    /* Per-domain storage slots */
    fake_config_domain_slot_t slots[CONFIG_DOMAIN_MAX];
} fake_config_storage_state_t;

/**
 * @brief Initialize / reset the fake configuration storage state and clear all counters.
 */
void fake_config_storage_reset(void);

/**
 * @brief Obtain an i_config_storage_t port handle bound to the fake storage adapter.
 */
i_config_storage_t fake_config_storage_get_port(void);

/**
 * @brief Direct inspection of the internal fake state.
 */
const fake_config_storage_state_t *fake_config_storage_get_state(void);

/**
 * @brief Inject return codes for operations.
 */
void fake_config_storage_set_errors(int init_err, int save_err, int load_err, int reset_err);

/**
 * @brief Pre-load domain data directly into fake storage without triggering save counters.
 */
int fake_config_storage_seed_domain(config_domain_t domain, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_CONFIG_STORAGE_H_ */
