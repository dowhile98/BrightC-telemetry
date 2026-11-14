/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_bkpsram_stm32.h
 * @brief Infrastructure adapter for STM32H7 Backup SRAM peripheral.
 */

#ifndef APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_BKPSRAM_STM32_H_
#define APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_BKPSRAM_STM32_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domain/storage/bkpsram_ring.h"
#include "interfaces/i_bkpsram.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STM32H7_BKPSRAM_BASE_ADDR   0x38800000U
#define STM32H7_BKPSRAM_SIZE        4096U

typedef struct {
    void              *base_addr;
    size_t             size_bytes;
    bkpsram_ring_ctx_t ring;
    bool               is_initialized;
} adapter_bkpsram_stm32_t;

/**
 * @brief Initialize the STM32 Backup SRAM adapter.
 *
 * @param[in,out] self      Adapter instance.
 * @param[in]     base_addr Base memory address of Backup SRAM (passed from composition root).
 * @param[in]     size      Total size in bytes (typically 4096).
 * @return 0 on success, negative errno on failure.
 */
int adapter_bkpsram_stm32_init(adapter_bkpsram_stm32_t *self, void *base_addr, size_t size);

/**
 * @brief Obtain the abstract i_bkpsram_t port handle.
 */
i_bkpsram_t adapter_bkpsram_stm32_port(adapter_bkpsram_stm32_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFRASTRUCTURE_ADAPTERS_ADAPTER_BKPSRAM_STM32_H_ */
