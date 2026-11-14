/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file fake_bkpsram.h
 * @brief In-memory Fake Backup SRAM adapter for host unit tests.
 *
 * Pure C99/C11. Zero hardware dependencies.
 */

#ifndef TESTS_MOCKS_FAKE_BKPSRAM_H_
#define TESTS_MOCKS_FAKE_BKPSRAM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domain/storage/bkpsram_ring.h"
#include "interfaces/i_bkpsram.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t            storage[BKPSRAM_MIN_STORAGE_SIZE];
    bkpsram_ring_ctx_t ring;
    bool               is_initialized;
    int                simulated_error;
} fake_bkpsram_t;

int fake_bkpsram_init(fake_bkpsram_t *self);
void fake_bkpsram_reset(fake_bkpsram_t *self);
i_bkpsram_t fake_bkpsram_port(fake_bkpsram_t *self);
i_bkpsram_t fake_bkpsram_get_port(void);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_MOCKS_FAKE_BKPSRAM_H_ */
