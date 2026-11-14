/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file adapter_bkpsram_stm32.c
 * @brief Infrastructure adapter for STM32H7 Backup SRAM.
 */

#include "infrastructure/adapters/adapter_bkpsram_stm32.h"

#include <errno.h>
#include <string.h>

#if defined(CONFIG_SOC_FAMILY_STM32) || defined(CONFIG_SOC_SERIES_STM32H7X)
#include <zephyr/sys/barrier.h>
#include <stm32h7xx.h>
#endif

static int bkpsram_stm32_hw_init(void)
{
#if defined(CONFIG_SOC_FAMILY_STM32) || defined(CONFIG_SOC_SERIES_STM32H7X)
    /* Enable Backup SRAM clock in AHB4 */
    RCC->AHB4ENR |= RCC_AHB4ENR_BKPRAMEN;
    barrier_dsync_fence_full();

    /* Enable write access to Backup Domain in PWR CR1 */
    PWR->CR1 |= PWR_CR1_DBP;
    barrier_dsync_fence_full();

    /* Enable Backup Regulator in PWR CR2 and wait for ready flag */
    PWR->CR2 |= PWR_CR2_BREN;
    uint32_t timeout = 100000U;
    while (((PWR->CR2 & PWR_CR2_BRRDY) == 0U) && (timeout > 0U)) {
        timeout--;
    }

    if (timeout == 0U) {
        return -ETIMEDOUT;
    }
#endif
    return 0;
}

static int stm32_init_op(void *impl)
{
    adapter_bkpsram_stm32_t *self = (adapter_bkpsram_stm32_t *)impl;
    if (self == NULL || self->base_addr == NULL) {
        return -EINVAL;
    }

    int ret = bkpsram_stm32_hw_init();
    if (ret != 0) {
        return ret;
    }

    return bkpsram_ring_init(&self->ring, self->base_addr, self->size_bytes);
}

static int stm32_write_record_op(void *impl, alarm_nv_record_t *record)
{
    adapter_bkpsram_stm32_t *self = (adapter_bkpsram_stm32_t *)impl;
    if (self == NULL || !self->is_initialized || record == NULL) {
        return -EINVAL;
    }
    return bkpsram_ring_push(&self->ring, record);
}

static int stm32_get_next_uncommitted_op(void *impl,
                                        uint32_t cursor,
                                        alarm_nv_record_t *out_record,
                                        uint32_t *next_cursor)
{
    adapter_bkpsram_stm32_t *self = (adapter_bkpsram_stm32_t *)impl;
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return bkpsram_ring_get_next_uncommitted(&self->ring, cursor, out_record, next_cursor);
}

static int stm32_mark_committed_op(void *impl, uint32_t seq_id)
{
    adapter_bkpsram_stm32_t *self = (adapter_bkpsram_stm32_t *)impl;
    if (self == NULL || !self->is_initialized) {
        return -EINVAL;
    }
    return bkpsram_ring_mark_committed(&self->ring, seq_id);
}

static uint32_t stm32_uncommitted_count_op(void *impl)
{
    adapter_bkpsram_stm32_t *self = (adapter_bkpsram_stm32_t *)impl;
    if (self == NULL || !self->is_initialized) {
        return 0U;
    }
    return bkpsram_ring_uncommitted_count(&self->ring);
}

static bool stm32_is_valid_op(void *impl)
{
    adapter_bkpsram_stm32_t *self = (adapter_bkpsram_stm32_t *)impl;
    if (self == NULL || !self->is_initialized) {
        return false;
    }
    return bkpsram_ring_is_valid(&self->ring);
}

static const i_bkpsram_api_t s_bkpsram_stm32_api = {
    .init = stm32_init_op,
    .write_record = stm32_write_record_op,
    .get_next_uncommitted = stm32_get_next_uncommitted_op,
    .mark_committed = stm32_mark_committed_op,
    .uncommitted_count = stm32_uncommitted_count_op,
    .is_valid = stm32_is_valid_op,
};

int adapter_bkpsram_stm32_init(adapter_bkpsram_stm32_t *self, void *base_addr, size_t size)
{
    if (self == NULL || base_addr == NULL || size < BKPSRAM_MIN_STORAGE_SIZE) {
        return -EINVAL;
    }

    self->base_addr = base_addr;
    self->size_bytes = size;
    self->is_initialized = false;

    int ret = stm32_init_op(self);
    if (ret == 0) {
        self->is_initialized = true;
    }
    return ret;
}

i_bkpsram_t adapter_bkpsram_stm32_port(adapter_bkpsram_stm32_t *self)
{
    i_bkpsram_t port = {
        .api = &s_bkpsram_stm32_api,
        .impl = self,
    };
    return port;
}
